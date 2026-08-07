/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_ptx_status_led.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rc_ptx_status_led, CONFIG_ESB_PTX_APP_LOG_LEVEL);

#if DT_NODE_EXISTS(DT_ALIAS(status_led))
#define STATUS_LED_NODE DT_ALIAS(status_led)
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
#define STATUS_LED_AVAILABLE 1
#else
#define STATUS_LED_AVAILABLE 0
#endif

/* 500 ms on / 500 ms off → 1 Hz when link lost. */
#define LOST_PERIOD_MS       1000U
#define PAIRING_PERIOD_MS     160U /* 80 ms on / 80 ms off */
/*
 * After the last CTRL TX_SUCCESS (PRX ACK), wait this long then 1 Hz lost blink.
 * Must be > Hub CTRL / PTX TX interval (~100 ms).
 */
#define LINK_LOST_TIMEOUT_MS  500U

enum status_led_mode {
	STATUS_LED_LOST = 0, /* 1 Hz */
	STATUS_LED_LINK,     /* solid */
	STATUS_LED_PAIRING,  /* rapid */
};

static enum status_led_mode mode = STATUS_LED_LOST;
static bool led_on;
static bool pairing_active;
static struct k_work_delayable blink_work;
static struct k_work_delayable lost_work;

static uint32_t period_for_mode(enum status_led_mode m)
{
	switch (m) {
	case STATUS_LED_PAIRING:
		return PAIRING_PERIOD_MS;
	case STATUS_LED_LOST:
		return LOST_PERIOD_MS;
	case STATUS_LED_LINK:
	default:
		return 0U;
	}
}

static void led_apply(bool on)
{
#if STATUS_LED_AVAILABLE
	(void)gpio_pin_set_dt(&status_led, on ? 1 : 0);
#else
	ARG_UNUSED(on);
#endif
	led_on = on;
}

static void apply_mode(enum status_led_mode next)
{
	uint32_t period;

	mode = next;
	(void)k_work_cancel_delayable(&blink_work);

	period = period_for_mode(mode);
	if (period == 0U) {
		/* LINK: solid on. */
		led_apply(true);
		return;
	}

	led_apply(true);
	(void)k_work_reschedule(&blink_work, K_MSEC(period / 2U));
}

static void blink_work_handler(struct k_work *work)
{
	uint32_t period;

	ARG_UNUSED(work);

	period = period_for_mode(mode);
	if (period == 0U) {
		led_apply(true);
		return;
	}

	led_apply(!led_on);
	(void)k_work_reschedule(&blink_work, K_MSEC(period / 2U));
}

static void lost_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (pairing_active) {
		return;
	}

	apply_mode(STATUS_LED_LOST);
}

int rc_ptx_status_led_init(void)
{
#if !STATUS_LED_AVAILABLE
	LOG_WRN("status-led alias missing — status LED disabled");
	return -ENOENT;
#else
	int err;

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_ERR("status-led GPIO not ready");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (err) {
		LOG_ERR("status-led configure failed: %d", err);
		return err;
	}

	k_work_init_delayable(&blink_work, blink_work_handler);
	k_work_init_delayable(&lost_work, lost_work_handler);
	pairing_active = false;
	apply_mode(STATUS_LED_LOST);
	LOG_WRN("status LED P2.07 — lost 1 Hz / PRX link solid / pair rapid");
	return 0;
#endif
}

void rc_ptx_status_led_on_link(void)
{
#if !STATUS_LED_AVAILABLE
	return;
#else
	if (pairing_active) {
		return;
	}

	(void)k_work_reschedule(&lost_work, K_MSEC(LINK_LOST_TIMEOUT_MS));

	if (mode != STATUS_LED_LINK) {
		apply_mode(STATUS_LED_LINK);
	}
#endif
}

void rc_ptx_status_led_set_pairing(bool pairing)
{
#if !STATUS_LED_AVAILABLE
	ARG_UNUSED(pairing);
	return;
#else
	pairing_active = pairing;
	(void)k_work_cancel_delayable(&lost_work);

	if (pairing) {
		apply_mode(STATUS_LED_PAIRING);
		return;
	}

	apply_mode(STATUS_LED_LOST);
#endif
}
