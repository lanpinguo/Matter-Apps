/*
 * High-level MLX90640 helper for the portable thermal camera.
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MLX90640_SENSOR_H_
#define MLX90640_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

#define MLX90640_WIDTH  32
#define MLX90640_HEIGHT 24
#define MLX90640_PIXELS (MLX90640_WIDTH * MLX90640_HEIGHT)

/** Temperature in centi-degrees Celsius (0.01 °C). */
typedef int16_t mlx_temp_cC_t;

struct mlx90640_frame {
	mlx_temp_cC_t pixels[MLX90640_PIXELS];
	mlx_temp_cC_t ta_cC;
	mlx_temp_cC_t tmin_cC;
	mlx_temp_cC_t tmax_cC;
	uint16_t seq;
};

bool mlx90640_available(void);
int mlx90640_init(void);

/**
 * Capture one complete thermal frame (both subpages) into @p out.
 * Blocking; typically ~500 ms at 2 Hz refresh.
 */
int mlx90640_capture(struct mlx90640_frame *out);

/** Refresh rate code: 0=0.5Hz … 5=16Hz (Melexis encoding). Default 2 (=2 Hz). */
int mlx90640_set_refresh_rate(uint8_t rate_code);

#endif /* MLX90640_SENSOR_H_ */
