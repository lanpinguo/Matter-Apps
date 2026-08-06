/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Protocol (LE):
 *   LogCtrl write:
 *     START 0x01 | flags | limit_u16 | from_u16 | mod_mask_u32 | level_max_u8 | kw_len | kw[]
 *       flags bit0 = take_tail; level_max 0xFF = all levels
 *     STOP  0x02
 *   LogData notify:
 *     BEGIN 0x01 | total_u16 | boot_id_u32
 *     REC   0x02 | idx_u16 | boot_id_u32 | uptime_ms_u32 | mod_u8 | level_u8 | text_len | text[]
 *     END   0x03 | status_u8   (0=ok 1=aborted 2=error)
 */

#include "hub_ble_log.h"

#include "hub_flash_log.h"
#include "hub_log.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define HUB_LOG_OP_START 0x01U
#define HUB_LOG_OP_STOP  0x02U

#define HUB_LOG_PKT_BEGIN 0x01U
#define HUB_LOG_PKT_REC   0x02U
#define HUB_LOG_PKT_END   0x03U

#define HUB_LOG_END_OK      0U
#define HUB_LOG_END_ABORT   1U
#define HUB_LOG_END_ERROR   2U

#define HUB_LOG_FLAG_TAKE_TAIL BIT(0)

#define HUB_LOG_NOTIFY_RETRY_MS 5U
#define HUB_LOG_NOTIFY_GAP_MS   2U
#define HUB_LOG_PKT_MAX         180U

static const struct bt_gatt_attr *log_data_attr;
static struct k_work_delayable log_tx_work;
static bool session_running;
static bool send_begin;
static bool have_pending;
static uint16_t session_total;
static uint8_t end_status;
static uint8_t tx_buf[HUB_LOG_PKT_MAX];
static struct hub_flash_log_rec pending_rec;

static void log_tx_work_handler(struct k_work *work);

void hub_ble_log_init(void)
{
	k_work_init_delayable(&log_tx_work, log_tx_work_handler);
	session_running = false;
	have_pending = false;
	log_data_attr = NULL;
}

void hub_ble_log_bind(const struct bt_gatt_attr *data_attr)
{
	log_data_attr = data_attr;
}

void hub_ble_log_on_phone_disconnect(void)
{
	(void)k_work_cancel_delayable(&log_tx_work);
	if (session_running || hub_flash_log_export_active()) {
		hub_flash_log_export_abort();
		session_running = false;
	}
}

static int notify_buf(const uint8_t *data, uint16_t len)
{
	int err;

	if (log_data_attr == NULL) {
		return -ENODEV;
	}
	err = bt_gatt_notify(NULL, log_data_attr, data, len);
	return err;
}

static void schedule_tx(uint32_t delay_ms)
{
	(void)k_work_reschedule(&log_tx_work, K_MSEC(delay_ms));
}

static void finish_session(uint8_t status)
{
	uint8_t pkt[2] = { HUB_LOG_PKT_END, status };

	hub_flash_log_export_abort();
	session_running = false;
	have_pending = false;
	(void)notify_buf(pkt, sizeof(pkt));
}

static int pack_and_notify_rec(const struct hub_flash_log_rec *rec)
{
	uint8_t text_len = rec->text_len;
	uint16_t len;
	int err;

	if ((size_t)14U + text_len > sizeof(tx_buf)) {
		text_len = (uint8_t)(sizeof(tx_buf) - 14U);
	}

	tx_buf[0] = HUB_LOG_PKT_REC;
	sys_put_le16(rec->index, &tx_buf[1]);
	sys_put_le32(rec->boot_id, &tx_buf[3]);
	sys_put_le32(rec->uptime_ms, &tx_buf[7]);
	tx_buf[11] = rec->mod;
	tx_buf[12] = rec->level;
	tx_buf[13] = text_len;
	memcpy(&tx_buf[14], rec->text, text_len);
	len = (uint16_t)(14U + text_len);

	err = notify_buf(tx_buf, len);
	return err;
}

static void log_tx_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (!session_running) {
		return;
	}

	if (send_begin) {
		tx_buf[0] = HUB_LOG_PKT_BEGIN;
		sys_put_le16(session_total, &tx_buf[1]);
		sys_put_le32(0U, &tx_buf[3]);
		err = notify_buf(tx_buf, 7);
		if (err == -ENOMEM || err == -EAGAIN) {
			schedule_tx(HUB_LOG_NOTIFY_RETRY_MS);
			return;
		}
		if (err) {
			finish_session(HUB_LOG_END_ERROR);
			return;
		}
		send_begin = false;
		schedule_tx(HUB_LOG_NOTIFY_GAP_MS);
		return;
	}

	if (!have_pending) {
		err = hub_flash_log_export_next(&pending_rec);
		if (err == 1) {
			finish_session(end_status);
			return;
		}
		if (err) {
			finish_session(HUB_LOG_END_ERROR);
			return;
		}
		have_pending = true;
	}

	err = pack_and_notify_rec(&pending_rec);
	if (err == -ENOMEM || err == -EAGAIN) {
		schedule_tx(HUB_LOG_NOTIFY_RETRY_MS);
		return;
	}
	if (err) {
		finish_session(HUB_LOG_END_ERROR);
		return;
	}

	have_pending = false;
	schedule_tx(HUB_LOG_NOTIFY_GAP_MS);
}

ssize_t hub_ble_log_ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset,
			       uint8_t flags)
{
	const uint8_t *p = buf;
	struct hub_flash_log_query q;
	char keyword[HUB_FLASH_LOG_KEYWORD_MAX];
	uint8_t op;
	uint8_t kw_len;
	uint16_t total;
	int err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len < 1U || p == NULL) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	op = p[0];
	if (op == HUB_LOG_OP_STOP) {
		(void)k_work_cancel_delayable(&log_tx_work);
		if (session_running) {
			end_status = HUB_LOG_END_ABORT;
			finish_session(HUB_LOG_END_ABORT);
		}
		return len;
	}

	if (op != HUB_LOG_OP_START) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}
	/* START: 1+1+2+2+4+1+1 = 12 min */
	if (len < 12U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	if (session_running) {
		(void)k_work_cancel_delayable(&log_tx_work);
		hub_flash_log_export_abort();
		session_running = false;
	}

	memset(&q, 0, sizeof(q));
	q.take_tail = (p[1] & HUB_LOG_FLAG_TAKE_TAIL) != 0U;
	q.limit = sys_get_le16(&p[2]);
	q.from = sys_get_le16(&p[4]);
	q.mod_mask = sys_get_le32(&p[6]);
	if (p[10] == 0xFFU) {
		q.level_max = -1;
	} else {
		q.level_max = (int)p[10];
	}
	kw_len = p[11];
	if ((uint16_t)(12U + kw_len) > len) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}
	if (kw_len > 0U) {
		if (kw_len >= sizeof(keyword)) {
			kw_len = sizeof(keyword) - 1U;
		}
		memcpy(keyword, &p[12], kw_len);
		keyword[kw_len] = '\0';
		q.keyword = keyword;
	} else {
		q.keyword = NULL;
	}
	if (q.mod_mask == 0U) {
		q.mod_mask = HUB_LOG_MOD_MASK_ALL;
	}
	if (q.take_tail && q.limit == 0U) {
		q.limit = 50U;
	}

	err = hub_flash_log_export_start(&q, &total);
	if (err) {
		uint8_t pkt[2] = { HUB_LOG_PKT_END, HUB_LOG_END_ERROR };

		(void)notify_buf(pkt, sizeof(pkt));
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	session_total = total;
	send_begin = true;
	have_pending = false;
	end_status = HUB_LOG_END_OK;
	session_running = true;
	HUB_INF_M(HUB_MOD_PHONE, "BLE log export start total=%u\n", total);
	schedule_tx(0);
	return len;
}
