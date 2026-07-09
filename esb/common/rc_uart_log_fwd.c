/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_uart_log_fwd.h"

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_backend_std.h>

LOG_MODULE_REGISTER(rc_uart_log_fwd, CONFIG_LOG_DEFAULT_LEVEL);

#define LOG_LINE_BUF_SIZE 160U

static struct uart_rc_link *uart_link;
static uint8_t line_buf[LOG_LINE_BUF_SIZE];
static uint8_t out_buf[LOG_LINE_BUF_SIZE];
static uint8_t current_level;
static uint8_t debug_seq;
static bool forward_enabled;
static bool local_enabled;

static void send_debug_fragment(uint8_t level, const char *text, size_t len,
				uint8_t frag_idx, bool more)
{
	struct uart_rc_debug_log log;

	if (uart_link == NULL || len == 0U) {
		return;
	}

	memset(&log, 0, sizeof(log));
	log.seq = debug_seq++;
	log.level = level;
	log.flags = frag_idx & 0x7FU;
	if (more) {
		log.flags |= UART_RC_DEBUG_FLAG_MORE;
	}

	while (len > 0U) {
		size_t chunk = MIN(len, UART_RC_LINK_DEBUG_MAX_TEXT);

		log.text_len = (uint8_t)chunk;
		memcpy(log.text, text, chunk);
		(void)uart_rc_link_send_debug_log(uart_link, &log);

		text += chunk;
		len -= chunk;
		log.flags = (++frag_idx) & 0x7FU;
		if (len > 0U) {
			log.flags |= UART_RC_DEBUG_FLAG_MORE;
		}
	}
}

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	for (size_t i = 0; i < length; i++) {
		uint8_t c = data[i];

		if (c == '\r') {
			continue;
		}

		if (c == '\n') {
			if (line_buf[0] != '\0') {
				send_debug_fragment(current_level, (const char *)line_buf,
						    strlen((const char *)line_buf), 0, false);
			}
			line_buf[0] = '\0';
			continue;
		}

		size_t pos = strlen((const char *)line_buf);

		if (pos >= (LOG_LINE_BUF_SIZE - 1U)) {
			send_debug_fragment(current_level, (const char *)line_buf, pos, 0, true);
			line_buf[0] = '\0';
			pos = 0U;
		}

		line_buf[pos] = c;
		line_buf[pos + 1U] = '\0';
	}

	return (int)length;
}

LOG_OUTPUT_DEFINE(rc_uart_fwd_output, char_out, out_buf, LOG_LINE_BUF_SIZE);

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);

	if (!forward_enabled) {
		return;
	}

	current_level = msg->log.hdr.desc.level;
	log_output_msg_process(&rc_uart_fwd_output, &msg->log, false);
}

static void panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	log_backend_std_panic(&rc_uart_fwd_output);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	log_backend_std_dropped(&rc_uart_fwd_output, cnt);
}

static const struct log_backend_api rc_uart_log_backend_api = {
	.process = process,
	.panic = panic,
	.dropped = dropped,
};

LOG_BACKEND_DEFINE(rc_uart_log_backend, rc_uart_log_backend_api, true);

int rc_uart_log_fwd_init(struct uart_rc_link *link)
{
	if (link == NULL) {
		return -EINVAL;
	}

	uart_link = link;
	line_buf[0] = '\0';
	forward_enabled = false;
	local_enabled = true;

	return 0;
}

int rc_uart_log_fwd_apply(const struct uart_rc_debug_ctrl *ctrl)
{
	if (ctrl == NULL) {
		return -EINVAL;
	}

	forward_enabled = (ctrl->flags & UART_RC_DEBUG_FLAG_FORWARD) != 0U;
	local_enabled = (ctrl->flags & UART_RC_DEBUG_FLAG_LOCAL) != 0U;

	LOG_INF("Debug forward=%d local=%d level=%u", forward_enabled, local_enabled,
		ctrl->level);
	return 0;
}
