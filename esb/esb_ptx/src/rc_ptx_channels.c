/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_ptx_channels.h"

#include <zephyr/sys/util.h>

static uint8_t *ctrl_seq_ref;

struct ptx_base_ctx {
	uint16_t base;
};

static int read_scaled_base(void *ctx, uint16_t *value)
{
	const struct ptx_base_ctx *cfg = ctx;

	*value = (uint16_t)(cfg->base + (ctrl_seq_ref ? (*ctrl_seq_ref % 100U) : 0U));
	return 0;
}

static int read_fixed(void *ctx, uint16_t *value)
{
	*value = (uint16_t)(uintptr_t)ctx;
	return 0;
}

static struct ptx_base_ctx aileron_cfg = {
	.base = 1500,
};

static const struct rc_channel_slot ptx_slots[] = {
	{ read_fixed, NULL, (void *)(uintptr_t)1000 },
	{ read_scaled_base, NULL, &aileron_cfg },
	{ read_fixed, NULL, (void *)(uintptr_t)1500 },
	{ read_fixed, NULL, (void *)(uintptr_t)1500 },
	{ read_fixed, NULL, (void *)(uintptr_t)1800 },
	{ read_fixed, NULL, (void *)(uintptr_t)1200 },
};

const struct rc_channel_bank rc_ptx_control_bank = {
	.slots = ptx_slots,
	.slot_count = ARRAY_SIZE(ptx_slots),
};

void rc_ptx_channels_bind_seq(uint8_t *seq)
{
	ctrl_seq_ref = seq;
}
