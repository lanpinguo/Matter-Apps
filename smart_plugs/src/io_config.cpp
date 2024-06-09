/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef CONFIG_ARCH_POSIX
#include <unistd.h>
#else
#include <zephyr/posix/unistd.h>
#endif

#include <zephyr/logging/log.h>

#include "io_config.h"


LOG_MODULE_REGISTER(io_config);





IO_Config::RelayMode_t IO_Config::UpdateMode()
{
	int err;

    int mode = 0;

    if(mInitialized == 0){
        for(int i = 0; i < 3; i++){
            if (!device_is_ready(mIOConfig[i].port)) {
                LOG_ERR("io-config %d is not ready!\n\r", i);
                return INVALID_MODE;
            }

            err = gpio_pin_configure_dt(&mIOConfig[i], GPIO_INPUT);
            if (err < 0) {
                LOG_ERR("io-config init %d failed!\n\r", i);
                return INVALID_MODE;
            }        
        }	
        k_sleep(K_MSEC(100));
        mInitialized = 1;
    }

    mode |= gpio_pin_get_dt(&mIOConfig[1]) > 0 ? 0x1 : 0; 
     
    mode |= gpio_pin_get_dt(&mIOConfig[2]) > 0 ? 0x2 : 0; 

    mMode = static_cast<IO_Config::RelayMode_t>(mode);
    LOG_INF("io-config init mode: %u\n\r", mode);
	return mMode;
}

IO_Config::RelayMode_t IO_Config::GetCurrentMode()
{
	return mMode;
}

