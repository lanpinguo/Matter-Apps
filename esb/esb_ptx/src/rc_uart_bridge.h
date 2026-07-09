/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_UART_BRIDGE_H_
#define RC_UART_BRIDGE_H_

#include "rc_link.h"
#include "uart_rc_link.h"

int rc_uart_bridge_init(void);
struct uart_rc_link *rc_uart_bridge_link(void);
void rc_uart_bridge_on_esb_status(const struct rc_link_frame *status);

#endif /* RC_UART_BRIDGE_H_ */
