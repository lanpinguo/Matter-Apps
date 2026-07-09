/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_UART_LOG_FWD_H_
#define RC_UART_LOG_FWD_H_

#include "uart_rc_link.h"

int rc_uart_log_fwd_init(struct uart_rc_link *link);
int rc_uart_log_fwd_apply(const struct uart_rc_debug_ctrl *ctrl);

#endif /* RC_UART_LOG_FWD_H_ */
