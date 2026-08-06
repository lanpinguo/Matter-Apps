/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 *
 * UART shell helpers for BQ25895 bring-up / fault diagnosis.
 *
 * Console (uart20 @ 115200):
 *   bq dump              — dump REG00..REG14 + decode key fields
 *   bq read <reg>        — read one register (hex or decimal)
 *   bq write <reg> <val> — write one register
 *   bq status            — decoded charge / ADC snapshot
 *
 * STAT pin blinking ~1 Hz usually means a fault is latched in REG0C.
 */

#include "bq25895.h"

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#define BQ_REG_FIRST 0x00U
#define BQ_REG_LAST  0x14U

static int parse_u8(const char *s, uint8_t *out)
{
	char *end = NULL;
	unsigned long v;

	if (s == NULL || out == NULL) {
		return -EINVAL;
	}
	v = strtoul(s, &end, 0);
	if (end == s || *end != '\0' || v > 0xFFUL) {
		return -EINVAL;
	}
	*out = (uint8_t)v;
	return 0;
}

static void decode_fault(const struct shell *sh, uint8_t fault)
{
	uint8_t chrg = (fault >> 4) & 0x03U;
	uint8_t ntc = fault & 0x07U;

	shell_print(sh, "REG0C FAULT = 0x%02x%s", fault, fault ? "" : " (none)");
	if (fault == 0U) {
		return;
	}
	if (fault & BIT(7)) {
		shell_print(sh, "  WATCHDOG_FAULT");
	}
	if (fault & BIT(6)) {
		shell_print(sh, "  BOOST/OTG_FAULT");
	}
	switch (chrg) {
	case 1:
		shell_print(sh, "  CHRG_FAULT: input (VBUS OVP / bad source)");
		break;
	case 2:
		shell_print(sh, "  CHRG_FAULT: thermal shutdown");
		break;
	case 3:
		shell_print(sh, "  CHRG_FAULT: safety timer expired");
		break;
	default:
		break;
	}
	if (fault & BIT(3)) {
		shell_print(sh, "  BAT_FAULT (OVP)");
	}
	switch (ntc) {
	case 1:
		shell_print(sh, "  NTC_FAULT: TS cold");
		break;
	case 2:
		shell_print(sh, "  NTC_FAULT: TS hot");
		break;
	case 5:
		shell_print(sh, "  NTC_FAULT: TS cold (boost)");
		break;
	case 6:
		shell_print(sh, "  NTC_FAULT: TS hot (boost)");
		break;
	default:
		break;
	}
}

static void decode_status_reg(const struct shell *sh, uint8_t reg0b)
{
	uint8_t vbus = (reg0b >> 5) & 0x07U;
	uint8_t chg = (reg0b >> 3) & 0x03U;

	shell_print(sh, "REG0B STATUS = 0x%02x", reg0b);
	shell_print(sh, "  VBUS_STAT=%u (%s)", vbus,
		    bq25895_vbus_state_str((enum bq25895_vbus_state)vbus));
	shell_print(sh, "  CHRG_STAT=%u (%s)", chg,
		    bq25895_charge_state_str((enum bq25895_charge_state)chg));
	shell_print(sh, "  PG_STAT=%u  SDP_STAT=%u  VSYS_STAT=%u",
		    (reg0b >> 2) & 1U, (reg0b >> 1) & 1U, reg0b & 1U);
}

static int cmd_bq_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!bq25895_available()) {
		shell_error(sh, "BQ25895 I2C bus not ready");
		return -ENODEV;
	}

	bq25895_log_dump();
	return 0;
}

static int cmd_bq_scan(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	bq25895_i2c_scan();
	return 0;
}

static int cmd_bq_read(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t reg;
	uint8_t val;
	int err;

	if (argc < 2) {
		shell_error(sh, "usage: bq read <reg>");
		return -EINVAL;
	}
	err = parse_u8(argv[1], &reg);
	if (err || reg > BQ_REG_LAST) {
		shell_error(sh, "reg must be 0x00..0x14");
		return -EINVAL;
	}

	err = bq25895_reg_read(reg, &val);
	if (err) {
		shell_error(sh, "read REG%02X failed: %d", reg, err);
		return err;
	}
	shell_print(sh, "REG%02X = 0x%02x (%u)", reg, val, val);
	return 0;
}

static int cmd_bq_write(const struct shell *sh, size_t argc, char **argv)
{
	uint8_t reg;
	uint8_t val;
	int err;

	if (argc < 3) {
		shell_error(sh, "usage: bq write <reg> <val>");
		return -EINVAL;
	}
	err = parse_u8(argv[1], &reg);
	if (err || reg > BQ_REG_LAST) {
		shell_error(sh, "reg must be 0x00..0x14");
		return -EINVAL;
	}
	err = parse_u8(argv[2], &val);
	if (err) {
		shell_error(sh, "invalid value");
		return -EINVAL;
	}

	err = bq25895_reg_write(reg, val);
	if (err) {
		shell_error(sh, "write REG%02X failed: %d", reg, err);
		return err;
	}
	shell_print(sh, "wrote REG%02X = 0x%02x", reg, val);
	return 0;
}

static int cmd_bq_status(const struct shell *sh, size_t argc, char **argv)
{
	struct bq25895_status st;
	uint8_t reg0b;
	uint8_t fault;
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!bq25895_available()) {
		shell_error(sh, "BQ25895 not available");
		return -ENODEV;
	}

	err = bq25895_read(&st);
	if (err) {
		shell_error(sh, "status read failed: %d", err);
		return err;
	}

	(void)bq25895_reg_read(0x0B, &reg0b);
	(void)bq25895_reg_read(0x0C, &fault);

	shell_print(sh, "BAT=%umV (%u%%) SYS=%umV VBUS=%umV(%s) ICHG=%umA",
		    st.batt_mv, st.battery_pct, st.sys_mv, st.vbus_mv,
		    bq25895_vbus_state_str(st.vbus_state), st.charge_ma);
	shell_print(sh, "charge=%s PG=%d VBUS_GD=%d",
		    bq25895_charge_state_str(st.charge_state),
		    st.power_good ? 1 : 0, st.vbus_present ? 1 : 0);
	decode_status_reg(sh, reg0b);
	decode_fault(sh, fault);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(bq_cmds,
	SHELL_CMD(dump, NULL, "Dump REG00..REG14 and decode status/fault",
		  cmd_bq_dump),
	SHELL_CMD(scan, NULL, "Scan I2C bus for ACKing addresses",
		  cmd_bq_scan),
	SHELL_CMD_ARG(read, NULL, "Read one register: bq read <reg>",
		      cmd_bq_read, 2, 0),
	SHELL_CMD_ARG(write, NULL, "Write one register: bq write <reg> <val>",
		      cmd_bq_write, 3, 0),
	SHELL_CMD(status, NULL, "Decoded charge / ADC snapshot", cmd_bq_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bq, &bq_cmds, "BQ25895 charger debug", NULL);
