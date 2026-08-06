/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * BLE pull of flash application logs via Hub GATT LogCtrl / LogData.
 */

#ifndef HUB_BLE_LOG_H_
#define HUB_BLE_LOG_H_

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

void hub_ble_log_init(void);
void hub_ble_log_bind(const struct bt_gatt_attr *data_attr);
void hub_ble_log_on_phone_disconnect(void);

ssize_t hub_ble_log_ctrl_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset,
			       uint8_t flags);

#endif /* HUB_BLE_LOG_H_ */
