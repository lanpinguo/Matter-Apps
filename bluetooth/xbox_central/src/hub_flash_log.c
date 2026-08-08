/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Single shared circular log on external SPI NOR (all levels share one ring).
 * 4KB sector erase + soft-delete flags; mixed pages reclaim via scratch GC
 * (drop same-level, keep other levels packed at page start).
 */

#include "hub_flash_log.h"

#include "hub_log.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if !DT_NODE_HAS_STATUS(DT_NODELABEL(mx25r64), okay)
#error "mx25r64 not enabled — set status okay in board overlay"
#endif

#define HUB_FLASH_NODE      DT_NODELABEL(mx25r64)
#define HUB_LOG_OFF         0U
#define HUB_LOG_SIZE        (1024U * 1024U)
#define META_MAGIC          0x48424C47U /* 'HBLG' */
#define META_VERSION        5U
#define REC_MAGIC           0xA55CU
#define HUB_REC_F_LIVE      0xFFU
#define HUB_REC_F_DEAD      0x00U
#define RAM_RING_SIZE       2048U
#define MAX_LINE            160U
#define DUMP_REF_MAX        256U
#define PAGE_BUF_MAX        4096U

struct hub_flash_meta {
	uint32_t magic;
	uint32_t version;
	uint32_t boot_id;
	uint32_t head;
	uint32_t wrapped;
	uint32_t seq;
	uint32_t reserved[2];
};

struct hub_flash_rec_hdr {
	uint16_t magic;
	uint8_t level;
	uint8_t mod;
	uint8_t len;
	uint8_t flags;
	uint32_t boot_id;
	uint32_t uptime_ms;
} __packed;

struct dump_ref {
	uint32_t boot_id;
	uint32_t uptime_ms;
	uint32_t abs_off;
	uint8_t level;
	uint8_t mod;
	uint8_t len;
	uint8_t pad;
};

static const struct device *flash_dev;
static size_t page_size;
static size_t data_off;   /* absolute offset of ring start within HUB_LOG region */
static size_t data_size;  /* ring size (excludes meta + scratch pages) */
static size_t scratch_off; /* absolute offset of scratch page within chip */

static struct hub_flash_meta meta;
static bool ready;
static struct k_mutex lock;
static struct k_work flush_work;
/* Dedicated WQ so SPI erase/write never blocks HCI TX (system workqueue). */
#define HUB_LOG_WQ_STACK_SIZE 2048
#define HUB_LOG_WQ_PRIO       8
static K_THREAD_STACK_DEFINE(hub_log_wq_stack, HUB_LOG_WQ_STACK_SIZE);
static struct k_work_q hub_log_wq;
static bool hub_log_wq_started;

static uint8_t ram_ring[RAM_RING_SIZE];
static size_t ram_in;
static size_t ram_out;
static size_t ram_used;

static uint8_t page_buf[PAGE_BUF_MAX];
static uint8_t keep_buf[PAGE_BUF_MAX];

static void format_ts(char *buf, size_t buflen, uint32_t boot_id, uint32_t uptime_ms)
{
	uint32_t ms = uptime_ms % 1000U;
	uint32_t sec = (uptime_ms / 1000U) % 60U;
	uint32_t min = (uptime_ms / 60000U) % 60U;
	uint32_t hour = uptime_ms / 3600000U;

	snprintk(buf, buflen, "b%u+%u:%02u:%02u.%03u", boot_id, hour, min, sec, ms);
}

static off_t data_abs(size_t data_rel)
{
	return (off_t)(HUB_LOG_OFF + data_off + data_rel);
}

static int meta_write(void)
{
	int err;

	err = flash_erase(flash_dev, HUB_LOG_OFF, page_size);
	if (err) {
		return err;
	}
	return flash_write(flash_dev, HUB_LOG_OFF, &meta, sizeof(meta));
}

static int meta_load(void)
{
	int err;

	err = flash_read(flash_dev, HUB_LOG_OFF, &meta, sizeof(meta));
	if (err) {
		return err;
	}
	if (meta.magic != META_MAGIC || meta.version != META_VERSION ||
	    meta.head > data_size) {
		return -ENOENT;
	}
	return 0;
}

static int erase_data_page(size_t data_rel_off)
{
	size_t page_rel = (data_rel_off / page_size) * page_size;

	return flash_erase(flash_dev, data_abs(page_rel), page_size);
}

static int erase_scratch(void)
{
	return flash_erase(flash_dev, (off_t)(HUB_LOG_OFF + scratch_off), page_size);
}

static bool hdr_struct_ok(const struct hub_flash_rec_hdr *hdr)
{
	return hdr->magic == REC_MAGIC && hdr->len > 0U && hdr->len < MAX_LINE &&
	       hdr->mod < HUB_MOD_COUNT && hdr->level <= HUB_LOG_DBG;
}

static bool hdr_live(const struct hub_flash_rec_hdr *hdr)
{
	return hdr_struct_ok(hdr) && hdr->flags == HUB_REC_F_LIVE;
}

/**
 * Soft-delete one record by clearing flags (NOR 1→0). Does not free space.
 */
static int soft_delete_at(size_t data_rel)
{
	uint8_t dead = HUB_REC_F_DEAD;
	off_t flags_off = data_abs(data_rel) +
			  (off_t)offsetof(struct hub_flash_rec_hdr, flags);

	return flash_write(flash_dev, flags_off, &dead, 1);
}

/**
 * When head is at a page boundary, skip LIVE (and structurally valid DEAD)
 * records packed from the page start until free erased space.
 */
static int align_head_past_live(void)
{
	struct hub_flash_rec_hdr hdr;
	size_t page_end;
	size_t off = meta.head;
	size_t step;
	int err;

	if (off >= data_size) {
		meta.head = 0U;
		off = 0U;
	}
	if (off % page_size != 0U) {
		return 0;
	}

	page_end = (off / page_size) * page_size + page_size;
	if (page_end > data_size) {
		page_end = data_size;
	}

	while (off + sizeof(hdr) <= page_end) {
		err = flash_read(flash_dev, data_abs(off), &hdr, sizeof(hdr));
		if (err) {
			return err;
		}
		if (!hdr_struct_ok(&hdr)) {
			break;
		}
		step = sizeof(hdr) + hdr.len;
		if (off + step > page_end) {
			break;
		}
		off += step;
	}

	meta.head = off;
	return 0;
}

/**
 * Reclaim @p victim_page_rel for writes of @p drop_level:
 * keep LIVE records with other levels (compact to page start via scratch);
 * same-level LIVE entries are soft-deleted then dropped from the compacted page.
 */
static int reclaim_page_for_level(size_t victim_page_rel, uint8_t drop_level)
{
	struct hub_flash_rec_hdr hdr;
	size_t scan = 0;
	size_t keep_len = 0;
	size_t step;
	bool need_gc = false;
	int err;

	if (page_size > PAGE_BUF_MAX) {
		return -ENOMEM;
	}

	err = flash_read(flash_dev, data_abs(victim_page_rel), page_buf, page_size);
	if (err) {
		return err;
	}

	while (scan + sizeof(hdr) <= page_size) {
		memcpy(&hdr, &page_buf[scan], sizeof(hdr));
		if (!hdr_struct_ok(&hdr)) {
			break;
		}
		step = sizeof(hdr) + hdr.len;
		if (scan + step > page_size) {
			break;
		}
		if (hdr.flags == HUB_REC_F_LIVE) {
			if (hdr.level == drop_level) {
				(void)soft_delete_at(victim_page_rel + scan);
			} else {
				need_gc = true;
				if (keep_len + step > page_size) {
					return -ENOMEM;
				}
				memcpy(&keep_buf[keep_len], &page_buf[scan], step);
				keep_len += step;
			}
		}
		scan += step;
	}

	if (!need_gc) {
		return erase_data_page(victim_page_rel);
	}

	err = erase_scratch();
	if (err) {
		return err;
	}
	err = flash_write(flash_dev, (off_t)(HUB_LOG_OFF + scratch_off), keep_buf,
			  keep_len);
	if (err) {
		return err;
	}

	err = erase_data_page(victim_page_rel);
	if (err) {
		return err;
	}
	err = flash_read(flash_dev, (off_t)(HUB_LOG_OFF + scratch_off), page_buf,
			 keep_len);
	if (err) {
		return err;
	}
	err = flash_write(flash_dev, data_abs(victim_page_rel), page_buf, keep_len);
	if (err) {
		return err;
	}
	(void)erase_scratch();
	return 0;
}

static int ensure_space_for_level(uint8_t level, size_t need)
{
	size_t head_page;
	size_t end;
	size_t end_page;
	size_t victim;
	int err;

	if (need > data_size) {
		return -ENOMEM;
	}

	err = align_head_past_live();
	if (err) {
		return err;
	}

	head_page = meta.head / page_size;
	end = meta.head + need;
	if (end <= data_size) {
		end_page = (end - 1U) / page_size;
		if (end_page != head_page) {
			victim = end_page * page_size;
			err = reclaim_page_for_level(victim, level);
			if (err) {
				return err;
			}
		} else if (meta.head % page_size == 0U) {
			victim = meta.head;
			err = reclaim_page_for_level(victim, level);
			if (err) {
				return err;
			}
			err = align_head_past_live();
			if (err) {
				return err;
			}
			if (meta.head + need > data_size ||
			    (meta.head / page_size) != (victim / page_size)) {
				/* keepers filled the page — try next page */
				if (meta.head >= data_size) {
					meta.head = 0U;
					meta.wrapped = 1U;
				}
				if (meta.head % page_size == 0U) {
					err = reclaim_page_for_level(meta.head, level);
					if (err) {
						return err;
					}
					err = align_head_past_live();
					if (err) {
						return err;
					}
				}
			}
		}
		return 0;
	}

	victim = 0;
	meta.wrapped = 1U;
	meta.head = 0U;
	err = reclaim_page_for_level(victim, level);
	if (err) {
		return err;
	}
	err = align_head_past_live();
	if (err) {
		return err;
	}
	if (need > page_size) {
		return -ENOMEM;
	}
	return 0;
}

static int write_record(uint8_t mod, int level, const char *text, uint8_t len)
{
	struct hub_flash_rec_hdr hdr = {
		.magic = REC_MAGIC,
		.level = (uint8_t)level,
		.mod = mod,
		.len = len,
		.flags = HUB_REC_F_LIVE,
		.boot_id = meta.boot_id,
		.uptime_ms = k_uptime_get_32(),
	};
	size_t total = sizeof(hdr) + len;
	off_t abs;
	int err;

	err = ensure_space_for_level((uint8_t)level, total);
	if (err) {
		return err;
	}

	/*
	 * After GC, keepers may leave little room on this page. Advance to the
	 * next page and reclaim again if the record would cross the page end.
	 */
	if (meta.head + total > data_size ||
	    (meta.head / page_size) != ((meta.head + total - 1U) / page_size)) {
		size_t next = ((meta.head / page_size) + 1U) * page_size;

		if (next >= data_size) {
			meta.wrapped = 1U;
			meta.head = 0U;
		} else {
			meta.head = next;
		}
		err = reclaim_page_for_level(meta.head, (uint8_t)level);
		if (err) {
			return err;
		}
		err = align_head_past_live();
		if (err) {
			return err;
		}
		if (meta.head + total > data_size ||
		    (meta.head / page_size) != ((meta.head + total - 1U) / page_size)) {
			return -ENOMEM;
		}
	}

	abs = data_abs(meta.head);
	err = flash_write(flash_dev, abs, &hdr, sizeof(hdr));
	if (err) {
		return err;
	}
	err = flash_write(flash_dev, abs + (off_t)sizeof(hdr), text, len);
	if (err) {
		return err;
	}

	meta.head += total;
	if (meta.head >= data_size) {
		meta.head = 0U;
		meta.wrapped = 1U;
	}
	meta.seq++;

	if ((meta.seq & 0x0FU) == 0U) {
		(void)meta_write();
	}
	return 0;
}

static bool ram_pop_line(uint8_t *mod, int *level, char *out, size_t out_sz,
			 uint8_t *out_len)
{
	uint8_t m;
	uint8_t lvl;
	uint8_t len;
	size_t i;
	unsigned int key;

	key = irq_lock();
	if (ram_used < 3U) {
		irq_unlock(key);
		return false;
	}

	m = ram_ring[ram_out];
	ram_out = (ram_out + 1U) % RAM_RING_SIZE;
	lvl = ram_ring[ram_out];
	ram_out = (ram_out + 1U) % RAM_RING_SIZE;
	len = ram_ring[ram_out];
	ram_out = (ram_out + 1U) % RAM_RING_SIZE;
	ram_used -= 3U;

	if (len > ram_used || len >= out_sz) {
		ram_in = ram_out = ram_used = 0U;
		irq_unlock(key);
		return false;
	}

	for (i = 0; i < len; i++) {
		out[i] = (char)ram_ring[ram_out];
		ram_out = (ram_out + 1U) % RAM_RING_SIZE;
	}
	ram_used -= len;
	irq_unlock(key);

	out[len] = '\0';
	*mod = m;
	*level = lvl;
	*out_len = len;
	return true;
}

static void flush_work_handler(struct k_work *work)
{
	char line[MAX_LINE];
	uint8_t len;
	uint8_t mod;
	int level;
	int err;

	ARG_UNUSED(work);

	k_mutex_lock(&lock, K_FOREVER);
	while (ram_pop_line(&mod, &level, line, sizeof(line), &len)) {
		err = write_record(mod, level, line, len);
		if (err) {
			break;
		}
	}
	(void)meta_write();
	k_mutex_unlock(&lock);
}

int hub_flash_log_init(void)
{
	struct flash_pages_info info;
	int err;

	k_mutex_init(&lock);
	k_work_init(&flush_work, flush_work_handler);
	k_work_queue_init(&hub_log_wq);
	k_work_queue_start(&hub_log_wq, hub_log_wq_stack,
			   K_THREAD_STACK_SIZEOF(hub_log_wq_stack), HUB_LOG_WQ_PRIO,
			   NULL);
	(void)k_thread_name_set(&hub_log_wq.thread, "hub_flog");
	hub_log_wq_started = true;

	flash_dev = DEVICE_DT_GET(HUB_FLASH_NODE);
	if (!device_is_ready(flash_dev)) {
		flash_dev = NULL;
		return -ENODEV;
	}

	err = flash_get_page_info_by_offs(flash_dev, HUB_LOG_OFF, &info);
	if (err) {
		flash_dev = NULL;
		return err;
	}
	page_size = info.size;
	/* Need: meta page + >=1 data page + scratch page */
	if (page_size == 0U || page_size > PAGE_BUF_MAX ||
	    (HUB_LOG_SIZE % page_size) != 0U ||
	    HUB_LOG_SIZE < page_size * 3U) {
		flash_dev = NULL;
		return -EINVAL;
	}

	data_off = page_size;
	scratch_off = HUB_LOG_SIZE - page_size;
	data_size = HUB_LOG_SIZE - (2U * page_size); /* exclude meta + scratch */

	if (meta_load() != 0) {
		memset(&meta, 0, sizeof(meta));
		meta.magic = META_MAGIC;
		meta.version = META_VERSION;
		meta.boot_id = 1U;
		err = meta_write();
		if (err) {
			flash_dev = NULL;
			return err;
		}
		(void)erase_data_page(0);
		(void)erase_scratch();
	} else {
		meta.boot_id++;
		(void)meta_write();
	}

	ready = true;
	return 0;
}

bool hub_flash_log_ready(void)
{
	return ready;
}

void hub_flash_log_append(uint8_t mod, int level, const char *text, size_t len)
{
	size_t i;
	unsigned int key;

	if (!ready || text == NULL || len == 0U || mod >= HUB_MOD_COUNT ||
	    level < HUB_LOG_ERR || level > HUB_LOG_DBG) {
		return;
	}
	if (len > MAX_LINE - 1U) {
		len = MAX_LINE - 1U;
	}
	if (ram_used + 3U + len > RAM_RING_SIZE) {
		return;
	}

	key = irq_lock();
	ram_ring[ram_in] = mod;
	ram_in = (ram_in + 1U) % RAM_RING_SIZE;
	ram_ring[ram_in] = (uint8_t)level;
	ram_in = (ram_in + 1U) % RAM_RING_SIZE;
	ram_ring[ram_in] = (uint8_t)len;
	ram_in = (ram_in + 1U) % RAM_RING_SIZE;
	for (i = 0; i < len; i++) {
		ram_ring[ram_in] = (uint8_t)text[i];
		ram_in = (ram_in + 1U) % RAM_RING_SIZE;
	}
	ram_used += 3U + len;
	irq_unlock(key);

	if (hub_log_wq_started) {
		(void)k_work_submit_to_queue(&hub_log_wq, &flush_work);
	}
}

void hub_flash_log_flush(void)
{
	if (!ready) {
		return;
	}
	(void)k_work_cancel(&flush_work);
	flush_work_handler(&flush_work);
}

int hub_flash_log_clear(void)
{
	uint32_t next_boot;
	int err;

	if (!ready) {
		return -ENODEV;
	}

	hub_flash_log_flush();
	k_mutex_lock(&lock, K_FOREVER);
	next_boot = meta.boot_id + 1U;
	err = flash_erase(flash_dev, HUB_LOG_OFF, HUB_LOG_SIZE);
	if (err == 0) {
		memset(&meta, 0, sizeof(meta));
		meta.magic = META_MAGIC;
		meta.version = META_VERSION;
		meta.boot_id = next_boot ? next_boot : 1U;
		err = meta_write();
		(void)erase_data_page(0);
		(void)erase_scratch();
	}
	ram_in = ram_out = ram_used = 0U;
	k_mutex_unlock(&lock);
	return err;
}

/**
 * Read record at @p off. Returns 0 for LIVE match, -ENOENT for structurally
 * valid but soft-deleted (caller should skip by len), -EINVAL for free/corrupt.
 */
static int read_rec_at(size_t off, struct hub_flash_rec_hdr *hdr, char *text,
		       size_t text_sz)
{
	off_t abs = data_abs(off);
	int err;

	err = flash_read(flash_dev, abs, hdr, sizeof(*hdr));
	if (err) {
		return err;
	}
	if (!hdr_struct_ok(hdr) || hdr->len >= text_sz) {
		return -EINVAL;
	}
	if (hdr->flags != HUB_REC_F_LIVE) {
		return -ENOENT;
	}
	err = flash_read(flash_dev, abs + (off_t)sizeof(*hdr), text, hdr->len);
	if (err) {
		return err;
	}
	text[hdr->len] = '\0';
	return 0;
}

static bool text_has_keyword(const char *text, const char *keyword)
{
	char t;
	char k;
	const char *p;
	const char *q;

	if (keyword == NULL || keyword[0] == '\0') {
		return true;
	}
	if (text == NULL) {
		return false;
	}

	for (; *text != '\0'; text++) {
		p = text;
		q = keyword;
		while (*q != '\0') {
			t = *p;
			k = *q;
			if (t >= 'A' && t <= 'Z') {
				t = (char)(t - 'A' + 'a');
			}
			if (k >= 'A' && k <= 'Z') {
				k = (char)(k - 'A' + 'a');
			}
			if (t == '\0' || t != k) {
				break;
			}
			p++;
			q++;
		}
		if (*q == '\0') {
			return true;
		}
	}
	return false;
}

static bool rec_match(const struct hub_flash_rec_hdr *hdr, const char *text,
		      const struct hub_flash_log_query *q)
{
	if (((q->mod_mask >> hdr->mod) & 1U) == 0U) {
		return false;
	}
	if (q->level_max >= 0 && (int)hdr->level > q->level_max) {
		return false;
	}
	if (!text_has_keyword(text, q->keyword)) {
		return false;
	}
	return true;
}

void hub_flash_log_status(const struct shell *sh)
{
	uint8_t i;

	if (!ready) {
		shell_print(sh, "flash log: not ready");
		return;
	}
	shell_print(sh, "flash log: shared ring size=%u B page=%u B data=%u B boot_id=%u",
		    (unsigned int)HUB_LOG_SIZE, (unsigned int)page_size,
		    (unsigned int)data_size, meta.boot_id);
	shell_print(sh, "  head=%u wrapped=%u seq=%u ram_pending=%u scratch_off=%u",
		    (unsigned int)meta.head, (unsigned int)meta.wrapped,
		    (unsigned int)meta.seq, (unsigned int)ram_used,
		    (unsigned int)scratch_off);
	shell_print(sh, "  reclaim: drop same-level, keep others via GC (soft-delete + 4K)");
	shell_print(sh, "  uart_mirror=%s", hub_log_uart_mirror ? "on" : "off");
	shell_print(sh, "  module levels:");
	for (i = 0; i < HUB_MOD_COUNT; i++) {
		shell_print(sh, "    %-5s %s", hub_log_mod_str(i),
			    hub_log_level_str(hub_log_get_level(i)));
	}
}

int hub_flash_log_dump(const struct shell *sh, const struct hub_flash_log_query *q)
{
	static struct dump_ref refs[DUMP_REF_MAX];
	struct hub_flash_rec_hdr hdr;
	char text[MAX_LINE];
	char ts[32];
	char filt[96];
	size_t off;
	size_t end;
	size_t visited;
	size_t step;
	size_t nfilt = 0;
	unsigned int nrefs = 0;
	unsigned int skip;
	unsigned int printed = 0;
	unsigned int i;
	unsigned int end_i;
	uint8_t mi;
	int err;

	if (q == NULL) {
		return -EINVAL;
	}
	if (!ready) {
		shell_error(sh, "flash log not ready");
		return -ENODEV;
	}

	hub_flash_log_flush();
	k_mutex_lock(&lock, K_FOREVER);

	if (meta.wrapped) {
		off = meta.head;
		end = meta.head;
	} else {
		off = 0;
		end = meta.head;
	}

	visited = 0;
	while (visited < data_size) {
		if (!meta.wrapped && off >= end) {
			break;
		}
		if (meta.wrapped && visited > 0U && off == end) {
			break;
		}
		err = read_rec_at(off, &hdr, text, sizeof(text));
		if (err == -ENOENT) {
			/* Soft-deleted: still advance by record size. */
			err = flash_read(flash_dev, data_abs(off), &hdr, sizeof(hdr));
			if (err || !hdr_struct_ok(&hdr)) {
				step = page_size - (off % page_size);
				if (step == 0U) {
					step = page_size;
				}
			} else {
				step = sizeof(hdr) + hdr.len;
			}
			off = (off + step) % data_size;
			visited += step;
			continue;
		}
		if (err) {
			step = page_size - (off % page_size);
			if (step == 0U) {
				step = page_size;
			}
			off = (off + step) % data_size;
			visited += step;
			if (!meta.wrapped) {
				break;
			}
			continue;
		}
		step = sizeof(hdr) + hdr.len;
		if (rec_match(&hdr, text, q) && nrefs < DUMP_REF_MAX) {
			refs[nrefs].boot_id = hdr.boot_id;
			refs[nrefs].uptime_ms = hdr.uptime_ms;
			refs[nrefs].abs_off = (uint32_t)data_abs(off);
			refs[nrefs].level = hdr.level;
			refs[nrefs].mod = hdr.mod;
			refs[nrefs].len = hdr.len;
			nrefs++;
		}
		off = (off + step) % data_size;
		visited += step;
		if (meta.wrapped && off == end) {
			break;
		}
		if (!meta.wrapped && off >= end) {
			break;
		}
	}

	if (q->take_tail && q->from == 0U) {
		if (q->limit == 0U || q->limit > nrefs) {
			skip = 0;
		} else {
			skip = nrefs - q->limit;
		}
	} else {
		skip = q->from;
		if (skip > nrefs) {
			skip = nrefs;
		}
	}

	end_i = nrefs;
	if (q->limit != 0U && (skip + q->limit) < end_i) {
		end_i = skip + q->limit;
	}

	for (i = skip; i < end_i; i++) {
		err = flash_read(flash_dev, (off_t)refs[i].abs_off, &hdr, sizeof(hdr));
		if (err || refs[i].len >= sizeof(text) || !hdr_live(&hdr)) {
			continue;
		}
		err = flash_read(flash_dev,
				 (off_t)refs[i].abs_off + (off_t)sizeof(hdr),
				 text, refs[i].len);
		if (err) {
			continue;
		}
		text[refs[i].len] = '\0';
		format_ts(ts, sizeof(ts), refs[i].boot_id, refs[i].uptime_ms);
		shell_print(sh, "[#%u %s %s:%s] %s", i, ts,
			    hub_log_mod_str(refs[i].mod),
			    hub_log_level_str(refs[i].level), text);
		printed++;
	}

	k_mutex_unlock(&lock);

	if (q->mod_mask == HUB_LOG_MOD_MASK_ALL) {
		snprintk(filt, sizeof(filt), "all");
	} else if (q->mod_mask == 0U) {
		snprintk(filt, sizeof(filt), "none");
	} else {
		unsigned int n_in = 0;
		unsigned int n_ex = 0;

		for (mi = 0; mi < HUB_MOD_COUNT; mi++) {
			if (((q->mod_mask >> mi) & 1U) != 0U) {
				n_in++;
			} else {
				n_ex++;
			}
		}
		filt[0] = '\0';
		nfilt = 0;
		if (n_ex > 0U && n_ex <= n_in) {
			nfilt = snprintk(filt, sizeof(filt), "all");
			for (mi = 0; mi < HUB_MOD_COUNT; mi++) {
				if (((q->mod_mask >> mi) & 1U) != 0U) {
					continue;
				}
				nfilt += snprintk(filt + nfilt, sizeof(filt) - nfilt,
						  ",!%s", hub_log_mod_str(mi));
				if (nfilt >= sizeof(filt) - 1U) {
					break;
				}
			}
		} else {
			for (mi = 0; mi < HUB_MOD_COUNT; mi++) {
				if (((q->mod_mask >> mi) & 1U) == 0U) {
					continue;
				}
				nfilt += snprintk(filt + nfilt, sizeof(filt) - nfilt, "%s%s",
						  nfilt ? "," : "", hub_log_mod_str(mi));
				if (nfilt >= sizeof(filt) - 1U) {
					break;
				}
			}
		}
	}

	shell_print(sh,
		    "--- %u/%u line(s) from=%u limit=%u mod=%s lvl<=%s kw=%s ---",
		    printed, nrefs, q->from, q->limit, filt,
		    q->level_max < 0 ? "dbg" : hub_log_level_str(q->level_max),
		    (q->keyword != NULL && q->keyword[0] != '\0') ? q->keyword : "-");
	if (nrefs >= DUMP_REF_MAX) {
		shell_print(sh, "note: dump capped at %u matches", DUMP_REF_MAX);
	}
	return 0;
}

/* ---- Non-shell export (BLE) ---- */

static struct dump_ref export_refs[DUMP_REF_MAX];
static unsigned int export_skip;
static unsigned int export_end;
static unsigned int export_pos;
static bool export_active;
static char export_keyword[HUB_FLASH_LOG_KEYWORD_MAX];

bool hub_flash_log_export_active(void)
{
	return export_active;
}

void hub_flash_log_export_abort(void)
{
	k_mutex_lock(&lock, K_FOREVER);
	export_active = false;
	export_pos = export_skip = export_end = 0U;
	k_mutex_unlock(&lock);
}

int hub_flash_log_export_start(const struct hub_flash_log_query *q,
			       uint16_t *out_total)
{
	struct hub_flash_log_query local;
	struct hub_flash_rec_hdr hdr;
	char text[MAX_LINE];
	size_t off;
	size_t end;
	size_t visited;
	size_t step;
	unsigned int nrefs = 0;
	unsigned int skip;
	unsigned int end_i;
	size_t kwlen;
	int err;

	if (q == NULL || out_total == NULL) {
		return -EINVAL;
	}
	if (!ready) {
		return -ENODEV;
	}

	k_mutex_lock(&lock, K_FOREVER);
	if (export_active) {
		k_mutex_unlock(&lock);
		return -EBUSY;
	}

	local = *q;
	export_keyword[0] = '\0';
	if (q->keyword != NULL && q->keyword[0] != '\0') {
		kwlen = strlen(q->keyword);
		if (kwlen >= sizeof(export_keyword)) {
			kwlen = sizeof(export_keyword) - 1U;
		}
		memcpy(export_keyword, q->keyword, kwlen);
		export_keyword[kwlen] = '\0';
		local.keyword = export_keyword;
	} else {
		local.keyword = NULL;
	}

	k_mutex_unlock(&lock);
	hub_flash_log_flush();
	k_mutex_lock(&lock, K_FOREVER);

	if (meta.wrapped) {
		off = meta.head;
		end = meta.head;
	} else {
		off = 0;
		end = meta.head;
	}

	visited = 0;
	while (visited < data_size) {
		if (!meta.wrapped && off >= end) {
			break;
		}
		if (meta.wrapped && visited > 0U && off == end) {
			break;
		}
		err = read_rec_at(off, &hdr, text, sizeof(text));
		if (err == -ENOENT) {
			err = flash_read(flash_dev, data_abs(off), &hdr, sizeof(hdr));
			if (err || !hdr_struct_ok(&hdr)) {
				step = page_size - (off % page_size);
				if (step == 0U) {
					step = page_size;
				}
			} else {
				step = sizeof(hdr) + hdr.len;
			}
			off = (off + step) % data_size;
			visited += step;
			continue;
		}
		if (err) {
			step = page_size - (off % page_size);
			if (step == 0U) {
				step = page_size;
			}
			off = (off + step) % data_size;
			visited += step;
			if (!meta.wrapped) {
				break;
			}
			continue;
		}
		step = sizeof(hdr) + hdr.len;
		if (rec_match(&hdr, text, &local) && nrefs < DUMP_REF_MAX) {
			export_refs[nrefs].boot_id = hdr.boot_id;
			export_refs[nrefs].uptime_ms = hdr.uptime_ms;
			export_refs[nrefs].abs_off = (uint32_t)data_abs(off);
			export_refs[nrefs].level = hdr.level;
			export_refs[nrefs].mod = hdr.mod;
			export_refs[nrefs].len = hdr.len;
			nrefs++;
		}
		off = (off + step) % data_size;
		visited += step;
		if (meta.wrapped && off == end) {
			break;
		}
		if (!meta.wrapped && off >= end) {
			break;
		}
	}

	if (local.take_tail && local.from == 0U) {
		if (local.limit == 0U || local.limit > nrefs) {
			skip = 0;
		} else {
			skip = nrefs - local.limit;
		}
	} else {
		skip = local.from;
		if (skip > nrefs) {
			skip = nrefs;
		}
	}

	end_i = nrefs;
	if (local.limit != 0U && (skip + local.limit) < end_i) {
		end_i = skip + local.limit;
	}

	export_skip = skip;
	export_end = end_i;
	export_pos = skip;
	export_active = true;
	*out_total = (uint16_t)(end_i - skip);
	k_mutex_unlock(&lock);
	return 0;
}

int hub_flash_log_export_next(struct hub_flash_log_rec *out)
{
	struct hub_flash_rec_hdr hdr;
	int err;

	if (out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&lock, K_FOREVER);
	if (!export_active) {
		k_mutex_unlock(&lock);
		return -EINVAL;
	}

	while (export_pos < export_end) {
		err = flash_read(flash_dev, (off_t)export_refs[export_pos].abs_off,
				 &hdr, sizeof(hdr));
		if (err || export_refs[export_pos].len >= sizeof(out->text) ||
		    !hdr_live(&hdr)) {
			export_pos++;
			continue;
		}
		err = flash_read(flash_dev,
				 (off_t)export_refs[export_pos].abs_off +
					 (off_t)sizeof(hdr),
				 out->text, export_refs[export_pos].len);
		if (err) {
			export_pos++;
			continue;
		}

		out->text[export_refs[export_pos].len] = '\0';
		out->index = (uint16_t)export_pos;
		out->level = export_refs[export_pos].level;
		out->mod = export_refs[export_pos].mod;
		out->boot_id = export_refs[export_pos].boot_id;
		out->uptime_ms = export_refs[export_pos].uptime_ms;
		out->text_len = export_refs[export_pos].len;
		export_pos++;
		k_mutex_unlock(&lock);
		return 0;
	}

	export_active = false;
	k_mutex_unlock(&lock);
	return 1;
}
