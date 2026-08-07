/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Hub-side ESB / PTX control helpers (UART RC link on uart30).
 */

#ifndef HUB_ESB_H_
#define HUB_ESB_H_

#include <stdbool.h>
#include <stdint.h>

#include "uart_rc_link.h"

struct hub_esb_snapshot {
	bool hub_cfg_ram_valid;
	bool hub_cfg_flash_valid;
	bool pair_session_active;
	bool log_forward;
	bool xbox_ctrl_active;
	bool last_status_valid;
	int64_t last_status_age_ms; /* -1 if never received */
	struct uart_rc_link_status last_status;
	struct uart_rc_esb_config hub_cfg;
	/** Last ESB_RSP from PTX age (-1 never); passive presence hint. */
	int64_t last_ptx_rsp_age_ms;
};

/** Fill link / pair / cfg snapshot for shell diagnostics (no UART probe). */
void hub_esb_snapshot(struct hub_esb_snapshot *out);

/**
 * Active presence check: GET_CONFIG and wait briefly (ESB_PING_TIMEOUT_MS).
 * @return 0 PTX present and @p cfg filled (if non-NULL);
 *         -ENODATA PTX present but cfg unavailable;
 *         -ETIMEDOUT PTX not answering;
 *         other errno on send error.
 */
int hub_esb_ping_ptx(struct uart_rc_esb_config *cfg);

/** Copy current Hub RAM cfg; returns -ENOENT if none. */
int hub_esb_get_cfg(struct uart_rc_esb_config *cfg);

/** Force OTA PAIR on PTX (same as Btn1 hold). */
int hub_esb_force_pair(void);

/** Push Hub-saved cfg to PTX (SET_RADIO/SET_ADDR/APPLY). */
int hub_esb_push_to_ptx(void);

/**
 * Query PTX GET_CONFIG and wait briefly for RSP.
 * On success copies into @p cfg (may also refresh Hub RAM cache).
 * Same as hub_esb_ping_ptx() with cfg required for decode into Hub cache.
 */
int hub_esb_query_ptx(struct uart_rc_esb_config *cfg);

/** Enable/disable PTX log forward over UART DEBUG_LOG. */
int hub_esb_set_log_forward(bool enable);

/** Delete Hub flash esb_radio and clear RAM cfg. */
int hub_esb_clear_saved(void);

#endif /* HUB_ESB_H_ */
