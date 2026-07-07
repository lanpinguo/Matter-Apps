/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "xbox_hids.h"

#include <string.h>

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

static uint8_t notify_cb(struct bt_conn *conn,
			 struct bt_gatt_subscribe_params *params,
			 const void *data, uint16_t length)
{
	struct xbox_hids_report *rep;
	struct xbox_hids *hids;

	ARG_UNUSED(conn);

	rep = CONTAINER_OF(params, struct xbox_hids_report, subscribe);
	hids = rep->owner;

	if (!hids || !hids->report_cb || !data || length == 0U) {
		return BT_GATT_ITER_CONTINUE;
	}

	hids->report_cb(rep->report_id, data, length, hids->user_data);
	return BT_GATT_ITER_CONTINUE;
}

void xbox_hids_init(struct xbox_hids *hids)
{
	memset(hids, 0, sizeof(*hids));
}

static uint16_t chrc_value_handle_get(const struct bt_gatt_dm *dm,
				      const struct bt_gatt_dm_attr *chrc_attr)
{
	const struct bt_gatt_chrc *chrc = bt_gatt_dm_attr_chrc_val(chrc_attr);
	const struct bt_gatt_dm_attr *value;
	const struct bt_gatt_dm_attr *by_handle;

	if (chrc == NULL) {
		return 0;
	}

	value = bt_gatt_dm_desc_by_uuid(dm, chrc_attr, chrc->uuid);
	if (value != NULL) {
		return value->handle;
	}

	value = bt_gatt_dm_attr_next(dm, chrc_attr);
	if ((value != NULL) && bt_uuid_cmp(value->uuid, BT_UUID_GATT_CHRC)) {
		return value->handle;
	}

	by_handle = bt_gatt_dm_attr_by_handle(dm, chrc_attr->handle + 1U);
	if (by_handle != NULL) {
		return by_handle->handle;
	}

	return chrc_attr->handle + 1U;
}

static int report_add(struct xbox_hids *hids, const struct bt_gatt_dm *dm,
		      const struct bt_gatt_dm_attr *chrc_attr, uint8_t index)
{
	const struct bt_gatt_chrc *chrc = bt_gatt_dm_attr_chrc_val(chrc_attr);
	const struct bt_gatt_dm_attr *ccc_desc;
	struct xbox_hids_report *rep;
	uint16_t value_handle;
	char uuid_str[40];

	if (hids->report_count >= XBOX_HIDS_MAX_REPORTS) {
		return -ENOMEM;
	}

	if (chrc == NULL) {
		return 0;
	}

	if (!(chrc->properties & BT_GATT_CHRC_NOTIFY)) {
		return 0;
	}

	ccc_desc = bt_gatt_dm_desc_by_uuid(dm, chrc_attr, BT_UUID_GATT_CCC);
	if (!ccc_desc) {
		printk("Input report missing CCC (index %u)\n", index);
		return 0;
	}

	value_handle = chrc_value_handle_get(dm, chrc_attr);
	if (value_handle == 0U) {
		printk("Input report missing value handle (index %u)\n", index);
		return 0;
	}

	rep = &hids->reports[hids->report_count];
	rep->owner = hids;
	rep->report_id = index + 1U;
	rep->value_handle = value_handle;
	rep->ccc_handle = ccc_desc->handle;
	hids->report_count++;

	bt_uuid_to_str(chrc->uuid, uuid_str, sizeof(uuid_str));
	printk("Found input report #%u uuid=%s val=0x%04x ccc=0x%04x props=0x%02x\n",
	       rep->report_id, uuid_str, rep->value_handle, rep->ccc_handle,
	       chrc->properties);

	return 0;
}

int xbox_hids_setup(struct bt_gatt_dm *dm, struct xbox_hids *hids,
		    xbox_hids_report_cb cb, void *user_data)
{
	const struct bt_gatt_dm_attr *chrc = NULL;
	uint8_t index = 0;

	xbox_hids_release(hids);

	hids->conn = bt_conn_ref(bt_gatt_dm_conn_get(dm));
	hids->report_cb = cb;
	hids->user_data = user_data;

	while ((chrc = bt_gatt_dm_char_next(dm, chrc)) != NULL) {
		const struct bt_gatt_chrc *chrc_val =
			bt_gatt_dm_attr_chrc_val(chrc);
		int err;

		if (bt_uuid_cmp(chrc_val->uuid, BT_UUID_HIDS_REPORT)) {
			continue;
		}

		err = report_add(hids, dm, chrc, index);
		if (err < 0) {
			return err;
		}

		index++;
	}

	if (hids->report_count == 0U) {
		printk("No notify-capable HID reports found\n");
		return -ENOENT;
	}

	return 0;
}

int xbox_hids_subscribe_all(struct xbox_hids *hids)
{
	uint8_t i;
	int err;
	int subscribed = 0;

	if (!hids->conn) {
		return -ENOTCONN;
	}

	for (i = 0; i < hids->report_count; i++) {
		struct xbox_hids_report *rep = &hids->reports[i];

		rep->subscribe.notify = notify_cb;
		rep->subscribe.value = BT_GATT_CCC_NOTIFY;
		rep->subscribe.value_handle = rep->value_handle;
		rep->subscribe.ccc_handle = rep->ccc_handle;
		atomic_set_bit(rep->subscribe.flags,
			       BT_GATT_SUBSCRIBE_FLAG_VOLATILE);

		err = bt_gatt_subscribe(hids->conn, &rep->subscribe);
		if (err) {
			printk("Subscribe report %u failed: %d\n",
			       rep->report_id, err);
			continue;
		}

		printk("Subscribed to input report %u\n", rep->report_id);
		subscribed++;
	}

	return subscribed > 0 ? 0 : -EIO;
}

void xbox_hids_release(struct xbox_hids *hids)
{
	uint8_t i;

	if (hids->conn) {
		for (i = 0; i < hids->report_count; i++) {
			(void)bt_gatt_unsubscribe(hids->conn,
						  &hids->reports[i].subscribe);
		}

		bt_conn_unref(hids->conn);
	}

	memset(hids, 0, sizeof(*hids));
}
