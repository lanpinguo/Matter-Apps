/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "xbox_report.h"

#include <stdio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#define STICK_CENTER 32767

static const char *dpad_name(uint8_t hat)
{
	switch (hat) {
	case 0:
		return "center";
	case 1:
		return "up";
	case 2:
		return "up-right";
	case 3:
		return "right";
	case 4:
		return "down-right";
	case 5:
		return "down";
	case 6:
		return "down-left";
	case 7:
		return "left";
	case 8:
		return "up-left";
	default:
		return "unknown";
	}
}

bool xbox_report_parse(const uint8_t *data, uint8_t len,
		       struct xbox_gamepad_state *out)
{
	const uint8_t *p;
	uint8_t face;
	uint8_t center;

	if (len == 0U || out == NULL) {
		return false;
	}

	if ((len >= (XBOX_INPUT_REPORT_SIZE + 1U)) &&
	    (data[0] == XBOX_INPUT_REPORT_ID)) {
		p = &data[1];
	} else if (len >= XBOX_INPUT_REPORT_SIZE) {
		p = data;
	} else {
		return false;
	}

	out->lx = (int16_t)(sys_get_le16(&p[XBOX_OFF_LX]) - STICK_CENTER);
	out->ly = (int16_t)(sys_get_le16(&p[XBOX_OFF_LY]) - STICK_CENTER);
	out->rx = (int16_t)(sys_get_le16(&p[XBOX_OFF_RX]) - STICK_CENTER);
	out->ry = (int16_t)(sys_get_le16(&p[XBOX_OFF_RY]) - STICK_CENTER);
	out->lt = sys_get_le16(&p[XBOX_OFF_LT]) & 0x03FFU;
	out->rt = sys_get_le16(&p[XBOX_OFF_RT]) & 0x03FFU;
	out->dpad = p[XBOX_OFF_DPAD] & 0x0FU;

	face = p[XBOX_OFF_BTN_FACE];
	center = p[XBOX_OFF_BTN_CENTER];

	out->btn_a = (face & BIT(0)) != 0U;
	out->btn_b = (face & BIT(1)) != 0U;
	out->btn_x = (face & BIT(3)) != 0U;
	out->btn_y = (face & BIT(4)) != 0U;
	out->btn_lb = (face & BIT(6)) != 0U;
	out->btn_rb = (face & BIT(7)) != 0U;

	out->btn_view = (center & BIT(2)) != 0U;
	out->btn_menu = (center & BIT(3)) != 0U;
	out->btn_guide = (center & BIT(4)) != 0U;
	out->btn_ls = (center & BIT(5)) != 0U;
	out->btn_rs = (center & BIT(6)) != 0U;

	out->btn_share = (p[XBOX_OFF_BTN_SHARE] & BIT(0)) != 0U;

	return true;
}

void xbox_report_print(const struct xbox_gamepad_state *state)
{
	char pressed[160];
	size_t off = 0;

	if (state == NULL) {
		return;
	}

	pressed[0] = '\0';

#define APPEND_BTN(label, active)					      \
	do {								      \
		if (active) {						      \
			off += snprintf(pressed + off, sizeof(pressed) - off, \
					off == 0U ? "%s" : ",%s", label);   \
			if (off >= sizeof(pressed)) {			      \
				break;					      \
			}						      \
		}							      \
	} while (0)

	APPEND_BTN("A", state->btn_a);
	APPEND_BTN("B", state->btn_b);
	APPEND_BTN("X", state->btn_x);
	APPEND_BTN("Y", state->btn_y);
	APPEND_BTN("LB", state->btn_lb);
	APPEND_BTN("RB", state->btn_rb);
	APPEND_BTN("View", state->btn_view);
	APPEND_BTN("Menu", state->btn_menu);
	APPEND_BTN("Guide", state->btn_guide);
	APPEND_BTN("LS", state->btn_ls);
	APPEND_BTN("RS", state->btn_rs);
	APPEND_BTN("Share", state->btn_share);

#undef APPEND_BTN

	printk("sticks L(%d,%d) R(%d,%d) triggers LT=%u RT=%u dpad=%s(%u)",
	       state->lx, state->ly, state->rx, state->ry,
	       state->lt, state->rt, dpad_name(state->dpad), state->dpad);

	if (pressed[0] != '\0') {
		printk(" buttons=%s", pressed);
	}

	printk("\n");
}
