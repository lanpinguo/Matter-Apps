/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_UART_CONFIG_BRIDGE_H_
#define RC_UART_CONFIG_BRIDGE_H_

#include "uart_rc_link.h"

int rc_uart_config_bridge_init(void);
struct uart_rc_link *rc_uart_config_bridge_link(void);

#endif /* RC_UART_CONFIG_BRIDGE_H_ */
