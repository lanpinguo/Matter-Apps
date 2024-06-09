/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <cstdint>
#include <zephyr/drivers/gpio.h>

class IO_Relay {
public:
	enum Action_t : uint8_t {
		ON_ACTION = 0,
		OFF_ACTION,
		LEVEL_ACTION,

		INVALID_ACTION
	};

	enum State_t : uint8_t {
		kState_On = 0,
		kState_Off,
	};

	using RelayCallback = void (*)(Action_t, int32_t);

    IO_Relay(): mGIPO_P0(DEVICE_DT_GET(DT_NODELABEL(gpio0))){
        int err;


        if (!device_is_ready(mGIPO_P0)) {
            return;
        }

        err = gpio_pin_configure(mGIPO_P0, 22,
                    GPIO_OUTPUT | GPIO_ACTIVE_LOW);
        if (err < 0) {
            return;
        }        

        err = gpio_pin_configure(mGIPO_P0, 24,
                    GPIO_OUTPUT | GPIO_ACTIVE_LOW);
        if (err < 0) {
            return;
        }        

    }

	// int Init(bool aDefaultState = false);
	// bool IsTurnedOn() const { return mState == kState_On; }
	// bool InitiateAction(Action_t aAction, int32_t aActor, uint8_t *aValue);
	// void SetCallbacks(RelayCallback aActionInitiatedClb, RelayCallback aActionCompletedClb);
	// const device *GetDevice(int slot) { return mIOExpander[slot]->dev; }
	// void SuppressOutput();
	int Set(int slot, int chl, Action_t aAction);

private:
	State_t mState;
    const struct device * mGIPO_P0;
	uint32_t mRelaySlot;

	RelayCallback mActionInitiatedClb;
	RelayCallback mActionCompletedClb;
};
