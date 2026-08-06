/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "hub_log.h"

#include "hub_flash_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/printk.h>

bool hub_log_uart_mirror;

static int hub_mod_level[HUB_MOD_COUNT] = {
	[HUB_MOD_SYS] = HUB_LOG_INF,
	[HUB_MOD_XBOX] = HUB_LOG_INF,
	[HUB_MOD_HID] = HUB_LOG_INF,
	[HUB_MOD_PHONE] = HUB_LOG_INF,
	[HUB_MOD_UART] = HUB_LOG_INF,
	[HUB_MOD_ESB] = HUB_LOG_INF,
	[HUB_MOD_BQ] = HUB_LOG_INF,
	[HUB_MOD_INPUT] = HUB_LOG_WRN,
};

void hub_printk(uint8_t mod, int level, const char *fmt, ...)
{
	char buf[160];
	va_list ap;
	int n;

	if (mod >= HUB_MOD_COUNT || fmt == NULL) {
		return;
	}
	if (level > hub_mod_level[mod]) {
		return;
	}

	va_start(ap, fmt);
	n = vsnprintk(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n < 0) {
		return;
	}
	if (n >= (int)sizeof(buf)) {
		n = (int)sizeof(buf) - 1;
	}

	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
		buf[--n] = '\0';
	}

	if (n > 0) {
		hub_flash_log_append(mod, level, buf, (size_t)n);
	}

	if (hub_log_uart_mirror) {
		printk("[%s:%s] %s\n", hub_log_mod_str(mod), hub_log_level_str(level),
		       buf);
	}
}

const char *hub_log_level_str(int level)
{
	switch (level) {
	case HUB_LOG_ERR:
		return "err";
	case HUB_LOG_WRN:
		return "wrn";
	case HUB_LOG_INF:
		return "inf";
	case HUB_LOG_DBG:
		return "dbg";
	default:
		return "?";
	}
}

int hub_log_level_from_str(const char *s)
{
	if (s == NULL) {
		return -1;
	}
	if (!strcmp(s, "err") || !strcmp(s, "error") || !strcmp(s, "0")) {
		return HUB_LOG_ERR;
	}
	if (!strcmp(s, "wrn") || !strcmp(s, "warn") || !strcmp(s, "1")) {
		return HUB_LOG_WRN;
	}
	if (!strcmp(s, "inf") || !strcmp(s, "info") || !strcmp(s, "2")) {
		return HUB_LOG_INF;
	}
	if (!strcmp(s, "dbg") || !strcmp(s, "debug") || !strcmp(s, "3")) {
		return HUB_LOG_DBG;
	}
	return -1;
}

const char *hub_log_mod_str(uint8_t mod)
{
	static const char *const names[HUB_MOD_COUNT] = {
		[HUB_MOD_SYS] = "sys",
		[HUB_MOD_XBOX] = "xbox",
		[HUB_MOD_HID] = "hid",
		[HUB_MOD_PHONE] = "phone",
		[HUB_MOD_UART] = "uart",
		[HUB_MOD_ESB] = "esb",
		[HUB_MOD_BQ] = "bq",
		[HUB_MOD_INPUT] = "input",
	};

	if (mod >= HUB_MOD_COUNT) {
		return "?";
	}
	return names[mod];
}

int hub_log_mod_from_str(const char *s)
{
	uint8_t i;

	if (s == NULL) {
		return -1;
	}
	for (i = 0; i < HUB_MOD_COUNT; i++) {
		if (!strcmp(s, hub_log_mod_str(i))) {
			return i;
		}
	}
	return -1;
}

int hub_log_get_level(uint8_t mod)
{
	if (mod >= HUB_MOD_COUNT) {
		return -1;
	}
	return hub_mod_level[mod];
}

int hub_log_set_level(uint8_t mod, int level)
{
	if (mod >= HUB_MOD_COUNT || level < HUB_LOG_ERR || level > HUB_LOG_DBG) {
		return -1;
	}
	hub_mod_level[mod] = level;
	return 0;
}

void hub_log_set_level_all(int level)
{
	uint8_t i;

	if (level < HUB_LOG_ERR || level > HUB_LOG_DBG) {
		return;
	}
	for (i = 0; i < HUB_MOD_COUNT; i++) {
		hub_mod_level[i] = level;
	}
}
