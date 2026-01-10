/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <cstdint>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

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

	using IO_RelayCallback = void (*)(Action_t, int32_t);

	IO_Relay() : mState(kState_Off)
	{
		/* Use zephyr,user node for custom GPIO configuration */
		static const struct gpio_dt_spec relay1_spec = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, relay1_gpios);
		static const struct gpio_dt_spec relay2_spec = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, relay2_gpios);

		mRelay1 = relay1_spec;
		mRelay2 = relay2_spec;

		int err;



		/* Configure relay pins for direct GPIO control.
		 * For ACTIVE_HIGH relays, GPIO_OUTPUT_INACTIVE means output LOW (relay off)
		 */
		err = gpio_pin_configure_dt(&mRelay1, GPIO_OUTPUT_INACTIVE);
		if (err < 0) {
			return;
		}

		err = gpio_pin_configure_dt(&mRelay2, GPIO_OUTPUT_INACTIVE);
		if (err < 0) {
			return;
		}

	};

	int Init(bool aDefaultState = false)
	{
		mState = aDefaultState ? kState_On : kState_Off;
		gpio_pin_configure_dt(&mRelay1, aDefaultState ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure_dt(&mRelay2, aDefaultState ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
		return 0;
	};
	bool IsTurnedOn() const { return mState == kState_On; };
	bool InitiateAction(Action_t aAction, int32_t aActor, uint8_t *aValue);
	void SetCallbacks(IO_RelayCallback aActionInitiatedClb, IO_RelayCallback aActionCompletedClb){
		mActionInitiatedClb = aActionInitiatedClb;
		mActionCompletedClb = aActionCompletedClb;
	};

	int Set(int slot, int chl, Action_t aAction);

private:
	State_t mState;
	struct gpio_dt_spec mRelay1;
	struct gpio_dt_spec mRelay2;

	uint32_t mRelaySlot;

	IO_RelayCallback mActionInitiatedClb;
	IO_RelayCallback mActionCompletedClb;
};
