/*
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "mlx90640_sensor.h"

#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <math.h>
#include <string.h>

#if !DT_NODE_EXISTS(DT_NODELABEL(mlx90640))
#error "Devicetree node mlx90640 is required"
#endif

#define MLX90640_ADDR            0x33
#define MLX90640_DEFAULT_REFRESH 0x02 /* 2 Hz */
#define MLX90640_EMISSIVITY      0.95f

static const struct i2c_dt_spec mlx_i2c = I2C_DT_SPEC_GET(DT_NODELABEL(mlx90640));

static paramsMLX90640 s_params;
static uint16_t s_ee[MLX90640_EEPROM_DUMP_NUM];
static uint16_t s_frame[834];
static float s_to[MLX90640_PIXELS];
static uint16_t s_seq;
static bool s_ready;

bool mlx90640_available(void)
{
	return s_ready;
}

int mlx90640_init(void)
{
	int status;

	if (!device_is_ready(mlx_i2c.bus)) {
		printk("MLX90640: I2C bus not ready\n");
		return -ENODEV;
	}

	MLX90640_I2CInit();

	status = MLX90640_DumpEE(MLX90640_ADDR, s_ee);
	if (status != 0) {
		printk("MLX90640: DumpEE failed (%d)\n", status);
		return -EIO;
	}

	status = MLX90640_ExtractParameters(s_ee, &s_params);
	if (status != 0) {
		printk("MLX90640: ExtractParameters failed (%d)\n", status);
		return -EINVAL;
	}

	status = MLX90640_SetChessMode(MLX90640_ADDR);
	if (status != 0) {
		printk("MLX90640: SetChessMode failed (%d)\n", status);
		return -EIO;
	}

	status = MLX90640_SetResolution(MLX90640_ADDR, 0x02); /* 18-bit */
	if (status != 0) {
		printk("MLX90640: SetResolution failed (%d)\n", status);
		return -EIO;
	}

	status = MLX90640_SetRefreshRate(MLX90640_ADDR, MLX90640_DEFAULT_REFRESH);
	if (status != 0) {
		printk("MLX90640: SetRefreshRate failed (%d)\n", status);
		return -EIO;
	}

	s_ready = true;
	printk("MLX90640: ready @ 0x%02x (2 Hz, chess, 18-bit)\n", mlx_i2c.addr);
	return 0;
}

int mlx90640_set_refresh_rate(uint8_t rate_code)
{
	int status;

	if (!s_ready) {
		return -ENODEV;
	}
	if (rate_code > 7U) {
		return -EINVAL;
	}

	status = MLX90640_SetRefreshRate(MLX90640_ADDR, rate_code);
	return (status == 0) ? 0 : -EIO;
}

static mlx_temp_cC_t float_to_cC(float t)
{
	if (t > 327.67f) {
		t = 327.67f;
	}
	if (t < -327.68f) {
		t = -327.68f;
	}
	return (mlx_temp_cC_t)lroundf(t * 100.0f);
}

int mlx90640_capture(struct mlx90640_frame *out)
{
	float ta;
	float tr;
	mlx_temp_cC_t tmin;
	mlx_temp_cC_t tmax;
	int status;
	int subpages = 0;

	if (!s_ready || out == NULL) {
		return -EINVAL;
	}

	/*
	 * Chess mode: RAM keeps the previous subpage. Read two consecutive
	 * subpages, then run CalculateTo once for a complete 32x24 image.
	 */
	while (subpages < 2) {
		status = MLX90640_GetFrameData(MLX90640_ADDR, s_frame);
		if (status < 0) {
			k_msleep(10);
			continue;
		}
		subpages++;
	}

	ta = MLX90640_GetTa(s_frame, &s_params);
	tr = ta - 8.0f; /* reflected-temperature estimate */
	MLX90640_CalculateTo(s_frame, &s_params, MLX90640_EMISSIVITY, tr, s_to);
	MLX90640_BadPixelsCorrection(s_params.brokenPixels, s_to, 1, &s_params);
	MLX90640_BadPixelsCorrection(s_params.outlierPixels, s_to, 1, &s_params);

	tmin = float_to_cC(s_to[0]);
	tmax = tmin;
	for (int i = 0; i < MLX90640_PIXELS; i++) {
		mlx_temp_cC_t v = float_to_cC(s_to[i]);

		out->pixels[i] = v;
		if (v < tmin) {
			tmin = v;
		}
		if (v > tmax) {
			tmax = v;
		}
	}

	out->ta_cC = float_to_cC(ta);
	out->tmin_cC = tmin;
	out->tmax_cC = tmax;
	out->seq = ++s_seq;
	return 0;
}
