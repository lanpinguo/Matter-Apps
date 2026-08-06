/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * hub> flog show [N] [from <idx>] [/kw|kw:kw] [mod|!mod|all]... [lvl]
 * hub> flog find <kw> [N] [from <idx>] [mod|!mod]... [lvl]
 * hub> flog clear | status | mirror on|off
 *
 * Line numbers [#idx ...] are absolute among matching rows (oldest=0).
 *
 * Examples:
 *   flog show
 *   flog show 100
 *   flog show from 200          # from #200, next 50
 *   flog show from 200 20       # from #200, next 20
 *   flog show /0x3e 50
 *   flog show kw:fail !bq
 *   flog find disconnect
 *   flog find Security 20 xbox
 */

#include "hub_flash_log.h"
#include "hub_log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/shell/shell.h>

static int parse_mod_token(const char *tok, uint32_t *mask, bool *have_include,
			   bool *have_exclude)
{
	const char *name = tok;
	bool exclude = false;
	int mod;

	if (tok[0] == '!' || tok[0] == '-') {
		exclude = true;
		name = tok + 1;
		if (name[0] == '\0') {
			return -EINVAL;
		}
	}

	if (!strcmp(name, "all")) {
		if (exclude) {
			*mask = 0U;
			*have_exclude = true;
		} else {
			*mask = HUB_LOG_MOD_MASK_ALL;
			*have_include = true;
		}
		return 0;
	}

	mod = hub_log_mod_from_str(name);
	if (mod < 0) {
		return -ENOENT;
	}

	if (exclude) {
		if (!*have_include && !*have_exclude) {
			*mask = HUB_LOG_MOD_MASK_ALL;
		}
		*mask &= ~(1u << (unsigned)mod);
		*have_exclude = true;
	} else {
		if (!*have_include) {
			*mask = 0U;
			*have_include = true;
		}
		*mask |= (1u << (unsigned)mod);
	}
	return 0;
}

static bool is_uint_token(const char *s)
{
	if (s == NULL || s[0] == '\0') {
		return false;
	}
	for (; *s != '\0'; s++) {
		if (*s < '0' || *s > '9') {
			return false;
		}
	}
	return true;
}

static int parse_query_args(const struct shell *sh, size_t argc, char **argv,
			    size_t start, struct hub_flash_log_query *q,
			    bool keyword_required)
{
	bool have_include = false;
	bool have_exclude = false;
	bool got_limit = false;
	int v;
	int err;
	size_t i = start;

	q->limit = 50;
	q->from = 0;
	q->take_tail = true;
	q->mod_mask = HUB_LOG_MOD_MASK_ALL;
	q->level_max = -1;
	q->keyword = NULL;

	if (keyword_required) {
		if (argc <= i) {
			shell_error(sh, "usage: flog find <keyword> ...");
			return -EINVAL;
		}
		q->keyword = argv[i++];
	}

	while (argc > i) {
		if (!strcmp(argv[i], "from")) {
			if (argc <= i + 1 || !is_uint_token(argv[i + 1])) {
				shell_error(sh, "from needs an index");
				return -EINVAL;
			}
			q->from = (unsigned int)strtoul(argv[i + 1], NULL, 0);
			q->take_tail = false;
			i += 2;
			continue;
		}

		if (argv[i][0] == '/' && argv[i][1] != '\0') {
			q->keyword = &argv[i][1];
			i++;
			continue;
		}
		if (!strncmp(argv[i], "kw:", 3) && argv[i][3] != '\0') {
			q->keyword = &argv[i][3];
			i++;
			continue;
		}

		v = hub_log_level_from_str(argv[i]);
		if (v >= 0 && argv[i][0] != '!' && argv[i][0] != '-') {
			q->level_max = v;
			i++;
			if (argc > i) {
				shell_error(sh, "unexpected args after level");
				return -EINVAL;
			}
			break;
		}

		if (is_uint_token(argv[i])) {
			q->limit = (unsigned int)strtoul(argv[i], NULL, 0);
			got_limit = true;
			i++;
			continue;
		}

		err = parse_mod_token(argv[i], &q->mod_mask, &have_include, &have_exclude);
		if (err == -ENOENT) {
			shell_error(sh, "unknown token '%s'", argv[i]);
			shell_print(sh,
				    "mods: sys xbox hid phone uart esb bq input");
			shell_print(sh, "also: from <idx>  /keyword  kw:keyword");
			return -EINVAL;
		}
		if (err) {
			shell_error(sh, "bad module token '%s'", argv[i]);
			return -EINVAL;
		}
		i++;
	}

	ARG_UNUSED(got_limit);
	if (keyword_required && (q->keyword == NULL || q->keyword[0] == '\0')) {
		shell_error(sh, "keyword required");
		return -EINVAL;
	}
	return 0;
}

static int cmd_flog_show(const struct shell *sh, size_t argc, char **argv)
{
	struct hub_flash_log_query q;
	int err;

	err = parse_query_args(sh, argc, argv, 1, &q, false);
	if (err) {
		return err;
	}
	return hub_flash_log_dump(sh, &q);
}

static int cmd_flog_find(const struct shell *sh, size_t argc, char **argv)
{
	struct hub_flash_log_query q;
	int err;

	err = parse_query_args(sh, argc, argv, 1, &q, true);
	if (err) {
		return err;
	}
	return hub_flash_log_dump(sh, &q);
}

static int cmd_flog_clear(const struct shell *sh, size_t argc, char **argv)
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	err = hub_flash_log_clear();
	if (err) {
		shell_error(sh, "clear failed: %d", err);
		return err;
	}
	shell_print(sh, "flash log cleared");
	return 0;
}

static int cmd_flog_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	hub_flash_log_status(sh);
	return 0;
}

static int cmd_flog_mirror(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "uart mirror: %s", hub_log_uart_mirror ? "on" : "off");
		shell_print(sh, "usage: flog mirror <on|off>");
		return 0;
	}
	if (!strcmp(argv[1], "on") || !strcmp(argv[1], "1")) {
		hub_log_uart_mirror = true;
	} else if (!strcmp(argv[1], "off") || !strcmp(argv[1], "0")) {
		hub_log_uart_mirror = false;
	} else {
		shell_error(sh, "use on|off");
		return -EINVAL;
	}
	shell_print(sh, "uart mirror -> %s", hub_log_uart_mirror ? "on" : "off");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(flog_cmds,
	SHELL_CMD_ARG(show, NULL,
		      "Show: [N] [from idx] [/kw] [mod|!mod]... [lvl]",
		      cmd_flog_show, 1, 12),
	SHELL_CMD_ARG(find, NULL,
		      "Find: <kw> [N] [from idx] [mod|!mod]... [lvl]",
		      cmd_flog_find, 2, 12),
	SHELL_CMD(clear, NULL, "Erase flash log", cmd_flog_clear),
	SHELL_CMD(status, NULL, "Flash log + module levels", cmd_flog_status),
	SHELL_CMD_ARG(mirror, NULL, "UART mirror on|off", cmd_flog_mirror, 1, 1),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(flog, &flog_cmds, "External flash application log", NULL);
