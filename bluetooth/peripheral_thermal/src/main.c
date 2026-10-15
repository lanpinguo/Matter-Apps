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

#include "bq25895.h"
#include "mlx90640_sensor.h"

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/*
 * Status LED is DK LED3 → DT led2 → P2.07 (see board overlay).
 * Single LED for run-mode indication (no separate CON LED).
 */
#define STATUS_LED             DK_LED3
#define WAKE_BUTTON_MSK        DK_BTN1_MSK
#define FIXED_PASSKEY          123456

/* Standby → deep sleep after this idle time without a phone connection. */
#define STANDBY_TO_DEEP_SLEEP_MS (5 * 60 * 1000)
/* Mode LED: solid in WORK; slow blink in STANDBY; off in DEEP_SLEEP. */
#define STANDBY_LED_ON_MS  200
#define STANDBY_LED_OFF_MS 1800

enum run_mode {
	RUN_MODE_WORK = 0,
	RUN_MODE_STANDBY,
	RUN_MODE_DEEP_SLEEP,
};

/* Thermal GATT service — unique base, distinct from peripheral_status */
#define THERMAL_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0001, 0x1000, 0x8000, 0x00805f9b34fb)

#define THERMAL_FRAME_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0002, 0x1000, 0x8000, 0x00805f9b34fb)

#define THERMAL_CTRL_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0003, 0x1000, 0x8000, 0x00805f9b34fb)

#define THERMAL_POWER_UUID_VAL \
	BT_UUID_128_ENCODE(0x5448524d, 0x0004, 0x1000, 0x8000, 0x00805f9b34fb)

#define FRAME_MAGIC          0x5448524DU /* 'THRM' */
#define FRAME_PROTO_VER      0x01
#define FRAME_HEADER_SIZE    18
#define FRAME_MAX_PAYLOAD    226 /* leaves room under typical 244-byte ATT notify */
#define PIXELS_PER_CHUNK     ((FRAME_MAX_PAYLOAD) / 2)

/*
 * GATT attr indices (see BT_GATT_SERVICE_DEFINE):
 *  0 service, 1–2 frame, 3 frame CCC, 4–5 ctrl, 6–7 power, 8 power CCC
 */
#define THERMAL_FRAME_VALUE_ATTR_IDX 2
#define THERMAL_PWR_VALUE_ATTR_IDX   7

#define PWR_FLAG_VALID       BIT(0)
#define PWR_FLAG_POWER_GOOD  BIT(1)
#define PWR_FLAG_VBUS        BIT(2)

#define POWER_HEARTBEAT_IDLE_MS     5000
#define POWER_HEARTBEAT_CHARGING_MS 1000

BUILD_ASSERT(FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD <= 244);

struct thermal_power_payload {
	uint8_t version;
	uint8_t flags;
	uint8_t charge_state;
	uint8_t vbus_state;
	uint8_t fault;
	uint8_t battery_pct;
	uint16_t batt_mv;
	uint16_t sys_mv;
	uint16_t vbus_mv;
	uint16_t charge_ma;
} __packed;

static struct bt_conn *current_conn;
static bool notify_enabled;
static bool power_notify_enabled;
static struct k_work adv_work;
static struct k_work_delayable power_work;
static struct k_work_delayable deep_sleep_work;
static struct mlx90640_frame frame_buf;
static struct thermal_power_payload power_data;
static uint8_t notify_pkt[FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD];
static enum run_mode current_mode = RUN_MODE_STANDBY;

static void capture_thread(void *p1, void *p2, void *p3);
static void power_monitor_start(void);
static void advertising_start(void);
static void mode_enter_work(void);
static void mode_enter_standby(void);
static void mode_enter_deep_sleep(void);
static void mode_wake_to_standby(void);
static const char *mode_str(enum run_mode mode);

static void frame_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Frame notify %s\n", notify_enabled ? "on" : "off");
}

static void power_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	power_notify_enabled = (value == BT_GATT_CCC_NOTIFY);
	printk("Power notify %s\n", power_notify_enabled ? "on" : "off");
	if (power_notify_enabled) {
		(void)k_work_reschedule(&power_work, K_NO_WAIT);
	}
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

static ssize_t power_read(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	ARG_UNUSED(attr);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &power_data,
				 sizeof(power_data));
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
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(THERMAL_POWER_UUID_VAL),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       power_read, NULL, NULL),
	BT_GATT_CCC(power_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
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

		err = bt_gatt_notify(current_conn,
				     &thermal_svc.attrs[THERMAL_FRAME_VALUE_ATTR_IDX],
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
		/* Thermal IR only in WORK (phone connected + notify on). */
		if (current_mode != RUN_MODE_WORK || !mlx90640_available() ||
		    !notify_enabled || current_conn == NULL) {
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

static void power_work_handler(struct k_work *work)
{
	struct bq25895_status st;
	struct thermal_power_payload payload = {
		.version = 1U,
	};
	uint32_t next_ms = POWER_HEARTBEAT_IDLE_MS;
	bool charging;
	int err;

	ARG_UNUSED(work);

	if (current_mode == RUN_MODE_DEEP_SLEEP) {
		return;
	}

	err = bq25895_read(&st);
	if (err == 0) {
		payload.flags = PWR_FLAG_VALID;
		if (st.power_good) {
			payload.flags |= PWR_FLAG_POWER_GOOD;
		}
		if (st.vbus_present) {
			payload.flags |= PWR_FLAG_VBUS;
		}
		payload.charge_state = (uint8_t)st.charge_state;
		payload.vbus_state = (uint8_t)st.vbus_state;
		payload.fault = st.fault;
		payload.battery_pct = st.battery_pct;
		payload.batt_mv = st.batt_mv;
		payload.sys_mv = st.sys_mv;
		payload.vbus_mv = st.vbus_mv;
		payload.charge_ma = st.charge_ma;

		charging = (st.charge_state == BQ25895_CHG_PRE_CHARGE) ||
			   (st.charge_state == BQ25895_CHG_FAST_CHARGING) ||
			   (st.vbus_present &&
			    st.charge_state != BQ25895_CHG_NOT_CHARGING);
		if (st.vbus_present || charging) {
			next_ms = POWER_HEARTBEAT_CHARGING_MS;
		}

		printk("BQ25895 batt=%umV %u%% vbus=%umV(%s) %s\n",
		       st.batt_mv, st.battery_pct, st.vbus_mv,
		       bq25895_vbus_state_str(st.vbus_state),
		       bq25895_charge_state_str(st.charge_state));
	} else if (err != -ENODEV) {
		printk("BQ25895 read failed: %d\n", err);
	}

	power_data = payload;

	if (power_notify_enabled && current_conn != NULL) {
		err = bt_gatt_notify(current_conn,
				     &thermal_svc.attrs[THERMAL_PWR_VALUE_ATTR_IDX],
				     &payload, sizeof(payload));
		if (err && err != -ENOTCONN && err != -EAGAIN) {
			printk("Power notify failed: %d\n", err);
		}
	}

	if (current_mode != RUN_MODE_DEEP_SLEEP) {
		k_work_schedule(&power_work, K_MSEC(next_ms));
	}
}

static void bq25895_on_int(void)
{
	if (current_mode != RUN_MODE_DEEP_SLEEP) {
		(void)k_work_reschedule(&power_work, K_NO_WAIT);
	}
}

static void power_monitor_start(void)
{
	int err;

	bq25895_set_event_cb(bq25895_on_int);
	k_work_init_delayable(&power_work, power_work_handler);

	err = bq25895_init();
	if (err || !bq25895_available()) {
		printk("BQ25895 unavailable (err %d) — power status disabled\n",
		       err);
		return;
	}

	(void)k_work_schedule(&power_work, K_MSEC(500));
}

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

	if (current_mode == RUN_MODE_DEEP_SLEEP) {
		return;
	}

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
	if (current_mode == RUN_MODE_DEEP_SLEEP) {
		return;
	}
	k_work_submit(&adv_work);
}

static void advertising_stop(void)
{
	int err = bt_le_adv_stop();

	if (err && err != -EALREADY) {
		printk("Advertising stop failed (err %d)\n", err);
	}
}

static const char *mode_str(enum run_mode mode)
{
	switch (mode) {
	case RUN_MODE_WORK:
		return "WORK";
	case RUN_MODE_STANDBY:
		return "STANDBY";
	case RUN_MODE_DEEP_SLEEP:
		return "DEEP_SLEEP";
	default:
		return "?";
	}
}

static void deep_sleep_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	if (current_mode == RUN_MODE_STANDBY) {
		mode_enter_deep_sleep();
	}
}

static void mode_enter_work(void)
{
	(void)k_work_cancel_delayable(&deep_sleep_work);
	current_mode = RUN_MODE_WORK;
	dk_set_led_on(STATUS_LED);
	printk("Mode → %s (phone connected, IR active)\n", mode_str(current_mode));
}

static void mode_enter_standby(void)
{
	current_mode = RUN_MODE_STANDBY;
	(void)k_work_reschedule(&deep_sleep_work,
				K_MSEC(STANDBY_TO_DEEP_SLEEP_MS));
	if (bq25895_available()) {
		(void)k_work_reschedule(&power_work, K_MSEC(500));
	}
	advertising_start();
	printk("Mode → %s (no phone, IR idle, deep sleep in %u s)\n",
	       mode_str(current_mode), STANDBY_TO_DEEP_SLEEP_MS / 1000U);
}

static void mode_enter_deep_sleep(void)
{
	if (current_mode == RUN_MODE_DEEP_SLEEP) {
		return;
	}

	current_mode = RUN_MODE_DEEP_SLEEP;
	(void)k_work_cancel_delayable(&deep_sleep_work);
	(void)k_work_cancel_delayable(&power_work);
	notify_enabled = false;
	power_notify_enabled = false;
	advertising_stop();
	dk_set_led_off(STATUS_LED);
	printk("Mode → %s (BLE+IR off; press Button1 to wake)\n",
	       mode_str(current_mode));
}

static void mode_wake_to_standby(void)
{
	if (current_mode != RUN_MODE_DEEP_SLEEP) {
		return;
	}

	printk("Wake button — leaving DEEP_SLEEP\n");
	mode_enter_standby();
}

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if ((has_changed & WAKE_BUTTON_MSK) == 0U) {
		return;
	}
	/* Active on press (button down). */
	if ((button_state & WAKE_BUTTON_MSK) == 0U) {
		return;
	}

	if (current_mode == RUN_MODE_DEEP_SLEEP) {
		mode_wake_to_standby();
	}
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
	mode_enter_work();
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
	notify_enabled = false;
	power_notify_enabled = false;

	if (current_conn) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	if (current_mode != RUN_MODE_DEEP_SLEEP) {
		mode_enter_standby();
	}
}

static void recycled_cb(void)
{
	if (current_mode != RUN_MODE_DEEP_SLEEP) {
		advertising_start();
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled_cb,
};

int main(void)
{
	int err;

	printk("MLX90640 portable thermal camera\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
	}

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Buttons init failed (err %d)\n", err);
	}

	err = mlx90640_init();
	if (err) {
		printk("MLX90640 init failed (%d) — BLE still starts for bring-up\n",
		       err);
	}

	power_monitor_start();
	k_work_init_delayable(&deep_sleep_work, deep_sleep_work_handler);

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

	mode_enter_standby();

	for (;;) {
		switch (current_mode) {
		case RUN_MODE_WORK:
			dk_set_led_on(STATUS_LED);
			k_sleep(K_MSEC(200));
			break;
		case RUN_MODE_STANDBY:
			dk_set_led_on(STATUS_LED);
			k_sleep(K_MSEC(STANDBY_LED_ON_MS));
			if (current_mode == RUN_MODE_STANDBY) {
				dk_set_led_off(STATUS_LED);
				k_sleep(K_MSEC(STANDBY_LED_OFF_MS));
			}
			break;
		case RUN_MODE_DEEP_SLEEP:
		default:
			dk_set_led_off(STATUS_LED);
			k_sleep(K_MSEC(500));
			break;
		}
	}
}
