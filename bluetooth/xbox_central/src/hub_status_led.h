/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * System status LED on P2.07 (DT alias status-led).
 */

#ifndef HUB_STATUS_LED_H_
#define HUB_STATUS_LED_H_

#include <stdbool.h>

enum hub_status_led_mode {
	/** LED off. */
	HUB_STATUS_LED_OFF = 0,
	/** Slow blink: firmware running, idle / scanning. */
	HUB_STATUS_LED_IDLE,
	/** Faster blink: Xbox (or primary link) connected. */
	HUB_STATUS_LED_ACTIVE,
	/** Solid on: fault / attention. */
	HUB_STATUS_LED_FAULT,
};

/**
 * Configure GPIO from DT alias status-led and start idle heartbeat.
 * Safe to call when alias is missing (returns -ENOENT, no-op).
 */
int hub_status_led_init(void);

void hub_status_led_set_mode(enum hub_status_led_mode mode);

/** Convenience: ACTIVE when @p connected, else IDLE (keeps FAULT if set). */
void hub_status_led_set_xbox_connected(bool connected);

#endif /* HUB_STATUS_LED_H_ */
