/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Status LED on DT alias status-led (P2.07):
 *   off       — no CTRL / link lost / boot
 *   link      — CTRL present (incl. keepalive): slow blink 0.5 Hz
 *   activity  — channel values changed: single flash, then back to link
 *   pairing   — rapid blink
 */

#ifndef RC_PRX_STATUS_LED_H_
#define RC_PRX_STATUS_LED_H_

#include <stdbool.h>

int rc_prx_status_led_init(void);

/**
 * ISR-safe: any CTRL RX (incl. keepalive).
 * Starts/keeps link slow blink; refreshes lost timer. Does not flash.
 */
void rc_prx_status_led_on_link(void);

/**
 * ISR-safe: CTRL with channel-value change — flash once, then link blink.
 */
void rc_prx_status_led_on_activity(void);

/** Pair-mode indication (highest priority). */
void rc_prx_status_led_set_pairing(bool pairing);

#endif /* RC_PRX_STATUS_LED_H_ */
