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
	/** 1 Hz blink: link lost / idle / scanning. */
	HUB_STATUS_LED_IDLE,
	/** Solid on: Xbox (or primary link) connected and stable. */
	HUB_STATUS_LED_ACTIVE,
	/** Rapid blink: ESB OTA pairing in progress. */
	HUB_STATUS_LED_PAIRING,
	/** Solid on: fault / attention. */
	HUB_STATUS_LED_FAULT,
};

/**
 * Configure GPIO from DT alias status-led and start 1 Hz idle blink.
 * Safe to call when alias is missing (returns -ENOENT, no-op).
 */
int hub_status_led_init(void);

void hub_status_led_set_mode(enum hub_status_led_mode mode);

/** Convenience: solid on when @p connected, else 1 Hz idle (keeps FAULT/PAIRING). */
void hub_status_led_set_xbox_connected(bool connected);

/** Rapid blink while ESB OTA pair session is active; restores idle/active when done. */
void hub_status_led_set_pairing(bool pairing);

#endif /* HUB_STATUS_LED_H_ */
