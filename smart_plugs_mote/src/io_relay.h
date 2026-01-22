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
 
	 IO_Relay(): mIOExpander{
		 [0] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_0)),
		 [1] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_1)),
		 [2] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_2)),
		 [3] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_3)),
		 [4] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_4)),
		 [5] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_5)),
		 [6] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_6)),
		 [7] = I2C_DT_SPEC_GET(DT_NODELABEL(io_expander_7))
	 }{
 
	 }
 
	 int Init(int slot, bool aDefaultState = false){ return 0; };
	 bool IsTurnedOn(int slot) const { return mState[slot] == kState_On; }
	 bool InitiateAction(Action_t aAction, int32_t aActor, uint8_t *aValue);
	 void SetCallbacks(int slot, IO_RelayCallback aActionInitiatedClb, IO_RelayCallback aActionCompletedClb);
	//  const device *GetDevice(int slot) { return mIOExpander[slot]->dev; }
	//  void SuppressOutput(int slot);
	 int Set(int slot, int chl, Action_t aAction);
 
 private:
	 State_t mState[8];
	 const struct i2c_dt_spec mIOExpander[8];
 
	 IO_RelayCallback mActionInitiatedClb[8];
	 IO_RelayCallback mActionCompletedClb[8];
 };
 