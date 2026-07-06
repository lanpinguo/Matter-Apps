/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_link.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

static uint8_t crc8(const uint8_t *data, size_t len)
{
	uint8_t crc = 0;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];

		for (int bit = 0; bit < 8; bit++) {
			if ((crc & 0x80U) != 0U) {
				crc = (uint8_t)((crc << 1) ^ 0x07U);
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

static int validate_frame_meta(uint8_t magic, uint8_t type, uint8_t channel_count)
{
	if (magic != RC_LINK_MAGIC) {
		return -EPROTO;
	}

	if (type != RC_LINK_TYPE_CTRL && type != RC_LINK_TYPE_STATUS) {
		return -EPROTO;
	}

	if (channel_count > RC_LINK_MAX_CHANNELS) {
		return -EINVAL;
	}

	return 0;
}

int rc_link_pack(const struct rc_link_frame *frame, uint8_t *buf, size_t buf_len)
{
	size_t wire_size;
	int err;

	if (frame == NULL || buf == NULL) {
		return -EINVAL;
	}

	err = validate_frame_meta(frame->magic, frame->type, frame->channel_count);
	if (err) {
		return err;
	}

	wire_size = rc_link_frame_wire_size(frame->channel_count);
	if (buf_len < wire_size) {
		return -ENOMEM;
	}

	buf[0] = frame->magic;
	buf[1] = frame->type;
	buf[2] = frame->seq;
	buf[3] = frame->flags;
	buf[4] = frame->channel_count;

	for (uint8_t i = 0; i < frame->channel_count; i++) {
		sys_put_le16(frame->channels[i], &buf[RC_LINK_HEADER_SIZE + (i * 2U)]);
	}

	buf[wire_size - 1U] = crc8(buf, wire_size - 1U);

	return (int)wire_size;
}

int rc_link_unpack(const uint8_t *buf, size_t buf_len, struct rc_link_frame *frame)
{
	uint8_t channel_count;
	size_t wire_size;
	uint8_t crc;

	if (buf == NULL || frame == NULL) {
		return -EINVAL;
	}

	if (buf_len < RC_LINK_MIN_FRAME_SIZE) {
		return -EINVAL;
	}

	channel_count = buf[4];
	if (buf_len < rc_link_frame_wire_size(channel_count)) {
		return -EINVAL;
	}

	wire_size = rc_link_frame_wire_size(channel_count);
	crc = crc8(buf, wire_size - 1U);
	if (crc != buf[wire_size - 1U]) {
		return -EBADMSG;
	}

	if (validate_frame_meta(buf[0], buf[1], channel_count) != 0) {
		return -EPROTO;
	}

	frame->magic = buf[0];
	frame->type = buf[1];
	frame->seq = buf[2];
	frame->flags = buf[3];
	frame->channel_count = channel_count;

	for (uint8_t i = 0; i < channel_count; i++) {
		frame->channels[i] = sys_get_le16(&buf[RC_LINK_HEADER_SIZE + (i * 2U)]);
	}

	return 0;
}
