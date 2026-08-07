/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Heartbeat / status indication on DT alias status-led (P2.07).
 */

#include "hub_status_led.h"

#include "hub_log.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

HUB_LOG_MODULE_DEFINE(HUB_MOD_SYS);

#if DT_NODE_EXISTS(DT_ALIAS(status_led))
#define STATUS_LED_NODE DT_ALIAS(status_led)
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios);
#define STATUS_LED_AVAILABLE 1
#else
#define STATUS_LED_AVAILABLE 0
#endif

#define IDLE_PERIOD_MS     1000U /* 500 ms on / 500 ms off */
#define ACTIVE_PERIOD_MS    400U /* 200 ms on / 200 ms off */
#define PAIRING_PERIOD_MS   160U /* 80 ms on / 80 ms off — distinct rapid blink */
#define FAULT_PERIOD_MS       0U /* solid */

static enum hub_status_led_mode mode = HUB_STATUS_LED_OFF;
static bool led_on;
static bool xbox_connected;
static bool fault_latched;
static bool pairing_active;
static struct k_work_delayable blink_work;

static uint32_t period_for_mode(enum hub_status_led_mode m)
{
	switch (m) {
	case HUB_STATUS_LED_PAIRING:
		return PAIRING_PERIOD_MS;
	case HUB_STATUS_LED_ACTIVE:
		return ACTIVE_PERIOD_MS;
	case HUB_STATUS_LED_FAULT:
		return FAULT_PERIOD_MS;
	case HUB_STATUS_LED_IDLE:
		return IDLE_PERIOD_MS;
	case HUB_STATUS_LED_OFF:
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

static void blink_work_handler(struct k_work *work)
{
	uint32_t period;

	ARG_UNUSED(work);

	period = period_for_mode(mode);
	if (period == 0U) {
		/* OFF or FAULT: solid state already applied. */
		return;
	}

	led_apply(!led_on);
	(void)k_work_reschedule(&blink_work, K_MSEC(period / 2U));
}

static void apply_mode(enum hub_status_led_mode next)
{
	uint32_t period;

	mode = next;
	(void)k_work_cancel_delayable(&blink_work);

	period = period_for_mode(mode);
	if (mode == HUB_STATUS_LED_OFF) {
		led_apply(false);
		return;
	}
	if (mode == HUB_STATUS_LED_FAULT || period == 0U) {
		led_apply(true);
		return;
	}

	led_apply(true);
	(void)k_work_reschedule(&blink_work, K_MSEC(period / 2U));
}

int hub_status_led_init(void)
{
#if !STATUS_LED_AVAILABLE
	HUB_WRN("status-led alias missing — status LED disabled\n");
	return -ENOENT;
#else
	int err;

	if (!gpio_is_ready_dt(&status_led)) {
		HUB_ERR("status-led GPIO not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (err) {
		HUB_ERR("status-led configure failed: %d\n", err);
		return err;
	}

	k_work_init_delayable(&blink_work, blink_work_handler);
	xbox_connected = false;
	fault_latched = false;
	pairing_active = false;
	apply_mode(HUB_STATUS_LED_IDLE);
	HUB_INF("status LED on P2.07 — idle heartbeat\n");
	return 0;
#endif
}

void hub_status_led_set_mode(enum hub_status_led_mode next)
{
#if !STATUS_LED_AVAILABLE
	ARG_UNUSED(next);
	return;
#else
	if (next == HUB_STATUS_LED_FAULT) {
		fault_latched = true;
		pairing_active = false;
	} else if (next == HUB_STATUS_LED_OFF) {
		fault_latched = false;
		xbox_connected = false;
		pairing_active = false;
	} else if (next == HUB_STATUS_LED_PAIRING) {
		fault_latched = false;
		pairing_active = true;
	} else {
		fault_latched = false;
		pairing_active = false;
		xbox_connected = (next == HUB_STATUS_LED_ACTIVE);
	}
	apply_mode(next);
#endif
}

void hub_status_led_set_xbox_connected(bool connected)
{
#if !STATUS_LED_AVAILABLE
	ARG_UNUSED(connected);
	return;
#else
	xbox_connected = connected;
	if (fault_latched || pairing_active) {
		return;
	}
	apply_mode(connected ? HUB_STATUS_LED_ACTIVE : HUB_STATUS_LED_IDLE);
#endif
}

void hub_status_led_set_pairing(bool pairing)
{
#if !STATUS_LED_AVAILABLE
	ARG_UNUSED(pairing);
	return;
#else
	pairing_active = pairing;
	if (fault_latched) {
		return;
	}
	if (pairing) {
		apply_mode(HUB_STATUS_LED_PAIRING);
		return;
	}
	apply_mode(xbox_connected ? HUB_STATUS_LED_ACTIVE : HUB_STATUS_LED_IDLE);
#endif
}
