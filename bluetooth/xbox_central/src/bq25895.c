/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "bq25895.h"

#include <errno.h>
#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include "hub_log.h"

HUB_LOG_MODULE_DEFINE(HUB_MOD_BQ);
#include <zephyr/sys/util.h>

/* Register map (subset used for status reporting / bring-up). */
#define BQ25895_REG_IINLIM     0x00 /* EN_HIZ / EN_ILIM / IINLIM */
#define BQ25895_REG_ADC_CTRL   0x02 /* CONV_RATE / ICO / HVDCP / MAXC / DPDM */
#define BQ25895_REG_SYS_CTRL   0x03 /* OTG_CONFIG / CHG_CONFIG */
#define BQ25895_REG_ICHG       0x04 /* ICHG[6:0], 64 mA/step */
#define BQ25895_REG_IPRETERM   0x05 /* IPRECHG[7:4] / ITERM[3:0], 64 mA/step */
#define BQ25895_REG_CHG_VOLT   0x06 /* VREG / BATLOWV / VRECHG */
#define BQ25895_REG_TIMER      0x07 /* EN_TERM / WATCHDOG / EN_TIMER */
#define BQ25895_REG_CHG_STATUS 0x0B /* VBUS_STAT / CHRG_STAT / PG_STAT */
#define BQ25895_REG_FAULT      0x0C
#define BQ25895_REG_BATV       0x0E /* THERM_STAT[7], BATV[6:0] */
#define BQ25895_REG_SYSV       0x0F /* SYSV[6:0] */
#define BQ25895_REG_VBUSV      0x11 /* VBUS_GD[7], VBUSV[6:0] */
#define BQ25895_REG_ICHGR      0x12 /* ICHGR[6:0] */

#define BQ25895_ADC_CONV_RATE  BIT(6) /* 1 = continuous 1 Hz ADC */
#define BQ25895_ICO_EN         BIT(4)
#define BQ25895_HVDCP_EN       BIT(3)
#define BQ25895_MAXC_EN        BIT(2)
#define BQ25895_OTG_CONFIG     BIT(5)
#define BQ25895_CHG_CONFIG     BIT(4)
#define BQ25895_VRECHG         BIT(0) /* 1 = 200 mV recharge threshold */
#define BQ25895_WATCHDOG_MASK  (BIT(5) | BIT(4))
#define BQ25895_EN_ILIM        BIT(6)
#define BQ25895_IINLIM_MASK    0x3FU
#define BQ25895_ICHG_MASK      0x7FU
#define BQ25895_IPRECHG_SHIFT  4
#define BQ25895_IPRETERM_MASK  0xFFU

/* Current programming (datasheet). */
#define BQ25895_ICHG_STEP_MA      64U
#define BQ25895_ICHG_MAX_MA       3008U
#define BQ25895_IPRETERM_STEP_MA  64U
#define BQ25895_IPRETERM_MAX_MA   1024U
#define BQ25895_IINLIM_OFFSET_MA  100U
#define BQ25895_IINLIM_STEP_MA    50U

/*
 * Battery profile — default 200 mAh LiPo @ ~0.5C.
 * Override via Kconfig (menuconfig / prj.conf) when swapping packs, e.g.:
 *   CONFIG_BQ25895_BATT_CAPACITY_MAH=1000
 */
#ifndef CONFIG_BQ25895_BATT_CAPACITY_MAH
#define CONFIG_BQ25895_BATT_CAPACITY_MAH 200
#endif
#ifndef CONFIG_BQ25895_CHARGE_RATE_MILLIC
#define CONFIG_BQ25895_CHARGE_RATE_MILLIC 500
#endif

#define BQ25895_BATT_CAPACITY_MAH ((uint32_t)CONFIG_BQ25895_BATT_CAPACITY_MAH)
#define BQ25895_CHARGE_RATE_MILLIC ((uint32_t)CONFIG_BQ25895_CHARGE_RATE_MILLIC)

/* USB500-class input limit — enough headroom for small ICHG + system load. */
#define BQ25895_IINLIM_TARGET_MA  500U

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

/** Round @p ma up to @p step, clamp to [step, max]. */
static uint16_t round_current_ma(uint32_t ma, uint16_t step, uint16_t max_ma)
{
	uint32_t rounded;

	if (ma < step) {
		return step;
	}
	rounded = ((ma + (step / 2U)) / step) * step;
	if (rounded < step) {
		rounded = step;
	}
	if (rounded > max_ma) {
		rounded = max_ma;
	}
	return (uint16_t)rounded;
}

static uint16_t profile_ichg_ma(void)
{
	uint32_t target =
		(BQ25895_BATT_CAPACITY_MAH * BQ25895_CHARGE_RATE_MILLIC) / 1000U;

	return round_current_ma(target, BQ25895_ICHG_STEP_MA, BQ25895_ICHG_MAX_MA);
}

/** Termination / precharge ≈ 0.1C, floored at chip minimum 64 mA. */
static uint16_t profile_iterm_ma(void)
{
	uint32_t target = (BQ25895_BATT_CAPACITY_MAH + 9U) / 10U;

	return round_current_ma(target, BQ25895_IPRETERM_STEP_MA,
				BQ25895_IPRETERM_MAX_MA);
}

static uint8_t encode_ichg(uint16_t ma)
{
	return (uint8_t)((ma / BQ25895_ICHG_STEP_MA) & BQ25895_ICHG_MASK);
}

static uint8_t encode_ipreterm_nibble(uint16_t ma)
{
	uint16_t code;

	if (ma < BQ25895_IPRETERM_STEP_MA) {
		ma = BQ25895_IPRETERM_STEP_MA;
	}
	code = (uint16_t)(ma / BQ25895_IPRETERM_STEP_MA);
	if (code == 0U) {
		code = 1U;
	}
	if (code > 16U) {
		code = 16U;
	}
	/* REG05: field value N programs (N+1)*64 mA → store N = code-1. */
	return (uint8_t)(code - 1U);
}

static uint8_t encode_iinlim(uint16_t ma)
{
	uint16_t code;

	if (ma < BQ25895_IINLIM_OFFSET_MA) {
		ma = BQ25895_IINLIM_OFFSET_MA;
	}
	code = (uint16_t)((ma - BQ25895_IINLIM_OFFSET_MA) / BQ25895_IINLIM_STEP_MA);
	if (code > BQ25895_IINLIM_MASK) {
		code = BQ25895_IINLIM_MASK;
	}
	return (uint8_t)code;
}


#if DT_HAS_ALIAS(battery_charger)

#define BQ25895_NODE DT_ALIAS(battery_charger)
#define BQ25895_HAS_INT DT_NODE_HAS_PROP(BQ25895_NODE, int_gpios)

/* Coalesce DPDM / multi-fault INT bursts before touching I2C. */
#define BQ25895_INT_DEBOUNCE_MS 30

static const struct i2c_dt_spec charger = I2C_DT_SPEC_GET(BQ25895_NODE);

static bq25895_event_cb_t event_cb;
static bool irq_armed;

#if BQ25895_HAS_INT
static const struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_GET(BQ25895_NODE, int_gpios);
static struct gpio_callback int_cb_data;
static struct k_work_delayable int_work;

static void bq25895_int_work_handler(struct k_work *work)
{
	bq25895_event_cb_t cb = event_cb;

	ARG_UNUSED(work);

	if (cb != NULL) {
		cb();
	}
}

static void bq25895_int_isr(const struct device *port, struct gpio_callback *cb,
			    uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	(void)k_work_reschedule(&int_work, K_MSEC(BQ25895_INT_DEBOUNCE_MS));
}

static int bq25895_irq_init(void)
{
	int err;

	if (!gpio_is_ready_dt(&int_gpio)) {
		HUB_ERR("BQ25895 INT GPIO not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&int_gpio, GPIO_INPUT);
	if (err) {
		HUB_ERR("BQ25895 INT configure failed: %d\n", err);
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (err) {
		HUB_ERR("BQ25895 INT irq configure failed: %d\n", err);
		return err;
	}

	k_work_init_delayable(&int_work, bq25895_int_work_handler);
	gpio_init_callback(&int_cb_data, bq25895_int_isr, BIT(int_gpio.pin));
	err = gpio_add_callback(int_gpio.port, &int_cb_data);
	if (err) {
		HUB_ERR("BQ25895 INT callback add failed: %d\n", err);
		return err;
	}

	irq_armed = true;
	HUB_INF("BQ25895 INT on %s.%u (edge-to-active, %u ms debounce)\n",
		int_gpio.port->name, int_gpio.pin, BQ25895_INT_DEBOUNCE_MS);
	return 0;
}
#else /* !BQ25895_HAS_INT */
static int bq25895_irq_init(void)
{
	HUB_WRN("BQ25895 int-gpios missing — IRQ disabled\n");
	irq_armed = false;
	return 0;
}
#endif /* BQ25895_HAS_INT */

bool bq25895_available(void)
{
	return device_is_ready(charger.bus);
}

bool bq25895_irq_ready(void)
{
	return irq_armed;
}

void bq25895_set_event_cb(bq25895_event_cb_t cb)
{
	event_cb = cb;
}

/*
 * Defaults leave HVDCP/MaxCharge/ICO on. On a plain 5 V USB/bench supply that
 * causes PUMPX current pulses (VBUS dips). Keep OTG/boost enabled so battery
 * can power the onboard rail via PMID when VBUS is absent.
 *
 * Charge currents follow the LiPo capacity profile (see Kconfig /
 * BQ25895_BATT_CAPACITY_MAH) so a 200 mAh cell is not hit with the chip's
 * default ~2 A ICHG.
 */
static int bq25895_apply_safe_config(void)
{
	uint8_t reg;
	uint16_t ichg_ma;
	uint16_t iterm_ma;
	int err;

	ichg_ma = profile_ichg_ma();
	iterm_ma = profile_iterm_ma();

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_ADC_CTRL, &reg);
	if (err) {
		return err;
	}
	reg |= BQ25895_ADC_CONV_RATE;
	reg &= (uint8_t)~(BQ25895_ICO_EN | BQ25895_HVDCP_EN | BQ25895_MAXC_EN);
	err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_ADC_CTRL, reg);
	if (err) {
		return err;
	}

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_SYS_CTRL, &reg);
	if (err) {
		return err;
	}
	/* Charge on; keep OTG/boost on so BAT can supply onboard rail via PMID. */
	reg |= BQ25895_CHG_CONFIG | BQ25895_OTG_CONFIG;
	err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_SYS_CTRL, reg);
	if (err) {
		return err;
	}

	/* REG00: keep EN_ILIM, set IINLIM (USB500). D+/D- may overwrite later. */
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_IINLIM, &reg);
	if (err) {
		return err;
	}
	reg = (uint8_t)((reg & (uint8_t)~BQ25895_IINLIM_MASK) |
			encode_iinlim(BQ25895_IINLIM_TARGET_MA) | BQ25895_EN_ILIM);
	err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_IINLIM, reg);
	if (err) {
		return err;
	}

	/* REG04: fast-charge current from capacity profile. */
	err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_ICHG, encode_ichg(ichg_ma));
	if (err) {
		return err;
	}

	/*
	 * REG05: IPRECHG + ITERM. Chip minimum is 64 mA (~0.32C on 200 mAh);
	 * that is the gentlest termination the BQ25895 allows.
	 */
	reg = (uint8_t)((encode_ipreterm_nibble(iterm_ma) << BQ25895_IPRECHG_SHIFT) |
			encode_ipreterm_nibble(iterm_ma));
	err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_IPRETERM, reg);
	if (err) {
		return err;
	}

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_CHG_VOLT, &reg);
	if (err) {
		return err;
	}
	/* 200 mV recharge hysteresis — reduces done/charging chatter near VRECHG. */
	if ((reg & BQ25895_VRECHG) == 0U) {
		reg |= BQ25895_VRECHG;
		err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_CHG_VOLT, reg);
		if (err) {
			return err;
		}
	}

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_TIMER, &reg);
	if (err) {
		return err;
	}
	/* Disable I2C watchdog so a POR/default restore cannot re-enable HVDCP/OTG. */
	if ((reg & BQ25895_WATCHDOG_MASK) != 0U) {
		reg &= (uint8_t)~BQ25895_WATCHDOG_MASK;
		err = i2c_reg_write_byte_dt(&charger, BQ25895_REG_TIMER, reg);
		if (err) {
			return err;
		}
	}

	HUB_INF("BQ25895 batt %u mAh @ %u mC → ICHG=%u mA ITERM/IPRE=%u mA IINLIM=%u mA\n",
		(unsigned int)BQ25895_BATT_CAPACITY_MAH,
		(unsigned int)BQ25895_CHARGE_RATE_MILLIC, ichg_ma, iterm_ma,
		BQ25895_IINLIM_TARGET_MA);
	return 0;
}

/* Cheap check: only rewrite when POR/default bits came back. */
static int bq25895_ensure_safe_config(void)
{
	uint8_t reg02;
	uint8_t reg03;
	uint8_t reg04;
	uint8_t expect_ichg;
	int err;

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_ADC_CTRL, &reg02);
	if (err) {
		return err;
	}
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_SYS_CTRL, &reg03);
	if (err) {
		return err;
	}
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_ICHG, &reg04);
	if (err) {
		return err;
	}

	expect_ichg = encode_ichg(profile_ichg_ma());

	if (((reg02 & BQ25895_ADC_CONV_RATE) == 0U) ||
	    ((reg02 & (BQ25895_ICO_EN | BQ25895_HVDCP_EN | BQ25895_MAXC_EN)) != 0U) ||
	    ((reg03 & BQ25895_OTG_CONFIG) == 0U) ||
	    ((reg03 & BQ25895_CHG_CONFIG) == 0U) ||
	    ((reg04 & BQ25895_ICHG_MASK) != expect_ichg)) {
		HUB_WRN("BQ25895 defaults restored — reapplying safe config\n");
		return bq25895_apply_safe_config();
	}

	return 0;
}

int bq25895_init(void)
{
	uint8_t reg;
	int err;

	if (!device_is_ready(charger.bus)) {
		HUB_ERR("BQ25895 I2C bus not ready\n");
		return -ENODEV;
	}

	HUB_INF("BQ25895 probe on %s addr 0x%02x\n", charger.bus->name, charger.addr);

	/* Probe: a successful register read means the charger ACKs. */
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_CHG_STATUS, &reg);
	if (err) {
		HUB_ERR("BQ25895 not responding at 0x%02x (err %d)\n",
		       charger.addr, err);
		bq25895_i2c_scan();
		return -ENODEV;
	}

	err = bq25895_apply_safe_config();
	if (err) {
		HUB_ERR("BQ25895 safe config failed (err %d)\n", err);
		return err;
	}

	err = bq25895_irq_init();
	if (err) {
		return err;
	}

	/* Clear any latched fault so subsequent INT pulses are not blocked. */
	(void)i2c_reg_read_byte_dt(&charger, BQ25895_REG_FAULT, &reg);
	(void)i2c_reg_read_byte_dt(&charger, BQ25895_REG_FAULT, &reg);

	HUB_INF("BQ25895 ready (HVDCP/MAXC/ICO off, OTG on, WD off, VRECHG=200mV, "
		"profile %umAh)\n",
		(unsigned int)BQ25895_BATT_CAPACITY_MAH);
	return 0;
}

int bq25895_read(struct bq25895_status *out)
{
	uint8_t status_reg;
	uint8_t reg;
	uint8_t fault_live;
	int err;

	if (out == NULL) {
		return -EINVAL;
	}

	/* Re-apply if a POR restored chip defaults (HVDCP/OTG back on). */
	(void)bq25895_ensure_safe_config();

	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_CHG_STATUS, &status_reg);
	if (err) {
		return err;
	}
	out->vbus_state = (enum bq25895_vbus_state)((status_reg >> 5) & 0x07U);
	out->charge_state = (enum bq25895_charge_state)((status_reg >> 3) & 0x03U);
	out->power_good = (status_reg & BIT(2)) != 0U;

	/* 1st REG0C = latched since last read (what caused INT); 2nd = live. */
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_FAULT, &out->fault);
	if (err) {
		return err;
	}
	err = i2c_reg_read_byte_dt(&charger, BQ25895_REG_FAULT, &fault_live);
	if (err) {
		return err;
	}
	ARG_UNUSED(fault_live);

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

int bq25895_reg_read(uint8_t reg, uint8_t *value)
{
	if (value == NULL || reg > 0x14U) {
		return -EINVAL;
	}
	if (!device_is_ready(charger.bus)) {
		return -ENODEV;
	}
	return i2c_reg_read_byte_dt(&charger, reg, value);
}

int bq25895_reg_write(uint8_t reg, uint8_t value)
{
	if (reg > 0x14U) {
		return -EINVAL;
	}
	if (!device_is_ready(charger.bus)) {
		return -ENODEV;
	}
	return i2c_reg_write_byte_dt(&charger, reg, value);
}

int bq25895_reg_dump(uint8_t start, uint8_t end, uint8_t *buf, size_t count)
{
	int err;

	if (buf == NULL || start > end || end > 0x14U) {
		return -EINVAL;
	}
	if (count < (size_t)(end - start + 1U)) {
		return -ENOMEM;
	}
	if (!device_is_ready(charger.bus)) {
		return -ENODEV;
	}

	for (uint8_t reg = start; reg <= end; reg++) {
		err = i2c_reg_read_byte_dt(&charger, reg, &buf[reg - start]);
		if (err) {
			return err;
		}
	}
	return 0;
}

void bq25895_log_dump(void)
{
	uint8_t regs[0x15];
	uint8_t reg0b;
	uint8_t fault;
	uint8_t chrg;
	uint8_t ntc;
	int err;

	err = bq25895_reg_dump(0x00, 0x14, regs, sizeof(regs));
	if (err) {
		HUB_FORCE("BQ25895 dump failed: %d\n", err);
		return;
	}

	HUB_FORCE("==== BQ25895 REG00..REG14 ====\n");
	for (uint8_t reg = 0; reg <= 0x14U; reg++) {
		HUB_FORCE("  REG%02X = 0x%02x\n", reg, regs[reg]);
	}

	reg0b = regs[0x0B];
	fault = regs[0x0C];
	chrg = (fault >> 4) & 0x03U;
	ntc = fault & 0x07U;

	HUB_FORCE("---- STATUS REG0B=0x%02x ----\n", reg0b);
	HUB_FORCE("  VBUS_STAT=%u (%s) CHRG_STAT=%u (%s) PG=%u\n",
	       (reg0b >> 5) & 0x07U,
	       bq25895_vbus_state_str((enum bq25895_vbus_state)((reg0b >> 5) & 0x07U)),
	       (reg0b >> 3) & 0x03U,
	       bq25895_charge_state_str((enum bq25895_charge_state)((reg0b >> 3) & 0x03U)),
	       (reg0b >> 2) & 0x01U);

	HUB_FORCE("---- FAULT REG0C=0x%02x ----\n", fault);
	if (fault == 0U) {
		HUB_FORCE("  (no fault)\n");
	} else {
		if (fault & BIT(7)) {
			HUB_FORCE("  WATCHDOG_FAULT\n");
		}
		if (fault & BIT(6)) {
			HUB_FORCE("  BOOST/OTG_FAULT\n");
		}
		switch (chrg) {
		case 1:
			HUB_FORCE("  CHRG_FAULT: input (VBUS OVP / bad source)\n");
			break;
		case 2:
			HUB_FORCE("  CHRG_FAULT: thermal shutdown\n");
			break;
		case 3:
			HUB_FORCE("  CHRG_FAULT: safety timer expired\n");
			break;
		default:
			break;
		}
		if (fault & BIT(3)) {
			HUB_FORCE("  BAT_FAULT (OVP)\n");
		}
		switch (ntc) {
		case 1:
			HUB_FORCE("  NTC_FAULT: TS cold\n");
			break;
		case 2:
			HUB_FORCE("  NTC_FAULT: TS hot\n");
			break;
		case 5:
			HUB_FORCE("  NTC_FAULT: TS cold (boost)\n");
			break;
		case 6:
			HUB_FORCE("  NTC_FAULT: TS hot (boost)\n");
			break;
		default:
			break;
		}
	}
	HUB_FORCE("Hint: STAT ~1Hz blink usually means REG0C fault (NTC/TS common).\n");
	HUB_FORCE("==== end BQ25895 dump ====\n");
}

void bq25895_i2c_scan(void)
{
	uint8_t dst;
	int found = 0;
	int err;

	if (!device_is_ready(charger.bus)) {
		HUB_FORCE("I2C bus not ready for scan\n");
		return;
	}

	HUB_FORCE("I2C scan 0x08..0x77 on %s:\n", charger.bus->name);
	for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
		err = i2c_read(charger.bus, &dst, 1, addr);
		if (err == 0) {
			HUB_FORCE("  ACK at 0x%02x\n", addr);
			found++;
		}
	}
	if (found == 0) {
		HUB_FORCE("  (no devices ACK — check REGN/VBUS power, SDA/SCL "
		       "solder & pull-ups, SCL=P1.11 SDA=P1.12)\n");
	} else {
		HUB_FORCE("  %d device(s) found (BQ25895 should be 0x6a)\n", found);
	}
}

#else /* No BQ25895 in devicetree */

bool bq25895_available(void)
{
	return false;
}

bool bq25895_irq_ready(void)
{
	return false;
}

void bq25895_set_event_cb(bq25895_event_cb_t cb)
{
	ARG_UNUSED(cb);
}

int bq25895_init(void)
{
	HUB_ERR("No BQ25895 charger in devicetree\n");
	return -ENODEV;
}

int bq25895_read(struct bq25895_status *out)
{
	ARG_UNUSED(out);
	return -ENODEV;
}

int bq25895_reg_read(uint8_t reg, uint8_t *value)
{
	ARG_UNUSED(reg);
	ARG_UNUSED(value);
	return -ENODEV;
}

int bq25895_reg_write(uint8_t reg, uint8_t value)
{
	ARG_UNUSED(reg);
	ARG_UNUSED(value);
	return -ENODEV;
}

int bq25895_reg_dump(uint8_t start, uint8_t end, uint8_t *buf, size_t count)
{
	ARG_UNUSED(start);
	ARG_UNUSED(end);
	ARG_UNUSED(buf);
	ARG_UNUSED(count);
	return -ENODEV;
}

void bq25895_log_dump(void)
{
	HUB_FORCE("BQ25895 not in devicetree\n");
}

void bq25895_i2c_scan(void)
{
	HUB_FORCE("BQ25895 not in devicetree\n");
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
