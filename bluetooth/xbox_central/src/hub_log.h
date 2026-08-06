/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Per-module runtime logging for xbox_central.
 * Default: store to external flash, UART mirror off.
 *
 *   loglevel                 — list modules
 *   loglevel <mod|all> <lvl>
 *   flog show [N] [mod] [lvl]
 */

#ifndef HUB_LOG_H_
#define HUB_LOG_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/printk.h>

enum hub_log_level {
	HUB_LOG_ERR = 0,
	HUB_LOG_WRN = 1,
	HUB_LOG_INF = 2,
	HUB_LOG_DBG = 3,
};

enum hub_log_mod {
	HUB_MOD_SYS = 0,
	HUB_MOD_XBOX,
	HUB_MOD_HID,
	HUB_MOD_PHONE,
	HUB_MOD_UART,
	HUB_MOD_ESB,
	HUB_MOD_BQ,
	HUB_MOD_INPUT,
	HUB_MOD_COUNT,
};

#define HUB_LOG_MOD_MASK_ALL ((1u << HUB_MOD_COUNT) - 1u)

/** When true, accepted HUB_* lines also print to UART. Default false. */
extern bool hub_log_uart_mirror;

/**
 * Declare the default module for this translation unit.
 * Place once after includes; then HUB_ERR/WRN/INF/DBG use it.
 */
#define HUB_LOG_MODULE_DEFINE(_mod) \
	static const uint8_t __hub_log_module = (_mod)

void hub_printk(uint8_t mod, int level, const char *fmt, ...);

#define HUB_ERR(...) hub_printk(__hub_log_module, HUB_LOG_ERR, __VA_ARGS__)
#define HUB_WRN(...) hub_printk(__hub_log_module, HUB_LOG_WRN, __VA_ARGS__)
#define HUB_INF(...) hub_printk(__hub_log_module, HUB_LOG_INF, __VA_ARGS__)
#define HUB_DBG(...) hub_printk(__hub_log_module, HUB_LOG_DBG, __VA_ARGS__)

/** Explicit module (for files that span several domains, e.g. main.c). */
#define HUB_ERR_M(_mod, ...) hub_printk((_mod), HUB_LOG_ERR, __VA_ARGS__)
#define HUB_WRN_M(_mod, ...) hub_printk((_mod), HUB_LOG_WRN, __VA_ARGS__)
#define HUB_INF_M(_mod, ...) hub_printk((_mod), HUB_LOG_INF, __VA_ARGS__)
#define HUB_DBG_M(_mod, ...) hub_printk((_mod), HUB_LOG_DBG, __VA_ARGS__)

/** Always print to UART (bypasses filter + flash). */
#define HUB_FORCE(...) printk(__VA_ARGS__)

const char *hub_log_level_str(int level);
int hub_log_level_from_str(const char *s);

const char *hub_log_mod_str(uint8_t mod);
int hub_log_mod_from_str(const char *s);

int hub_log_get_level(uint8_t mod);
int hub_log_set_level(uint8_t mod, int level);
/** Set every module to @p level. */
void hub_log_set_level_all(int level);

#endif /* HUB_LOG_H_ */
