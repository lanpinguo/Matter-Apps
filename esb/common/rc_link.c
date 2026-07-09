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

	if (type != RC_LINK_TYPE_CTRL &&
	    type != RC_LINK_TYPE_STATUS &&
	    type != RC_LINK_TYPE_PAIR) {
		return -EPROTO;
	}

	if (channel_count > RC_LINK_MAX_CHANNELS) {
		return -EINVAL;
	}

	return 0;
}

static void pair_payload_to_words(const struct rc_link_pair_payload *pair, uint16_t words[8])
{
	words[0] = (uint16_t)pair->base0[0] | ((uint16_t)pair->base0[1] << 8);
	words[1] = (uint16_t)pair->base0[2] | ((uint16_t)pair->base0[3] << 8);
	words[2] = (uint16_t)pair->base1[0] | ((uint16_t)pair->base1[1] << 8);
	words[3] = (uint16_t)pair->base1[2] | ((uint16_t)pair->base1[3] << 8);
	words[4] = (uint16_t)pair->prefixes[0] | ((uint16_t)pair->prefixes[1] << 8);
	words[5] = (uint16_t)pair->prefixes[2] | ((uint16_t)pair->prefixes[3] << 8);
	words[6] = (uint16_t)pair->prefixes[4] | ((uint16_t)pair->prefixes[5] << 8);
	words[7] = (uint16_t)pair->prefixes[6] | ((uint16_t)pair->prefixes[7] << 8);
}

static void words_to_pair_payload(const uint16_t words[8], struct rc_link_pair_payload *pair)
{
	pair->base0[0] = (uint8_t)(words[0] & 0xFFU);
	pair->base0[1] = (uint8_t)((words[0] >> 8) & 0xFFU);
	pair->base0[2] = (uint8_t)(words[1] & 0xFFU);
	pair->base0[3] = (uint8_t)((words[1] >> 8) & 0xFFU);
	pair->base1[0] = (uint8_t)(words[2] & 0xFFU);
	pair->base1[1] = (uint8_t)((words[2] >> 8) & 0xFFU);
	pair->base1[2] = (uint8_t)(words[3] & 0xFFU);
	pair->base1[3] = (uint8_t)((words[3] >> 8) & 0xFFU);
	pair->prefixes[0] = (uint8_t)(words[4] & 0xFFU);
	pair->prefixes[1] = (uint8_t)((words[4] >> 8) & 0xFFU);
	pair->prefixes[2] = (uint8_t)(words[5] & 0xFFU);
	pair->prefixes[3] = (uint8_t)((words[5] >> 8) & 0xFFU);
	pair->prefixes[4] = (uint8_t)(words[6] & 0xFFU);
	pair->prefixes[5] = (uint8_t)((words[6] >> 8) & 0xFFU);
	pair->prefixes[6] = (uint8_t)(words[7] & 0xFFU);
	pair->prefixes[7] = (uint8_t)((words[7] >> 8) & 0xFFU);
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

int rc_link_pair_encode(const struct rc_link_pair_payload *pair, uint8_t seq,
			struct rc_link_frame *frame)
{
	if (pair == NULL || frame == NULL) {
		return -EINVAL;
	}

	frame->magic = RC_LINK_MAGIC;
	frame->type = RC_LINK_TYPE_PAIR;
	frame->seq = seq;
	frame->flags = 0U;
	frame->channel_count = RC_LINK_PAIR_WORDS;
	pair_payload_to_words(pair, frame->channels);

	return 0;
}

int rc_link_pair_decode(const struct rc_link_frame *frame, struct rc_link_pair_payload *pair)
{
	if (frame == NULL || pair == NULL) {
		return -EINVAL;
	}

	if (frame->magic != RC_LINK_MAGIC ||
	    frame->type != RC_LINK_TYPE_PAIR ||
	    frame->channel_count != RC_LINK_PAIR_WORDS) {
		return -EPROTO;
	}

	words_to_pair_payload(frame->channels, pair);
	return 0;
}
