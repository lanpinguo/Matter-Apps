/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bq25895.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

/* Register map (subset used for status reporting). */
#define BQ25895_REG_ADC_CTRL   0x02 /* CONV_START[7], CONV_RATE[6] */
#define BQ25895_REG_WD_CTRL    0x03 /* WD_RST[6] */
#define BQ25895_REG_CHG_STATUS 0x0B /* VBUS_STAT / CHRG_STAT / PG_STAT */
#define BQ25895_REG_FAULT      0x0C
#define BQ25895_REG_BATV       0x0E /* THERM_STAT[7], BATV[6:0] */
#define BQ25895_REG_SYSV       0x0F /* SYSV[6:0] */
#define BQ25895_REG_VBUSV      0x11 /* VBUS_GD[7], VBUSV[6:0] */
#define BQ25895_REG_ICHGR      0x12 /* ICHGR[6:0] */

#define BQ25895_ADC_CONV_RATE  BIT(6) /* 1 = continuous 1 Hz ADC */
#define BQ25895_WD_RST         BIT(6)

/* ADC scaling per datasheet. */
#define BQ25895_BATV_OFFSET_MV  2304U
#define BQ25895_BATV_STEP_MV    20U
#define BQ25895_SYSV_OFFSET_MV  2304U
#define BQ25895_SYSV_STEP_MV    20U
#define BQ25895_VBUSV_OFFSET_MV 2600U
#define BQ25895_VBUSV_STEP_MV   100U
#define BQ25895_ICHGR_STEP_MA   50U

/* Rough single-cell Li-ion SoC end points. */
#define BQ25895_BATT_EMPTY_MV 3300U
#define BQ25895_BATT_FULL_MV  4200U

static uint8_t soc_from_mv(uint16_t mv)
{
	if (mv <= BQ25895_BATT_EMPTY_MV) {
		return 0U;
	}
	if (mv >= BQ25895_BATT_FULL_MV) {
		return 100U;
	}
	return (uint8_t)(((uint32_t)(mv - BQ25895_BATT_EMPTY_MV) * 100U) /
			 (BQ25895_BATT_FULL_MV - BQ25895_BATT_EMPTY_MV));
}

#if DT_HAS_ALIAS(battery_charger)

static const struct i2c_dt_spec charger = I2C_DT_SPEC_GET(DT_ALIAS(battery_charger));

bool bq25895_available(void)
{
	return device_is_ready(charger.bus);
}

static int bq25895_enable_adc(void)
{
	uint8_t reg;
	int err;

	/* Keep the ADC in continuous mode so readings stay fresh. */
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_ADC_CTRL, &reg);
	if (err) {
		return err;
	}

	if ((reg & BQ25895_ADC_CONV_RATE) == 0U) {
		reg |= BQ25895_ADC_CONV_RATE;
		err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_ADC_CTRL, reg);
		if (err) {
			return err;
		}
	}

	/* Kick the charger watchdog so it does not reset our ADC settings. */
	(void)i2c_reg_update_byte_dt(&charger, BQ25895_REG_WD_CTRL,
				     BQ25895_WD_RST, BQ25895_WD_RST);
	return 0;
}

int bq25895_init(void)
{
	uint8_t reg;
	int err;

	if (!device_is_ready(charger.bus)) {
		printk("BQ25895 I2C bus not ready\n");
		return -ENODEV;
	}

	/* Probe: a successful register read means the charger ACKs. */
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_CHG_STATUS, &reg);
	if (err) {
		printk("BQ25895 not responding at 0x%02x (err %d)\n",
		       charger.addr, err);
		return -ENODEV;
	}

	err = bq25895_enable_adc();
	if (err) {
		printk("BQ25895 ADC enable failed (err %d)\n", err);
		return err;
	}

	printk("BQ25895 charger ready on %s addr 0x%02x\n",
	       charger.bus->name, charger.addr);
	return 0;
}

int bq25895_read(struct bq25895_status *out)
{
	uint8_t status_reg;
	uint8_t reg;
	int err;

	if (out == NULL) {
		return -EINVAL;
	}

	/* Make sure the ADC stays continuous even after a watchdog reset. */
	(void)bq25895_enable_adc();

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_CHG_STATUS, &status_reg);
	if (err) {
		return err;
	}
	out->vbus_state = (enum bq25895_vbus_state)((status_reg >> 5) & 0x07U);
	out->charge_state = (enum bq25895_charge_state)((status_reg >> 3) & 0x03U);
	out->power_good = (status_reg & BIT(2)) != 0U;

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_FAULT, &out->fault);
	if (err) {
		return err;
	}

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_BATV, &reg);
	if (err) {
		return err;
	}
	out->batt_mv = BQ25895_BATV_OFFSET_MV +
		       (uint16_t)(reg & 0x7FU) * BQ25895_BATV_STEP_MV;
	out->battery_pct = soc_from_mv(out->batt_mv);

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_SYSV, &reg);
	if (err) {
		return err;
	}
	out->sys_mv = BQ25895_SYSV_OFFSET_MV +
		      (uint16_t)(reg & 0x7FU) * BQ25895_SYSV_STEP_MV;

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_VBUSV, &reg);
	if (err) {
		return err;
	}
	out->vbus_present = (reg & BIT(7)) != 0U;
	if ((reg & 0x7FU) != 0U) {
		out->vbus_mv = BQ25895_VBUSV_OFFSET_MV +
			       (uint16_t)(reg & 0x7FU) * BQ25895_VBUSV_STEP_MV;
	} else {
		out->vbus_mv = 0U;
	}

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_ICHGR, &reg);
	if (err) {
		return err;
	}
	out->charge_ma = (uint16_t)(reg & 0x7FU) * BQ25895_ICHGR_STEP_MA;

	return 0;
}

#else /* No BQ25895 in devicetree */

bool bq25895_available(void)
{
	return false;
}

int bq25895_init(void)
{
	printk("No BQ25895 charger in devicetree\n");
	return -ENODEV;
}

int bq25895_read(struct bq25895_status *out)
{
	ARG_UNUSED(out);
	return -ENODEV;
}

#endif /* DT_HAS_ALIAS(battery_charger) */

const char *bq25895_charge_state_str(enum bq25895_charge_state state)
{
	switch (state) {
	case BQ25895_CHG_PRE_CHARGE:
		return "pre-charge";
	case BQ25895_CHG_FAST_CHARGING:
		return "charging";
	case BQ25895_CHG_DONE:
		return "done";
	default:
		return "not-charging";
	}
}

const char *bq25895_vbus_state_str(enum bq25895_vbus_state state)
{
	switch (state) {
	case BQ25895_VBUS_USB_SDP:
		return "USB-SDP";
	case BQ25895_VBUS_USB_CDP:
		return "USB-CDP";
	case BQ25895_VBUS_USB_DCP:
		return "USB-DCP";
	case BQ25895_VBUS_MAXCHARGE:
		return "MaxCharge";
	case BQ25895_VBUS_UNKNOWN:
		return "adapter";
	case BQ25895_VBUS_NONSTANDARD:
		return "non-std";
	case BQ25895_VBUS_OTG:
		return "OTG";
	default:
		return "none";
	}
}
