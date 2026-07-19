/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * INA226-based lead-acid battery charge monitor interface.
 */

#ifndef BATTERY_MONITOR_H_
#define BATTERY_MONITOR_H_

#include <stdbool.h>
#include <stdint.h>

/** One battery measurement snapshot. */
struct battery_reading {
	int32_t voltage_mv;   /* Bus voltage, millivolts (always >= 0). */
	int32_t current_ma;   /* Signed current; > 0 = charging (per wiring). */
	int32_t power_mw;     /* Power magnitude, milliwatts. */
};

enum battery_charge_state {
	BATTERY_STATE_IDLE = 0,
	BATTERY_STATE_CHARGING,
	BATTERY_STATE_DISCHARGING,
};

/** True when an INA226 exists in the devicetree and the device is ready. */
bool battery_monitor_available(void);

/** Probe and initialize the INA226. Returns 0 on success (or when absent). */
int battery_monitor_init(void);

/** Fetch a fresh voltage/current/power sample. Returns 0 on success. */
int battery_monitor_read(struct battery_reading *out);

/** Derive charge state from the signed current. */
enum battery_charge_state battery_monitor_state(const struct battery_reading *r);

/** Human-readable charge state ("charging" / "discharging" / "idle"). */
const char *battery_state_str(enum battery_charge_state state);

#endif /* BATTERY_MONITOR_H_ */
