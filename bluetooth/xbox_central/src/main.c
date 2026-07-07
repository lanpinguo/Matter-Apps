/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * BLE Central app for Xbox Series X|S controller (model 1914) verification.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/settings/settings.h>

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

#define CONN_LED                 DK_LED1
#define REPORT_LOG_INTERVAL_MS   100

#define KEY_PAIRING_ACCEPT       DK_BTN1_MSK
#define KEY_PAIRING_REJECT       DK_BTN2_MSK

static struct bt_conn *default_conn;
static struct bt_conn *auth_conn;
static struct xbox_hids hids;
static int64_t last_report_log_ms;
static bool discovery_active;

static void set_conn_led(bool on)
{
	(void)dk_set_led(CONN_LED, on ? 1 : 0);
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

	if ((now - last_report_log_ms) < REPORT_LOG_INTERVAL_MS) {
		return;
	}
	last_report_log_ms = now;

	if (!xbox_report_parse(data, len, &state)) {
		log_raw_report(report_id, data, len);
		return;
	}

	xbox_report_print(&state);
}

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

static void connected(struct bt_conn *conn, uint8_t conn_err)
{
	char addr[BT_ADDR_LE_STR_LEN];

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

	printk("Connected: %s\n", addr);
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

	if (auth_conn) {
		bt_conn_unref(auth_conn);
		auth_conn = NULL;
	}

	printk("Disconnected: %s reason 0x%02x %s\n", addr, reason,
	       bt_hci_err_to_str(reason));
	set_conn_led(false);
	discovery_active = false;
	xbox_hids_release(&hids);

	if (default_conn != conn) {
		return;
	}

	bt_conn_unref(default_conn);
	default_conn = NULL;
	restart_scan();
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

	printk("Xbox BLE Central (nRF54L15)\n");
	printk("Put Xbox controller in pairing mode (hold Sync).\n");

	xbox_hids_init(&hids);

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

	err = bt_scan_start(BT_SCAN_TYPE_SCAN_ACTIVE);
	if (err) {
		printk("Scan start failed: %d\n", err);
		return 0;
	}

	printk("Scanning for HID devices...\n");
	return 0;
}
