/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Defer UART ESB_REQ handling out of the UART ISR. Radio reinit (PAIR/APPLY)
 * must not run in interrupt context.
 */

#ifndef RC_UART_ESB_REQ_WORK_H_
#define RC_UART_ESB_REQ_WORK_H_

#include "uart_rc_link.h"

int rc_uart_esb_req_work_init(void);
int rc_uart_esb_req_submit(struct uart_rc_link *link, const struct uart_rc_esb_req *req);

#endif /* RC_UART_ESB_REQ_WORK_H_ */
