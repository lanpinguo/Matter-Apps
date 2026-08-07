/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Status LED on DT alias status-led (P2.07):
 *   lost     — 1 Hz blink (no ESB ACK from PRX)
 *   link     — solid on (CTRL TX ACKed by PRX)
 *   pairing  — rapid blink (OTA PAIR broadcast)
 */

#ifndef RC_PTX_STATUS_LED_H_
#define RC_PTX_STATUS_LED_H_

#include <stdbool.h>

int rc_ptx_status_led_init(void);

/** ISR-safe: PRX ACK'd CTRL — solid on; refresh lost timer. */
void rc_ptx_status_led_on_link(void);

/** OTA PAIR broadcast indication (highest priority over link/lost). */
void rc_ptx_status_led_set_pairing(bool pairing);

#endif /* RC_PTX_STATUS_LED_H_ */
