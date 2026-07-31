/*
 * Zephyr I2C backend for the Melexis MLX90640 API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "MLX90640_I2C_Driver.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include <errno.h>
#include <string.h>

#if !DT_NODE_EXISTS(DT_NODELABEL(mlx90640))
#error "Devicetree node mlx90640 is required"
#endif

static const struct i2c_dt_spec mlx_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(mlx90640));

/* nRF TWIM prefers bounded chunk sizes for large EEPROM/RAM dumps. */
#define MLX_I2C_CHUNK_WORDS 32U

void MLX90640_I2CInit(void)
{
	/* Bus is ready via DT; nothing else to do. */
}

void MLX90640_I2CFreqSet(int freq)
{
	ARG_UNUSED(freq);
	/* Clock frequency is configured in the board overlay. */
}

int MLX90640_I2CGeneralReset(void)
{
	uint8_t cmd = 0x06;
	struct i2c_msg msg = {
		.buf = &cmd,
		.len = 1,
		.flags = I2C_MSG_WRITE | I2C_MSG_STOP,
	};

	/* General call address 0x00 */
	return i2c_transfer(mlx_i2c.bus, &msg, 1, 0x00);
}

int MLX90640_I2CRead(uint8_t slaveAddr, uint16_t startAddress,
		     uint16_t nMemAddressRead, uint16_t *data)
{
	uint8_t addr_be[2];
	uint8_t raw[MLX_I2C_CHUNK_WORDS * 2U];
	uint16_t remaining = nMemAddressRead;
	uint16_t addr = startAddress;
	uint16_t *out = data;
	int err;

	if (!device_is_ready(mlx_i2c.bus) || data == NULL) {
		return -ENODEV;
	}

	ARG_UNUSED(slaveAddr); /* DT address is authoritative */

	while (remaining > 0U) {
		uint16_t words = MIN(remaining, (uint16_t)MLX_I2C_CHUNK_WORDS);
		uint16_t bytes = words * 2U;

		sys_put_be16(addr, addr_be);

		err = i2c_write_read_dt(&mlx_i2c, addr_be, sizeof(addr_be), raw, bytes);
		if (err) {
			printk("MLX I2C read 0x%04x x%u failed: %d\n", addr, words, err);
			return err;
		}

		for (uint16_t i = 0; i < words; i++) {
			out[i] = sys_get_be16(&raw[i * 2U]);
		}

		out += words;
		addr += words;
		remaining -= words;
	}

	return 0;
}

int MLX90640_I2CWrite(uint8_t slaveAddr, uint16_t writeAddress, uint16_t data)
{
	uint8_t buf[4];
	int err;

	if (!device_is_ready(mlx_i2c.bus)) {
		return -ENODEV;
	}

	ARG_UNUSED(slaveAddr);

	sys_put_be16(writeAddress, &buf[0]);
	sys_put_be16(data, &buf[2]);

	err = i2c_write_dt(&mlx_i2c, buf, sizeof(buf));
	if (err) {
		printk("MLX I2C write 0x%04x failed: %d\n", writeAddress, err);
	}

	return err;
}
