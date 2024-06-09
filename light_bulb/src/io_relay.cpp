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

#include "io_relay.h"


LOG_MODULE_REGISTER(io_ctrl);





int IO_Relay::Set(int slot, int chl, Action_t aAction)
{


	if(aAction == ON_ACTION){

        if (gpio_pin_set(mGIPO_P0,  22, 1) != 0) {
        }
        if (gpio_pin_set(mGIPO_P0,  24, 0) != 0) {
        }

        k_msleep(500);

        if (gpio_pin_set(mGIPO_P0,  22, 1) != 0) {
        }
        if (gpio_pin_set(mGIPO_P0,  24, 1) != 0) {
        }
        
	}
	else{

        if (gpio_pin_set(mGIPO_P0,  22, 0) != 0) {
        }
        if (gpio_pin_set(mGIPO_P0,  24, 1) != 0) {
        }

        k_msleep(500);

        if (gpio_pin_set(mGIPO_P0,  22, 0) != 0) {
        }
        if (gpio_pin_set(mGIPO_P0,  24, 0) != 0) {
        }
	}

	return 0;
}



