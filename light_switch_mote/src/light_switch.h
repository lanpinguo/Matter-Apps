/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once
#include <app/util/basic-types.h>
#include <lib/core/CHIPError.h>

#include "binding/binding_handler.h"

#include <atomic>

/** @class LightSwitch
 *  @brief Class for controlling CHIP light bulbs over a Thread network
 *
 *  Supports three local switch endpoints (1–3) mapped to touch sensors snr1–snr3.
 */
class LightSwitch {
public:
	static constexpr chip::EndpointId kSwitchEndpointMin = 1;
	static constexpr chip::EndpointId kSwitchEndpointMax = 3;
	static constexpr size_t kSwitchEndpointCount = 3;

	enum class Action : uint8_t {
		Toggle, /* Switch state on lighting-app device */
		On, /* Turn on light on lighting-app device */
		Off /* Turn off light on lighting-app device */
	};

	void Init();
	void SetCurrentSwitchEndpoint(chip::EndpointId lightSwitchEndpoint);
	void InitiateActionSwitch(Action action);
	void InitiateActionSwitch(chip::EndpointId lightSwitchEndpoint, Action action);
	void DimmerChangeBrightness();
	void DimmerChangeBrightness(chip::EndpointId lightSwitchEndpoint);
	chip::EndpointId GetLightSwitchEndpointId() { return mLightSwitchEndpoint; }
	static void SwitchChangedHandler(const EmberBindingTableEntry &binding,
					 chip::OperationalDeviceProxy *deviceProxy,
					 Nrf::Matter::BindingHandler::BindingData &bindingData);

	static LightSwitch &GetInstance()
	{
		static LightSwitch sLightSwitch;
		return sLightSwitch;
	}

private:
	static void OnOffProcessCommand(chip::CommandId commandId, const EmberBindingTableEntry &binding,
					chip::OperationalDeviceProxy *device,
					Nrf::Matter::BindingHandler::BindingData &bindingData);
	static void LevelControlProcessCommand(chip::CommandId commandId, const EmberBindingTableEntry &binding,
					       chip::OperationalDeviceProxy *device,
					       Nrf::Matter::BindingHandler::BindingData &bindingData);
	static bool IsValidSwitchEndpoint(chip::EndpointId endpoint);

	constexpr static auto kOnePercentBrightnessApproximation = 3;
	constexpr static auto kMaximumBrightness = 254;

	chip::EndpointId mLightSwitchEndpoint = kSwitchEndpointMin;
	uint16_t mBrightness[kSwitchEndpointCount] = {};
};
