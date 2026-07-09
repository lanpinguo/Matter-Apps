/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef XBOX_HIDS_H_
#define XBOX_HIDS_H_

#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <bluetooth/gatt_dm.h>

#define XBOX_HIDS_MAX_REPORTS 8

struct xbox_hids;

typedef void (*xbox_hids_report_cb)(uint8_t report_id, const uint8_t *data,
				    uint16_t len, void *user_data);
typedef void (*xbox_hids_ready_cb)(int err, void *user_data);

struct xbox_hids_report {
	struct xbox_hids *owner;
	uint8_t report_id;
	uint16_t value_handle;
	uint16_t ccc_handle;
	struct bt_gatt_subscribe_params subscribe;
};

struct xbox_hids {
	struct bt_conn *conn;
	xbox_hids_report_cb report_cb;
	void *user_data;
	uint8_t report_count;
	uint16_t info_handle;
	uint16_t report_map_handle;
	uint16_t ctrl_point_handle;
	bool info_valid;
	bool report_map_valid;
	bool ctrl_point_valid;
	struct xbox_hids_report reports[XBOX_HIDS_MAX_REPORTS];
};

void xbox_hids_init(struct xbox_hids *hids);
int xbox_hids_setup(struct bt_gatt_dm *dm, struct xbox_hids *hids,
		    xbox_hids_report_cb cb, void *user_data);
int xbox_hids_activate(struct xbox_hids *hids, xbox_hids_ready_cb cb, void *user_data);
int xbox_hids_subscribe_all(struct xbox_hids *hids);
int xbox_hids_exit_suspend(struct xbox_hids *hids);
void xbox_hids_release(struct xbox_hids *hids);

#endif /* XBOX_HIDS_H_ */
