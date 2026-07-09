/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "uart_rc_link.h"

#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/byteorder.h>

uint16_t uart_rc_link_hdlc_fcs16(const uint8_t *buf, size_t len)
{
	uint16_t fcs = 0xFFFFU;
	size_t i;

	for (i = 0; i < len; i++) {
		fcs ^= buf[i];
		for (uint8_t bit = 0U; bit < 8U; bit++) {
			if ((fcs & 0x0001U) != 0U) {
				fcs = (fcs >> 1) ^ 0x8408U;
			} else {
				fcs >>= 1;
			}
		}
	}

	return fcs;
}

static void uart_rc_link_reset(struct uart_rc_link *link)
{
	link->rx_in_frame = 0U;
	link->rx_escape = 0U;
	link->rx_raw_pos = 0U;
}

static void uart_rc_link_hdlc_put_raw(struct uart_rc_link *link, uint8_t byte)
{
	if (byte == UART_RC_LINK_HDLC_FLAG || byte == UART_RC_LINK_HDLC_ESC) {
		uart_poll_out(link->uart, UART_RC_LINK_HDLC_ESC);
		uart_poll_out(link->uart, byte ^ UART_RC_LINK_HDLC_XOR);
	} else {
		uart_poll_out(link->uart, byte);
	}
}

static int uart_rc_link_hdlc_send(struct uart_rc_link *link, const uint8_t *data,
				  size_t len)
{
	uint16_t fcs;
	uint8_t fcs_lo;
	uint8_t fcs_hi;

	if (link == NULL || link->uart == NULL) {
		return -EINVAL;
	}

	if (len == 0U || data == NULL) {
		return -EINVAL;
	}

	fcs = uart_rc_link_hdlc_fcs16(data, len) ^ 0xFFFFU;
	fcs_lo = (uint8_t)(fcs & 0xFFU);
	fcs_hi = (uint8_t)(fcs >> 8);

	uart_poll_out(link->uart, UART_RC_LINK_HDLC_FLAG);

	for (size_t i = 0; i < len; i++) {
		uart_rc_link_hdlc_put_raw(link, data[i]);
	}

	uart_rc_link_hdlc_put_raw(link, fcs_lo);
	uart_rc_link_hdlc_put_raw(link, fcs_hi);
	uart_poll_out(link->uart, UART_RC_LINK_HDLC_FLAG);

	return 0;
}

static void uart_rc_link_dispatch(struct uart_rc_link *link)
{
	struct uart_rc_link_ctrl ctrl;
	struct uart_rc_link_status status;
	struct uart_rc_esb_req esb_req;
	struct uart_rc_esb_rsp esb_rsp;
	struct uart_rc_debug_ctrl debug_ctrl;
	struct uart_rc_debug_log debug_log;
	uint8_t type;
	uint8_t len;
	uint16_t rx_fcs;
	uint16_t calc_fcs;
	const uint8_t *payload;

	if (link->rx_raw_pos < 4U) {
		return;
	}

	type = link->rx_raw[0];
	len = link->rx_raw[1];

	if (len > UART_RC_LINK_MAX_PAYLOAD ||
	    link->rx_raw_pos < (uint8_t)(2U + len + 2U)) {
		return;
	}

	rx_fcs = (uint16_t)link->rx_raw[2U + len] |
		 ((uint16_t)link->rx_raw[2U + len + 1U] << 8);
	calc_fcs = uart_rc_link_hdlc_fcs16(link->rx_raw, 2U + len) ^ 0xFFFFU;

	if (rx_fcs != calc_fcs) {
		return;
	}

	payload = &link->rx_raw[2];

	if (type == UART_RC_LINK_TYPE_CTRL && link->handlers.on_ctrl != NULL &&
	    uart_rc_link_decode_ctrl(payload, len, &ctrl) == 0) {
		link->handlers.on_ctrl(&ctrl, link->handlers.user_data);
	} else if (type == UART_RC_LINK_TYPE_STATUS &&
		   link->handlers.on_status != NULL &&
		   uart_rc_link_decode_status(payload, len, &status) == 0) {
		link->handlers.on_status(&status, link->handlers.user_data);
	} else if (type == UART_RC_LINK_TYPE_ESB_REQ &&
		   link->handlers.on_esb_req != NULL &&
		   uart_rc_link_decode_esb_req(payload, len, &esb_req) == 0) {
		link->handlers.on_esb_req(&esb_req, link->handlers.user_data);
	} else if (type == UART_RC_LINK_TYPE_ESB_RSP &&
		   link->handlers.on_esb_rsp != NULL &&
		   uart_rc_link_decode_esb_rsp(payload, len, &esb_rsp) == 0) {
		link->handlers.on_esb_rsp(&esb_rsp, link->handlers.user_data);
	} else if (type == UART_RC_LINK_TYPE_DEBUG_CTRL &&
		   link->handlers.on_debug_ctrl != NULL &&
		   uart_rc_link_decode_debug_ctrl(payload, len, &debug_ctrl) == 0) {
		link->handlers.on_debug_ctrl(&debug_ctrl, link->handlers.user_data);
	} else if (type == UART_RC_LINK_TYPE_DEBUG_LOG &&
		   link->handlers.on_debug_log != NULL &&
		   uart_rc_link_decode_debug_log(payload, len, &debug_log) == 0) {
		link->handlers.on_debug_log(&debug_log, link->handlers.user_data);
	}
}

int uart_rc_link_encode_ctrl(const struct uart_rc_link_ctrl *ctrl,
			     uint8_t *payload, size_t payload_len)
{
	size_t need;

	if (ctrl == NULL || payload == NULL) {
		return -EINVAL;
	}

	if (ctrl->channel_count == 0U ||
	    ctrl->channel_count > UART_RC_LINK_MAX_CHANNELS) {
		return -EINVAL;
	}

	need = 2U + ((size_t)ctrl->channel_count * sizeof(uint16_t));
	if (payload_len < need) {
		return -ENOMEM;
	}

	payload[0] = ctrl->seq;
	payload[1] = ctrl->channel_count;

	for (uint8_t i = 0; i < ctrl->channel_count; i++) {
		sys_put_le16(ctrl->channels[i], &payload[2U + (i * 2U)]);
	}

	return (int)need;
}

int uart_rc_link_decode_ctrl(const uint8_t *payload, size_t payload_len,
			     struct uart_rc_link_ctrl *ctrl)
{
	uint8_t count;

	if (payload == NULL || ctrl == NULL || payload_len < 2U) {
		return -EINVAL;
	}

	count = payload[1];
	if (count == 0U || count > UART_RC_LINK_MAX_CHANNELS) {
		return -EINVAL;
	}

	if (payload_len < (2U + ((size_t)count * sizeof(uint16_t)))) {
		return -EINVAL;
	}

	ctrl->seq = payload[0];
	ctrl->channel_count = count;

	for (uint8_t i = 0; i < count; i++) {
		ctrl->channels[i] = sys_get_le16(&payload[2U + (i * 2U)]);
	}

	return 0;
}

int uart_rc_link_encode_status(const struct uart_rc_link_status *status,
			       uint8_t *payload, size_t payload_len)
{
	if (status == NULL || payload == NULL || payload_len < 10U) {
		return -EINVAL;
	}

	payload[0] = status->seq;
	sys_put_le16((uint16_t)status->roll, &payload[1]);
	sys_put_le16((uint16_t)status->pitch, &payload[3]);
	sys_put_le16((uint16_t)status->yaw, &payload[5]);
	sys_put_le16(status->battery_mv, &payload[7]);
	payload[9] = status->flags;

	return 10;
}

int uart_rc_link_decode_status(const uint8_t *payload, size_t payload_len,
			       struct uart_rc_link_status *status)
{
	if (status == NULL || payload == NULL || payload_len < 10U) {
		return -EINVAL;
	}

	status->seq = payload[0];
	status->roll = (int16_t)sys_get_le16(&payload[1]);
	status->pitch = (int16_t)sys_get_le16(&payload[3]);
	status->yaw = (int16_t)sys_get_le16(&payload[5]);
	status->battery_mv = sys_get_le16(&payload[7]);
	status->flags = payload[9];

	return 0;
}

static int uart_rc_link_encode_msg3(uint8_t b0, uint8_t b1, uint8_t b2,
				    const uint8_t *data, uint8_t data_len,
				    uint8_t *payload, size_t payload_len)
{
	if (payload == NULL || data_len > UART_RC_ESB_DATA_MAX) {
		return -EINVAL;
	}

	if (data_len > 0U && data == NULL) {
		return -EINVAL;
	}

	if (payload_len < (size_t)(3U + data_len)) {
		return -ENOMEM;
	}

	payload[0] = b0;
	payload[1] = b1;
	payload[2] = b2;

	if (data_len > 0U) {
		memcpy(&payload[3], data, data_len);
	}

	return (int)(3U + data_len);
}

static int uart_rc_link_decode_msg3(const uint8_t *payload, size_t payload_len,
				    uint8_t *b0, uint8_t *b1, uint8_t *b2,
				    const uint8_t **data, uint8_t *data_len)
{
	if (payload == NULL || payload_len < 3U || b0 == NULL || b1 == NULL ||
	    b2 == NULL || data == NULL || data_len == NULL) {
		return -EINVAL;
	}

	*b0 = payload[0];
	*b1 = payload[1];
	*b2 = payload[2];
	*data_len = payload[2];

	if (*data_len > UART_RC_ESB_DATA_MAX ||
	    payload_len < (size_t)(3U + *data_len)) {
		return -EINVAL;
	}

	*data = &payload[3];
	return 0;
}

int uart_rc_link_encode_esb_config(const struct uart_rc_esb_config *cfg,
				   uint8_t *payload, size_t payload_len)
{
	if (cfg == NULL || payload == NULL || payload_len < sizeof(*cfg)) {
		return -EINVAL;
	}

	memcpy(payload, cfg, sizeof(*cfg));
	return (int)sizeof(*cfg);
}

int uart_rc_link_decode_esb_config(const uint8_t *payload, size_t payload_len,
				   struct uart_rc_esb_config *cfg)
{
	if (payload == NULL || cfg == NULL || payload_len < sizeof(*cfg)) {
		return -EINVAL;
	}

	memcpy(cfg, payload, sizeof(*cfg));
	return 0;
}

int uart_rc_link_encode_esb_req(const struct uart_rc_esb_req *req,
				uint8_t *payload, size_t payload_len)
{
	if (req == NULL) {
		return -EINVAL;
	}

	return uart_rc_link_encode_msg3(req->seq, req->cmd, req->data_len, req->data,
					req->data_len, payload, payload_len);
}

int uart_rc_link_decode_esb_req(const uint8_t *payload, size_t payload_len,
				struct uart_rc_esb_req *req)
{
	const uint8_t *data;
	uint8_t data_len;
	int err;

	if (req == NULL) {
		return -EINVAL;
	}

	err = uart_rc_link_decode_msg3(payload, payload_len, &req->seq, &req->cmd,
				       &req->data_len, &data, &data_len);
	if (err != 0) {
		return err;
	}

	memcpy(req->data, data, data_len);
	return 0;
}

int uart_rc_link_encode_esb_rsp(const struct uart_rc_esb_rsp *rsp,
				uint8_t *payload, size_t payload_len)
{
	uint8_t hdr[3];

	if (rsp == NULL) {
		return -EINVAL;
	}

	hdr[0] = rsp->seq;
	hdr[1] = rsp->cmd;
	hdr[2] = (uint8_t)rsp->status;

	if (payload_len < (size_t)(3U + rsp->data_len)) {
		return -ENOMEM;
	}

	memcpy(payload, hdr, sizeof(hdr));
	payload[3] = rsp->data_len;

	if (rsp->data_len > 0U) {
		memcpy(&payload[4], rsp->data, rsp->data_len);
	}

	return (int)(4U + rsp->data_len);
}

int uart_rc_link_decode_esb_rsp(const uint8_t *payload, size_t payload_len,
				struct uart_rc_esb_rsp *rsp)
{
	if (rsp == NULL || payload_len < 4U) {
		return -EINVAL;
	}

	rsp->seq = payload[0];
	rsp->cmd = payload[1];
	rsp->status = (int8_t)payload[2];
	rsp->data_len = payload[3];

	if (rsp->data_len > UART_RC_ESB_DATA_MAX ||
	    payload_len < (size_t)(4U + rsp->data_len)) {
		return -EINVAL;
	}

	if (rsp->data_len > 0U) {
		memcpy(rsp->data, &payload[4], rsp->data_len);
	}

	return 0;
}

int uart_rc_link_encode_debug_ctrl(const struct uart_rc_debug_ctrl *ctrl,
				   uint8_t *payload, size_t payload_len)
{
	if (ctrl == NULL || payload == NULL || payload_len < 4U) {
		return -EINVAL;
	}

	payload[0] = ctrl->seq;
	payload[1] = ctrl->flags;
	payload[2] = ctrl->level;
	payload[3] = ctrl->reserved;

	return 4;
}

int uart_rc_link_decode_debug_ctrl(const uint8_t *payload, size_t payload_len,
				   struct uart_rc_debug_ctrl *ctrl)
{
	if (ctrl == NULL || payload == NULL || payload_len < 4U) {
		return -EINVAL;
	}

	ctrl->seq = payload[0];
	ctrl->flags = payload[1];
	ctrl->level = payload[2];
	ctrl->reserved = payload[3];

	return 0;
}

int uart_rc_link_encode_debug_log(const struct uart_rc_debug_log *log,
				  uint8_t *payload, size_t payload_len)
{
	if (log == NULL || payload == NULL) {
		return -EINVAL;
	}

	if (log->text_len > UART_RC_LINK_DEBUG_MAX_TEXT ||
	    payload_len < (size_t)(4U + log->text_len)) {
		return -EINVAL;
	}

	payload[0] = log->seq;
	payload[1] = log->flags;
	payload[2] = log->level;
	payload[3] = log->text_len;
	memcpy(&payload[4], log->text, log->text_len);

	return (int)(4U + log->text_len);
}

int uart_rc_link_decode_debug_log(const uint8_t *payload, size_t payload_len,
				  struct uart_rc_debug_log *log)
{
	if (log == NULL || payload == NULL || payload_len < 4U) {
		return -EINVAL;
	}

	log->seq = payload[0];
	log->flags = payload[1];
	log->level = payload[2];
	log->text_len = payload[3];

	if (log->text_len > UART_RC_LINK_DEBUG_MAX_TEXT ||
	    payload_len < (size_t)(4U + log->text_len)) {
		return -EINVAL;
	}

	memcpy(log->text, &payload[4], log->text_len);

	return 0;
}

int uart_rc_link_init(struct uart_rc_link *link, const struct device *uart,
		      const struct uart_rc_link_handlers *handlers)
{
	if (link == NULL || uart == NULL) {
		return -EINVAL;
	}

	memset(link, 0, sizeof(*link));
	link->uart = uart;

	if (handlers != NULL) {
		link->handlers = *handlers;
	}

	uart_rc_link_reset(link);
	return 0;
}

int uart_rc_link_start_rx(struct uart_rc_link *link)
{
	if (link == NULL || link->uart == NULL) {
		return -EINVAL;
	}

	if (!device_is_ready(link->uart)) {
		return -ENODEV;
	}

	uart_rc_link_reset(link);
	uart_irq_callback_user_data_set(link->uart, uart_rc_link_isr, link);
	uart_irq_rx_enable(link->uart);

	return 0;
}

int uart_rc_link_send_packet(struct uart_rc_link *link, uint8_t type,
			     const uint8_t *payload, uint8_t len)
{
	uint8_t inner[2U + UART_RC_LINK_MAX_PAYLOAD];

	if (link == NULL || link->uart == NULL) {
		return -EINVAL;
	}

	if (len > UART_RC_LINK_MAX_PAYLOAD) {
		return -EINVAL;
	}

	if (len > 0U && payload == NULL) {
		return -EINVAL;
	}

	inner[0] = type;
	inner[1] = len;

	if (len > 0U) {
		memcpy(&inner[2], payload, len);
	}

	return uart_rc_link_hdlc_send(link, inner, 2U + len);
}

int uart_rc_link_send_ctrl(struct uart_rc_link *link,
			   const struct uart_rc_link_ctrl *ctrl)
{
	uint8_t payload[UART_RC_LINK_MAX_PAYLOAD];
	int len;

	if (ctrl == NULL) {
		return -EINVAL;
	}

	len = uart_rc_link_encode_ctrl(ctrl, payload, sizeof(payload));
	if (len < 0) {
		return len;
	}

	return uart_rc_link_send_packet(link, UART_RC_LINK_TYPE_CTRL, payload,
					(uint8_t)len);
}

int uart_rc_link_send_status(struct uart_rc_link *link,
			     const struct uart_rc_link_status *status)
{
	uint8_t payload[UART_RC_LINK_MAX_PAYLOAD];
	int len;

	if (status == NULL) {
		return -EINVAL;
	}

	len = uart_rc_link_encode_status(status, payload, sizeof(payload));
	if (len < 0) {
		return len;
	}

	return uart_rc_link_send_packet(link, UART_RC_LINK_TYPE_STATUS, payload,
					(uint8_t)len);
}

int uart_rc_link_send_esb_req(struct uart_rc_link *link,
			      const struct uart_rc_esb_req *req)
{
	uint8_t payload[UART_RC_LINK_MAX_PAYLOAD];
	int len;

	if (req == NULL) {
		return -EINVAL;
	}

	len = uart_rc_link_encode_esb_req(req, payload, sizeof(payload));
	if (len < 0) {
		return len;
	}

	return uart_rc_link_send_packet(link, UART_RC_LINK_TYPE_ESB_REQ, payload,
					(uint8_t)len);
}

int uart_rc_link_send_esb_rsp(struct uart_rc_link *link,
			      const struct uart_rc_esb_rsp *rsp)
{
	uint8_t payload[UART_RC_LINK_MAX_PAYLOAD];
	int len;

	if (rsp == NULL) {
		return -EINVAL;
	}

	len = uart_rc_link_encode_esb_rsp(rsp, payload, sizeof(payload));
	if (len < 0) {
		return len;
	}

	return uart_rc_link_send_packet(link, UART_RC_LINK_TYPE_ESB_RSP, payload,
					(uint8_t)len);
}

int uart_rc_link_send_debug_ctrl(struct uart_rc_link *link,
				 const struct uart_rc_debug_ctrl *ctrl)
{
	uint8_t payload[UART_RC_LINK_MAX_PAYLOAD];
	int len;

	if (ctrl == NULL) {
		return -EINVAL;
	}

	len = uart_rc_link_encode_debug_ctrl(ctrl, payload, sizeof(payload));
	if (len < 0) {
		return len;
	}

	return uart_rc_link_send_packet(link, UART_RC_LINK_TYPE_DEBUG_CTRL, payload,
					(uint8_t)len);
}

int uart_rc_link_send_debug_log(struct uart_rc_link *link,
				const struct uart_rc_debug_log *log)
{
	uint8_t payload[UART_RC_LINK_MAX_PAYLOAD];
	int len;

	if (log == NULL) {
		return -EINVAL;
	}

	len = uart_rc_link_encode_debug_log(log, payload, sizeof(payload));
	if (len < 0) {
		return len;
	}

	return uart_rc_link_send_packet(link, UART_RC_LINK_TYPE_DEBUG_LOG, payload,
					(uint8_t)len);
}

void uart_rc_link_feed(struct uart_rc_link *link, uint8_t byte)
{
	if (link == NULL) {
		return;
	}

	if (byte == UART_RC_LINK_HDLC_FLAG) {
		if (link->rx_in_frame != 0U) {
			uart_rc_link_dispatch(link);
		}

		link->rx_in_frame = 1U;
		link->rx_escape = 0U;
		link->rx_raw_pos = 0U;
		return;
	}

	if (link->rx_in_frame == 0U) {
		return;
	}

	if (link->rx_escape != 0U) {
		if (link->rx_raw_pos >= sizeof(link->rx_raw)) {
			uart_rc_link_reset(link);
			return;
		}

		link->rx_raw[link->rx_raw_pos++] = byte ^ UART_RC_LINK_HDLC_XOR;
		link->rx_escape = 0U;
		return;
	}

	if (byte == UART_RC_LINK_HDLC_ESC) {
		link->rx_escape = 1U;
		return;
	}

	if (link->rx_raw_pos >= sizeof(link->rx_raw)) {
		uart_rc_link_reset(link);
		return;
	}

	link->rx_raw[link->rx_raw_pos++] = byte;
}

void uart_rc_link_isr(const struct device *dev, void *user_data)
{
	struct uart_rc_link *link = user_data;
	uint8_t buf[16];
	int rd;

	if (link == NULL) {
		return;
	}

	while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
		rd = uart_fifo_read(dev, buf, sizeof(buf));
		if (rd <= 0) {
			break;
		}

		for (int i = 0; i < rd; i++) {
			uart_rc_link_feed(link, buf[i]);
		}
	}
}
