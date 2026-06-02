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
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/devicetree.h>
#include <soc.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/settings/settings.h>

#include <dk_buttons_and_leds.h>

#define DEVICE_NAME             CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN         (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED          DK_LED1
#define CON_STATUS_LED          DK_LED2
#define RUN_LED_BLINK_INTERVAL  1000
#define ADC_SAMPLE_INTERVAL_MS  1000
#define FIXED_PASSKEY           123456

#define STATUS1_BUTTON          DK_BTN1_MSK
#define STATUS2_BUTTON          DK_BTN2_MSK

/* Custom service UUID — same base as NSMS but different instance IDs */
#define MY_SERVICE_UUID_VAL \
	BT_UUID_128_ENCODE(0x57a70000, 0x9350, 0x11ed, 0xa1eb, 0x0242ac120002)

/* Single status characteristic UUID */
#define MY_STATUS_CHAR_UUID_VAL \
	BT_UUID_128_ENCODE(0x57a70006, 0x9350, 0x11ed, 0xa1eb, 0x0242ac120002)

/* Advertising data — use custom service UUID */
static const struct bt_uuid_128 adv_uuid = BT_UUID_INIT_128(MY_SERVICE_UUID_VAL);

/* Status buffer and mutex for the single characteristic */
static char status_buf[256];
static K_MUTEX_DEFINE(status_mtx);

/* Custom GATT service with one characteristic for all status updates */
static ssize_t my_status_read(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr,
			      void *buf, uint16_t len, uint16_t offset)
{
	ssize_t ret;

	k_mutex_lock(&status_mtx, K_MSEC(100));
	ret = bt_gatt_attr_read(conn, attr, buf, len, offset,
				status_buf, strlen(status_buf));
	k_mutex_unlock(&status_mtx);
	return ret;
}

BT_GATT_SERVICE_DEFINE(my_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(MY_SERVICE_UUID_VAL)),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(MY_STATUS_CHAR_UUID_VAL),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       my_status_read, NULL,
			       (void *)status_buf),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* Send a status notification to all connected peers */
static void send_status_update(const char *str)
{
	k_mutex_lock(&status_mtx, K_MSEC(100));
	strncpy(status_buf, str, sizeof(status_buf) - 1);
	status_buf[sizeof(status_buf) - 1] = '\0';
	k_mutex_unlock(&status_mtx);

	bt_gatt_notify(NULL, &my_svc.attrs[1], str, strlen(str));
}

static struct k_work adv_work;

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
static char adc_status_buf[ADC_CHANNEL_COUNT][32];
#else
#define ADC_STATUS_ENABLED 0
#endif

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, MY_SERVICE_UUID_VAL),
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

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected    = connected,
	.disconnected = disconnected,
	.recycled     = recycled_cb,
};

static void button_changed(uint32_t button_state, uint32_t has_changed)
{
	/* Send combined button state in one notification */
	char msg[64];
	const char *btn1 = (button_state & STATUS1_BUTTON) ? "Pressed" : "Released";
	const char *btn2 = (button_state & STATUS2_BUTTON) ? "Pressed" : "Released";
	snprintf(msg, sizeof(msg), "BTN1 %s\nBTN2 %s", btn1, btn2);
	send_status_update(msg);
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

#if DT_HAS_ALIAS(pcf8574a_left) && DT_HAS_ALIAS(pcf8574a_right)
#define PCF8574_INT_ENABLED 1
#define PCF_DEV_COUNT 2
enum {
	PCF_LEFT = 0,
	PCF_RIGHT = 1,
};

static const struct device *const pcf_devs[PCF_DEV_COUNT] = {
	DEVICE_DT_GET(DT_ALIAS(pcf8574a_left)),
	DEVICE_DT_GET(DT_ALIAS(pcf8574a_right)),
};
static const struct i2c_dt_spec pcf_i2c[PCF_DEV_COUNT] = {
	I2C_DT_SPEC_GET(DT_ALIAS(pcf8574a_left)),
	I2C_DT_SPEC_GET(DT_ALIAS(pcf8574a_right)),
};
static const char *const pcf_names[PCF_DEV_COUNT] = {
	"PCF_L",
	"PCF_R",
};
/* Shared INT# from both PCF8574 chips -> P0.02 (active low, open-drain). */
static const struct gpio_dt_spec pcf_shared_int =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pcf8574_int_gpios);
static struct gpio_callback pcf_int_cb;
static struct k_work pcf_shared_work;
static uint8_t pcf_last_state[PCF_DEV_COUNT];
static bool pcf_last_valid[PCF_DEV_COUNT];

/*
 * PCF8574 is quasi-bidirectional: write 0xFF to release all pins (input-high).
 * Use I2C directly so pcf857x driver keeps pins as "input" and gpio_port_get_raw()
 * still works (it returns -EOPNOTSUPP if pins are marked output in the driver).
 */
static int pcf8574_preset_inputs(const struct i2c_dt_spec *i2c, const char *name)
{
	uint8_t val = 0xFF;
	int err;

	if (!device_is_ready(i2c->bus)) {
		printk("%s I2C bus not ready\n", name);
		return -ENODEV;
	}

	err = i2c_write_dt(i2c, &val, 1);
	if (err) {
		printk("%s preset 0xFF failed (err %d)\n", name, err);
	}

	return err;
}

static void pcf8574a_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	gpio_port_value_t port_val;
	char msg[128];
	char *p = msg;
	bool has_change = false;
	int err;

	for (size_t i = 0; i < PCF_DEV_COUNT; i++) {
		uint8_t cur_state;
		uint8_t changed_mask;

		err = gpio_port_get_raw(pcf_devs[i], &port_val);
		if (err) {
			printk("%s read failed (err %d)\n", pcf_names[i], err);
			continue;
		}

		cur_state = (uint8_t)port_val;
		if (!pcf_last_valid[i]) {
			pcf_last_state[i] = cur_state;
			pcf_last_valid[i] = true;
			continue;
		}

		changed_mask = cur_state ^ pcf_last_state[i];
		if (changed_mask == 0U) {
			continue;
		}

		pcf_last_state[i] = cur_state;
		has_change = true;
		p += snprintf(p, sizeof(msg) - (size_t)(p - msg),
			      "%s 0x%02x mask 0x%02x\n", pcf_names[i], cur_state, changed_mask);
	}
	if (has_change) {
		printk("%s", msg);
		send_status_update(msg);
	}
}

static void pcf_int_mcu_handler(const struct device *port, struct gpio_callback *cb,
				uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&pcf_shared_work);
}

static int init_pcf8574_int(void)
{
	int err;
	gpio_port_value_t port_val;

	k_work_init(&pcf_shared_work, pcf8574a_work_handler);

	for (size_t i = 0; i < PCF_DEV_COUNT; i++) {
		pcf_last_valid[i] = false;

		if (!device_is_ready(pcf_devs[i])) {
			printk("%s device not ready\n", pcf_names[i]);
			return -ENODEV;
		}

		err = pcf8574_preset_inputs(&pcf_i2c[i], pcf_names[i]);
		if (err) {
			return err;
		}

		err = gpio_port_get_raw(pcf_devs[i], &port_val);
		if (err) {
			printk("%s initial read failed (err %d)\n", pcf_names[i], err);
			return err;
		}

		pcf_last_state[i] = (uint8_t)port_val;
		pcf_last_valid[i] = true;
		printk("%s init OK, port=0x%02x\n", pcf_names[i], pcf_last_state[i]);
	}

	if (!gpio_is_ready_dt(&pcf_shared_int)) {
		printk("PCF8574 shared INT GPIO not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&pcf_shared_int, GPIO_INPUT);
	if (err) {
		printk("PCF8574 INT configure failed (err %d)\n", err);
		return err;
	}

	/* INT# is active-low open-drain: trigger on falling edge. */
	err = gpio_pin_interrupt_configure_dt(&pcf_shared_int, GPIO_INT_EDGE_FALLING);
	if (err) {
		printk("PCF8574 INT IRQ configure failed (err %d)\n", err);
		return err;
	}

	gpio_init_callback(&pcf_int_cb, pcf_int_mcu_handler, BIT(pcf_shared_int.pin));
	err = gpio_add_callback(pcf_shared_int.port, &pcf_int_cb);
	if (err) {
		printk("PCF8574 INT callback add failed (err %d)\n", err);
		return err;
	}

	printk("PCF8574 shared INT ready on %s pin %u\n",
	       pcf_shared_int.port->name, pcf_shared_int.pin);

	return 0;
}
#else
static int init_pcf8574_int(void)
{
	return 0;
}
#endif

#if ADC_STATUS_ENABLED
static void update_adc_status(size_t index)
{
	const struct adc_dt_spec *adc_channel = &adc_channels[index];
	struct adc_sequence sequence = { 0 };
	int32_t sample_mv;
	int err;

	err = adc_sequence_init_dt(adc_channel, &sequence);
	if (err) {
		snprintk(adc_status_buf[index], sizeof(adc_status_buf[index]),
			 "CH%u init err %d", adc_channel->channel_id, err);
		return;
	}

	sequence.buffer = &adc_samples[index];
	sequence.buffer_size = sizeof(adc_samples[index]);

	err = adc_read_dt(adc_channel, &sequence);
	if (err) {
		snprintk(adc_status_buf[index], sizeof(adc_status_buf[index]),
			 "CH%u read err %d", adc_channel->channel_id, err);
		return;
	}

	sample_mv = adc_samples[index];
	err = adc_raw_to_millivolts_dt(adc_channel, &sample_mv);
	if (err) {
		snprintk(adc_status_buf[index], sizeof(adc_status_buf[index]),
			 "CH%u raw %d", adc_channel->channel_id, adc_samples[index]);
	} else {
		snprintk(adc_status_buf[index], sizeof(adc_status_buf[index]),
			 "CH%u %d mV", adc_channel->channel_id, sample_mv);
	}
}

static void adc_work_handler(struct k_work *work)
{
	char msg[128];
	char *p = msg;

	for (size_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
		update_adc_status(i);
		p += snprintf(p, sizeof(msg) - (p - msg), "%s\n", adc_status_buf[i]);
	}

	send_status_update(msg);
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

	err = init_pcf8574_int();
	if (err) {
		printk("PCF8574 interrupt init failed (err %d)\n", err);
		return 0;
	}

	err = init_adc();
	if (err) {
		printk("ADC init failed (err %d)\n", err);
		return 0;
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
