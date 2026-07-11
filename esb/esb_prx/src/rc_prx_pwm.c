/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include "rc_prx_pwm.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "uart_rc_link.h"

LOG_MODULE_REGISTER(rc_prx_pwm, CONFIG_ESB_PRX_APP_LOG_LEVEL);

#define RC_PWM_PERIOD_NS    PWM_USEC(20000)
#define RC_PWM_PULSE_MIN_NS PWM_USEC(1000)
#define RC_PWM_PULSE_MAX_NS PWM_USEC(2000)
#define RC_PWM_VALUE_MAX    1000U
#define RC_PWM_FAILSAFE_MS  500U

/*
 * PWM output i reads CTRL channel pwm_ctrl_index[i].
 * CH4 (P1.10) is throttle from Xbox RT.
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

static struct k_work_delayable failsafe_work;
static bool pwm_ready;

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

	if (index >= RC_PRX_PWM_CHANNEL_COUNT || !pwm_ready) {
		return;
	}

	pulse_ns = value_to_pulse_ns(value);
	err = pwm_set_dt(&rc_pwms[index], RC_PWM_PERIOD_NS, pulse_ns);
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

int rc_prx_pwm_init(void)
{
	for (uint8_t i = 0; i < RC_PRX_PWM_CHANNEL_COUNT; i++) {
		if (!pwm_is_ready_dt(&rc_pwms[i])) {
			LOG_ERR("PWM%u device not ready", i);
			return -ENODEV;
		}
	}

	k_work_init_delayable(&failsafe_work, failsafe_work_handler);
	pwm_ready = true;
	apply_failsafe();
	LOG_INF("RC PWM ready: %u ch @ 50 Hz (CH4=RT throttle)", RC_PRX_PWM_CHANNEL_COUNT);
	return 0;
}

void rc_prx_pwm_apply_ctrl(const struct rc_link_frame *ctrl)
{
	if (ctrl == NULL || !pwm_ready) {
		return;
	}

	apply_ctrl_channels(ctrl->channels, ctrl->channel_count);
	(void)k_work_reschedule(&failsafe_work, K_MSEC(RC_PWM_FAILSAFE_MS));
}
