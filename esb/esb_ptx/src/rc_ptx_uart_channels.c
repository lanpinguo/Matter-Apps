/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_ptx_uart_channels.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "rc_ptx_channels.h"

static uint16_t uart_channel_values[UART_RC_LINK_MAX_CHANNELS];
static size_t uart_channel_count;
static int64_t uart_last_update_ms;

static int read_uart_channel(void *ctx, uint16_t *value)
{
	uintptr_t index = (uintptr_t)ctx;

	if (index >= uart_channel_count) {
		return -ENOENT;
	}

	*value = uart_channel_values[index];
	return 0;
}

static bool uart_channel_slot_available(void *ctx)
{
	uintptr_t index = (uintptr_t)ctx;

	return rc_ptx_uart_channels_active() && index < uart_channel_count;
}

static struct rc_channel_slot uart_slots[UART_RC_LINK_MAX_CHANNELS];

static const struct rc_channel_bank uart_bank = {
	.slots = uart_slots,
	.slot_count = ARRAY_SIZE(uart_slots),
};

void rc_ptx_uart_channels_update(const struct uart_rc_link_ctrl *ctrl)
{
	if (ctrl == NULL || ctrl->channel_count == 0U ||
	    ctrl->channel_count > UART_RC_LINK_MAX_CHANNELS) {
		return;
	}

	uart_channel_count = ctrl->channel_count;

	for (uint8_t i = 0; i < ctrl->channel_count; i++) {
		uart_channel_values[i] = ctrl->channels[i];
	}

	uart_last_update_ms = k_uptime_get();
}

bool rc_ptx_uart_channels_active(void)
{
	if (uart_channel_count == 0U) {
		return false;
	}

	return (k_uptime_get() - uart_last_update_ms) < RC_PTX_UART_LINK_TIMEOUT_MS;
}

const struct rc_channel_bank *rc_ptx_get_active_control_bank(void)
{
	if (rc_ptx_uart_channels_active()) {
		return &uart_bank;
	}

	return &rc_ptx_control_bank;
}

static int rc_ptx_uart_channels_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(uart_slots); i++) {
		uart_slots[i].read = read_uart_channel;
		uart_slots[i].is_available = uart_channel_slot_available;
		uart_slots[i].ctx = (void *)i;
	}

	return 0;
}

SYS_INIT(rc_ptx_uart_channels_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
