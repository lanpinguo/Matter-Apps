/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_CHANNEL_BANK_H_
#define RC_CHANNEL_BANK_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "rc_link.h"

typedef int (*rc_channel_read_fn)(void *ctx, uint16_t *value);
typedef bool (*rc_channel_available_fn)(void *ctx);

struct rc_channel_slot {
	rc_channel_read_fn read;
	rc_channel_available_fn is_available;
	void *ctx;
};

struct rc_channel_bank {
	const struct rc_channel_slot *slots;
	size_t slot_count;
};

uint8_t rc_channel_bank_sample(const struct rc_channel_bank *bank,
			       uint16_t *values, uint8_t max_values);

int rc_link_frame_fill(struct rc_link_frame *frame, uint8_t type, uint8_t seq, uint8_t flags,
		       const struct rc_channel_bank *bank);

void rc_link_log_channels(const char *prefix, const struct rc_link_frame *frame);

#endif /* RC_CHANNEL_BANK_H_ */
