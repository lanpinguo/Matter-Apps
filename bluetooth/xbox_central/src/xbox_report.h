/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef XBOX_REPORT_H_
#define XBOX_REPORT_H_

#include <stdbool.h>
#include <stdint.h>

#define XBOX_INPUT_REPORT_ID     1U
#define XBOX_INPUT_REPORT_SIZE   16U

/* Xbox Series X|S BLE report byte layout (after optional report ID). */
#define XBOX_OFF_LX              0
#define XBOX_OFF_LY              2
#define XBOX_OFF_RX              4
#define XBOX_OFF_RY              6
#define XBOX_OFF_LT              8
#define XBOX_OFF_RT              10
#define XBOX_OFF_DPAD            12
#define XBOX_OFF_BTN_FACE        13
#define XBOX_OFF_BTN_CENTER      14
#define XBOX_OFF_BTN_SHARE       15

struct xbox_gamepad_state {
	int16_t lx;
	int16_t ly;
	int16_t rx;
	int16_t ry;
	uint16_t lt;
	uint16_t rt;
	uint8_t dpad;
	bool btn_a;
	bool btn_b;
	bool btn_x;
	bool btn_y;
	bool btn_lb;
	bool btn_rb;
	bool btn_view;
	bool btn_menu;
	bool btn_guide;
	bool btn_ls;
	bool btn_rs;
	bool btn_share;
};

bool xbox_report_parse(const uint8_t *data, uint8_t len,
		       struct xbox_gamepad_state *out);
void xbox_report_print(const struct xbox_gamepad_state *state);

#endif /* XBOX_REPORT_H_ */
