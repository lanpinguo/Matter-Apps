/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "rc_prx_pwm.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "uart_rc_link.h"

LOG_MODULE_REGISTER(rc_prx_pwm, CONFIG_ESB_PRX_APP_LOG_LEVEL);

#define RC_PWM_HZ           50U
#define RC_PWM_PERIOD_NS    PWM_USEC(20000)
#define RC_PWM_PULSE_MIN_NS PWM_USEC(1000)
#define RC_PWM_PULSE_MAX_NS PWM_USEC(2000)
#define RC_PWM_VALUE_MAX    1000U
#define RC_PWM_FAILSAFE_MS  500U

#define PCA9685_NODE          DT_NODELABEL(pca9685)
#define PCA9685_INIT_RETRIES  5
#define PCA9685_INIT_RETRY_MS 20
#define PCA9685_REG_PRE_SCALE 0xFE
#define PCA9685_PWM_STEPS     4096U
#define PCA9685_PRESCALE_MIN  0x03
#define PCA9685_PRESCALE_MAX  0xFF

/*
 * PWM output i reads CTRL channel pwm_ctrl_index[i] via PCA9685 LEDi.
 * CH4 (LED4) is throttle from Xbox RT.
 */
static const uint8_t pwm_ctrl_index[RC_PRX_PWM_CHANNEL_COUNT] = {
	UART_RC_CH_LX,
	UART_RC_CH_LY,
	UART_RC_CH_RX,
	UART_RC_CH_RY,
	UART_RC_CH_RT,
};

/* Stick PWMs failsafe to center; throttle (RT) fails to low. */
static const uint16_t failsafe_values[RC_PRX_PWM_CHANNEL_COUNT] = {
	500U, 500U, 500U, 500U, 0U,
};

static const struct pwm_dt_spec rc_pwms[RC_PRX_PWM_CHANNEL_COUNT] = {
	PWM_DT_SPEC_GET(DT_NODELABEL(rc_pwm0)),
	PWM_DT_SPEC_GET(DT_NODELABEL(rc_pwm1)),
	PWM_DT_SPEC_GET(DT_NODELABEL(rc_pwm2)),
	PWM_DT_SPEC_GET(DT_NODELABEL(rc_pwm3)),
	PWM_DT_SPEC_GET(DT_NODELABEL(rc_pwm4)),
};

/* PCA9685 /OE is active-low (DT GPIO_ACTIVE_LOW). */
static const struct gpio_dt_spec pca9685_oe = GPIO_DT_SPEC_GET(DT_NODELABEL(pca9685_oe), gpios);

static const struct device *const pca9685_dev = DEVICE_DT_GET(PCA9685_NODE);
static const struct i2c_dt_spec pca9685_i2c = I2C_DT_SPEC_GET(PCA9685_NODE);

static struct k_work_delayable failsafe_work;
static struct k_work pwm_apply_work;
static bool pwm_ready;
static uint32_t pca_period_count;
static uint8_t pca_prescale;

/* Latest CTRL snapshot — filled from ESB ISR, applied on system workqueue. */
static uint16_t pending_channels[RC_LINK_MAX_CHANNELS];
static uint8_t pending_channel_count;

static uint32_t value_to_pulse_ns(uint16_t value)
{
	uint32_t span = RC_PWM_PULSE_MAX_NS - RC_PWM_PULSE_MIN_NS;

	if (value > RC_PWM_VALUE_MAX) {
		value = RC_PWM_VALUE_MAX;
	}

	return RC_PWM_PULSE_MIN_NS + ((span * (uint32_t)value) / RC_PWM_VALUE_MAX);
}

/*
 * Sticks arrive as 0..1000. Xbox triggers are still raw 10-bit (0..1023)
 * from the Hub — normalize them here before PWM mapping.
 */
static uint16_t normalize_ctrl_value(uint8_t ctrl_index, uint16_t value)
{
	if (ctrl_index == UART_RC_CH_LT || ctrl_index == UART_RC_CH_RT) {
		if (value > 1023U) {
			value = 1023U;
		}
		return (uint16_t)((value * RC_PWM_VALUE_MAX) / 1023U);
	}

	if (value > RC_PWM_VALUE_MAX) {
		value = RC_PWM_VALUE_MAX;
	}
	return value;
}

static void set_pwm(uint8_t index, uint16_t value)
{
	int err;
	uint32_t pulse_ns;
	uint32_t pulse_count;

	if (index >= RC_PRX_PWM_CHANNEL_COUNT || !pwm_ready || pca_period_count == 0U) {
		return;
	}

	/*
	 * Use pwm_set_cycles with a period_count that matches the calibrated
	 * PRE_SCALE. Do not use pwm_set()/pwm_set_dt() — those assume the
	 * driver's hardcoded 25 MHz OSC and produced ~55 Hz on this board.
	 */
	pulse_ns = value_to_pulse_ns(value);
	pulse_count = (uint32_t)(((uint64_t)pulse_ns * pca_period_count) /
				 RC_PWM_PERIOD_NS);
	err = pwm_set_cycles(rc_pwms[index].dev, rc_pwms[index].channel,
			     pca_period_count, pulse_count, rc_pwms[index].flags);
	if (err != 0) {
		LOG_DBG("PWM%u set failed: %d", index, err);
	}
}

static void apply_failsafe(void)
{
	for (uint8_t i = 0; i < RC_PRX_PWM_CHANNEL_COUNT; i++) {
		set_pwm(i, failsafe_values[i]);
	}
}

static void apply_ctrl_channels(const uint16_t *channels, uint8_t count)
{
	for (uint8_t i = 0; i < RC_PRX_PWM_CHANNEL_COUNT; i++) {
		uint8_t src = pwm_ctrl_index[i];

		if (src < count) {
			set_pwm(i, normalize_ctrl_value(src, channels[src]));
		} else {
			set_pwm(i, failsafe_values[i]);
		}
	}
}

static void failsafe_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("CTRL timeout — PWM failsafe");
	apply_failsafe();
}

static void pwm_apply_work_handler(struct k_work *work)
{
	uint16_t channels[RC_LINK_MAX_CHANNELS];
	uint8_t count;
	unsigned int key;

	ARG_UNUSED(work);

	if (!pwm_ready) {
		return;
	}

	key = irq_lock();
	count = pending_channel_count;
	if (count > RC_LINK_MAX_CHANNELS) {
		count = RC_LINK_MAX_CHANNELS;
	}
	memcpy(channels, pending_channels, (size_t)count * sizeof(uint16_t));
	irq_unlock(key);

	apply_ctrl_channels(channels, count);
	(void)k_work_reschedule(&failsafe_work, K_MSEC(RC_PWM_FAILSAFE_MS));
}

static void pca9685_i2c_scan(void)
{
	uint8_t dst;
	int found = 0;

	if (!i2c_is_ready_dt(&pca9685_i2c)) {
		LOG_ERR("I2C bus not ready for PCA9685 scan");
		return;
	}

	LOG_ERR("I2C scan 0x40..0x70 on %s (expect PCA9685 @ 0x%02x):",
		pca9685_i2c.bus->name, pca9685_i2c.addr);
	for (uint16_t addr = 0x40; addr <= 0x70; addr++) {
		if (i2c_read(pca9685_i2c.bus, &dst, 1, addr) == 0) {
			LOG_ERR("  ACK at 0x%02x", addr);
			found++;
		}
	}
	if (found == 0) {
		LOG_ERR("  (no ACK — check VCC/GND, SCL=P1.12 SDA=P1.13, A2+A5→0x64)");
	}
}

static int pca9685_bringup(void)
{
	int err = -ENODEV;

	for (int i = 0; i < PCA9685_INIT_RETRIES; i++) {
		if (device_is_ready(pca9685_dev)) {
			return 0;
		}

		err = device_init(pca9685_dev);
		if (err == 0 && device_is_ready(pca9685_dev)) {
			return 0;
		}

		LOG_WRN("PCA9685 init attempt %d/%d failed: %d", i + 1,
			PCA9685_INIT_RETRIES, err);
		k_msleep(PCA9685_INIT_RETRY_MS);
	}

	return err != 0 ? err : -ENODEV;
}

/*
 * Datasheet 7.3.5: update_rate = OSC / (4096 × (PRE_SCALE + 1)).
 * Program PRE_SCALE from the calibrated OSC (CONFIG_RC_PCA9685_OSC_HZ).
 */
static int pca9685_configure_rate(uint32_t hz)
{
	uint64_t osc = (uint64_t)CONFIG_RC_PCA9685_OSC_HZ;
	uint64_t denom = (uint64_t)PCA9685_PWM_STEPS * hz;
	uint32_t prescale;
	uint8_t reg = PCA9685_REG_PRE_SCALE;
	uint8_t rb = 0;
	uint32_t expect_hz;
	int err;

	if (hz == 0U || denom == 0U) {
		return -EINVAL;
	}

	/* prescale = round(osc / (4096 * hz)) - 1 */
	prescale = (uint32_t)((osc + denom / 2ULL) / denom);
	if (prescale < (PCA9685_PRESCALE_MIN + 1U)) {
		return -EINVAL;
	}
	prescale -= 1U;
	if (prescale > PCA9685_PRESCALE_MAX) {
		return -EINVAL;
	}

	pca_prescale = (uint8_t)prescale;
	pca_period_count = PCA9685_PWM_STEPS * (prescale + 1U);

	err = pwm_set_cycles(pca9685_dev, 0, pca_period_count, 0, 0);
	if (err != 0) {
		LOG_ERR("PCA9685 rate set failed: %d", err);
		return err;
	}

	err = i2c_write_read_dt(&pca9685_i2c, &reg, 1, &rb, 1);
	if (err != 0) {
		LOG_WRN("PCA9685 PRE_SCALE readback failed: %d", err);
	} else if (rb != pca_prescale) {
		LOG_WRN("PCA9685 PRE_SCALE=0x%02x (wrote 0x%02x)", rb, pca_prescale);
	}

	expect_hz = (uint32_t)(osc / ((uint64_t)PCA9685_PWM_STEPS * (pca_prescale + 1U)));
	LOG_INF("PCA9685 PRE_SCALE=%u osc=%u Hz → expect ~%u Hz (target %u)",
		pca_prescale, CONFIG_RC_PCA9685_OSC_HZ, expect_hz, hz);
	return 0;
}

int rc_prx_pwm_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&pca9685_oe)) {
		LOG_ERR("PCA9685 OE GPIO not ready");
		return -ENODEV;
	}

	/* Enable outputs (OE active-low) before talking to the chip. */
	err = gpio_pin_configure_dt(&pca9685_oe, GPIO_OUTPUT_ACTIVE);
	if (err != 0) {
		LOG_ERR("PCA9685 OE configure failed: %d", err);
		return err;
	}

	err = pca9685_bringup();
	if (err != 0) {
		LOG_ERR("PCA9685 device init failed: %d", err);
		pca9685_i2c_scan();
		return err;
	}

	for (uint8_t i = 0; i < RC_PRX_PWM_CHANNEL_COUNT; i++) {
		if (!pwm_is_ready_dt(&rc_pwms[i])) {
			LOG_ERR("PWM%u device not ready", i);
			return -ENODEV;
		}
	}

	err = pca9685_configure_rate(RC_PWM_HZ);
	if (err != 0) {
		return err;
	}

	k_work_init_delayable(&failsafe_work, failsafe_work_handler);
	k_work_init(&pwm_apply_work, pwm_apply_work_handler);
	pwm_ready = true;
	apply_failsafe();
	LOG_INF("RC PWM ready: PCA9685@0x%02x %u ch @ %u Hz (OE=P2.10, CH4=RT)",
		pca9685_i2c.addr, RC_PRX_PWM_CHANNEL_COUNT, RC_PWM_HZ);
	return 0;
}

void rc_prx_pwm_apply_ctrl(const struct rc_link_frame *ctrl)
{
	uint8_t count;
	unsigned int key;

	if (ctrl == NULL || !pwm_ready) {
		return;
	}

	count = ctrl->channel_count;
	if (count > RC_LINK_MAX_CHANNELS) {
		count = RC_LINK_MAX_CHANNELS;
	}

	/*
	 * ESB RX runs in ISR; PCA9685 pwm_set uses I2C (k_sem_take) and must
	 * not run here — snapshot and defer to the system workqueue.
	 */
	key = irq_lock();
	pending_channel_count = count;
	memcpy(pending_channels, ctrl->channels, (size_t)count * sizeof(uint16_t));
	irq_unlock(key);

	(void)k_work_submit(&pwm_apply_work);
}
