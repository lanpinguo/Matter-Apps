/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * hub> loglevel
 * hub> loglevel <mod|all> <err|wrn|inf|dbg>
 */

#include "hub_log.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/shell/shell.h>

static int cmd_loglevel(const struct shell *sh, size_t argc, char **argv)
{
	int lvl;
	int mod;
	uint8_t i;

	if (argc < 2) {
		shell_print(sh, "module log levels:");
		for (i = 0; i < HUB_MOD_COUNT; i++) {
			shell_print(sh, "  %-5s %s (%d)", hub_log_mod_str(i),
				    hub_log_level_str(hub_log_get_level(i)),
				    hub_log_get_level(i));
		}
		shell_print(sh, "usage: loglevel <mod|all> <err|wrn|inf|dbg>");
		shell_print(sh, "mods: sys xbox hid phone uart esb bq input");
		return 0;
	}

	if (argc < 3) {
		shell_error(sh, "usage: loglevel <mod|all> <err|wrn|inf|dbg>");
		return -EINVAL;
	}

	lvl = hub_log_level_from_str(argv[2]);
	if (lvl < 0) {
		shell_error(sh, "invalid level '%s'", argv[2]);
		return -EINVAL;
	}

	if (!strcmp(argv[1], "all")) {
		hub_log_set_level_all(lvl);
		shell_print(sh, "all modules -> %s", hub_log_level_str(lvl));
		return 0;
	}

	mod = hub_log_mod_from_str(argv[1]);
	if (mod < 0) {
		shell_error(sh, "unknown module '%s'", argv[1]);
		return -EINVAL;
	}

	(void)hub_log_set_level((uint8_t)mod, lvl);
	shell_print(sh, "%s -> %s", hub_log_mod_str((uint8_t)mod),
		    hub_log_level_str(lvl));
	return 0;
}

SHELL_CMD_ARG_REGISTER(loglevel, NULL,
		       "Get/set per-module app log level",
		       cmd_loglevel, 1, 2);
