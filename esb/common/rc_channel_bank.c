/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_channel_bank.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rc_channel_bank, CONFIG_LOG_DEFAULT_LEVEL);

uint8_t rc_channel_bank_sample(const struct rc_channel_bank *bank,
				 uint16_t *values, uint8_t max_values)
{
	uint8_t count = 0;

	if (bank == NULL || values == NULL || bank->slots == NULL) {
		return 0;
	}

	for (size_t i = 0; i < bank->slot_count && count < max_values; i++) {
		const struct rc_channel_slot *slot = &bank->slots[i];
		int err;

		if (slot->read == NULL) {
			continue;
		}

		if (slot->is_available != NULL && !slot->is_available(slot->ctx)) {
			continue;
		}

		err = slot->read(slot->ctx, &values[count]);
		if (err != 0) {
			continue;
		}

		count++;
	}

	return count;
}

int rc_link_frame_fill(struct rc_link_frame *frame, uint8_t type, uint8_t seq, uint8_t flags,
		       const struct rc_channel_bank *bank)
{
	uint8_t count;

	if (frame == NULL || bank == NULL) {
		return -EINVAL;
	}

	count = rc_channel_bank_sample(bank, frame->channels, RC_LINK_MAX_CHANNELS);
	if (count == 0U) {
		return -ENOENT;
	}

	frame->magic = RC_LINK_MAGIC;
	frame->type = type;
	frame->seq = seq;
	frame->flags = flags;
	frame->channel_count = count;

	return 0;
}

void rc_link_log_channels(const char *prefix, const struct rc_link_frame *frame)
{
	if (frame == NULL) {
		return;
	}

	LOG_DBG("%s seq=%u count=%u flags=0x%02x", prefix, frame->seq,
		frame->channel_count, frame->flags);

	for (uint8_t i = 0; i < frame->channel_count; i++) {
		LOG_DBG("  ch%u=%u", i, frame->channels[i]);
	}
}
