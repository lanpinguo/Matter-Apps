/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_uart_esb_req_work.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "rc_esb_radio.h"

LOG_MODULE_REGISTER(rc_uart_esb_req_work, CONFIG_LOG_DEFAULT_LEVEL);

struct rc_uart_esb_req_msg {
	struct uart_rc_esb_req req;
	struct uart_rc_link *link;
};

#define RC_UART_ESB_REQ_MSGQ_LEN 4U

K_MSGQ_DEFINE(rc_uart_esb_req_msgq, sizeof(struct rc_uart_esb_req_msg),
	      RC_UART_ESB_REQ_MSGQ_LEN, 4);

static struct k_work rc_uart_esb_req_work;

static void rc_uart_esb_req_work_handler(struct k_work *work)
{
	struct rc_uart_esb_req_msg msg;
	struct uart_rc_esb_rsp rsp;

	ARG_UNUSED(work);

	while (k_msgq_get(&rc_uart_esb_req_msgq, &msg, K_NO_WAIT) == 0) {
		(void)rc_esb_radio_handle_req(&msg.req, &rsp);
		(void)uart_rc_link_send_esb_rsp(msg.link, &rsp);
	}

	if (k_msgq_num_used_get(&rc_uart_esb_req_msgq) > 0U) {
		(void)k_work_submit(&rc_uart_esb_req_work);
	}
}

int rc_uart_esb_req_work_init(void)
{
	k_work_init(&rc_uart_esb_req_work, rc_uart_esb_req_work_handler);
	return 0;
}

int rc_uart_esb_req_submit(struct uart_rc_link *link, const struct uart_rc_esb_req *req)
{
	struct rc_uart_esb_req_msg msg;
	int err;

	if (link == NULL || req == NULL) {
		return -EINVAL;
	}

	msg.link = link;
	msg.req = *req;

	err = k_msgq_put(&rc_uart_esb_req_msgq, &msg, K_NO_WAIT);
	if (err != 0) {
		LOG_WRN("ESB req queue full (cmd=0x%02x)", req->cmd);
		return err;
	}

	(void)k_work_submit(&rc_uart_esb_req_work);
	return 0;
}
