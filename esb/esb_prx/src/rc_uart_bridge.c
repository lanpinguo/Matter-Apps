/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_uart_config_bridge.h"

int rc_uart_bridge_init(void)
{
	return rc_uart_config_bridge_init();
}

struct uart_rc_link *rc_uart_bridge_link(void)
{
	return rc_uart_config_bridge_link();
}
