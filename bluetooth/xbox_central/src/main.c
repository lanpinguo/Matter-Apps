/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * BLE ground hub:
 * - BLE Central to Xbox controller
 * - BLE Peripheral to phone app
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include "hub_log.h"
#include <zephyr/sys/byteorder.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/hids.h>
#include <dk_buttons_and_leds.h>

#include "xbox_hids.h"
#include "xbox_report.h"
#include "uart_rc_link.h"
#include "bq25895.h"
#include "hub_ble_log.h"
#include "hub_esb.h"
#include "hub_flash_log.h"
#include "hub_status_led.h"

#define CONN_LED                       DK_LED1
#define REPORT_LOG_INTERVAL_MS         100
#define TELEMETRY_DEFAULT_INTERVAL_MS  50
#define TELEMETRY_MIN_INTERVAL_MS      20
#define TELEMETRY_MAX_INTERVAL_MS      500
#define UART_CTRL_HEARTBEAT_MS         100
#define POWER_HEARTBEAT_IDLE_MS        30000 /* SoC refresh when not charging */
#define POWER_HEARTBEAT_CHARGING_MS     1000 /* Live charge telemetry for phone */
#define CONFIG_PARAM_TELEMETRY_MS      1
#define SETTINGS_KEY_TELEMETRY_MS      "xbox_hub/telemetry_ms"
#define SETTINGS_KEY_XBOX_ADDR         "xbox_hub/xbox_addr"
#define SETTINGS_KEY_ESB_RADIO         "xbox_hub/esb_radio"
#define HUB_ESB_STORE_VERSION          1U

/* Fast LE interval after connect (7.5–11.25 ms). */
#define XBOX_CONN_INTERVAL_MIN           6U
#define XBOX_CONN_INTERVAL_MAX           9U
#define XBOX_CONN_LATENCY                0U
#define XBOX_CONN_TIMEOUT                400U
#define XBOX_SEC_RETRY_MAX               20U
#define XBOX_SEC_RETRY_MS                100U
#define XBOX_SCAN_RETRY_MS               2000U

#define KEY_PAIRING_ACCEPT             DK_BTN1_MSK
#define KEY_PAIRING_REJECT             DK_BTN2_MSK
#define KEY_ESB_PRX_PAIR               DK_BTN3_MSK
/* P1.02 = button0 / DK_BTN1 — same pin as KEY_PAIRING_ACCEPT (short vs long). */
#define KEY_ESB_PTX_PAIR               DK_BTN1_MSK
#define ESB_BTN_HOLD_MS                1500
#define ESB_PAIR_WATCHDOG_MS           32000
#define ESB_BOOT_RESTORE_DELAY_MS      500
#define ESB_QUERY_TIMEOUT_MS           800
#define ESB_PING_TIMEOUT_MS            300

static const char *uart_esb_cmd_name(uint8_t cmd)
{
	switch (cmd) {
	case UART_RC_ESB_CMD_GET_CONFIG:
		return "GET_CONFIG";
	case UART_RC_ESB_CMD_SET_RADIO:
		return "SET_RADIO";
	case UART_RC_ESB_CMD_SET_ADDR:
		return "SET_ADDR";
	case UART_RC_ESB_CMD_PAIR:
		return "PAIR";
	case UART_RC_ESB_CMD_APPLY:
		return "APPLY";
	case UART_RC_ESB_CMD_SAVE:
		return "SAVE";
	default:
		return "?";
	}
}

static void uart_log_addr8(const char *label, const uint8_t *p, size_t n)
{
	HUB_DBG_M(HUB_MOD_UART, "  %s:", label);
	for (size_t i = 0; i < n; i++) {
		HUB_DBG_M(HUB_MOD_UART, " %02x", p[i]);
	}
	HUB_DBG_M(HUB_MOD_UART, "\n");
}

static void uart_log_esb_cfg(const struct uart_rc_esb_config *cfg)
{
	if (cfg == NULL) {
		return;
	}

	HUB_DBG_M(HUB_MOD_UART, "  radio: pipe=%u pwr=%d delay=%u bitrate=%u\n", cfg->pipe, cfg->tx_power,
	       cfg->retransmit_delay, cfg->bitrate);
	uart_log_addr8("base0", cfg->base0, sizeof(cfg->base0));
	uart_log_addr8("base1", cfg->base1, sizeof(cfg->base1));
	uart_log_addr8("prefix", cfg->prefixes, sizeof(cfg->prefixes));
}

#define HUB_UUID_W1 0x9350
#define HUB_UUID_W2 0x11ed
#define HUB_UUID_W3 0xa1eb
#define HUB_UUID_W48 0x0242ac120002
#define HUB_SVC_UUID_VAL BT_UUID_128_ENCODE(0x57a71000, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_TELEM_UUID_VAL BT_UUID_128_ENCODE(0x57a71001, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_CFG_UUID_VAL BT_UUID_128_ENCODE(0x57a71002, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_PWR_UUID_VAL BT_UUID_128_ENCODE(0x57a71003, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_LOGCTRL_UUID_VAL BT_UUID_128_ENCODE(0x57a71004, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_LOGDATA_UUID_VAL BT_UUID_128_ENCODE(0x57a71005, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)

/* Power/charge status flags. */
#define HUB_PWR_FLAG_VALID       BIT(0)
#define HUB_PWR_FLAG_POWER_GOOD  BIT(1)
#define HUB_PWR_FLAG_VBUS        BIT(2)

struct hub_power_payload {
	uint8_t version;
	uint8_t flags;
	uint8_t charge_state;
	uint8_t vbus_state;
	uint8_t fault;
	uint8_t battery_pct;
	uint16_t batt_mv;
	uint16_t sys_mv;
	uint16_t vbus_mv;
	uint16_t charge_ma;
} __packed;

struct hub_telemetry_payload {
	uint8_t version;
	uint8_t seq;
	int16_t roll;
	int16_t pitch;
	int16_t yaw;
	uint16_t lt;
	uint16_t rt;
	uint16_t buttons;
	uint8_t dpad;
	uint8_t flags;
} __packed;

struct hub_cfg_payload {
	uint8_t version;
	uint16_t telemetry_interval_ms;
} __packed;

static struct bt_conn *default_conn;
static struct bt_conn *phone_conn;
static struct bt_conn *auth_conn;
static struct xbox_hids hids;
static struct xbox_gamepad_state latest_state;
static struct hub_telemetry_payload telemetry_data;
static struct hub_power_payload power_data;
static struct k_work_delayable power_work;
static uint16_t telemetry_interval_ms = TELEMETRY_DEFAULT_INTERVAL_MS;
static int64_t last_report_log_ms;
static bool discovery_active;
static K_MUTEX_DEFINE(data_mutex);
static struct k_work_delayable telemetry_work;
static struct k_work_delayable adv_restart_work;
static struct k_work_delayable adv_guard_work;
static struct k_work_delayable xbox_sec_work;
static struct k_work_delayable xbox_scan_retry_work;
static struct k_work xbox_connect_work;
static struct k_work xbox_post_connect_work;
static struct k_work phone_adv_resume_work;
static struct k_work xbox_sec_fail_work;
static struct bt_conn *xbox_sec_conn;
static struct bt_conn *xbox_post_connect_conn;
static struct bt_conn *xbox_sec_fail_conn;
static bt_addr_le_t xbox_connect_addr;
static struct bt_le_conn_param xbox_connect_param;
static uint8_t xbox_sec_retries;
static bool xbox_connecting;
static bool xbox_link_setup;
static uint8_t adv_restart_attempts;
static bool adv_running;
static struct uart_rc_link uart_link;
static uint8_t uart_ctrl_seq;
static uint8_t uart_esb_req_seq;
static uint8_t uart_debug_ctrl_seq;
static bool uart_debug_forward_enabled;
static struct uart_rc_esb_config uart_paired_cfg;
static bool uart_paired_cfg_valid;
/* Last successfully paired config persisted on Hub (restored after failed PAIR). */
static struct uart_rc_esb_config uart_esb_cfg_saved;
static bool uart_esb_cfg_saved_valid;

struct hub_esb_store {
	uint16_t version;
	struct uart_rc_esb_config cfg;
} __packed;

static struct uart_rc_link_status esb_last_status;
static bool esb_last_status_valid;
static int64_t esb_last_status_uptime_ms;
static K_SEM_DEFINE(esb_rsp_sem, 0, 1);
static struct uart_rc_esb_rsp esb_last_rsp;
static bool esb_last_rsp_valid;
static uint8_t esb_wait_rsp_cmd; /* 0 = not waiting */
static int64_t esb_last_ptx_rsp_uptime_ms;
static bool esb_last_ptx_rsp_seen;

static bool esb_ptx_hold_armed;
static bool esb_ptx_hold_fired;
static bool esb_debug_hold_armed;
static bool esb_debug_hold_fired;
static bool esb_pair_session_active;
static struct k_work_delayable esb_ptx_hold_work;
static struct k_work_delayable esb_debug_hold_work;
static struct k_work_delayable esb_pair_watchdog_work;
static struct k_work_delayable esb_boot_restore_work;
static struct k_work_delayable uart_ctrl_heartbeat_work;
static bt_addr_le_t bonded_xbox_addr;
static bool bonded_xbox_valid;
static bool xbox_input_logged;

static ssize_t telemetry_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 void *buf, uint16_t len, uint16_t offset);
static ssize_t cfg_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset);
static ssize_t cfg_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset, uint8_t flags);
static ssize_t power_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset);

BT_GATT_SERVICE_DEFINE(hub_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(HUB_SVC_UUID_VAL)),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(HUB_TELEM_UUID_VAL),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       telemetry_read_cb, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(HUB_CFG_UUID_VAL),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
			       cfg_read_cb, cfg_write_cb, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(HUB_PWR_UUID_VAL),
			       BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ,
			       power_read_cb, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(HUB_LOGCTRL_UUID_VAL),
			       BT_GATT_CHRC_WRITE,
			       BT_GATT_PERM_WRITE,
			       NULL, hub_ble_log_ctrl_write, NULL),
	BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(HUB_LOGDATA_UUID_VAL),
			       BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE,
			       NULL, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE)
);

/* Characteristic value attribute indices within hub_svc. */
#define HUB_PWR_VALUE_ATTR_IDX 7
#define HUB_LOGDATA_VALUE_ATTR_IDX 12

static const uint8_t adv_flags[] = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };
static const uint8_t adv_uuid[] = {
	BT_UUID_128_ENCODE(0x57a71000, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
};
static const struct bt_data adv_data[] = {
	BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static const struct bt_data scan_rsp[] = {
	BT_DATA(BT_DATA_UUID128_ALL, adv_uuid, sizeof(adv_uuid)),
};

static void restart_scan(void);
static int adv_start(void);
static void gatt_discover(struct bt_conn *conn);
static void clear_xbox_bond(const struct bt_conn *conn);
static void clear_bonded_xbox(void);
static void phone_adv_pause(void);
static void phone_adv_resume(void);
static void schedule_phone_adv_resume(void);
static void schedule_xbox_sec_fail(struct bt_conn *conn);
static bool hub_may_phone_adv(void);
static void schedule_xbox_scan_retry(uint32_t delay_ms);
static void xbox_sec_work_cancel(void);
static void xbox_request_security(struct bt_conn *conn);

struct xbox_adv_name_ctx {
	char name[32];
	bool found;
};

#define XBOX_MS_COMPANY_ID             0x045EU
#define XBOX_APPEARANCE_GAMEPAD        0x03C4U

struct xbox_adv_parse_ctx {
	struct xbox_adv_name_ctx name;
	bool ms_mfg;
	bool gamepad;
	uint16_t appearance;
};

static bool xbox_adv_parse_cb(struct bt_data *data, void *user_data)
{
	struct xbox_adv_parse_ctx *ctx = user_data;

	switch (data->type) {
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED:
		if (data->data_len < sizeof(ctx->name.name)) {
			memcpy(ctx->name.name, data->data, data->data_len);
			ctx->name.name[data->data_len] = '\0';
			ctx->name.found = true;
		}
		break;
	case BT_DATA_MANUFACTURER_DATA:
		if (data->data_len >= 2U &&
		    sys_get_le16(data->data) == XBOX_MS_COMPANY_ID) {
			ctx->ms_mfg = true;
		}
		break;
	case BT_DATA_GAP_APPEARANCE:
		if (data->data_len >= 2U) {
			ctx->appearance = sys_get_le16(data->data);
			if (ctx->appearance == XBOX_APPEARANCE_GAMEPAD) {
				ctx->gamepad = true;
			}
		}
		break;
	default:
		break;
	}

	return true;
}

static void xbox_log_adv_match(const char *addr, struct net_buf_simple *adv_data)
{
	struct xbox_adv_parse_ctx ctx = { 0 };

	if (adv_data != NULL) {
		bt_data_parse(adv_data, xbox_adv_parse_cb, &ctx);
	}

	if (ctx.name.found && strstr(ctx.name.name, "Xbox") != NULL) {
		HUB_INF_M(HUB_MOD_XBOX, "Xbox controller found: %s ('%s')\n", addr, ctx.name.name);
	} else if (ctx.ms_mfg) {
		HUB_INF_M(HUB_MOD_HID, "Microsoft HID device found: %s\n", addr);
	} else if (ctx.gamepad) {
		HUB_INF_M(HUB_MOD_HID, "HID gamepad found: %s (appearance 0x%04x)\n", addr, ctx.appearance);
	} else if (ctx.name.found) {
		HUB_INF_M(HUB_MOD_HID, "HID UUID match: %s (name '%s')\n", addr, ctx.name.name);
	} else {
		HUB_INF_M(HUB_MOD_HID, "HID UUID match: %s (name likely in scan response)\n", addr);
	}
}

static void xbox_sec_work_cancel(void)
{
	k_work_cancel_delayable(&xbox_sec_work);

	if (xbox_sec_conn != NULL) {
		bt_conn_unref(xbox_sec_conn);
		xbox_sec_conn = NULL;
	}

	xbox_sec_retries = 0U;
}

static void xbox_sec_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (xbox_sec_conn == NULL) {
		return;
	}

	if (bt_conn_get_security(xbox_sec_conn) >= BT_SECURITY_L2) {
		xbox_sec_work_cancel();
		return;
	}

	err = bt_conn_set_security(xbox_sec_conn, BT_SECURITY_L2);
	if (err == 0) {
		return;
	}

	HUB_WRN_M(HUB_MOD_XBOX, "Xbox security request err %d (retry %u)\n", err, xbox_sec_retries);

	if ((err == -EBUSY || err == -ENOMEM || err == -EAGAIN) &&
	    ++xbox_sec_retries < XBOX_SEC_RETRY_MAX) {
		k_work_schedule(&xbox_sec_work, K_MSEC(XBOX_SEC_RETRY_MS));
		return;
	}

	clear_xbox_bond(xbox_sec_conn);
	(void)bt_conn_disconnect(xbox_sec_conn, BT_HCI_ERR_AUTH_FAIL);
	xbox_sec_work_cancel();
}

static void xbox_request_security(struct bt_conn *conn)
{
	xbox_sec_work_cancel();
	xbox_sec_conn = bt_conn_ref(conn);

	if (bt_conn_get_security(conn) >= BT_SECURITY_L2) {
		gatt_discover(conn);
		return;
	}

	k_work_schedule(&xbox_sec_work, K_NO_WAIT);
}

static void clear_xbox_bond(const struct bt_conn *conn)
{
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	bt_addr_le_to_str(dst, addr, sizeof(addr));
	err = bt_unpair(BT_ID_DEFAULT, dst);
	if (err == 0) {
		HUB_WRN_M(HUB_MOD_XBOX, "Cleared bond for %s\n", addr);
	} else if (err != -ENOENT) {
		HUB_ERR_M(HUB_MOD_XBOX, "Bond clear failed for %s: %d\n", addr, err);
	}

	if (bonded_xbox_valid && bt_addr_le_cmp(dst, &bonded_xbox_addr) == 0) {
		bonded_xbox_valid = false;
		(void)settings_delete(SETTINGS_KEY_XBOX_ADDR);
	}
}

static void clear_bonded_xbox(void)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!bonded_xbox_valid) {
		HUB_INF_M(HUB_MOD_XBOX, "No bonded Xbox controller\n");
		return;
	}

	bt_addr_le_to_str(&bonded_xbox_addr, addr, sizeof(addr));
	(void)bt_unpair(BT_ID_DEFAULT, &bonded_xbox_addr);
	bonded_xbox_valid = false;
	(void)settings_delete(SETTINGS_KEY_XBOX_ADDR);
	HUB_WRN_M(HUB_MOD_XBOX, "Cleared bonded Xbox %s — hold Sync to pair again\n", addr);
}

static void phone_adv_pause(void)
{
	if (!adv_running || phone_conn != NULL) {
		return;
	}

	k_work_cancel_delayable(&adv_restart_work);

	if (bt_le_adv_stop() == 0) {
		adv_running = false;
		HUB_INF_M(HUB_MOD_PHONE, "Phone adv paused for Xbox link\n");
	}
}

static bool hub_may_phone_adv(void)
{
	return phone_conn == NULL && !xbox_connecting && !xbox_link_setup;
}

static void phone_adv_resume(void)
{
	if (!hub_may_phone_adv() || adv_running) {
		return;
	}

	if (adv_start() == 0) {
		HUB_INF_M(HUB_MOD_PHONE, "Phone adv resumed\n");
	}
}

static void phone_adv_resume_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	phone_adv_resume();
}

static void schedule_phone_adv_resume(void)
{
	(void)k_work_submit(&phone_adv_resume_work);
}

static void schedule_xbox_scan_retry(uint32_t delay_ms)
{
	k_work_cancel_delayable(&xbox_scan_retry_work);
	k_work_schedule(&xbox_scan_retry_work, K_MSEC(delay_ms));
}

static void xbox_scan_retry_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	/* Always resume phone adv off BT RX to avoid HCI sync deadlock. */
	phone_adv_resume();

	if (default_conn != NULL || xbox_connecting) {
		return;
	}

	HUB_WRN_M(HUB_MOD_XBOX, "Retrying Xbox scan\n");
	restart_scan();
}

static void xbox_link_failed_retry(uint8_t hci_err)
{
	xbox_connecting = false;
	xbox_link_setup = false;
	(void)k_work_cancel(&xbox_connect_work);

	if (hci_err == BT_HCI_ERR_CONN_FAIL_TO_ESTAB) {
		HUB_WRN_M(HUB_MOD_XBOX, "Xbox 0x3e — retry scan in %u ms\n", XBOX_SCAN_RETRY_MS);
		schedule_xbox_scan_retry(XBOX_SCAN_RETRY_MS);
		return;
	}

	/* Defer adv/scan HCI — callers often run on BT RX WQ. */
	schedule_xbox_scan_retry(0);
}

/*
 * Run LE create / adv-stop / scan-stop on the system workqueue.
 * Calling these sync HCI APIs from BT RX WQ can deadlock when
 * CONFIG_BT_BUF_CMD_TX_COUNT is low (cmd complete cannot be processed).
 */
static void xbox_connect_work_handler(struct k_work *work)
{
	struct bt_conn *conn = NULL;
	char addr[BT_ADDR_LE_STR_LEN];
	int err;

	ARG_UNUSED(work);

	if (default_conn != NULL) {
		xbox_connecting = false;
		xbox_link_setup = false;
		return;
	}

	bt_addr_le_to_str(&xbox_connect_addr, addr, sizeof(addr));
	phone_adv_pause();
	(void)bt_scan_stop();

	err = bt_conn_le_create(&xbox_connect_addr, BT_CONN_LE_CREATE_CONN,
				&xbox_connect_param, &conn);
	if (err != 0) {
		xbox_connecting = false;
		xbox_link_setup = false;
		HUB_ERR_M(HUB_MOD_XBOX, "Xbox connect failed: %d\n", err);
		schedule_xbox_scan_retry(0);
		return;
	}

	default_conn = bt_conn_ref(conn);
	bt_conn_unref(conn);
	HUB_INF_M(HUB_MOD_XBOX, "Connecting to Xbox %s\n", addr);
}

static void schedule_xbox_connect(const bt_addr_le_t *addr,
				  const struct bt_le_conn_param *param)
{
	if (addr == NULL || param == NULL) {
		return;
	}

	xbox_connect_addr = *addr;
	xbox_connect_param = *param;
	xbox_connecting = true;
	xbox_link_setup = true;
	(void)k_work_submit(&xbox_connect_work);
}

static void xbox_post_connect_work_handler(struct k_work *work)
{
	struct bt_conn *conn;

	ARG_UNUSED(work);

	conn = xbox_post_connect_conn;
	xbox_post_connect_conn = NULL;
	if (conn == NULL) {
		return;
	}

	(void)bt_scan_stop();
	phone_adv_pause();
	xbox_request_security(conn);
	bt_conn_unref(conn);
}

static void xbox_sec_fail_work_handler(struct k_work *work)
{
	struct bt_conn *conn;

	ARG_UNUSED(work);

	conn = xbox_sec_fail_conn;
	xbox_sec_fail_conn = NULL;
	if (conn == NULL) {
		return;
	}

	clear_xbox_bond(conn);
	(void)bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
	bt_conn_unref(conn);
}

static void schedule_xbox_sec_fail(struct bt_conn *conn)
{
	if (conn == NULL) {
		return;
	}

	if (xbox_sec_fail_conn != NULL) {
		bt_conn_unref(xbox_sec_fail_conn);
	}
	xbox_sec_fail_conn = bt_conn_ref(conn);
	(void)k_work_submit(&xbox_sec_fail_work);
}

static void bonded_xbox_store(const bt_addr_le_t *addr)
{
	if (addr == NULL) {
		return;
	}

	bonded_xbox_addr = *addr;
	bonded_xbox_valid = true;
	(void)settings_save_one(SETTINGS_KEY_XBOX_ADDR, addr, sizeof(*addr));
}

static void log_bonded_xbox_boot(void)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!bonded_xbox_valid) {
		HUB_INF_M(HUB_MOD_HID, "No bonded Xbox — first HID gamepad in range will be paired\n");
		return;
	}

	bt_addr_le_to_str(&bonded_xbox_addr, addr, sizeof(addr));
	HUB_INF_M(HUB_MOD_XBOX, "Bonded Xbox: %s (Hub ignores other controllers)\n", addr);
	HUB_INF_M(HUB_MOD_SYS, "Press Button 2 to forget and pair a different controller\n");
}

static void set_conn_led(bool on)
{
	(void)dk_set_led(CONN_LED, on ? 1 : 0);
	hub_status_led_set_xbox_connected(on);
}

static uint16_t axis_to_rc(int16_t axis)
{
	/* Map int16 stick [-32768, 32767] → RC channel [0, 1000]. */
	int32_t v = ((int32_t)axis + 32768) * 1000 / 65535;

	if (v < 0) {
		return 0U;
	}
	if (v > 1000) {
		return 1000U;
	}
	return (uint16_t)v;
}

/*
 * Single-channel drive for RC cars (forward/reverse on one PWM):
 *   center 500 = idle
 *   RT 0..1023 → 500..1000 (forward / upper half)
 *   LT 0..1023 → 500..0   (reverse / lower half)
 * Both pressed: net = 500 + fwd − rev (clamped).
 */
static uint16_t lt_rt_to_drive_rc(uint16_t lt, uint16_t rt)
{
	uint32_t fwd;
	uint32_t rev;
	int32_t v;

	if (lt > 1023U) {
		lt = 1023U;
	}
	if (rt > 1023U) {
		rt = 1023U;
	}

	fwd = ((uint32_t)rt * 500U) / 1023U;
	rev = ((uint32_t)lt * 500U) / 1023U;
	v = 500 + (int32_t)fwd - (int32_t)rev;
	if (v < 0) {
		return 0U;
	}
	if (v > 1000) {
		return 1000U;
	}
	return (uint16_t)v;
}

static void on_uart_status(const struct uart_rc_link_status *status, void *user_data)
{
	ARG_UNUSED(user_data);

	k_mutex_lock(&data_mutex, K_FOREVER);
	telemetry_data.roll = status->roll;
	telemetry_data.pitch = status->pitch;
	telemetry_data.yaw = status->yaw;
	telemetry_data.flags |= BIT(1);
	esb_last_status = *status;
	esb_last_status_valid = true;
	esb_last_status_uptime_ms = k_uptime_get();
	k_mutex_unlock(&data_mutex);

	if (esb_pair_session_active) {
		HUB_DBG_M(HUB_MOD_UART, "[UART<-PTX] STATUS seq=%u flags=0x%02x batt=%u R/P/Y=%d/%d/%d\n",
		       status->seq, status->flags, status->battery_mv, status->roll,
		       status->pitch, status->yaw);
	}
}

static int hub_esb_cfg_persist(const struct uart_rc_esb_config *cfg)
{
	struct hub_esb_store store = {
		.version = HUB_ESB_STORE_VERSION,
	};
	int err;

	if (cfg == NULL) {
		return -EINVAL;
	}

	store.cfg = *cfg;
	err = settings_save_one(SETTINGS_KEY_ESB_RADIO, &store, sizeof(store));
	if (err != 0) {
		return err;
	}

	uart_esb_cfg_saved = *cfg;
	uart_esb_cfg_saved_valid = true;
	uart_paired_cfg = *cfg;
	uart_paired_cfg_valid = true;
	return 0;
}

static void esb_pair_session_end(bool success)
{
	esb_pair_session_active = false;
	(void)k_work_cancel_delayable(&esb_pair_watchdog_work);
	hub_status_led_set_pairing(false);

	if (success) {
		if (uart_paired_cfg_valid) {
			int err = hub_esb_cfg_persist(&uart_paired_cfg);

			if (err == 0) {
				HUB_INF_M(HUB_MOD_ESB, "[PAIR] saved ESB config on Hub flash\n");
			} else {
				HUB_ERR_M(HUB_MOD_ESB, "[PAIR] Hub save failed: %d\n", err);
			}
		}
		return;
	}

	/* Failed attempt — keep previous Hub flash config in RAM if any. */
	if (uart_esb_cfg_saved_valid) {
		uart_paired_cfg = uart_esb_cfg_saved;
		uart_paired_cfg_valid = true;
	} else {
		uart_paired_cfg_valid = false;
	}
}

static void esb_pair_watchdog_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!esb_pair_session_active) {
		return;
	}

	esb_pair_session_end(false);
	HUB_ERR_M(HUB_MOD_ESB, "[PAIR] watchdog: no PTX pair-done within %d ms\n", ESB_PAIR_WATCHDOG_MS);
	HUB_DBG_M(HUB_MOD_ESB, "[PAIR] check: PRX in pair mode? PTX UART logs above? ESB RF link?\n");
}

static void on_uart_esb_rsp(const struct uart_rc_esb_rsp *rsp, void *user_data)
{
	struct uart_rc_esb_config cfg;

	ARG_UNUSED(user_data);

	HUB_DBG_M(HUB_MOD_UART, "[UART<-PTX] ESB_RSP seq=%u cmd=%s(0x%02x) status=%d data_len=%u\n",
	       rsp->seq, uart_esb_cmd_name(rsp->cmd), rsp->cmd, rsp->status, rsp->data_len);

	esb_last_rsp = *rsp;
	esb_last_rsp_valid = true;
	esb_last_ptx_rsp_seen = true;
	esb_last_ptx_rsp_uptime_ms = k_uptime_get();
	if (esb_wait_rsp_cmd != 0U && rsp->cmd == esb_wait_rsp_cmd) {
		esb_wait_rsp_cmd = 0U;
		k_sem_give(&esb_rsp_sem);
	}

	if (rsp->status != 0) {
		HUB_ERR_M(HUB_MOD_UART, "[UART<-PTX] ESB_RSP FAILED\n");
		if (rsp->cmd == UART_RC_ESB_CMD_PAIR) {
			esb_pair_session_end(false);
		}
		return;
	}

	switch (rsp->cmd) {
	case UART_RC_ESB_CMD_GET_CONFIG:
	case UART_RC_ESB_CMD_PAIR:
		if (rsp->data_len >= sizeof(cfg) &&
		    uart_rc_link_decode_esb_config(rsp->data, rsp->data_len, &cfg) == 0) {
			uart_paired_cfg = cfg;
			uart_paired_cfg_valid = true;
			uart_log_esb_cfg(&cfg);
			if (rsp->cmd == UART_RC_ESB_CMD_PAIR) {
				HUB_DBG_M(HUB_MOD_ESB, "[PAIR] PTX accepted PAIR — waiting OTA PRX ACK (max 30s)\n");
				HUB_DBG_M(HUB_MOD_ESB, "[PAIR] Ensure esb_prx is in pair mode "
				       "(no saved cfg, or hold PRX Btn1/P1.02 5s)\n");
			}
		} else {
			HUB_ERR_M(HUB_MOD_UART, "[UART<-PTX] ESB_RSP cfg decode failed (len=%u need=%u)\n",
			       rsp->data_len, (unsigned int)sizeof(cfg));
			if (rsp->cmd == UART_RC_ESB_CMD_PAIR) {
				esb_pair_session_end(false);
			}
		}
		break;
	case UART_RC_ESB_CMD_SET_ADDR:
		HUB_DBG_M(HUB_MOD_UART, "[UART<-PTX] addresses staged\n");
		break;
	case UART_RC_ESB_CMD_SET_RADIO:
		HUB_DBG_M(HUB_MOD_UART, "[UART<-PTX] radio params staged\n");
		break;
	case UART_RC_ESB_CMD_APPLY:
		HUB_DBG_M(HUB_MOD_UART, "[UART<-PTX] radio applied\n");
		break;
	case UART_RC_ESB_CMD_SAVE:
		HUB_DBG_M(HUB_MOD_UART, "[UART<-PTX] SAVE (PTX no-op; Hub owns flash)\n");
		break;
	default:
		break;
	}
}

static void on_uart_debug_log(const struct uart_rc_debug_log *log, void *user_data)
{
	char line[UART_RC_LINK_DEBUG_MAX_TEXT + 1];

	ARG_UNUSED(user_data);

	HUB_DBG_M(HUB_MOD_UART, "[PTX-LOG:%u] %.*s", log->level, log->text_len, log->text);
	if ((log->flags & UART_RC_DEBUG_FLAG_MORE) == 0U) {
		HUB_DBG_M(HUB_MOD_SYS, "\n");
	}

	/* PTX reports pair result via forwarded log lines. */
	if (esb_pair_session_active && log->text_len > 0U) {
		size_t n = (size_t)log->text_len;

		if (n >= sizeof(line)) {
			n = sizeof(line) - 1U;
		}

		memcpy(line, log->text, n);
		line[n] = '\0';

		if (strstr(line, "PRX ACK on PAIR") != NULL ||
		    strstr(line, "PAIR broadcast ended") != NULL) {
			esb_pair_session_end(true);
			HUB_DBG_M(HUB_MOD_ESB, "[PAIR] session complete (from PTX log)\n");
		} else if (strstr(line, "PAIR broadcast timed out") != NULL) {
			esb_pair_session_end(false);
			HUB_ERR_M(HUB_MOD_ESB, "[PAIR] session failed: PTX timed out waiting for PRX ACK\n");
		}
	}
}

static int uart_hub_send_esb_req(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
	struct uart_rc_esb_req req = {
		.seq = uart_esb_req_seq++,
		.cmd = cmd,
		.data_len = data_len,
	};
	int err;

	if (data_len > 0U && data != NULL) {
		memcpy(req.data, data, data_len);
	}

	HUB_DBG_M(HUB_MOD_UART, "[UART->PTX] ESB_REQ seq=%u cmd=%s(0x%02x) data_len=%u\n",
	       req.seq, uart_esb_cmd_name(cmd), cmd, data_len);

	err = uart_rc_link_send_esb_req(&uart_link, &req);
	if (err != 0) {
		HUB_ERR_M(HUB_MOD_UART, "[UART->PTX] ESB_REQ send failed: %d\n", err);
	}
	return err;
}

static int uart_hub_send_debug_ctrl(uint8_t flags, uint8_t level)
{
	struct uart_rc_debug_ctrl ctrl = {
		.seq = uart_debug_ctrl_seq++,
		.flags = flags,
		.level = level,
		.reserved = 0U,
	};

	return uart_rc_link_send_debug_ctrl(&uart_link, &ctrl);
}

static int uart_hub_apply_esb_config(const struct uart_rc_esb_config *cfg)
{
	uint8_t radio_payload[5];
	uint8_t addr_payload[16];
	int err;

	if (cfg == NULL) {
		return -EINVAL;
	}

	radio_payload[0] = cfg->bitrate;
	radio_payload[1] = (uint8_t)cfg->tx_power;
	sys_put_le16(cfg->retransmit_delay, &radio_payload[2]);
	radio_payload[4] = cfg->pipe;

	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_SET_RADIO, radio_payload,
				    sizeof(radio_payload));
	if (err != 0) {
		return err;
	}

	memcpy(&addr_payload[0], cfg->base0, 4U);
	memcpy(&addr_payload[4], cfg->base1, 4U);
	memcpy(&addr_payload[8], cfg->prefixes, 8U);

	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_SET_ADDR, addr_payload,
				    sizeof(addr_payload));
	if (err != 0) {
		return err;
	}

	/* PTX has no local pair store — APPLY only (no SAVE). */
	return uart_hub_send_esb_req(UART_RC_ESB_CMD_APPLY, NULL, 0U);
}

static void uart_hub_restore_esb_config_to_ptx(void)
{
	int err;

	if (!uart_paired_cfg_valid) {
		HUB_INF_M(HUB_MOD_ESB, "No Hub-saved ESB config — hold Btn1/P1.02 to OTA pair\n");
		return;
	}

	err = uart_hub_apply_esb_config(&uart_paired_cfg);
	if (err != 0) {
		HUB_ERR_M(HUB_MOD_ESB, "Boot push ESB config to PTX failed: %d\n", err);
		return;
	}

	HUB_INF_M(HUB_MOD_ESB, "Pushed Hub-saved ESB config to PTX (SET_RADIO/SET_ADDR/APPLY)\n");
	uart_log_esb_cfg(&uart_paired_cfg);
}

static void esb_boot_restore_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	uart_hub_restore_esb_config_to_ptx();
}

static void uart_hub_trigger_esb_ptx_pair(void)
{
	int err;

	uart_paired_cfg_valid = false;
	esb_pair_session_active = true;
	hub_status_led_set_pairing(true);

	/* Stream PTX pair logs to Hub console during the session. */
	uart_debug_forward_enabled = true;
	err = uart_hub_send_debug_ctrl(UART_RC_DEBUG_FLAG_FORWARD, LOG_LEVEL_WRN);
	HUB_DBG_M(HUB_MOD_ESB, "[PAIR] enable PTX log forward (err=%d)\n", err);

	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_PAIR, NULL, 0U);
	if (err != 0) {
		esb_pair_session_end(false);
		HUB_ERR_M(HUB_MOD_ESB, "[PAIR] ESB_REQ PAIR send failed (err %d)\n", err);
		return;
	}

	(void)k_work_cancel_delayable(&esb_pair_watchdog_work);
	k_work_schedule(&esb_pair_watchdog_work, K_MSEC(ESB_PAIR_WATCHDOG_MS));
	HUB_DBG_M(HUB_MOD_ESB, "[PAIR] waiting for PTX ESB_RSP + OTA PRX ACK...\n");
}

static void uart_hub_trigger_esb_prx_pair(void)
{
	int err;

	if (!uart_paired_cfg_valid) {
		HUB_ERR_M(HUB_MOD_ESB, "No Hub ESB config — hold Btn1/P1.02 (pair) first\n");
		return;
	}

	err = uart_hub_apply_esb_config(&uart_paired_cfg);
	if (err != 0) {
		HUB_ERR_M(HUB_MOD_ESB, "ESB UART apply failed (err %d)\n", err);
		return;
	}

	HUB_INF_M(HUB_MOD_ESB, "ESB config applied on UART device (SET_RADIO/SET_ADDR/APPLY)\n");
}

void hub_esb_snapshot(struct hub_esb_snapshot *out)
{
	int64_t now;

	if (out == NULL) {
		return;
	}

	memset(out, 0, sizeof(*out));
	out->hub_cfg_ram_valid = uart_paired_cfg_valid;
	out->hub_cfg_flash_valid = uart_esb_cfg_saved_valid;
	out->pair_session_active = esb_pair_session_active;
	out->log_forward = uart_debug_forward_enabled;
	out->xbox_ctrl_active = (hids.conn != NULL);
	if (uart_paired_cfg_valid) {
		out->hub_cfg = uart_paired_cfg;
	} else if (uart_esb_cfg_saved_valid) {
		out->hub_cfg = uart_esb_cfg_saved;
	}

	k_mutex_lock(&data_mutex, K_FOREVER);
	out->last_status_valid = esb_last_status_valid;
	if (esb_last_status_valid) {
		out->last_status = esb_last_status;
		now = k_uptime_get();
		out->last_status_age_ms = now - esb_last_status_uptime_ms;
	} else {
		out->last_status_age_ms = -1;
	}
	k_mutex_unlock(&data_mutex);

	if (esb_last_ptx_rsp_seen) {
		out->last_ptx_rsp_age_ms = k_uptime_get() - esb_last_ptx_rsp_uptime_ms;
	} else {
		out->last_ptx_rsp_age_ms = -1;
	}
}

int hub_esb_get_cfg(struct uart_rc_esb_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}
	if (!uart_paired_cfg_valid && !uart_esb_cfg_saved_valid) {
		return -ENOENT;
	}
	*cfg = uart_paired_cfg_valid ? uart_paired_cfg : uart_esb_cfg_saved;
	return 0;
}

int hub_esb_force_pair(void)
{
	if (esb_pair_session_active) {
		return -EBUSY;
	}
	uart_hub_trigger_esb_ptx_pair();
	return esb_pair_session_active ? 0 : -EIO;
}

int hub_esb_push_to_ptx(void)
{
	if (!uart_paired_cfg_valid && !uart_esb_cfg_saved_valid) {
		return -ENOENT;
	}
	if (!uart_paired_cfg_valid) {
		uart_paired_cfg = uart_esb_cfg_saved;
		uart_paired_cfg_valid = true;
	}
	return uart_hub_apply_esb_config(&uart_paired_cfg);
}

int hub_esb_ping_ptx(struct uart_rc_esb_config *cfg)
{
	int err;

	(void)k_sem_take(&esb_rsp_sem, K_NO_WAIT);
	esb_wait_rsp_cmd = UART_RC_ESB_CMD_GET_CONFIG;
	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_GET_CONFIG, NULL, 0U);
	if (err != 0) {
		esb_wait_rsp_cmd = 0U;
		return err;
	}

	err = k_sem_take(&esb_rsp_sem, K_MSEC(ESB_PING_TIMEOUT_MS));
	esb_wait_rsp_cmd = 0U;
	if (err != 0) {
		return -ETIMEDOUT;
	}
	if (!esb_last_rsp_valid || esb_last_rsp.cmd != UART_RC_ESB_CMD_GET_CONFIG) {
		return -EIO;
	}

	/* Any GET_CONFIG RSP means PTX is on the wire. */
	if (esb_last_rsp.status != 0) {
		return (cfg != NULL) ? -ENODATA : 0;
	}
	if (cfg == NULL) {
		return 0;
	}
	if (esb_last_rsp.data_len < sizeof(*cfg) ||
	    uart_rc_link_decode_esb_config(esb_last_rsp.data, esb_last_rsp.data_len, cfg) != 0) {
		return -ENODATA;
	}

	uart_paired_cfg = *cfg;
	uart_paired_cfg_valid = true;
	return 0;
}

int hub_esb_query_ptx(struct uart_rc_esb_config *cfg)
{
	int err;

	(void)k_sem_take(&esb_rsp_sem, K_NO_WAIT);
	esb_wait_rsp_cmd = UART_RC_ESB_CMD_GET_CONFIG;
	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_GET_CONFIG, NULL, 0U);
	if (err != 0) {
		esb_wait_rsp_cmd = 0U;
		return err;
	}

	err = k_sem_take(&esb_rsp_sem, K_MSEC(ESB_QUERY_TIMEOUT_MS));
	esb_wait_rsp_cmd = 0U;
	if (err != 0) {
		return -ETIMEDOUT;
	}
	if (!esb_last_rsp_valid || esb_last_rsp.cmd != UART_RC_ESB_CMD_GET_CONFIG) {
		return -EIO;
	}
	if (esb_last_rsp.status != 0) {
		return esb_last_rsp.status;
	}
	if (cfg == NULL) {
		return 0;
	}
	if (esb_last_rsp.data_len < sizeof(*cfg) ||
	    uart_rc_link_decode_esb_config(esb_last_rsp.data, esb_last_rsp.data_len, cfg) != 0) {
		return -EBADMSG;
	}

	uart_paired_cfg = *cfg;
	uart_paired_cfg_valid = true;
	return 0;
}

int hub_esb_set_log_forward(bool enable)
{
	uint8_t flags = enable ? UART_RC_DEBUG_FLAG_FORWARD : 0U;
	int err;

	err = uart_hub_send_debug_ctrl(flags, LOG_LEVEL_INF);
	if (err == 0) {
		uart_debug_forward_enabled = enable;
	}
	return err;
}

int hub_esb_clear_saved(void)
{
	int err;

	err = settings_delete(SETTINGS_KEY_ESB_RADIO);
	if (err != 0 && err != -ENOENT) {
		return err;
	}

	uart_paired_cfg_valid = false;
	uart_esb_cfg_saved_valid = false;
	memset(&uart_paired_cfg, 0, sizeof(uart_paired_cfg));
	memset(&uart_esb_cfg_saved, 0, sizeof(uart_esb_cfg_saved));
	return 0;
}

static void esb_ptx_hold_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	esb_ptx_hold_fired = true;
	uart_hub_trigger_esb_ptx_pair();
}

static void esb_debug_hold_handler(struct k_work *work)
{
	uint8_t flags;

	ARG_UNUSED(work);

	esb_debug_hold_fired = true;
	uart_debug_forward_enabled = !uart_debug_forward_enabled;
	flags = uart_debug_forward_enabled ? UART_RC_DEBUG_FLAG_FORWARD : 0U;
	(void)uart_hub_send_debug_ctrl(flags, LOG_LEVEL_INF);
	HUB_INF_M(HUB_MOD_ESB, "ESB debug forward %s (Btn3 long press)\n",
	       uart_debug_forward_enabled ? "on" : "off");
}

static void uart_send_ctrl_from_state(const struct xbox_gamepad_state *s)
{
	struct uart_rc_link_ctrl ctrl = {
		.seq = uart_ctrl_seq++,
		.channel_count = UART_RC_CH_COUNT_DEFAULT,
	};

	ctrl.channels[UART_RC_CH_LX] = axis_to_rc(s->lx);
	ctrl.channels[UART_RC_CH_LY] = axis_to_rc(s->ly);
	ctrl.channels[UART_RC_CH_RX] = axis_to_rc(s->rx);
	ctrl.channels[UART_RC_CH_RY] = axis_to_rc(s->ry);
	ctrl.channels[UART_RC_CH_LT] = s->lt;
	ctrl.channels[UART_RC_CH_RT] = s->rt;
	/* Aux switches: released=0, pressed=1000 (RC 3-pos mid unused). */
	ctrl.channels[UART_RC_CH_AUX0] = s->btn_a ? 1000U : 0U;
	ctrl.channels[UART_RC_CH_AUX1] = s->btn_b ? 1000U : 0U;
	/* CH8 drive: RT upper half / LT lower half (car forward+reverse). */
	ctrl.channels[UART_RC_CH_AUX2] = lt_rt_to_drive_rc(s->lt, s->rt);

	(void)uart_rc_link_send_ctrl(&uart_link, &ctrl);
}

static void uart_ctrl_heartbeat_handler(struct k_work *work)
{
	struct xbox_gamepad_state state;

	ARG_UNUSED(work);

	/*
	 * Xbox may stop HID reports while sticks are idle. Keep UART CTRL
	 * flowing so PTX/PRX do not hit link / PWM failsafe timeouts.
	 */
	if (hids.conn != NULL) {
		k_mutex_lock(&data_mutex, K_FOREVER);
		state = latest_state;
		k_mutex_unlock(&data_mutex);
		uart_send_ctrl_from_state(&state);
	}

	k_work_schedule(&uart_ctrl_heartbeat_work, K_MSEC(UART_CTRL_HEARTBEAT_MS));
}

static int uart_link_init(void)
{
	const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(uart30));
	struct uart_rc_link_handlers handlers = {
		.on_ctrl = NULL,
		.on_status = on_uart_status,
		.on_esb_req = NULL,
		.on_esb_rsp = on_uart_esb_rsp,
		.on_debug_ctrl = NULL,
		.on_debug_log = on_uart_debug_log,
		.user_data = NULL,
	};
	int err;

	err = uart_rc_link_init(&uart_link, uart, &handlers);
	if (err != 0) {
		return err;
	}

	err = uart_rc_link_start_rx(&uart_link);
	if (err != 0) {
		return err;
	}

	HUB_INF_M(HUB_MOD_UART, "UART RC link on uart30 (HDLC 0x%02x)\n", UART_RC_LINK_HDLC_FLAG);
	return 0;
}

static uint16_t state_to_buttons(const struct xbox_gamepad_state *s)
{
	uint16_t b = 0U;

	if (s->btn_a) {
		b |= BIT(0);
	}
	if (s->btn_b) {
		b |= BIT(1);
	}
	if (s->btn_x) {
		b |= BIT(2);
	}
	if (s->btn_y) {
		b |= BIT(3);
	}
	if (s->btn_lb) {
		b |= BIT(4);
	}
	if (s->btn_rb) {
		b |= BIT(5);
	}
	if (s->btn_view) {
		b |= BIT(6);
	}
	if (s->btn_menu) {
		b |= BIT(7);
	}
	if (s->btn_guide) {
		b |= BIT(8);
	}
	if (s->btn_ls) {
		b |= BIT(9);
	}
	if (s->btn_rs) {
		b |= BIT(10);
	}
	if (s->btn_share) {
		b |= BIT(11);
	}

	return b;
}

static void telemetry_update_from_state(const struct xbox_gamepad_state *s)
{
	telemetry_data.version = 1U;
	telemetry_data.seq++;
	telemetry_data.roll = s->rx / 64;
	telemetry_data.pitch = s->ry / 64;
	telemetry_data.yaw = s->lx / 64;
	telemetry_data.lt = s->lt;
	telemetry_data.rt = s->rt;
	telemetry_data.buttons = state_to_buttons(s);
	telemetry_data.dpad = s->dpad;
	telemetry_data.flags = default_conn ? BIT(0) : 0;
}

static int telemetry_interval_set(uint16_t new_ms)
{
	if (new_ms < TELEMETRY_MIN_INTERVAL_MS || new_ms > TELEMETRY_MAX_INTERVAL_MS) {
		return -EINVAL;
	}

	telemetry_interval_ms = new_ms;
	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_save_one(SETTINGS_KEY_TELEMETRY_MS, &telemetry_interval_ms,
				  sizeof(telemetry_interval_ms));
	}
	return 0;
}

static ssize_t telemetry_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 void *buf, uint16_t len, uint16_t offset)
{
	struct hub_telemetry_payload snapshot;

	ARG_UNUSED(attr);
	k_mutex_lock(&data_mutex, K_FOREVER);
	snapshot = telemetry_data;
	k_mutex_unlock(&data_mutex);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &snapshot, sizeof(snapshot));
}

static ssize_t cfg_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset)
{
	struct hub_cfg_payload cfg = {
		.version = 1U,
		.telemetry_interval_ms = telemetry_interval_ms,
	};

	ARG_UNUSED(conn);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &cfg, sizeof(cfg));
}

static ssize_t cfg_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	const uint8_t *p = buf;
	uint16_t value;
	int err;

	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U || len < 3U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	value = sys_get_le16(&p[1]);
	if (p[0] == CONFIG_PARAM_TELEMETRY_MS) {
		err = telemetry_interval_set(value);
		if (err) {
			return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
		}
		HUB_INF_M(HUB_MOD_PHONE, "Config updated: telemetry_interval_ms=%u\n", telemetry_interval_ms);
		return len;
	}

	return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
}

static void telemetry_work_handler(struct k_work *work)
{
	int err;
	struct hub_telemetry_payload snapshot;

	ARG_UNUSED(work);

	k_mutex_lock(&data_mutex, K_FOREVER);
	snapshot = telemetry_data;
	k_mutex_unlock(&data_mutex);

	err = bt_gatt_notify(NULL, &hub_svc.attrs[2], &snapshot, sizeof(snapshot));
	if (err && err != -ENOTCONN && err != -EAGAIN) {
		HUB_ERR_M(HUB_MOD_PHONE, "Telemetry notify failed: %d\n", err);
	}

	k_work_schedule(&telemetry_work, K_MSEC(telemetry_interval_ms));
}

static ssize_t power_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			     void *buf, uint16_t len, uint16_t offset)
{
	struct hub_power_payload snapshot;

	ARG_UNUSED(attr);
	k_mutex_lock(&data_mutex, K_FOREVER);
	snapshot = power_data;
	k_mutex_unlock(&data_mutex);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &snapshot, sizeof(snapshot));
}

static void power_work_handler(struct k_work *work)
{
	static uint8_t last_charge_state = 0xFFU;
	static uint8_t last_fault = 0xFFU;
	static bool last_vbus;
	struct bq25895_status st;
	struct hub_power_payload payload = {
		.version = 1U,
	};
	uint32_t next_ms = POWER_HEARTBEAT_IDLE_MS;
	bool charging;
	bool state_changed;
	int err;

	ARG_UNUSED(work);

	err = bq25895_read(&st);
	if (err == 0) {
		payload.flags = HUB_PWR_FLAG_VALID;
		if (st.power_good) {
			payload.flags |= HUB_PWR_FLAG_POWER_GOOD;
		}
		if (st.vbus_present) {
			payload.flags |= HUB_PWR_FLAG_VBUS;
		}
		payload.charge_state = (uint8_t)st.charge_state;
		payload.vbus_state = (uint8_t)st.vbus_state;
		payload.fault = st.fault;
		payload.battery_pct = st.battery_pct;
		payload.batt_mv = st.batt_mv;
		payload.sys_mv = st.sys_mv;
		payload.vbus_mv = st.vbus_mv;
		payload.charge_ma = st.charge_ma;

		charging = (st.charge_state == BQ25895_CHG_PRE_CHARGE) ||
			   (st.charge_state == BQ25895_CHG_FAST_CHARGING) ||
			   (st.vbus_present && st.charge_state != BQ25895_CHG_NOT_CHARGING);
		/*
		 * While VBUS is present (plug / charge / done), poll faster so
		 * the phone GATT Power notify tracks voltage/current live.
		 */
		if (st.vbus_present || charging) {
			next_ms = POWER_HEARTBEAT_CHARGING_MS;
		}

		state_changed = (st.charge_state != last_charge_state) ||
				(st.vbus_present != last_vbus) ||
				(st.fault != last_fault);
		if (state_changed) {
			HUB_INF_M(HUB_MOD_BQ,
				  "BQ25895 batt=%umV %u%% sys=%umV vbus=%umV(%s) ichg=%umA %s\n",
				  st.batt_mv, st.battery_pct, st.sys_mv, st.vbus_mv,
				  bq25895_vbus_state_str(st.vbus_state), st.charge_ma,
				  bq25895_charge_state_str(st.charge_state));
			if (st.fault != 0U) {
				HUB_WRN_M(HUB_MOD_BQ,
					  "BQ25895 FAULT REG0C=0x%02x (STAT 1Hz blink)\n",
					  st.fault);
			}
			last_charge_state = (uint8_t)st.charge_state;
			last_vbus = st.vbus_present;
			last_fault = st.fault;
		} else if (next_ms == POWER_HEARTBEAT_CHARGING_MS) {
			HUB_DBG_M(HUB_MOD_BQ,
				  "BQ25895 live batt=%umV %u%% ichg=%umA %s\n",
				  st.batt_mv, st.battery_pct, st.charge_ma,
				  bq25895_charge_state_str(st.charge_state));
		}
	} else {
		HUB_ERR_M(HUB_MOD_BQ, "BQ25895 read failed: %d\n", err);
	}

	k_mutex_lock(&data_mutex, K_FOREVER);
	power_data = payload;
	k_mutex_unlock(&data_mutex);

	err = bt_gatt_notify(NULL, &hub_svc.attrs[HUB_PWR_VALUE_ATTR_IDX],
			     &payload, sizeof(payload));
	if (err && err != -ENOTCONN && err != -EAGAIN) {
		HUB_ERR_M(HUB_MOD_BQ, "Power notify failed: %d\n", err);
	}

	k_work_schedule(&power_work, K_MSEC(next_ms));
}

static void bq25895_on_int(void)
{
	/* INT path: sample ASAP; also resets the heartbeat timer. */
	(void)k_work_reschedule(&power_work, K_NO_WAIT);
}

static void power_monitor_start(void)
{
	int err;

	bq25895_set_event_cb(bq25895_on_int);

	err = bq25895_init();
	if (err || !bq25895_available()) {
		HUB_ERR_M(HUB_MOD_BQ, "BQ25895 unavailable (err %d) — power status disabled\n", err);
		return;
	}

	if (!bq25895_irq_ready()) {
		HUB_WRN_M(HUB_MOD_BQ, "BQ25895 INT not armed — heartbeat-only sampling\n");
	}

	k_work_init_delayable(&power_work, power_work_handler);
	k_work_schedule(&power_work, K_NO_WAIT);
}

static int settings_set_cb(const char *name, size_t len, settings_read_cb read_cb,
			   void *cb_arg)
{
	if (strcmp(name, "telemetry_ms") == 0 && len == sizeof(telemetry_interval_ms)) {
		ssize_t rd = read_cb(cb_arg, &telemetry_interval_ms, sizeof(telemetry_interval_ms));
		if (rd == sizeof(telemetry_interval_ms)) {
			if (telemetry_interval_ms < TELEMETRY_MIN_INTERVAL_MS ||
			    telemetry_interval_ms > TELEMETRY_MAX_INTERVAL_MS) {
				telemetry_interval_ms = TELEMETRY_DEFAULT_INTERVAL_MS;
			}
			return 0;
		}
	}

	if (strcmp(name, "xbox_addr") == 0 && len == sizeof(bonded_xbox_addr)) {
		ssize_t rd = read_cb(cb_arg, &bonded_xbox_addr, sizeof(bonded_xbox_addr));

		if (rd == sizeof(bonded_xbox_addr)) {
			bonded_xbox_valid = true;
			return 0;
		}
	}

	if (strcmp(name, "esb_radio") == 0 && len == sizeof(struct hub_esb_store)) {
		struct hub_esb_store store;
		ssize_t rd = read_cb(cb_arg, &store, sizeof(store));

		if (rd == (ssize_t)sizeof(store) && store.version == HUB_ESB_STORE_VERSION) {
			uart_paired_cfg = store.cfg;
			uart_paired_cfg_valid = true;
			uart_esb_cfg_saved = store.cfg;
			uart_esb_cfg_saved_valid = true;
			return 0;
		}
	}

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(xbox_hub, "xbox_hub", NULL, settings_set_cb, NULL, NULL);

static void log_raw_report(uint8_t report_id, const uint8_t *data, uint16_t len)
{
	uint16_t i;

	HUB_DBG_M(HUB_MOD_HID, "raw report id=%u len=%u:", report_id, len);
	for (i = 0; i < len; i++) {
		HUB_DBG_M(HUB_MOD_SYS, " %02x", data[i]);
	}
	HUB_DBG_M(HUB_MOD_SYS, "\n");
}

static void input_report_cb(uint8_t report_id, const uint8_t *data,
			    uint16_t len, void *user_data)
{
	struct xbox_gamepad_state state;
	int64_t now = k_uptime_get();

	ARG_UNUSED(user_data);

	if (!xbox_report_parse(data, len, &state)) {
		log_raw_report(report_id, data, len);
		return;
	}

	if (!xbox_input_logged && hids.conn != NULL) {
		char addr[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(bt_conn_get_dst(hids.conn), addr, sizeof(addr));
		HUB_INF_M(HUB_MOD_XBOX, "Receiving Xbox input from %s\n", addr);
		HUB_INF_M(HUB_MOD_XBOX, "Xbox link ready on %s — Sync LED should stop on THIS controller\n",
		       addr);
		xbox_input_logged = true;
	}

	k_mutex_lock(&data_mutex, K_FOREVER);
	latest_state = state;
	telemetry_update_from_state(&state);
	k_mutex_unlock(&data_mutex);
	uart_send_ctrl_from_state(&state);

	if ((now - last_report_log_ms) >= REPORT_LOG_INTERVAL_MS) {
		last_report_log_ms = now;
		xbox_report_print(&state);
	}
}

static void scan_filter_match(struct bt_scan_device_info *device_info,
			      struct bt_scan_filter_match *filter_match,
			      bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	if (!filter_match->uuid.match || (filter_match->uuid.count != 1)) {
		return;
	}

	if (default_conn != NULL || xbox_connecting || !connectable) {
		return;
	}

	if (bonded_xbox_valid &&
	    bt_addr_le_cmp(device_info->recv_info->addr, &bonded_xbox_addr) != 0) {
		return;
	}

	if (device_info->conn_param == NULL) {
		return;
	}

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	xbox_log_adv_match(addr, device_info->adv_data);
	schedule_xbox_connect(device_info->recv_info->addr, device_info->conn_param);
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	ARG_UNUSED(device_info);
	HUB_ERR_M(HUB_MOD_XBOX, "Connection attempt failed\n");
	xbox_link_failed_retry(BT_HCI_ERR_CONN_FAIL_TO_ESTAB);
}

static void scan_connecting(struct bt_scan_device_info *device_info,
			    struct bt_conn *conn)
{
	ARG_UNUSED(device_info);
	default_conn = bt_conn_ref(conn);
}

static void scan_filter_no_match(struct bt_scan_device_info *device_info,
				 bool connectable)
{
	char addr[BT_ADDR_LE_STR_LEN];

	ARG_UNUSED(connectable);

	if (device_info->recv_info->adv_type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	if (default_conn != NULL || xbox_connecting) {
		return;
	}

	if (bonded_xbox_valid &&
	    bt_addr_le_cmp(device_info->recv_info->addr, &bonded_xbox_addr) != 0) {
		return;
	}

	if (device_info->conn_param == NULL) {
		return;
	}

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	HUB_INF_M(HUB_MOD_PHONE, "Direct advertising from %s\n", addr);
	schedule_xbox_connect(device_info->recv_info->addr, device_info->conn_param);
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match,
		scan_connecting_error, scan_connecting);

static void xbox_activate_done_cb(int err, void *user_data)
{
	ARG_UNUSED(user_data);

	xbox_link_setup = false;

	if (err != 0) {
		HUB_ERR_M(HUB_MOD_HID, "HID activate failed: %d\n", err);
	} else {
		HUB_INF_M(HUB_MOD_PHONE, "Xbox HID ready — resuming phone advertising\n");
	}

	schedule_phone_adv_resume();
}

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
	int err;

	ARG_UNUSED(context);

	discovery_active = false;
	HUB_DBG_M(HUB_MOD_HID, "GATT discovery completed\n");

	err = xbox_hids_setup(dm, &hids, input_report_cb, NULL);
	if (err) {
		HUB_ERR_M(HUB_MOD_HID, "HID client setup failed: %d\n", err);
		goto release_dm;
	}

	err = xbox_hids_activate(&hids, xbox_activate_done_cb, NULL);
	if (err) {
		HUB_ERR_M(HUB_MOD_HID, "HID activate start failed: %d\n", err);
		xbox_link_setup = false;
		schedule_phone_adv_resume();
	}

release_dm:
	err = bt_gatt_dm_data_release(dm);
	if (err) {
		HUB_ERR_M(HUB_MOD_HID, "Discovery data release failed: %d\n", err);
	}
}

static void discovery_service_not_found_cb(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	discovery_active = false;
	xbox_link_setup = false;
	HUB_ERR_M(HUB_MOD_HID, "HID service not found\n");
	schedule_phone_adv_resume();
}

static void discovery_error_found_cb(struct bt_conn *conn, int err, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	discovery_active = false;
	xbox_link_setup = false;
	HUB_ERR_M(HUB_MOD_HID, "GATT discovery failed: %d\n", err);
	schedule_phone_adv_resume();
}

static const struct bt_gatt_dm_cb discovery_cb = {
	.completed = discovery_completed_cb,
	.service_not_found = discovery_service_not_found_cb,
	.error_found = discovery_error_found_cb,
};

static void gatt_discover(struct bt_conn *conn)
{
	int err;

	if (conn != default_conn || discovery_active) {
		return;
	}

	discovery_active = true;
	err = bt_gatt_dm_start(conn, BT_UUID_HIDS, &discovery_cb, NULL);
	if (err) {
		discovery_active = false;
		HUB_ERR_M(HUB_MOD_HID, "Discovery start failed: %d\n", err);
	}
}

static void restart_scan(void)
{
	int err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);

	if (err) {
		HUB_ERR_M(HUB_MOD_XBOX, "Scan restart failed: %d\n", err);
	}
}

static int adv_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2,
				  adv_data, ARRAY_SIZE(adv_data),
				  scan_rsp, ARRAY_SIZE(scan_rsp));

	if (err == -EALREADY) {
		adv_running = true;
		return 0;
	}

	if (!err) {
		adv_running = true;
	}

	return err;
}

static void adv_guard_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (!hub_may_phone_adv()) {
		k_work_schedule(&adv_guard_work, K_SECONDS(2));
		return;
	}

	if (adv_running) {
		k_work_schedule(&adv_guard_work, K_SECONDS(2));
		return;
	}

	if (adv_start() != 0) {
		HUB_ERR_M(HUB_MOD_PHONE, "Phone advertising guard failed\n");
	}

	k_work_schedule(&adv_guard_work, K_SECONDS(2));
}

static void adv_restart_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (!hub_may_phone_adv()) {
		return;
	}

	err = adv_start();
	if (!err) {
		adv_restart_attempts = 0U;
		HUB_INF_M(HUB_MOD_PHONE, "Phone advertising restarted\n");
		return;
	}

	adv_restart_attempts++;
	HUB_ERR_M(HUB_MOD_PHONE, "Phone advertising restart failed: %d (attempt %u)\n",
	       err, adv_restart_attempts);

	if (adv_restart_attempts < 10U) {
		k_work_schedule(&adv_restart_work, K_MSEC(300));
	}
}

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];
	struct bt_conn_info info;

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (conn_err) {
		HUB_ERR_M(HUB_MOD_XBOX, "Connect failed %s: 0x%02x %s\n", addr, conn_err,
		       bt_hci_err_to_str(conn_err));
		if (conn == default_conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;
		}
		xbox_link_failed_retry(conn_err);
		return;
	}

	if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL) {
		phone_conn = bt_conn_ref(conn);
		adv_running = false;
		adv_restart_attempts = 0U;
		k_work_cancel_delayable(&adv_restart_work);
		HUB_INF_M(HUB_MOD_PHONE, "Phone connected: %s\n", addr);
		return;
	}

	HUB_INF_M(HUB_MOD_XBOX, "Xbox connected: %s\n", addr);
	xbox_connecting = false;
	xbox_link_setup = true;
	set_conn_led(true);

	if (xbox_post_connect_conn != NULL) {
		bt_conn_unref(xbox_post_connect_conn);
	}
	xbox_post_connect_conn = bt_conn_ref(conn);
	(void)k_work_submit(&xbox_post_connect_work);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (auth_conn == conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	if (phone_conn == conn) {
		bt_conn_unref(phone_conn);
		phone_conn = NULL;
		adv_running = false;
		hub_ble_log_on_phone_disconnect();
		HUB_INF_M(HUB_MOD_PHONE, "Phone disconnected: %s reason 0x%02x %s\n", addr, reason,
		       bt_hci_err_to_str(reason));
		adv_restart_attempts = 0U;
		k_work_schedule(&adv_restart_work, K_MSEC(200));
		return;
	}

	HUB_INF_M(HUB_MOD_XBOX, "Xbox disconnected: %s reason 0x%02x %s\n", addr, reason,
	       bt_hci_err_to_str(reason));
	set_conn_led(false);
	discovery_active = false;
	xbox_link_setup = false;
	xbox_sec_work_cancel();
	(void)k_work_cancel(&xbox_post_connect_work);
	(void)k_work_cancel(&xbox_sec_fail_work);
	if (xbox_post_connect_conn != NULL) {
		bt_conn_unref(xbox_post_connect_conn);
		xbox_post_connect_conn = NULL;
	}
	if (xbox_sec_fail_conn != NULL) {
		bt_conn_unref(xbox_sec_fail_conn);
		xbox_sec_fail_conn = NULL;
	}
	xbox_hids_release(&hids);

	if (default_conn == conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
		xbox_link_failed_retry(reason);
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		HUB_INF_M(HUB_MOD_XBOX, "Security changed: %s level %u\n", addr, level);
	} else {
		HUB_ERR_M(HUB_MOD_XBOX, "Security failed: %s level %u err %d %s\n", addr, level,
		       err, bt_security_err_to_str(err));
		if (err == BT_SECURITY_ERR_PIN_OR_KEY_MISSING ||
		    err == BT_SECURITY_ERR_AUTH_FAIL ||
		    err == BT_SECURITY_ERR_KEY_REJECTED ||
		    err == BT_SECURITY_ERR_UNSPECIFIED) {
			schedule_xbox_sec_fail(conn);
		}
		return;
	}

	if (level >= BT_SECURITY_L2) {
		xbox_sec_work_cancel();
		gatt_discover(conn);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
	.security_changed = security_changed,
};

static void scan_init(void)
{
	int err;
	static const struct bt_le_conn_param xbox_conn_param =
		BT_LE_CONN_PARAM_INIT(XBOX_CONN_INTERVAL_MIN, XBOX_CONN_INTERVAL_MAX,
				      XBOX_CONN_LATENCY, XBOX_CONN_TIMEOUT);
	struct bt_scan_init_param scan_init = {
		.connect_if_match = 0,
		.scan_param = NULL,
		.conn_param = &xbox_conn_param,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HIDS);
	if (err) {
		HUB_ERR_M(HUB_MOD_SYS, "UUID filter setup failed: %d\n", err);
		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		HUB_ERR_M(HUB_MOD_SYS, "UUID filter enable failed: %d\n", err);
	}
}

static void num_comp_reply(bool accept)
{
	if (accept) {
		bt_conn_auth_passkey_confirm(auth_conn);
		HUB_INF_M(HUB_MOD_PHONE, "Pairing accepted\n");
	} else {
		bt_conn_auth_cancel(auth_conn);
		HUB_ERR_M(HUB_MOD_PHONE, "Pairing rejected\n");
	}

	bt_conn_unref(auth_conn);
	auth_conn = NULL;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	uint32_t button = button_state & has_changed;

	/*
	 * Btn1 (P1.02): long press 1.5s = ESB OTA pair;
	 * short press = BLE passkey accept, or BQ25895 dump when idle.
	 */
	if (button & KEY_ESB_PTX_PAIR) {
		if ((button_state & KEY_ESB_PTX_PAIR) != 0U) {
			esb_ptx_hold_armed = true;
			esb_ptx_hold_fired = false;
			k_work_schedule(&esb_ptx_hold_work, K_MSEC(ESB_BTN_HOLD_MS));
		} else {
			(void)k_work_cancel_delayable(&esb_ptx_hold_work);
			if (esb_ptx_hold_armed && !esb_ptx_hold_fired) {
				if (auth_conn != NULL) {
					num_comp_reply(true);
				} else {
					HUB_FORCE("Button1: BQ25895 register dump\n");
					bq25895_log_dump();
				}
			}
			esb_ptx_hold_armed = false;
		}
	}

	/*
	 * Btn3: short press = UART sync addresses to PRX;
	 * long press 1.5s = toggle ESB debug log forward.
	 */
	if (button & KEY_ESB_PRX_PAIR) {
		if ((button_state & KEY_ESB_PRX_PAIR) != 0U) {
			esb_debug_hold_armed = true;
			esb_debug_hold_fired = false;
			k_work_schedule(&esb_debug_hold_work, K_MSEC(ESB_BTN_HOLD_MS));
		} else {
			(void)k_work_cancel_delayable(&esb_debug_hold_work);
			if (esb_debug_hold_armed && !esb_debug_hold_fired) {
				uart_hub_trigger_esb_prx_pair();
			}
			esb_debug_hold_armed = false;
		}
	}

	if (button & KEY_PAIRING_REJECT) {
		if (auth_conn != NULL) {
			num_comp_reply(false);
		} else {
			k_work_cancel_delayable(&xbox_scan_retry_work);
			xbox_connecting = false;
			xbox_link_setup = false;
			clear_bonded_xbox();
			if (default_conn != NULL) {
				(void)bt_conn_disconnect(default_conn,
							 BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			}
			schedule_phone_adv_resume();
			schedule_xbox_scan_retry(0);
		}
	}
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	HUB_INF_M(HUB_MOD_PHONE, "Passkey for %s: %06u\n", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	HUB_INF_M(HUB_MOD_PHONE, "Auto-confirm passkey for %s: %06u\n", addr, passkey);
	(void)bt_conn_auth_passkey_confirm(conn);
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	HUB_WRN_M(HUB_MOD_PHONE, "Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];
	const bt_addr_le_t *dst = bt_conn_get_dst(conn);

	bt_addr_le_to_str(dst, addr, sizeof(addr));
	HUB_INF_M(HUB_MOD_PHONE, "Pairing complete: %s bonded=%d\n", addr, bonded);

	if (bonded) {
		bonded_xbox_store(dst);
		HUB_INF_M(HUB_MOD_XBOX, "Hub bonded to %s — Sync LED on THIS controller should stop blinking\n",
		       addr);
		HUB_INF_M(HUB_MOD_SYS, "If your controller still blinks, its MAC is different; press Button 2\n");
	}
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	HUB_ERR_M(HUB_MOD_PHONE, "Pairing failed: %s reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));

	if (reason == BT_SECURITY_ERR_PIN_OR_KEY_MISSING ||
	    reason == BT_SECURITY_ERR_AUTH_FAIL ||
	    reason == BT_SECURITY_ERR_KEY_REJECTED ||
	    reason == BT_SECURITY_ERR_UNSPECIFIED) {
		schedule_xbox_sec_fail(conn);
	}

	if (auth_conn == conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}
}

static struct bt_conn_auth_cb conn_auth_callbacks = {
	.passkey_display = auth_passkey_display,
	.passkey_confirm = auth_passkey_confirm,
	.cancel = auth_cancel,
};

static struct bt_conn_auth_info_cb conn_auth_info_callbacks = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
};

int main(void)
{
	int err;

	err = hub_flash_log_init();
	if (err) {
		HUB_FORCE("hub flash log init failed: %d (HUB_* UART-only)\n", err);
		hub_log_uart_mirror = true;
	}
	hub_ble_log_init();
	hub_ble_log_bind(&hub_svc.attrs[HUB_LOGDATA_VALUE_ATTR_IDX]);

	HUB_INF_M(HUB_MOD_PHONE, "Ground BLE Hub (Xbox + Phone) on nRF54L15\n");
	HUB_INF_M(HUB_MOD_PHONE, "Put Xbox controller in pairing mode (hold Sync).\n");

	xbox_hids_init(&hids);
	k_work_init_delayable(&telemetry_work, telemetry_work_handler);
	k_work_init_delayable(&adv_restart_work, adv_restart_work_handler);
	k_work_init_delayable(&adv_guard_work, adv_guard_work_handler);
	k_work_init_delayable(&xbox_sec_work, xbox_sec_work_handler);
	k_work_init_delayable(&xbox_scan_retry_work, xbox_scan_retry_handler);
	k_work_init(&xbox_connect_work, xbox_connect_work_handler);
	k_work_init(&xbox_post_connect_work, xbox_post_connect_work_handler);
	k_work_init(&phone_adv_resume_work, phone_adv_resume_work_handler);
	k_work_init(&xbox_sec_fail_work, xbox_sec_fail_work_handler);
	k_work_init_delayable(&esb_ptx_hold_work, esb_ptx_hold_handler);
	k_work_init_delayable(&esb_debug_hold_work, esb_debug_hold_handler);
	k_work_init_delayable(&esb_pair_watchdog_work, esb_pair_watchdog_handler);
	k_work_init_delayable(&esb_boot_restore_work, esb_boot_restore_handler);
	k_work_init_delayable(&uart_ctrl_heartbeat_work, uart_ctrl_heartbeat_handler);
	memset(&latest_state, 0, sizeof(latest_state));
	memset(&telemetry_data, 0, sizeof(telemetry_data));
	memset(&power_data, 0, sizeof(power_data));
	power_data.version = 1U;

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		HUB_ERR_M(HUB_MOD_PHONE, "Auth callback register failed\n");
		return 0;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		HUB_ERR_M(HUB_MOD_PHONE, "Auth info callback register failed\n");
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		HUB_ERR_M(HUB_MOD_SYS, "Bluetooth init failed: %d\n", err);
		return 0;
	}
	HUB_INF_M(HUB_MOD_SYS, "Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}
	log_bonded_xbox_boot();

	scan_init();

	err = dk_leds_init();
	if (err) {
		HUB_ERR_M(HUB_MOD_SYS, "LED init failed: %d\n", err);
		return 0;
	}

	err = hub_status_led_init();
	if (err && err != -ENOENT) {
		HUB_WRN_M(HUB_MOD_SYS, "status LED init failed: %d\n", err);
	}

	err = dk_buttons_init(button_handler);
	if (err) {
		HUB_ERR_M(HUB_MOD_SYS, "Button init failed: %d\n", err);
		return 0;
	}

	err = adv_start();
	if (err) {
		HUB_ERR_M(HUB_MOD_PHONE, "Advertising start failed: %d\n", err);
		return 0;
	}

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		HUB_ERR_M(HUB_MOD_XBOX, "Scan start failed: %d\n", err);
		return 0;
	}

	err = uart_link_init();
	if (err) {
		HUB_ERR_M(HUB_MOD_UART, "UART link init failed: %d\n", err);
		return 0;
	}

	/* PTX has no local pair store — push Hub flash config shortly after link up. */
	k_work_schedule(&esb_boot_restore_work, K_MSEC(ESB_BOOT_RESTORE_DELAY_MS));

	k_work_schedule(&telemetry_work, K_MSEC(telemetry_interval_ms));
	k_work_schedule(&uart_ctrl_heartbeat_work, K_MSEC(UART_CTRL_HEARTBEAT_MS));
	k_work_schedule(&adv_guard_work, K_SECONDS(2));

	/* BQ25895 is optional — never blocks BLE if absent. */
	power_monitor_start();
	HUB_INF_M(HUB_MOD_UART, "Scanning Xbox, advertising to phone, UART link enabled\n");
	HUB_INF_M(HUB_MOD_UART, "UART CTRL heartbeat %u ms while Xbox connected\n",
	       UART_CTRL_HEARTBEAT_MS);
	HUB_INF_M(HUB_MOD_BQ, "Btn1: dump BQ25895 regs | Btn3: re-push ESB cfg to UART | "
	       "Btn3 hold: debug log | Btn1(P1.02) hold: ESB OTA pair\n");
	HUB_FORCE("Shell ready — flog show | loglevel | bq dump | esb status\n");
	return 0;
}
