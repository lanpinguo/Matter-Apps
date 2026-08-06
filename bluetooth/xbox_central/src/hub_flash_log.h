/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Circular application log on external SPI NOR.
 */

#ifndef HUB_FLASH_LOG_H_
#define HUB_FLASH_LOG_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/shell/shell.h>

#define HUB_FLASH_LOG_TEXT_MAX 160U
#define HUB_FLASH_LOG_EXPORT_MAX 256U
#define HUB_FLASH_LOG_KEYWORD_MAX 32U

struct hub_flash_log_query {
	/** Max lines to print after @from. 0 = no limit. */
	unsigned int limit;
	/**
	 * Skip this many matching lines from the oldest.
	 * Ignored when @p take_tail is true (legacy "last N" mode).
	 */
	unsigned int from;
	/**
	 * If true (default for plain `flog show N`): print the last @p limit
	 * matching lines. Cleared when `from` is used.
	 */
	bool take_tail;
	uint32_t mod_mask;
	/** <0 = all levels; else only level <= level_max */
	int level_max;
	/** Optional case-insensitive substring in message text; NULL/"" = off */
	const char *keyword;
};

struct hub_flash_log_rec {
	uint16_t index;
	uint8_t level;
	uint8_t mod;
	uint32_t boot_id;
	uint32_t uptime_ms;
	uint8_t text_len;
	char text[HUB_FLASH_LOG_TEXT_MAX];
};

int hub_flash_log_init(void);
bool hub_flash_log_ready(void);

void hub_flash_log_append(uint8_t mod, int level, const char *text, size_t len);

void hub_flash_log_flush(void);
int hub_flash_log_clear(void);

int hub_flash_log_dump(const struct shell *sh, const struct hub_flash_log_query *q);

void hub_flash_log_status(const struct shell *sh);

/**
 * Begin a non-shell export session (for BLE). Copies @p q including keyword
 * (truncated). @p out_total receives matching count after filter/window.
 * Returns 0, or -EBUSY if another export is active.
 */
int hub_flash_log_export_start(const struct hub_flash_log_query *q,
			       uint16_t *out_total);

/** Fill next record. Returns 0, 1 if done, or negative errno. */
int hub_flash_log_export_next(struct hub_flash_log_rec *out);

void hub_flash_log_export_abort(void);
bool hub_flash_log_export_active(void);

#endif /* HUB_FLASH_LOG_H_ */
