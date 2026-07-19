/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "battery_monitor.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* Deadband around 0 A so sensor noise does not toggle the charge state. */
#define BATTERY_CHARGE_THRESHOLD_MA 30

#if DT_HAS_ALIAS(battery_monitor)

static const struct device *const batt_dev = DEVICE_DT_GET(DT_ALIAS(battery_monitor));

bool battery_monitor_available(void)
{
	return device_is_ready(batt_dev);
}

int battery_monitor_init(void)
{
	if (!device_is_ready(batt_dev)) {
		printk("INA226 battery monitor not ready\n");
		return -ENODEV;
	}

	printk("INA226 battery monitor ready (%s)\n", batt_dev->name);
	return 0;
}

int battery_monitor_read(struct battery_reading *out)
{
	struct sensor_value val;
	int err;

	if (out == NULL) {
		return -EINVAL;
	}

	err = sensor_sample_fetch(batt_dev);
	if (err) {
		return err;
	}

	err = sensor_channel_get(batt_dev, SENSOR_CHAN_VOLTAGE, &val);
	if (err) {
		return err;
	}
	out->voltage_mv = (int32_t)sensor_value_to_milli(&val);

	err = sensor_channel_get(batt_dev, SENSOR_CHAN_CURRENT, &val);
	if (err) {
		return err;
	}
	out->current_ma = (int32_t)sensor_value_to_milli(&val);

	err = sensor_channel_get(batt_dev, SENSOR_CHAN_POWER, &val);
	if (err) {
		return err;
	}
	out->power_mw = (int32_t)sensor_value_to_milli(&val);

	return 0;
}

#else /* No INA226 in devicetree */

bool battery_monitor_available(void)
{
	return false;
}

int battery_monitor_init(void)
{
	printk("No INA226 battery monitor in devicetree\n");
	return 0;
}

int battery_monitor_read(struct battery_reading *out)
{
	ARG_UNUSED(out);
	return -ENODEV;
}

#endif /* DT_HAS_ALIAS(battery_monitor) */

enum battery_charge_state battery_monitor_state(const struct battery_reading *r)
{
	if (r == NULL) {
		return BATTERY_STATE_IDLE;
	}

	if (r->current_ma > BATTERY_CHARGE_THRESHOLD_MA) {
		return BATTERY_STATE_CHARGING;
	}

	if (r->current_ma < -BATTERY_CHARGE_THRESHOLD_MA) {
		return BATTERY_STATE_DISCHARGING;
	}

	return BATTERY_STATE_IDLE;
}

const char *battery_state_str(enum battery_charge_state state)
{
	switch (state) {
	case BATTERY_STATE_CHARGING:
		return "charging";
	case BATTERY_STATE_DISCHARGING:
		return "discharging";
	default:
		return "idle";
	}
}
