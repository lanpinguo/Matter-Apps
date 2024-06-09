/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <cstdint>
#include <zephyr/drivers/gpio.h>

class IO_Config {
public:
	enum RelayMode_t : uint8_t {
    	COMPACT_COMPACT = 0x00,
        COMPACT_NORMAL = 0x01,	
		NORMAL_COMPACT = 0x02,
		NORMAL_NORMAL = 0x03,

		INVALID_MODE
	};


    IO_Config(): mIOConfig{
        [0] = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios),
        [1] = GPIO_DT_SPEC_GET(DT_ALIAS(sw1), gpios),
        [2] = GPIO_DT_SPEC_GET(DT_ALIAS(sw2), gpios)
    }{
        mInitialized = 0;
    }


	RelayMode_t GetCurrentMode();

	RelayMode_t UpdateMode();

private:
    int mInitialized;
	RelayMode_t mMode;
    const struct gpio_dt_spec mIOConfig[3];

};
