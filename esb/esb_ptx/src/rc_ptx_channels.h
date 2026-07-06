/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_PTX_CHANNELS_H_
#define RC_PTX_CHANNELS_H_

#include <stdint.h>

#include "rc_channel_bank.h"

extern const struct rc_channel_bank rc_ptx_control_bank;

void rc_ptx_channels_bind_seq(uint8_t *seq);

#endif /* RC_PTX_CHANNELS_H_ */
