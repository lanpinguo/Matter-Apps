/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_LINK_H_
#define RC_LINK_H_

#include <stddef.h>
#include <stdint.h>

#define RC_LINK_PIPE 0
#define RC_LINK_MAGIC 0xA5

#define RC_LINK_TYPE_CTRL   0x01U
#define RC_LINK_TYPE_STATUS 0x02U

#define RC_LINK_HEADER_SIZE 5U
#define RC_LINK_CRC_SIZE    1U
#define RC_LINK_MIN_FRAME_SIZE (RC_LINK_HEADER_SIZE + RC_LINK_CRC_SIZE)
#define RC_LINK_MAX_CHANNELS   16U

#define RC_STATUS_FLAG_ARMED    0x01U
#define RC_STATUS_FLAG_FAILSAFE 0x02U

struct rc_link_frame {
	uint8_t magic;
	uint8_t type;
	uint8_t seq;
	uint8_t flags;
	uint8_t channel_count;
	uint16_t channels[RC_LINK_MAX_CHANNELS];
};

static inline size_t rc_link_frame_wire_size(uint8_t channel_count)
{
	return RC_LINK_MIN_FRAME_SIZE + ((size_t)channel_count * sizeof(uint16_t));
}

int rc_link_pack(const struct rc_link_frame *frame, uint8_t *buf, size_t buf_len);
int rc_link_unpack(const uint8_t *buf, size_t buf_len, struct rc_link_frame *frame);

#endif /* RC_LINK_H_ */
