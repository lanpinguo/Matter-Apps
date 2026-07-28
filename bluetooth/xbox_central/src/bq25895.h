/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Minimal register-level driver for the TI BQ25895 battery charger /
 * power-path manager. Exposes power-supply and charge status over I2C.
 */

#ifndef BQ25895_H_
#define BQ25895_H_

#include <stdbool.h>
#include <stdint.h>

/* REG0B CHRG_STAT[1:0] — charging state. */
enum bq25895_charge_state {
	BQ25895_CHG_NOT_CHARGING = 0,
	BQ25895_CHG_PRE_CHARGE = 1,
	BQ25895_CHG_FAST_CHARGING = 2,
	BQ25895_CHG_DONE = 3,
};

/* REG0B VBUS_STAT[2:0] — detected input source. */
enum bq25895_vbus_state {
	BQ25895_VBUS_NONE = 0,
	BQ25895_VBUS_USB_SDP = 1,
	BQ25895_VBUS_USB_CDP = 2,
	BQ25895_VBUS_USB_DCP = 3,
	BQ25895_VBUS_MAXCHARGE = 4,
	BQ25895_VBUS_UNKNOWN = 5,
	BQ25895_VBUS_NONSTANDARD = 6,
	BQ25895_VBUS_OTG = 7,
};

struct bq25895_status {
	uint16_t batt_mv;   /* Battery voltage (BATV) */
	uint16_t sys_mv;    /* System voltage (SYSV) */
	uint16_t vbus_mv;   /* Input voltage (VBUSV), 0 when no input */
	uint16_t charge_ma; /* Charge current (ICHGR) */
	uint8_t battery_pct; /* Rough Li-ion SoC estimate, 0..100 */
	enum bq25895_charge_state charge_state;
	enum bq25895_vbus_state vbus_state;
	bool power_good;    /* REG0B PG_STAT */
	bool vbus_present;  /* REG11 VBUS_GD */
	uint8_t fault;      /* REG0C raw fault register */
};

/** True when a BQ25895 exists in the devicetree and answers on I2C. */
bool bq25895_available(void);

/** Probe the charger and enable continuous ADC. Returns 0 on success. */
int bq25895_init(void);

/** Read a full status snapshot. Returns 0 on success. */
int bq25895_read(struct bq25895_status *out);

/** Human-readable charge / input state strings. */
const char *bq25895_charge_state_str(enum bq25895_charge_state state);
const char *bq25895_vbus_state_str(enum bq25895_vbus_state state);

#endif /* BQ25895_H_ */
