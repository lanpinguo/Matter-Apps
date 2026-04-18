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
	int ret;
	uint8_t buf[2] = {0};

	if (slot < 0 || slot >= 2 || chl < 0 || chl >= 4) {
		LOG_ERR("IO_Relay::Set: invalid slot %d chl %d", slot, chl);
		return -1;
	}

	if (!device_is_ready(mIOExpander[slot].bus)) {
		LOG_ERR("I2C bus %s is not ready!\n\r",mIOExpander[slot].bus->name);
		return -1;
	}

	/* Keep the i2c pin with right state, recover bus first */
	i2c_recover_bus(mIOExpander[slot].bus);

	// ret = i2c_read_dt(&mIOExpander[slot], buf, 1);
	// if(ret != 0){
	// 	LOG_ERR("Failed to read from I2C device address %x at Reg. %x \n", mIOExpander[slot].addr, buf[0]);
	// 	return -1;
	// }	

	buf[0] = buf[0] & (~(0x3<<(2*chl)));
	if(aAction == OFF_ACTION){
		buf[0] |= (0x02 << (2*chl));
	}
	else{
		buf[0] |= (0x01 << (2*chl));
	}

	ret = i2c_write_dt(&mIOExpander[slot], buf, 1);
	if(ret != 0){
		LOG_ERR("Failed to write to I2C device address %x at reg. %x \n", mIOExpander[slot].addr, buf[0]);
		return -1;
	}

	k_sleep(K_MSEC(10));

	buf[0] = 0x00; // all outputs are low
	ret = i2c_write_dt(&mIOExpander[slot], buf, 1);
	if(ret != 0){
		LOG_ERR("Failed to write to I2C device address %x at reg. %x \n", mIOExpander[slot].addr, buf[0]);
		return -1;
	}

	LOG_INF("IO_Relay::Set: slot %d, chl %d, action %s", slot, chl, aAction == ON_ACTION ? "ON" : "OFF");
	return 0;
}

bool IO_Relay::InitiateAction(int slot, int chl, Action_t aAction, int32_t aActor, uint8_t *aValue)
{

	if(Set(slot, chl, aAction) != 0){
		return false;
	}

	return true;
}

void IO_Relay::SetCallbacks(int slot, int chl, IO_RelayCallback aActionInitiatedClb, IO_RelayCallback aActionCompletedClb)
{
	mActionInitiatedClb[slot][chl] = aActionInitiatedClb;
	mActionCompletedClb[slot][chl] = aActionCompletedClb;
}

int IO_Relay::Init(int slot, int chl, bool aDefaultState)
{
	if(Set(slot, chl, aDefaultState ? ON_ACTION : OFF_ACTION) != 0){
		return -1;
	}
	mState[slot][chl] = aDefaultState ? kState_On : kState_Off;
	return 0;
}

