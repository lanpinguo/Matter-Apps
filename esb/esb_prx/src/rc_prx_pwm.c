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

LOG_MODULE_REGISTER(rc_prx_pwm, CONFIG_ESB_PRX_APP_LOG_LEVEL);

#define RC_PWM_PERIOD_NS    PWM_USEC(20000)
#define RC_PWM_PULSE_MIN_NS PWM_USEC(1000)
#define RC_PWM_PULSE_MAX_NS PWM_USEC(2000)
#define RC_PWM_VALUE_MAX    1000U
#define RC_PWM_FAILSAFE_MS  500U

/* Stick channels failsafe to center; channel 4 (LT) fails to low. */
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

static void set_channel(uint8_t index, uint16_t value)
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

static void apply_values(const uint16_t *values, uint8_t count)
{
	uint8_t n = MIN(count, RC_PRX_PWM_CHANNEL_COUNT);

	for (uint8_t i = 0; i < n; i++) {
		set_channel(i, values[i]);
	}

	for (uint8_t i = n; i < RC_PRX_PWM_CHANNEL_COUNT; i++) {
		set_channel(i, failsafe_values[i]);
	}
}

static void failsafe_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_WRN("CTRL timeout — PWM failsafe");
	apply_values(failsafe_values, RC_PRX_PWM_CHANNEL_COUNT);
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
	apply_values(failsafe_values, RC_PRX_PWM_CHANNEL_COUNT);
	LOG_INF("RC PWM ready: %u ch @ 50 Hz (1000-2000 us)", RC_PRX_PWM_CHANNEL_COUNT);
	return 0;
}

void rc_prx_pwm_apply_ctrl(const struct rc_link_frame *ctrl)
{
	if (ctrl == NULL || !pwm_ready) {
		return;
	}

	apply_values(ctrl->channels, ctrl->channel_count);
	(void)k_work_reschedule(&failsafe_work, K_MSEC(RC_PWM_FAILSAFE_MS));
}
