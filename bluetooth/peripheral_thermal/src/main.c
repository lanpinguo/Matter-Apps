/*
 * Portable MLX90640 thermal camera — BLE peripheral.
 *
 * Streams 32x24 temperature frames to a phone over a custom GATT service.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>

#include <dk_buttons_and_leds.h>

#include "mlx90640_sensor.h"

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED         DK_LED1
#define CON_STATUS_LED         DK_LED2
#define RUN_LED_BLINK_INTERVAL 1000
#define FIXED_PASSKEY          123456

/* Thermal GATT service — unique base, distinct from peripheral_status */
#define THERMAL_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0001, 0x1000, 0x8000, 0x00805f9b34fb)

#define THERMAL_FRAME_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0002, 0x1000, 0x8000, 0x00805f9b34fb)

#define THERMAL_CTRL_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0003, 0x1000, 0x8000, 0x00805f9b34fb)

#define FRAME_MAGIC          0x5448524DU /* 'THRM' */
#define FRAME_PROTO_VER      0x01
#define FRAME_HEADER_SIZE    18
#define FRAME_MAX_PAYLOAD    226 /* leaves room under typical 244-byte ATT notify */
#define PIXELS_PER_CHUNK     ((FRAME_MAX_PAYLOAD) / 2)

BUILD_ASSERT(FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD <= 244);

static struct bt_conn *current_conn;
static bool notify_enabled;
static struct k_work adv_work;
static struct mlx90640_frame frame_buf;
static uint8_t notify_pkt[FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD];

static void capture_thread(void *p1, void *p2, void *p3);

static void frame_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Frame notify %s\n", notify_enabled ? "on" : "off");
}

static ssize_t ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  const void *buf, uint16_t len, uint16_t offset,
			  uint8_t flags)
{
	const uint8_t *data = buf;
	int err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0 || len < 1) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Byte0 = Melexis refresh rate code (0=0.5Hz … 5=16Hz). */
	err = mlx90640_set_refresh_rate(data[0]);
	if (err) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	printk("Refresh rate code set to %u\n", data[0]);
	return len;
}

BT_GATT_SERVICE_DEFINE(thermal_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(THERMAL_SERVICE_UUID_VAL)),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(THERMAL_FRAME_UUID_VAL),
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(frame_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(THERMAL_CTRL_UUID_VAL),
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE,
			       NULL, ctrl_write, NULL),
);

static void send_frame_chunks(const struct mlx90640_frame *fr)
{
	const uint16_t total_pixels = MLX90640_PIXELS;
	const uint8_t chunk_cnt =
		(uint8_t)DIV_ROUND_UP(total_pixels, PIXELS_PER_CHUNK);
	int err;

	if (!notify_enabled || current_conn == NULL) {
		return;
	}

	for (uint8_t chunk = 0; chunk < chunk_cnt; chunk++) {
		uint16_t start = (uint16_t)chunk * PIXELS_PER_CHUNK;
		uint16_t count = MIN((uint16_t)PIXELS_PER_CHUNK,
				     (uint16_t)(total_pixels - start));
		uint16_t payload = count * 2U;
		uint8_t *p = notify_pkt;

		sys_put_be32(FRAME_MAGIC, p); p += 4;
		*p++ = FRAME_PROTO_VER;
		*p++ = 0; /* flags */
		sys_put_le16(fr->seq, p); p += 2;
		*p++ = chunk;
		*p++ = chunk_cnt;
		*p++ = MLX90640_WIDTH;
		*p++ = MLX90640_HEIGHT;
		sys_put_le16((uint16_t)fr->ta_cC, p); p += 2;
		sys_put_le16((uint16_t)fr->tmin_cC, p); p += 2;
		sys_put_le16((uint16_t)fr->tmax_cC, p); p += 2;

		for (uint16_t i = 0; i < count; i++) {
			sys_put_le16((uint16_t)fr->pixels[start + i], p);
			p += 2;
		}

		err = bt_gatt_notify(current_conn, &thermal_svc.attrs[1],
				     notify_pkt, FRAME_HEADER_SIZE + payload);
		if (err) {
			printk("notify chunk %u failed: %d\n", chunk, err);
			return;
		}

		/* Give the controller a breath between large notifies */
		k_msleep(2);
	}
}

static void capture_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_msleep(1000);

	while (1) {
		if (!mlx90640_available() || !notify_enabled ||
		    current_conn == NULL) {
			k_msleep(100);
			continue;
		}

		int err = mlx90640_capture(&frame_buf);

		if (err == 0) {
			send_frame_chunks(&frame_buf);
			printk("Frame #%u  Ta=%.2f  min=%.2f  max=%.2f\n",
			       frame_buf.seq,
			       frame_buf.ta_cC / 100.0,
			       frame_buf.tmin_cC / 100.0,
			       frame_buf.tmax_cC / 100.0);
		} else {
			printk("capture failed: %d\n", err);
			k_msleep(50);
		}
	}
}

K_THREAD_DEFINE(capture_tid, 4096, capture_thread, NULL, NULL, NULL, 5, 0, 0);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, THERMAL_SERVICE_UUID_VAL),
};

static void adv_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd,
			      ARRAY_SIZE(sd));
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}
	printk("Advertising started as \"%s\"\n", DEVICE_NAME);
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (err) {
		printk("Connection failed, err 0x%02x\n", err);
		return;
	}

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Connected %s\n", addr);

	current_conn = bt_conn_ref(conn);
	dk_set_led_on(CON_STATUS_LED);
}

static void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
	printk("MTU updated: TX %u RX %u\n", tx, rx);
}

static struct bt_gatt_cb gatt_callbacks = {
	.att_mtu_updated = mtu_updated,
};

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x\n", reason);
	dk_set_led_off(CON_STATUS_LED);
	notify_enabled = false;

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}
}

static void recycled_cb(void)
{
	advertising_start();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled_cb,
};

int main(void)
{
	int blink_status = 0;
	int err;

	printk("MLX90640 portable thermal camera\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
	}

	err = mlx90640_init();
	if (err) {
		printk("MLX90640 init failed (%d) — BLE still starts for bring-up\n",
		       err);
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	bt_gatt_cb_register(&gatt_callbacks);

#if defined(CONFIG_BT_FIXED_PASSKEY)
	err = bt_passkey_set(FIXED_PASSKEY);
	if (err) {
		printk("Passkey set failed (err %d)\n", err);
	} else {
		printk("Fixed passkey: %d\n", FIXED_PASSKEY);
	}
#endif

	k_work_init(&adv_work, adv_work_handler);

	advertising_start();

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
