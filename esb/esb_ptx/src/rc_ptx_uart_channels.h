/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_PTX_UART_CHANNELS_H_
#define RC_PTX_UART_CHANNELS_H_

#include <stdint.h>

#include "rc_channel_bank.h"
#include "uart_rc_link.h"

#define RC_PTX_UART_LINK_TIMEOUT_MS 500

extern const struct rc_channel_bank rc_ptx_uart_control_bank;

void rc_ptx_uart_channels_update(const struct uart_rc_link_ctrl *ctrl);
bool rc_ptx_uart_channels_active(void);
const struct rc_channel_bank *rc_ptx_get_active_control_bank(void);

#endif /* RC_PTX_UART_CHANNELS_H_ */
