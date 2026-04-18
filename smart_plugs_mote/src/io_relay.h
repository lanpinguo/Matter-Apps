/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

 #pragma once

 #include <cstdint>
 #include <zephyr/drivers/gpio.h>
 #include <zephyr/drivers/i2c.h>
 
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
 
	 IO_Relay()
		 : mIOExpander{ 
			I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_0)),
			I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_2)) },
		   mIOManualButtonReader{ 
			I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_1)),
			I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_3)) }
	 {
	 }

	 int Init(int slot, int chl, bool aDefaultState = false);
	 bool IsTurnedOn(int slot, int chl) const { return mState[slot][chl] == kState_On; }
	 bool InitiateAction(int slot, int chl, Action_t aAction, int32_t aActor, uint8_t *aValue = nullptr);
	 void SetCallbacks(int slot, int chl, IO_RelayCallback aActionInitiatedClb, IO_RelayCallback aActionCompletedClb);
	//  const device *GetDevice(int slot) { return mIOExpander[slot]->dev; }
	//  void SuppressOutput(int slot);
	 int Set(int slot, int chl, Action_t aAction); 
 private:
	 State_t mState[2][4];
	 const struct i2c_dt_spec mIOExpander[2];
	 const struct i2c_dt_spec mIOManualButtonReader[2];
 
	 IO_RelayCallback mActionInitiatedClb[2][4];
	 IO_RelayCallback mActionCompletedClb[2][4];
 };
 