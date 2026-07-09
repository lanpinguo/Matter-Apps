/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Hub <-> ESB PTX UART protocol (HUART-RC)
 *
 * Physical link
 * -------------
 * Ground BLE Hub uses uart30; ESB PTX uses the console UART (uart20 on
 * nRF54L15 DK). Cross-connect TX/RX and GND. Default baud rate: 115200.
 *
 * Multiplexing with printk
 * ------------------------
 * ESB PTX shares the console UART with text logs. printk output is ASCII
 * and line-oriented. Binary frames use HDLC flag 0x7E; invalid frames are
 * dropped after FCS verification. ASCII log bytes are ignored until the
 * next 0x7E delimiter.
 *
 * When debug forwarding is enabled (TYPE_DEBUG_CTRL), logs are sent as
 * TYPE_DEBUG_LOG binary frames instead of plain text on the wire.
 *
 * Link layer (HDLC, same style as OpenThread Spinel RCP)
 * ------------------------------------------------------
 * On wire:
 *   0x7E | escaped( TYPE | LEN | PAYLOAD | FCS_LO | FCS_HI ) | 0x7E
 *
 * Byte stuffing: 0x7E -> 0x7D 0x5E, 0x7D -> 0x7D 0x5D (XOR 0x20).
 * FCS-16: PPP/HDLC CRC over TYPE+LEN+PAYLOAD, then XOR 0xFFFF (LE on wire).
 * LEN must be <= UART_RC_LINK_MAX_PAYLOAD (32).
 *
 * Message types
 * -------------
 * TYPE_CTRL (0x01)  Hub -> ESB PTX
 *   seq            u8
 *   channel_count  u8   (1 .. UART_RC_LINK_MAX_CHANNELS)
 *   channels[]     u16 LE per channel, 0..1000 typical
 *
 * TYPE_STATUS (0x02)  ESB PTX -> Hub
 *   seq            u8
 *   roll           i16 LE  (0.01 deg, aircraft IMU)
 *   pitch          i16 LE
 *   yaw            i16 LE
 *   battery_mv     u16 LE
 *   flags          u8     (RC_STATUS_FLAG_* from rc_link.h)
 *
 * TYPE_ESB_REQ (0x03)  Hub -> ESB PTX
 *   seq            u8
 *   cmd            u8     (UART_RC_ESB_CMD_*)
 *   data_len       u8
 *   data[]         up to UART_RC_ESB_DATA_MAX bytes
 *
 * TYPE_ESB_RSP (0x04)  ESB PTX -> Hub
 *   seq            u8     (matches request)
 *   cmd            u8
 *   status         i8     (0 = OK, negative errno)
 *   data_len       u8
 *   data[]         response payload
 *
 *   Commands:
 *     GET_CONFIG  -> data_len=0; rsp data is uart_rc_esb_config (21 B)
 *     SET_RADIO   -> data: bitrate(u8), tx_power(i8), retransmit_delay(u16 LE), pipe(u8)
 *     SET_ADDR    -> data: base0[4], base1[4], prefixes[8]
 *     PAIR        -> data_len=0; generate random addresses, rsp returns uart_rc_esb_config
 *     APPLY       -> reinitialize ESB with staged config
 *     SAVE        -> persist staged config to flash (settings)
 *
 * TYPE_DEBUG_CTRL (0x05)  Hub -> ESB PTX
 *   seq            u8
 *   flags          u8     (UART_RC_DEBUG_FLAG_*)
 *   level          u8     max Zephyr log level to forward (LOG_LEVEL_*)
 *   reserved       u8
 *
 * TYPE_DEBUG_LOG (0x06)  ESB PTX -> Hub
 *   seq            u8
 *   flags          u8     bit7=MORE fragments, bits0-6=fragment index
 *   level          u8
 *   text_len       u8
 *   text[]         up to UART_RC_LINK_DEBUG_MAX_TEXT bytes, no NUL
 */

#ifndef UART_RC_LINK_H_
#define UART_RC_LINK_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#define UART_RC_LINK_HDLC_FLAG     0x7EU
#define UART_RC_LINK_HDLC_ESC      0x7DU
#define UART_RC_LINK_HDLC_XOR      0x20U
#define UART_RC_LINK_TYPE_CTRL     0x01U
#define UART_RC_LINK_TYPE_STATUS   0x02U
#define UART_RC_LINK_TYPE_ESB_REQ  0x03U
#define UART_RC_LINK_TYPE_ESB_RSP  0x04U
#define UART_RC_LINK_TYPE_DEBUG_CTRL 0x05U
#define UART_RC_LINK_TYPE_DEBUG_LOG  0x06U
#define UART_RC_LINK_MAX_PAYLOAD   32U
#define UART_RC_LINK_MAX_CHANNELS  15U

#define UART_RC_CH_LX              0U
#define UART_RC_CH_LY              1U
#define UART_RC_CH_RX              2U
#define UART_RC_CH_RY              3U
#define UART_RC_CH_LT              4U
#define UART_RC_CH_RT              5U
#define UART_RC_CH_COUNT_DEFAULT   6U

#define UART_RC_ESB_CMD_GET_CONFIG 0x01U
#define UART_RC_ESB_CMD_SET_RADIO  0x02U
#define UART_RC_ESB_CMD_SET_ADDR   0x03U
#define UART_RC_ESB_CMD_PAIR       0x04U
#define UART_RC_ESB_CMD_APPLY      0x05U
#define UART_RC_ESB_CMD_SAVE       0x06U

#define UART_RC_ESB_DATA_MAX       28U
#define UART_RC_LINK_DEBUG_MAX_TEXT 28U

#define UART_RC_DEBUG_FLAG_FORWARD BIT(0)
#define UART_RC_DEBUG_FLAG_LOCAL   BIT(1)
#define UART_RC_DEBUG_FLAG_MORE    BIT(7)

struct uart_rc_link_ctrl {
	uint8_t seq;
	uint8_t channel_count;
	uint16_t channels[UART_RC_LINK_MAX_CHANNELS];
};

struct uart_rc_link_status {
	uint8_t seq;
	int16_t roll;
	int16_t pitch;
	int16_t yaw;
	uint16_t battery_mv;
	uint8_t flags;
};

struct uart_rc_esb_config {
	uint8_t bitrate;
	int8_t tx_power;
	uint16_t retransmit_delay;
	uint8_t pipe;
	uint8_t base0[4];
	uint8_t base1[4];
	uint8_t prefixes[8];
} __packed;

struct uart_rc_esb_req {
	uint8_t seq;
	uint8_t cmd;
	uint8_t data_len;
	uint8_t data[UART_RC_ESB_DATA_MAX];
};

struct uart_rc_esb_rsp {
	uint8_t seq;
	uint8_t cmd;
	int8_t status;
	uint8_t data_len;
	uint8_t data[UART_RC_ESB_DATA_MAX];
};

struct uart_rc_debug_ctrl {
	uint8_t seq;
	uint8_t flags;
	uint8_t level;
	uint8_t reserved;
};

struct uart_rc_debug_log {
	uint8_t seq;
	uint8_t flags;
	uint8_t level;
	uint8_t text_len;
	char text[UART_RC_LINK_DEBUG_MAX_TEXT];
};

struct uart_rc_link_handlers {
	void (*on_ctrl)(const struct uart_rc_link_ctrl *ctrl, void *user_data);
	void (*on_status)(const struct uart_rc_link_status *status, void *user_data);
	void (*on_esb_req)(const struct uart_rc_esb_req *req, void *user_data);
	void (*on_esb_rsp)(const struct uart_rc_esb_rsp *rsp, void *user_data);
	void (*on_debug_ctrl)(const struct uart_rc_debug_ctrl *ctrl, void *user_data);
	void (*on_debug_log)(const struct uart_rc_debug_log *log, void *user_data);
	void *user_data;
};

struct uart_rc_link {
	const struct device *uart;
	struct uart_rc_link_handlers handlers;
	uint8_t rx_in_frame;
	uint8_t rx_escape;
	uint8_t rx_raw_pos;
	uint8_t rx_raw[3U + UART_RC_LINK_MAX_PAYLOAD + 2U];
};

uint16_t uart_rc_link_hdlc_fcs16(const uint8_t *buf, size_t len);

int uart_rc_link_encode_ctrl(const struct uart_rc_link_ctrl *ctrl,
			     uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_ctrl(const uint8_t *payload, size_t payload_len,
			     struct uart_rc_link_ctrl *ctrl);

int uart_rc_link_encode_status(const struct uart_rc_link_status *status,
			       uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_status(const uint8_t *payload, size_t payload_len,
			       struct uart_rc_link_status *status);

int uart_rc_link_encode_esb_req(const struct uart_rc_esb_req *req,
				uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_esb_req(const uint8_t *payload, size_t payload_len,
				struct uart_rc_esb_req *req);

int uart_rc_link_encode_esb_rsp(const struct uart_rc_esb_rsp *rsp,
				uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_esb_rsp(const uint8_t *payload, size_t payload_len,
				struct uart_rc_esb_rsp *rsp);

int uart_rc_link_encode_debug_ctrl(const struct uart_rc_debug_ctrl *ctrl,
				   uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_debug_ctrl(const uint8_t *payload, size_t payload_len,
				   struct uart_rc_debug_ctrl *ctrl);

int uart_rc_link_encode_debug_log(const struct uart_rc_debug_log *log,
				  uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_debug_log(const uint8_t *payload, size_t payload_len,
				  struct uart_rc_debug_log *log);

int uart_rc_link_encode_esb_config(const struct uart_rc_esb_config *cfg,
				   uint8_t *payload, size_t payload_len);
int uart_rc_link_decode_esb_config(const uint8_t *payload, size_t payload_len,
				   struct uart_rc_esb_config *cfg);

int uart_rc_link_init(struct uart_rc_link *link, const struct device *uart,
		      const struct uart_rc_link_handlers *handlers);
int uart_rc_link_start_rx(struct uart_rc_link *link);
void uart_rc_link_feed(struct uart_rc_link *link, uint8_t byte);
void uart_rc_link_isr(const struct device *dev, void *user_data);

int uart_rc_link_send_packet(struct uart_rc_link *link, uint8_t type,
			     const uint8_t *payload, uint8_t len);
int uart_rc_link_send_ctrl(struct uart_rc_link *link,
			   const struct uart_rc_link_ctrl *ctrl);
int uart_rc_link_send_status(struct uart_rc_link *link,
			     const struct uart_rc_link_status *status);
int uart_rc_link_send_esb_req(struct uart_rc_link *link,
			      const struct uart_rc_esb_req *req);
int uart_rc_link_send_esb_rsp(struct uart_rc_link *link,
			      const struct uart_rc_esb_rsp *rsp);
int uart_rc_link_send_debug_ctrl(struct uart_rc_link *link,
				 const struct uart_rc_debug_ctrl *ctrl);
int uart_rc_link_send_debug_log(struct uart_rc_link *link,
				const struct uart_rc_debug_log *log);

#endif /* UART_RC_LINK_H_ */
