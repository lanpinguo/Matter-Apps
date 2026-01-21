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
#include <zephyr/kernel.h>

#include "io_relay.h"


LOG_MODULE_REGISTER(io_ctrl);





bool IO_Relay::InitiateAction(Action_t aAction, int32_t aActor, uint8_t *aValue)
{
	/* TODO: this function is called InitiateAction because we want to implement some features such as ramping up
	 * here. */
	bool action_initiated = false;
	State_t new_state;

	/* Initiate On/Off Action only when the previous one is complete. */
	if (mState == kState_Off && aAction == ON_ACTION) {
		action_initiated = true;
		new_state = kState_On;
	} else if (mState == kState_On && aAction == OFF_ACTION) {
		action_initiated = true;
		new_state = kState_Off;
	}

	if (action_initiated) {
		if (mActionInitiatedClb) {
			mActionInitiatedClb(aAction, aActor);
		}

		/* Execute the action */
		if (aAction == ON_ACTION || aAction == OFF_ACTION) {
			Set(0, 0, new_state == kState_On ? ON_ACTION : OFF_ACTION);
			mState = new_state;
		}

		if (mActionCompletedClb) {
			mActionCompletedClb(aAction, aActor);
		}
	}

	return action_initiated;
}

int IO_Relay::Set(int slot, int chl, Action_t aAction)
{
	ARG_UNUSED(slot);
	ARG_UNUSED(chl);

	if (aAction == ON_ACTION) {
		gpio_pin_set_dt(&mRelay1, 1);
		gpio_pin_set_dt(&mRelay2, 0);

		k_msleep(500);

		gpio_pin_set_dt(&mRelay1, 1);
		gpio_pin_set_dt(&mRelay2, 1);

	} else {
		gpio_pin_set_dt(&mRelay1, 0);
		gpio_pin_set_dt(&mRelay2, 1);

		k_msleep(500);

		gpio_pin_set_dt(&mRelay1, 0);
		gpio_pin_set_dt(&mRelay2, 0);
	}

	return 0;
}



