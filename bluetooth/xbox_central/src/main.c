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
#include <zephyr/sys/printk.h>
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

#define CONN_LED                       DK_LED1
#define REPORT_LOG_INTERVAL_MS         100
#define TELEMETRY_DEFAULT_INTERVAL_MS  50
#define TELEMETRY_MIN_INTERVAL_MS      20
#define TELEMETRY_MAX_INTERVAL_MS      500
#define CONFIG_PARAM_TELEMETRY_MS      1
#define SETTINGS_KEY_TELEMETRY_MS      "xbox_hub/telemetry_ms"

#define KEY_PAIRING_ACCEPT             DK_BTN1_MSK
#define KEY_PAIRING_REJECT             DK_BTN2_MSK
#define KEY_ESB_DEBUG_TOGGLE           DK_BTN3_MSK
#define KEY_ESB_PAIR                   DK_BTN4_MSK

#define HUB_UUID_W1 0x9350
#define HUB_UUID_W2 0x11ed
#define HUB_UUID_W3 0xa1eb
#define HUB_UUID_W48 0x0242ac120002
#define HUB_SVC_UUID_VAL BT_UUID_128_ENCODE(0x57a71000, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_TELEM_UUID_VAL BT_UUID_128_ENCODE(0x57a71001, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
#define HUB_CFG_UUID_VAL BT_UUID_128_ENCODE(0x57a71002, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)

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
static uint16_t telemetry_interval_ms = TELEMETRY_DEFAULT_INTERVAL_MS;
static int64_t last_report_log_ms;
static bool discovery_active;
static K_MUTEX_DEFINE(data_mutex);
static struct k_work_delayable telemetry_work;
static struct k_work_delayable adv_restart_work;
static struct k_work_delayable adv_guard_work;
static uint8_t adv_restart_attempts;
static bool adv_running;
static struct uart_rc_link uart_link;
static uint8_t uart_ctrl_seq;
static uint8_t uart_esb_req_seq;
static uint8_t uart_debug_ctrl_seq;
static bool uart_debug_forward_enabled;
static struct uart_rc_esb_config uart_paired_cfg;
static bool uart_paired_cfg_valid;

static ssize_t telemetry_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 void *buf, uint16_t len, uint16_t offset);
static ssize_t cfg_read_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   void *buf, uint16_t len, uint16_t offset);
static ssize_t cfg_write_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			    const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

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
			       cfg_read_cb, cfg_write_cb, NULL)
);

static const uint8_t adv_flags[] = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };
static const uint8_t adv_uuid[] = {
	BT_UUID_128_ENCODE(0x57a71000, HUB_UUID_W1, HUB_UUID_W2, HUB_UUID_W3, HUB_UUID_W48)
};
static const struct bt_data adv_data[] = {
	BT_DATA(BT_DATA_FLAGS, adv_flags, sizeof(adv_flags)),
	BT_DATA(BT_DATA_UUID128_ALL, adv_uuid, sizeof(adv_uuid)),
};
static const struct bt_data scan_rsp[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void set_conn_led(bool on)
{
	(void)dk_set_led(CONN_LED, on ? 1 : 0);
}

static uint16_t axis_to_rc(int16_t axis)
{
	int32_t v = ((int32_t)axis + 32767) * 1000 / 65534;

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
	k_mutex_unlock(&data_mutex);
}

static void on_uart_esb_rsp(const struct uart_rc_esb_rsp *rsp, void *user_data)
{
	struct uart_rc_esb_config cfg;

	ARG_UNUSED(user_data);

	if (rsp->status != 0) {
		printk("ESB rsp cmd=0x%02x failed: %d\n", rsp->cmd, rsp->status);
		return;
	}

	switch (rsp->cmd) {
	case UART_RC_ESB_CMD_GET_CONFIG:
	case UART_RC_ESB_CMD_PAIR:
		if (rsp->data_len >= sizeof(cfg) &&
		    uart_rc_link_decode_esb_config(rsp->data, rsp->data_len, &cfg) == 0) {
			printk("ESB cfg pipe=%u pwr=%d delay=%u\n", cfg.pipe, cfg.tx_power,
			       cfg.retransmit_delay);
			if (rsp->cmd == UART_RC_ESB_CMD_PAIR) {
				uart_paired_cfg = cfg;
				uart_paired_cfg_valid = true;
				printk("PTX paired; wire PRX UART and press btn4 to sync\n");
			}
		}
		break;
	case UART_RC_ESB_CMD_SET_ADDR:
		printk("ESB addresses staged\n");
		break;
	case UART_RC_ESB_CMD_APPLY:
		printk("ESB radio applied\n");
		break;
	case UART_RC_ESB_CMD_SAVE:
		printk("ESB config saved\n");
		break;
	default:
		printk("ESB rsp cmd=0x%02x ok\n", rsp->cmd);
		break;
	}
}

static void on_uart_debug_log(const struct uart_rc_debug_log *log, void *user_data)
{
	ARG_UNUSED(user_data);

	printk("[ESB:%u] %.*s", log->level, log->text_len, log->text);
	if ((log->flags & UART_RC_DEBUG_FLAG_MORE) == 0U) {
		printk("\n");
	}
}

static int uart_hub_send_esb_req(uint8_t cmd, const uint8_t *data, uint8_t data_len)
{
	struct uart_rc_esb_req req = {
		.seq = uart_esb_req_seq++,
		.cmd = cmd,
		.data_len = data_len,
	};

	if (data_len > 0U && data != NULL) {
		memcpy(req.data, data, data_len);
	}

	return uart_rc_link_send_esb_req(&uart_link, &req);
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

static void uart_hub_query_esb_config(void)
{
	(void)uart_hub_send_esb_req(UART_RC_ESB_CMD_GET_CONFIG, NULL, 0U);
}

static int uart_hub_sync_esb_config(const struct uart_rc_esb_config *cfg)
{
	uint8_t addr_payload[16];
	int err;

	if (cfg == NULL) {
		return -EINVAL;
	}

	memcpy(&addr_payload[0], cfg->base0, 4U);
	memcpy(&addr_payload[4], cfg->base1, 4U);
	memcpy(&addr_payload[8], cfg->prefixes, 8U);

	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_SET_ADDR, addr_payload,
				    sizeof(addr_payload));
	if (err != 0) {
		return err;
	}

	err = uart_hub_send_esb_req(UART_RC_ESB_CMD_APPLY, NULL, 0U);
	if (err != 0) {
		return err;
	}

	return uart_hub_send_esb_req(UART_RC_ESB_CMD_SAVE, NULL, 0U);
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

	(void)uart_rc_link_send_ctrl(&uart_link, &ctrl);
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

	printk("UART RC link on uart30 (HDLC 0x%02x)\n", UART_RC_LINK_HDLC_FLAG);
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
		printk("Config updated: telemetry_interval_ms=%u\n", telemetry_interval_ms);
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
		printk("Telemetry notify failed: %d\n", err);
	}

	k_work_schedule(&telemetry_work, K_MSEC(telemetry_interval_ms));
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
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(xbox_hub, "xbox_hub", NULL, settings_set_cb, NULL, NULL);

static void log_raw_report(uint8_t report_id, const uint8_t *data, uint16_t len)
{
	uint16_t i;

	printk("raw report id=%u len=%u:", report_id, len);
	for (i = 0; i < len; i++) {
		printk(" %02x", data[i]);
	}
	printk("\n");
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

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	printk("HID device found: %s connectable=%s\n", addr,
	       connectable ? "yes" : "no");
}

static void scan_connecting_error(struct bt_scan_device_info *device_info)
{
	ARG_UNUSED(device_info);
	printk("Connection attempt failed\n");
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
	int err;
	struct bt_conn *conn = NULL;
	char addr[BT_ADDR_LE_STR_LEN];

	if (device_info->recv_info->adv_type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bt_addr_le_to_str(device_info->recv_info->addr, addr, sizeof(addr));
	printk("Direct advertising from %s\n", addr);
	bt_scan_stop();

	err = bt_conn_le_create(device_info->recv_info->addr,
				BT_CONN_LE_CREATE_CONN,
				device_info->conn_param, &conn);
	if (!err) {
		default_conn = bt_conn_ref(conn);
		bt_conn_unref(conn);
	}
}

BT_SCAN_CB_INIT(scan_cb, scan_filter_match, scan_filter_no_match,
		scan_connecting_error, scan_connecting);

static void discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
	int err;

	ARG_UNUSED(context);

	discovery_active = false;
	printk("GATT discovery completed\n");

	err = xbox_hids_setup(dm, &hids, input_report_cb, NULL);
	if (err) {
		printk("HID client setup failed: %d\n", err);
		goto release_dm;
	}

	err = xbox_hids_subscribe_all(&hids);
	if (err) {
		printk("HID subscribe failed: %d\n", err);
	}

release_dm:
	err = bt_gatt_dm_data_release(dm);
	if (err) {
		printk("Discovery data release failed: %d\n", err);
	}
}

static void discovery_service_not_found_cb(struct bt_conn *conn, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	discovery_active = false;
	printk("HID service not found\n");
}

static void discovery_error_found_cb(struct bt_conn *conn, int err, void *context)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(context);
	discovery_active = false;
	printk("GATT discovery failed: %d\n", err);
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
		printk("Discovery start failed: %d\n", err);
	}
}

static void restart_scan(void)
{
	int err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);

	if (err) {
		printk("Scan restart failed: %d\n", err);
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

	if (phone_conn == NULL) {
		int err;

		if (adv_running) {
			k_work_schedule(&adv_guard_work, K_SECONDS(2));
			return;
		}

		err = adv_start();
		if (err) {
			printk("Phone advertising guard failed: %d\n", err);
		}
	}

	k_work_schedule(&adv_guard_work, K_SECONDS(2));
}

static void adv_restart_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	err = adv_start();
	if (!err) {
		adv_restart_attempts = 0U;
		printk("Phone advertising restarted\n");
		return;
	}

	adv_restart_attempts++;
	printk("Phone advertising restart failed: %d (attempt %u)\n",
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
		printk("Connect failed %s: 0x%02x %s\n", addr, conn_err,
		       bt_hci_err_to_str(conn_err));
		if (conn == default_conn) {
			bt_conn_unref(default_conn);
			default_conn = NULL;
			restart_scan();
		}
		return;
	}

	if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_PERIPHERAL) {
		phone_conn = bt_conn_ref(conn);
		adv_running = false;
		adv_restart_attempts = 0U;
		k_work_cancel_delayable(&adv_restart_work);
		printk("Phone connected: %s\n", addr);
		return;
	}

	printk("Xbox connected: %s\n", addr);
	set_conn_led(true);

	if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
		printk("Security request failed, starting discovery\n");
		gatt_discover(conn);
	}
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
		printk("Phone disconnected: %s reason 0x%02x %s\n", addr, reason,
		       bt_hci_err_to_str(reason));
		adv_restart_attempts = 0U;
		k_work_schedule(&adv_restart_work, K_MSEC(200));
		return;
	}

	printk("Xbox disconnected: %s reason 0x%02x %s\n", addr, reason,
	       bt_hci_err_to_str(reason));
	set_conn_led(false);
	discovery_active = false;
	xbox_hids_release(&hids);

	if (default_conn == conn) {
		bt_conn_unref(default_conn);
		default_conn = NULL;
		restart_scan();
	}
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
			     enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (!err) {
		printk("Security changed: %s level %u\n", addr, level);
	} else {
		printk("Security failed: %s level %u err %d %s\n", addr, level,
		       err, bt_security_err_to_str(err));
		return;
	}

	if (level >= BT_SECURITY_L2) {
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
	struct bt_scan_init_param scan_init = {
		.connect_if_match = 1,
		.scan_param = NULL,
		.conn_param = BT_LE_CONN_PARAM_DEFAULT,
	};

	bt_scan_init(&scan_init);
	bt_scan_cb_register(&scan_cb);

	err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_UUID, BT_UUID_HIDS);
	if (err) {
		printk("UUID filter setup failed: %d\n", err);
		return;
	}

	err = bt_scan_filter_enable(BT_SCAN_UUID_FILTER, false);
	if (err) {
		printk("UUID filter enable failed: %d\n", err);
	}
}

static void num_comp_reply(bool accept)
{
	if (accept) {
		bt_conn_auth_passkey_confirm(auth_conn);
		printk("Pairing accepted\n");
	} else {
		bt_conn_auth_cancel(auth_conn);
		printk("Pairing rejected\n");
	}

	bt_conn_unref(auth_conn);
	auth_conn = NULL;
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	uint32_t button = button_state & has_changed;

	if (button & KEY_ESB_DEBUG_TOGGLE) {
		uint8_t flags;

		uart_debug_forward_enabled = !uart_debug_forward_enabled;
		flags = uart_debug_forward_enabled ? UART_RC_DEBUG_FLAG_FORWARD : 0U;
		(void)uart_hub_send_debug_ctrl(flags, LOG_LEVEL_INF);
		printk("ESB debug forward %s\n", uart_debug_forward_enabled ? "on" : "off");
	}

	if (button & KEY_ESB_PAIR) {
		if (uart_paired_cfg_valid) {
			(void)uart_hub_sync_esb_config(&uart_paired_cfg);
			printk("ESB config synced to UART device\n");
		} else {
			(void)uart_hub_send_esb_req(UART_RC_ESB_CMD_PAIR, NULL, 0U);
			printk("ESB pair requested on PTX\n");
		}
	}

	if (!auth_conn) {
		return;
	}

	if (button & KEY_PAIRING_ACCEPT) {
		num_comp_reply(true);
	}

	if (button & KEY_PAIRING_REJECT) {
		num_comp_reply(false);
	}
}

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Passkey for %s: %06u\n", addr, passkey);
}

static void auth_passkey_confirm(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	auth_conn = bt_conn_ref(conn);
	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Confirm passkey for %s: %06u\n", addr, passkey);
	printk("Press Button 1 to accept, Button 2 to reject.\n");
}

static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing cancelled: %s\n", addr);
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing complete: %s bonded=%d\n", addr, bonded);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	printk("Pairing failed: %s reason %d %s\n", addr, reason,
	       bt_security_err_to_str(reason));
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

	printk("Ground BLE Hub (Xbox + Phone) on nRF54L15\n");
	printk("Put Xbox controller in pairing mode (hold Sync).\n");

	xbox_hids_init(&hids);
	k_work_init_delayable(&telemetry_work, telemetry_work_handler);
	k_work_init_delayable(&adv_restart_work, adv_restart_work_handler);
	k_work_init_delayable(&adv_guard_work, adv_guard_work_handler);
	memset(&latest_state, 0, sizeof(latest_state));
	memset(&telemetry_data, 0, sizeof(telemetry_data));

	err = bt_conn_auth_cb_register(&conn_auth_callbacks);
	if (err) {
		printk("Auth callback register failed\n");
		return 0;
	}

	err = bt_conn_auth_info_cb_register(&conn_auth_info_callbacks);
	if (err) {
		printk("Auth info callback register failed\n");
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed: %d\n", err);
		return 0;
	}
	printk("Bluetooth initialized\n");

	if (IS_ENABLED(CONFIG_SETTINGS)) {
		settings_load();
	}

	scan_init();

	err = dk_leds_init();
	if (err) {
		printk("LED init failed: %d\n", err);
		return 0;
	}

	err = dk_buttons_init(button_handler);
	if (err) {
		printk("Button init failed: %d\n", err);
		return 0;
	}

	err = adv_start();
	if (err) {
		printk("Advertising start failed: %d\n", err);
		return 0;
	}

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		printk("Scan start failed: %d\n", err);
		return 0;
	}

	err = uart_link_init();
	if (err) {
		printk("UART link init failed: %d\n", err);
		return 0;
	}

	uart_hub_query_esb_config();

	k_work_schedule(&telemetry_work, K_MSEC(telemetry_interval_ms));
	k_work_schedule(&adv_guard_work, K_SECONDS(2));
	printk("Scanning Xbox, advertising to phone, UART link enabled\n");
	return 0;
}
