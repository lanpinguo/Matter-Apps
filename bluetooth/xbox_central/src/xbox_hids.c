/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "xbox_hids.h"

#include <string.h>

#include <bluetooth/services/hids.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/printk.h>

enum xbox_hids_activate_step {
	XBOX_HIDS_ACTIVATE_REPORT_MAP,
	XBOX_HIDS_ACTIVATE_DONE,
};

static struct {
	struct xbox_hids *hids;
	xbox_hids_ready_cb ready_cb;
	void *user_data;
	enum xbox_hids_activate_step step;
	struct bt_gatt_read_params read_params;
} activate_ctx;

static uint8_t activate_read_cb(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_read_params *params,
				const void *data, uint16_t length);

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

		if (bt_uuid_cmp(chrc_val->uuid, BT_UUID_HIDS_INFO) == 0) {
			uint16_t handle = chrc_value_handle_get(dm, chrc);

			if (handle != 0U) {
				hids->info_handle = handle;
				hids->info_valid = true;
				printk("Found HID information val=0x%04x\n", handle);
			}
			continue;
		}

		if (bt_uuid_cmp(chrc_val->uuid, BT_UUID_HIDS_REPORT_MAP) == 0) {
			uint16_t handle = chrc_value_handle_get(dm, chrc);

			if (handle != 0U) {
				hids->report_map_handle = handle;
				hids->report_map_valid = true;
				printk("Found HID report map val=0x%04x\n", handle);
			}
			continue;
		}

		if (bt_uuid_cmp(chrc_val->uuid, BT_UUID_HIDS_CTRL_POINT) == 0) {
			uint16_t handle = chrc_value_handle_get(dm, chrc);

			if (handle != 0U) {
				hids->ctrl_point_handle = handle;
				hids->ctrl_point_valid = true;
				printk("Found HID control point val=0x%04x\n", handle);
			}
			continue;
		}

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

static void activate_finish(int err)
{
	struct xbox_hids *hids = activate_ctx.hids;
	xbox_hids_ready_cb cb = activate_ctx.ready_cb;
	void *user_data = activate_ctx.user_data;

	memset(&activate_ctx, 0, sizeof(activate_ctx));

	if (cb != NULL) {
		cb(err, user_data);
	}

	ARG_UNUSED(hids);
}

static int activate_read_start(uint16_t handle, const char *label)
{
	int err;

	if (handle == 0U) {
		return -EINVAL;
	}

	activate_ctx.read_params.func = activate_read_cb;
	activate_ctx.read_params.handle_count = 1;
	activate_ctx.read_params.single.handle = handle;
	activate_ctx.read_params.single.offset = 0;

	err = bt_gatt_read(activate_ctx.hids->conn, &activate_ctx.read_params);
	if (err != 0) {
		printk("HID %s read start failed: %d\n", label, err);
	}

	return err;
}

static void activate_subscribe_finish(void)
{
	int err;

	(void)xbox_hids_exit_suspend(activate_ctx.hids);
	err = xbox_hids_subscribe_all(activate_ctx.hids);
	if (err == 0) {
		printk("HID activate done\n");
	}
	activate_finish(err);
}

static int activate_step_handle_get(uint16_t *handle, const char **label)
{
	switch (activate_ctx.step) {
	case XBOX_HIDS_ACTIVATE_REPORT_MAP:
		*label = "report map";
		if (activate_ctx.hids->report_map_valid) {
			*handle = activate_ctx.hids->report_map_handle;
		}
		break;
	default:
		*label = "characteristic";
		break;
	}

	return (*handle != 0U) ? 0 : -ENOENT;
}

static uint8_t activate_read_cb(struct bt_conn *conn, uint8_t err,
				struct bt_gatt_read_params *params,
				const void *data, uint16_t length)
{
	const char *label = "characteristic";
	uint16_t handle = 0U;
	int rd_err;

	ARG_UNUSED(conn);
	ARG_UNUSED(params);
	ARG_UNUSED(data);
	ARG_UNUSED(length);

	if (err != 0) {
		printk("HID activate read step %u failed: %u\n", activate_ctx.step, err);
	}

	activate_ctx.step++;
	while (activate_ctx.step < XBOX_HIDS_ACTIVATE_DONE) {
		rd_err = activate_step_handle_get(&handle, &label);
		if (rd_err == 0) {
			return activate_read_start(handle, label) == 0 ?
				BT_GATT_ITER_CONTINUE : BT_GATT_ITER_STOP;
		}

		activate_ctx.step++;
	}

	activate_subscribe_finish();
	return BT_GATT_ITER_STOP;
}

static int activate_read_next(void)
{
	const char *label = "characteristic";
	uint16_t handle = 0U;
	int rd_err;

	while (activate_ctx.step < XBOX_HIDS_ACTIVATE_DONE) {
		rd_err = activate_step_handle_get(&handle, &label);
		if (rd_err == 0) {
			return activate_read_start(handle, label);
		}

		activate_ctx.step++;
	}

	activate_subscribe_finish();
	return 0;
}

int xbox_hids_activate(struct xbox_hids *hids, xbox_hids_ready_cb cb, void *user_data)
{
	int err;

	if (hids == NULL || hids->conn == NULL || hids->report_count == 0U) {
		return -EINVAL;
	}

	if (activate_ctx.hids != NULL) {
		return -EBUSY;
	}

	activate_ctx.hids = hids;
	activate_ctx.ready_cb = cb;
	activate_ctx.user_data = user_data;
	activate_ctx.step = XBOX_HIDS_ACTIVATE_REPORT_MAP;

	err = activate_read_next();
	if (err != 0) {
		activate_finish(err);
	}

	return err;
}

int xbox_hids_exit_suspend(struct xbox_hids *hids)
{
	uint8_t exit_suspend = BT_HIDS_CONTROL_POINT_EXIT_SUSPEND;
	int err;

	if (hids == NULL || !hids->conn || !hids->ctrl_point_valid) {
		return 0;
	}

	err = bt_gatt_write_without_response(hids->conn, hids->ctrl_point_handle,
					     &exit_suspend, sizeof(exit_suspend), false);
	if (err != 0) {
		printk("HID exit suspend failed: %d\n", err);
	}

	return err;
}

void xbox_hids_release(struct xbox_hids *hids)
{
	uint8_t i;

	if (activate_ctx.hids == hids) {
		memset(&activate_ctx, 0, sizeof(activate_ctx));
	}

	if (hids->conn) {
		for (i = 0; i < hids->report_count; i++) {
			(void)bt_gatt_unsubscribe(hids->conn,
						  &hids->reports[i].subscribe);
		}

		bt_conn_unref(hids->conn);
	}

	memset(hids, 0, sizeof(*hids));
}
