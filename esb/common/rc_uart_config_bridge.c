/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_uart_config_bridge.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include "rc_esb_radio.h"
#include "rc_uart_esb_req_work.h"
#include "rc_uart_log_fwd.h"
#include "uart_rc_link.h"

LOG_MODULE_REGISTER(rc_uart_cfg_bridge, CONFIG_LOG_DEFAULT_LEVEL);

static struct uart_rc_link uart_link;

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

int rc_uart_config_bridge_init(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	struct uart_rc_link_handlers handlers = {
		.on_ctrl = NULL,
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

	LOG_INF("UART config bridge ready (HDLC 0x%02x)", UART_RC_LINK_HDLC_FLAG);
	return 0;
}

struct uart_rc_link *rc_uart_config_bridge_link(void)
{
	return &uart_link;
}
