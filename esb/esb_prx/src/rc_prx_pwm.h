/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Map the first N control channels to RC-style PWM outputs (50 Hz).
 */
#ifndef RC_PRX_PWM_H_
#define RC_PRX_PWM_H_

#include <stdint.h>

#include "rc_link.h"

#define RC_PRX_PWM_CHANNEL_COUNT 5U

int rc_prx_pwm_init(void);

/** Update PWM pulse widths from a CTRL frame (channels 0..N-1, 0..1000). */
void rc_prx_pwm_apply_ctrl(const struct rc_link_frame *ctrl);

#endif /* RC_PRX_PWM_H_ */
