/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * Minimal register-level driver for the TI BQ25895 battery charger /
 * power-path manager. Exposes power-supply and charge status over I2C.
 */

#ifndef BQ25895_H_
#define BQ25895_H_

#include <stddef.h>
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
	uint8_t fault;      /* REG0C latched fault (1st read) */
};

/** Called from system workqueue after an INT pulse (coalesced). */
typedef void (*bq25895_event_cb_t)(void);

/** True when a BQ25895 exists in the devicetree and answers on I2C. */
bool bq25895_available(void);

/**
 * Probe the charger, apply safe defaults, and (if int-gpios present) arm INT.
 * Returns 0 on success.
 */
int bq25895_init(void);

/**
 * Register a callback invoked after INT (falling edge, debounced).
 * Pass NULL to unregister. Safe to call before or after bq25895_init().
 */
void bq25895_set_event_cb(bq25895_event_cb_t cb);

/** True when DT provides int-gpios and IRQ was armed successfully. */
bool bq25895_irq_ready(void);

/** Read a full status snapshot. Returns 0 on success. */
int bq25895_read(struct bq25895_status *out);

/** Read one 8-bit register (0x00..0x14). Returns 0 on success. */
int bq25895_reg_read(uint8_t reg, uint8_t *value);

/** Write one 8-bit register. Returns 0 on success. */
int bq25895_reg_write(uint8_t reg, uint8_t value);

/**
 * Dump registers [start..end] inclusive into @p buf.
 * @p count must be >= (end - start + 1). Returns 0 on success.
 */
int bq25895_reg_dump(uint8_t start, uint8_t end, uint8_t *buf, size_t count);

/** Print full REG00..REG14 dump + STATUS/FAULT decode via printk. */
void bq25895_log_dump(void);

/** Scan the charger I2C bus and print addresses that ACK. */
void bq25895_i2c_scan(void);

/** Human-readable charge / input state strings. */
const char *bq25895_charge_state_str(enum bq25895_charge_state state);
const char *bq25895_vbus_state_str(enum bq25895_vbus_state state);

#endif /* BQ25895_H_ */
