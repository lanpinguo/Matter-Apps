/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Status LED on DT alias status-led (P2.07):
 *   lost      — 1 Hz blink (no CTRL / boot)
 *   idle      — solid on (CTRL keepalive, channels unchanged)
 *   activity  — brief off pulse per active CTRL frame, then idle solid
 *   pairing   — rapid blink
 */

#ifndef RC_PRX_STATUS_LED_H_
#define RC_PRX_STATUS_LED_H_

#include <stdbool.h>

int rc_prx_status_led_init(void);

/**
 * ISR-safe: any CTRL RX (incl. keepalive).
 * Solid on while linked; refreshes lost timer. Does not flash.
 */
void rc_prx_status_led_on_link(void);

/**
 * ISR-safe: CTRL with channel-value change — flash once (brief off), then solid.
 */
void rc_prx_status_led_on_activity(void);

/** Pair-mode indication (highest priority). */
void rc_prx_status_led_set_pairing(bool pairing);

#endif /* RC_PRX_STATUS_LED_H_ */
