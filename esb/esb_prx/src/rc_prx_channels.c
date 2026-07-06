/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_prx_channels.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

static uint8_t *ctrl_seq_ref;

static int read_battery_mv(void *ctx, uint16_t *value)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000U);

	ARG_UNUSED(ctx);
	*value = (uint16_t)(4200U - (uptime_s % 300U));
	return 0;
}

static int read_altitude_dm(void *ctx, uint16_t *value)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000U);

	ARG_UNUSED(ctx);
	*value = (uint16_t)(100U + (uptime_s % 50U));
	return 0;
}

static int read_speed_dmps(void *ctx, uint16_t *value)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000U);

	ARG_UNUSED(ctx);
	*value = (uint16_t)(80U + (uptime_s % 20U));
	return 0;
}

static int read_rx_ctrl_seq(void *ctx, uint16_t *value)
{
	ARG_UNUSED(ctx);
	*value = ctrl_seq_ref ? *ctrl_seq_ref : 0U;
	return 0;
}

static bool gps_available(void *ctx)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000U);

	ARG_UNUSED(ctx);
	/* Demo optional channel: available after 30 s uptime. */
	return uptime_s >= 30U;
}

static int read_gps_satellites(void *ctx, uint16_t *value)
{
	uint32_t uptime_s = (uint32_t)(k_uptime_get() / 1000U);

	ARG_UNUSED(ctx);
	*value = (uint16_t)(6U + (uptime_s % 4U));
	return 0;
}

static const struct rc_channel_slot prx_slots[] = {
	{ read_battery_mv, NULL, NULL },
	{ read_altitude_dm, NULL, NULL },
	{ read_speed_dmps, NULL, NULL },
	{ read_rx_ctrl_seq, NULL, NULL },
	{ read_gps_satellites, gps_available, NULL },
};

const struct rc_channel_bank rc_prx_status_bank = {
	.slots = prx_slots,
	.slot_count = ARRAY_SIZE(prx_slots),
};

void rc_prx_channels_bind_ctrl_seq(uint8_t *seq)
{
	ctrl_seq_ref = seq;
}
