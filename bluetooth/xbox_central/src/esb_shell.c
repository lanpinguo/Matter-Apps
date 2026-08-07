/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * UART shell helpers for ESB PTX bring-up / pairing debug.
 *
 * Console (uart20 @ 115200):
 *   esb status           — Hub cfg / pair / PTX present ping / last STATUS
 *   esb ping             — active check whether esb_ptx answers on UART
 *   esb cfg              — dump Hub-saved addresses
 *   esb get              — GET_CONFIG from PTX (wait RSP)
 *   esb push             — push Hub cfg to PTX (SET_RADIO/SET_ADDR/APPLY)
 *   esb pair             — force OTA PAIR (same as Btn1 hold)
 *   esb log [on|off]     — PTX DEBUG_LOG forward
 *   esb clear            — delete Hub flash esb_radio
 */

#include "hub_esb.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

static void print_hex(const struct shell *sh, const char *label, const uint8_t *p, size_t n)
{
	char line[48];
	size_t i;
	int pos = 0;

	for (i = 0; i < n && pos < (int)sizeof(line) - 3; i++) {
		pos += snprintk(line + pos, sizeof(line) - (size_t)pos, "%02x%s", p[i],
				(i + 1U < n) ? " " : "");
	}
	shell_print(sh, "  %s: %s", label, line);
}

static void print_cfg(const struct shell *sh, const char *title,
		      const struct uart_rc_esb_config *cfg)
{
	shell_print(sh, "%s", title);
	shell_print(sh, "  bitrate=%u tx_power=%d delay=%u pipe=%u", cfg->bitrate,
		    cfg->tx_power, cfg->retransmit_delay, cfg->pipe);
	print_hex(sh, "base0", cfg->base0, sizeof(cfg->base0));
	print_hex(sh, "base1", cfg->base1, sizeof(cfg->base1));
	print_hex(sh, "prefix", cfg->prefixes, sizeof(cfg->prefixes));
}

static void print_ptx_presence(const struct shell *sh, int ping_err,
			       const struct hub_esb_snapshot *snap)
{
	if (ping_err == 0 || ping_err == -ENODATA) {
		shell_print(sh, "  ptx_present: YES%s",
			    (ping_err == -ENODATA) ? " (RSP ok, cfg decode skipped)" : " (GET_CONFIG ACK)");
	} else if (ping_err == -ETIMEDOUT) {
		shell_print(sh, "  ptx_present: NO (no UART RSP in 300 ms)");
		shell_print(sh, "    check: Hub uart30 ↔ PTX uart20 TX/RX/GND, PTX powered");
	} else {
		shell_print(sh, "  ptx_present: ERROR (%d)", ping_err);
	}

	if (snap->last_ptx_rsp_age_ms >= 0) {
		shell_print(sh, "  last PTX ESB_RSP age=%lld ms",
			    (long long)snap->last_ptx_rsp_age_ms);
	} else {
		shell_print(sh, "  last PTX ESB_RSP: never");
	}
}

static int cmd_esb_status(const struct shell *sh, size_t argc, char **argv)
{
	struct hub_esb_snapshot snap;
	struct uart_rc_esb_config cfg;
	int ping_err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	hub_esb_snapshot(&snap);

	shell_print(sh, "==== ESB / PTX link ====");
	shell_print(sh, "pinging PTX (GET_CONFIG, 300 ms)...");
	ping_err = hub_esb_ping_ptx(&cfg);
	hub_esb_snapshot(&snap); /* refresh ages after ping */
	print_ptx_presence(sh, ping_err, &snap);

	shell_print(sh, "  hub_cfg_ram=%s  hub_cfg_flash=%s",
		    snap.hub_cfg_ram_valid ? "yes" : "no",
		    snap.hub_cfg_flash_valid ? "yes" : "no");
	shell_print(sh, "  pair_session=%s  ptx_log_fwd=%s  xbox_ctrl=%s",
		    snap.pair_session_active ? "ACTIVE" : "idle",
		    snap.log_forward ? "on" : "off",
		    snap.xbox_ctrl_active ? "on (CTRL heartbeat)" : "off");

	if (snap.last_status_valid) {
		shell_print(sh,
			    "  last STATUS age=%lld ms seq=%u flags=0x%02x batt=%u mV",
			    (long long)snap.last_status_age_ms, snap.last_status.seq,
			    snap.last_status.flags, snap.last_status.battery_mv);
		shell_print(sh, "  R/P/Y=%d/%d/%d", snap.last_status.roll,
			    snap.last_status.pitch, snap.last_status.yaw);
		if (snap.last_status_age_ms >= 0 && snap.last_status_age_ms < 1000) {
			shell_print(sh, "  rf_link: OK (STATUS recent from PRX via PTX)");
		} else if (snap.last_status_age_ms >= 0) {
			shell_print(sh, "  rf_link: STALE (no STATUS >1s — PRX idle or RF down?)");
		}
	} else {
		shell_print(sh, "  last STATUS: never (PRX not reporting yet)");
	}

	if (ping_err == 0) {
		print_cfg(sh, "PTX live cfg:", &cfg);
	} else if (snap.hub_cfg_ram_valid || snap.hub_cfg_flash_valid) {
		print_cfg(sh, "Hub ESB cfg:", &snap.hub_cfg);
	}

	shell_print(sh, "tips: esb ping | esb pair | esb push | esb log on | flog mirror on");
	return 0;
}

static int cmd_esb_ping(const struct shell *sh, size_t argc, char **argv)
{
	struct hub_esb_snapshot snap;
	struct uart_rc_esb_config cfg;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "ping PTX (GET_CONFIG, 300 ms)...");
	err = hub_esb_ping_ptx(&cfg);
	hub_esb_snapshot(&snap);
	print_ptx_presence(sh, err, &snap);
	if (err == 0) {
		print_cfg(sh, "PTX cfg:", &cfg);
		return 0;
	}
	if (err == -ENODATA) {
		return 0;
	}
	return err;
}

static int cmd_esb_cfg(const struct shell *sh, size_t argc, char **argv)
{
	struct uart_rc_esb_config cfg;
	struct hub_esb_snapshot snap;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	hub_esb_snapshot(&snap);
	shell_print(sh, "hub_cfg_ram=%s hub_cfg_flash=%s pair=%s",
		    snap.hub_cfg_ram_valid ? "yes" : "no",
		    snap.hub_cfg_flash_valid ? "yes" : "no",
		    snap.pair_session_active ? "ACTIVE" : "idle");

	err = hub_esb_get_cfg(&cfg);
	if (err != 0) {
		shell_error(sh, "no Hub ESB config (run esb pair first)");
		return err;
	}
	print_cfg(sh, "Hub ESB cfg:", &cfg);
	return 0;
}

static int cmd_esb_get(const struct shell *sh, size_t argc, char **argv)
{
	struct uart_rc_esb_config cfg;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "GET_CONFIG -> PTX ...");
	err = hub_esb_query_ptx(&cfg);
	if (err != 0) {
		shell_error(sh, "GET_CONFIG failed: %d (PTX UART up?)", err);
		return err;
	}
	print_cfg(sh, "PTX cfg (also cached in Hub RAM):", &cfg);
	return 0;
}

static int cmd_esb_push(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = hub_esb_push_to_ptx();
	if (err != 0) {
		shell_error(sh, "push failed: %d (need Hub cfg — esb pair / esb get)", err);
		return err;
	}
	shell_print(sh, "pushed Hub cfg to PTX (SET_RADIO/SET_ADDR/APPLY)");
	return 0;
}

static int cmd_esb_pair(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "OTA PAIR start — PRX must be in pair mode (Btn1 5s or no flash cfg)");
	shell_print(sh, "PTX log forward ON; wait for PRX ACK (max ~32s)");
	err = hub_esb_force_pair();
	if (err != 0) {
		shell_error(sh, "pair start failed: %d", err);
		return err;
	}
	shell_print(sh, "pair session ACTIVE — use esb status / flog show esb");
	return 0;
}

static int cmd_esb_log(const struct shell *sh, size_t argc, char **argv)
{
	struct hub_esb_snapshot snap;
	int err;

	if (argc < 2) {
		hub_esb_snapshot(&snap);
		shell_print(sh, "ptx log forward: %s", snap.log_forward ? "on" : "off");
		shell_print(sh, "usage: esb log <on|off>");
		return 0;
	}

	if (!strcmp(argv[1], "on")) {
		err = hub_esb_set_log_forward(true);
	} else if (!strcmp(argv[1], "off")) {
		err = hub_esb_set_log_forward(false);
	} else {
		shell_error(sh, "usage: esb log <on|off>");
		return -EINVAL;
	}

	if (err != 0) {
		shell_error(sh, "log forward ctrl failed: %d", err);
		return err;
	}
	shell_print(sh, "ptx log forward -> %s", argv[1]);
	return 0;
}

static int cmd_esb_clear(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = hub_esb_clear_saved();
	if (err != 0) {
		shell_error(sh, "clear failed: %d", err);
		return err;
	}
	shell_print(sh, "Hub esb_radio cleared (PTX still needs esb push after new pair)");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(esb_cmds,
	SHELL_CMD(status, NULL, "Link / pair / PTX present ping", cmd_esb_status),
	SHELL_CMD(ping, NULL, "Check if esb_ptx answers on UART", cmd_esb_ping),
	SHELL_CMD(cfg, NULL, "Show Hub-saved ESB config", cmd_esb_cfg),
	SHELL_CMD(get, NULL, "GET_CONFIG from PTX", cmd_esb_get),
	SHELL_CMD(push, NULL, "Push Hub cfg to PTX", cmd_esb_push),
	SHELL_CMD(pair, NULL, "Force OTA PAIR", cmd_esb_pair),
	SHELL_CMD_ARG(log, NULL, "PTX log forward on|off", cmd_esb_log, 1, 1),
	SHELL_CMD(clear, NULL, "Delete Hub flash esb_radio", cmd_esb_clear),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(esb, &esb_cmds, "ESB PTX debug (pair / link / cfg)", NULL);
