/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_uart_bridge.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "rc_esb_radio.h"
#include "rc_ptx_uart_channels.h"
#include "rc_uart_esb_req_work.h"
#include "rc_uart_log_fwd.h"
#include "uart_rc_link.h"

LOG_MODULE_REGISTER(rc_uart_bridge, CONFIG_ESB_PTX_APP_LOG_LEVEL);

static struct uart_rc_link uart_link;
static uint8_t uart_status_seq;

static void on_uart_ctrl(const struct uart_rc_link_ctrl *ctrl, void *user_data)
{
	ARG_UNUSED(user_data);

	rc_ptx_uart_channels_update(ctrl);
	LOG_DBG("UART ctrl seq=%u count=%u", ctrl->seq, ctrl->channel_count);
}

static void on_uart_esb_req(const struct uart_rc_esb_req *req, void *user_data)
{
	ARG_UNUSED(user_data);

	(void)rc_uart_esb_req_submit(&uart_link, req);
}

static void on_uart_debug_ctrl(const struct uart_rc_debug_ctrl *ctrl, void *user_data)
{
	ARG_UNUSED(user_data);

	(void)rc_uart_log_fwd_apply(ctrl);
}

int rc_uart_bridge_init(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	struct uart_rc_link_handlers handlers = {
		.on_ctrl = on_uart_ctrl,
		.on_status = NULL,
		.on_esb_req = on_uart_esb_req,
		.on_esb_rsp = NULL,
		.on_debug_ctrl = on_uart_debug_ctrl,
		.on_debug_log = NULL,
		.user_data = NULL,
	};
	int err;

	err = rc_uart_esb_req_work_init();
	if (err != 0) {
		return err;
	}

	err = uart_rc_link_init(&uart_link, uart, &handlers);
	if (err != 0) {
		return err;
	}

	err = rc_uart_log_fwd_init(&uart_link);
	if (err != 0) {
		return err;
	}

	err = uart_rc_link_start_rx(&uart_link);
	if (err != 0) {
		LOG_ERR("UART RC link RX failed: %d", err);
		return err;
	}

	LOG_DBG("UART RC link on console (HDLC 0x%02x, multiplexed with logs)",
		UART_RC_LINK_HDLC_FLAG);
	return 0;
}

struct uart_rc_link *rc_uart_bridge_link(void)
{
	return &uart_link;
}

void rc_uart_bridge_on_esb_status(const struct rc_link_frame *status)
{
	struct uart_rc_link_status uart_status;

	if (status == NULL || status->type != RC_LINK_TYPE_STATUS) {
		return;
	}

	uart_status.seq = uart_status_seq++;
	uart_status.roll = 0;
	uart_status.pitch = 0;
	uart_status.yaw = 0;
	uart_status.battery_mv = 0U;
	uart_status.flags = status->flags;

	if (status->channel_count > 0U) {
		uart_status.battery_mv = status->channels[0];
	}
	if (status->channel_count > 1U) {
		uart_status.roll = (int16_t)status->channels[1];
	}
	if (status->channel_count > 2U) {
		uart_status.pitch = (int16_t)status->channels[2];
	}

	(void)uart_rc_link_send_status(&uart_link, &uart_status);
}
