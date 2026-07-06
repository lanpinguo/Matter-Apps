/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_PRX_CHANNELS_H_
#define RC_PRX_CHANNELS_H_

#include <stdint.h>

#include "rc_channel_bank.h"

extern const struct rc_channel_bank rc_prx_status_bank;

void rc_prx_channels_bind_ctrl_seq(uint8_t *seq);

#endif /* RC_PRX_CHANNELS_H_ */
