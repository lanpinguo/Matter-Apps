/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <bluetooth/services/nsms.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)


#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  1000
#define ADC_SAMPLE_INTERVAL_MS  1000
#define FIXED_PASSKEY           123456

#define STATUS1_BUTTON             DK_BTN1_MSK
#define STATUS2_BUTTON             DK_BTN2_MSK

static struct k_work adv_work;

/* Implementation of two status characteristics */
BT_NSMS_DEF(nsms_btn1, "Button 1", false, "Unknown", 20);
BT_NSMS_DEF(nsms_btn2, "Button 2", false, "Unknown", 20);

#if DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#define ADC_STATUS_ENABLED 1
#define ADC_CHANNEL_COUNT 3
static const struct adc_dt_spec adc_channels[ADC_CHANNEL_COUNT] = {
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1),
	ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2),
};
static struct k_work_delayable adc_work;
static int16_t adc_samples[ADC_CHANNEL_COUNT];

BT_NSMS_DEF(nsms_adc0, "ADC channel 0", false, "Unknown", 32);
BT_NSMS_DEF(nsms_adc1, "ADC channel 1", false, "Unknown", 32);
BT_NSMS_DEF(nsms_adc2, "ADC channel 2", false, "Unknown", 32);

static const struct bt_nsms *const adc_nsms[ADC_CHANNEL_COUNT] = {
	&nsms_adc0,
	&nsms_adc1,
	&nsms_adc2,
};
#else
#define ADC_STATUS_ENABLED 0
#endif


static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_NSMS_VAL),
};

static void adv_work_handler(struct k_work *work)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising successfully started\n");
}

static void advertising_start(void)
{
	k_work_submit(&adv_work);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	if (err) {
		printk("Connection failed, err 0x%02x %s\n", err, bt_hci_err_to_str(err));
		return;
	}

	printk("Connected\n");

	dk_set_led_on(CON_STATUS_LED);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	printk("Disconnected, reason 0x%02x %s\n", reason, bt_hci_err_to_str(reason));

	dk_set_led_off(CON_STATUS_LED);
}

static void recycled_cb(void)
{
	printk("Connection object available from previous conn. Disconnect is complete!\n");
	advertising_start();
}

#if IS_ENABLED(CONFIG_BT_STATUS_SECURITY_ENABLED)
static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level, err,
		       bt_security_err_to_str(err));
	}
}
#endif

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected        = connected,
	.disconnected     = disconnected,
	.recycled         = recycled_cb,
#if IS_ENABLED(CONFIG_BT_STATUS_SECURITY_ENABLED)
	.security_changed = security_changed,
#endif
};

#if IS_ENABLED(CONFIG_BT_STATUS_SECURITY_ENABLED)
static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing completed: %s, bonded: %d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Pairing failed conn: %s, reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed
};
#else
static struct bt_conn_auth_cb conn_auth_callbacks;
static struct bt_conn_auth_info_cb conn_auth_info_callbacks;
#endif /* IS_ENABLED(CONFIG_BT_STATUS_SECURITY_ENABLED) */


static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	if (has_changed & STATUS1_BUTTON) {
		bt_nsms_set_status(&nsms_btn1,
				   (button_state & STATUS1_BUTTON) ? "Pressed" : "Released");
	}
	if (has_changed & STATUS2_BUTTON) {
		bt_nsms_set_status(&nsms_btn2,
				   (button_state & STATUS2_BUTTON) ? "Pressed" : "Released");
	}
}

static int init_button(void)
{
	int err;

	err = dk_buttons_init(button_changed);
	if (err) {
		printk("Cannot init buttons (err: %d)\n", err);
	}

	return err;
}

#if ADC_STATUS_ENABLED
static void update_adc_status(size_t index)
{
	const struct adc_dt_spec *adc_channel = &adc_channels[index];
	struct adc_sequence sequence = { 0 };
	char status[32];
	int32_t sample_mv;
	int err;

	err = adc_sequence_init_dt(adc_channel, &sequence);
	if (err) {
		snprintk(status, sizeof(status), "ADC%u init err %d",
			 adc_channel->channel_id, err);
		goto update_status;
	}

	sequence.buffer = &adc_samples[index];
	sequence.buffer_size = sizeof(adc_samples[index]);

	err = adc_read_dt(adc_channel, &sequence);
	if (err) {
		snprintk(status, sizeof(status), "ADC%u read err %d",
			 adc_channel->channel_id, err);
		goto update_status;
	}

	sample_mv = adc_samples[index];
	err = adc_raw_to_millivolts_dt(adc_channel, &sample_mv);
	if (err) {
		snprintk(status, sizeof(status), "CH%u raw %d",
			 adc_channel->channel_id, adc_samples[index]);
	} else {
		snprintk(status, sizeof(status), "CH%u %d mV",
			 adc_channel->channel_id, sample_mv);
	}

update_status:
	err = bt_nsms_set_status(adc_nsms[index], status);
	if (err && err != -ENOTCONN) {
		printk("Failed to update ADC channel %u status (err %d)\n",
		       adc_channel->channel_id, err);
	}
}

static void adc_work_handler(struct k_work *work)
{
	for (size_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
		update_adc_status(i);
	}

	k_work_schedule(&adc_work, K_MSEC(ADC_SAMPLE_INTERVAL_MS));
}

static int init_adc(void)
{
	int err;

	if (!adc_is_ready_dt(&adc_channels[0])) {
		printk("ADC controller device not ready\n");
		return -ENODEV;
	}

	for (size_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
		err = adc_channel_setup_dt(&adc_channels[i]);
		if (err) {
			printk("Failed to setup ADC channel %u (err %d)\n",
			       adc_channels[i].channel_id, err);
			return err;
		}

		printk("ADC channel %u initialized\n", adc_channels[i].channel_id);
	}

	k_work_init_delayable(&adc_work, adc_work_handler);

	return 0;
}

static void adc_sampling_start(void)
{
	k_work_schedule(&adc_work, K_NO_WAIT);
}
#else
static int init_adc(void)
{
	printk("No ADC channel configured in devicetree\n");
	return 0;
}

static void adc_sampling_start(void)
{
}
#endif

int main(void)
{
	int blink_status = 0;
	int err;

	printk("Starting Bluetooth Peripheral Status sample\n");

	err = dk_leds_init();
	if (err) {
		printk("LEDs init failed (err %d)\n", err);
		return 0;
	}

	err = init_button();
	if (err) {
		printk("Button init failed (err %d)\n", err);
		return 0;
	}

	err = init_adc();
	if (err) {
		printk("ADC init failed (err %d)\n", err);
		return 0;
	}

	if (IS_ENABLED(CONFIG_BT_STATUS_SECURITY_ENABLED)) {
		err = bt_conn_auth_cb_register(&conn_auth_callbacks);
		if (err) {
			printk("Failed to register authorization callbacks.\n");
			return 0;
		}

		err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
		if (err) {
			printk("Failed to register authorization info callbacks.\n");
			return 0;
		}
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return 0;
	}

	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_BT_FIXED_PASSKEY)) {
		err = bt_passkey_set(FIXED_PASSKEY);
		if (err) {
			printk("Failed to set fixed passkey (err %d)\n", err);
			return 0;
		}

		printk("Fixed passkey set: %06u\n", FIXED_PASSKEY);
	}

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	k_work_init(&adv_work, adv_work_handler);
	advertising_start();
	adc_sampling_start();

	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(RUN_LED_BLINK_INTERVAL));
	}
}
