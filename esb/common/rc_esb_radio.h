/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef RC_ESB_RADIO_H_
#define RC_ESB_RADIO_H_

#include <stdbool.h>
#include <stdint.h>

#include <esb.h>

#include "rc_link.h"
#include "uart_rc_link.h"

typedef void (*rc_esb_event_handler_t)(struct esb_evt const *event);
typedef void (*rc_esb_radio_applied_cb_t)(void);

int rc_esb_radio_init(rc_esb_event_handler_t handler);
void rc_esb_radio_set_applied_cb(rc_esb_radio_applied_cb_t cb);
bool rc_esb_radio_has_saved_config(void);
int rc_esb_radio_load_settings(void);
int rc_esb_radio_get_config(struct uart_rc_esb_config *cfg);
int rc_esb_radio_set_radio(uint8_t bitrate, int8_t tx_power, uint16_t retransmit_delay,
			   uint8_t pipe);
int rc_esb_radio_set_addr(const uint8_t base0[4], const uint8_t base1[4],
			  const uint8_t prefixes[8]);
int rc_esb_radio_pair(struct uart_rc_esb_config *out_cfg);
int rc_esb_radio_export_pair_payload(struct rc_link_pair_payload *pair);
int rc_esb_radio_apply_pair_payload(const struct rc_link_pair_payload *pair, bool save_to_flash);
int rc_esb_radio_apply_cfg(const struct uart_rc_esb_config *cfg);
int rc_esb_radio_apply_pair_listen(void);
int rc_esb_radio_clear_saved_config(void);
int rc_esb_radio_apply(void);
int rc_esb_radio_save(void);
int rc_esb_radio_handle_req(const struct uart_rc_esb_req *req,
			    struct uart_rc_esb_rsp *rsp);

#endif /* RC_ESB_RADIO_H_ */
