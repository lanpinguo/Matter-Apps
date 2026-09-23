/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "light_switch.h"
#include "binding/binding_handler.h"

#ifdef CONFIG_CHIP_LIB_SHELL
#include "shell_commands.h"
#endif

#include <app/server/Server.h>
#include <app/util/binding-table.h>
#include <controller/InvokeInteraction.h>
#include <platform/CHIPDeviceLayer.h>
#include <zephyr/logging/log.h>

using namespace chip;
using namespace chip::app;

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

bool LightSwitch::IsValidSwitchEndpoint(EndpointId endpoint)
{
	return endpoint >= kSwitchEndpointMin && endpoint <= kSwitchEndpointMax;
}

void LightSwitch::Init()
{
	Nrf::Matter::BindingHandler::Init();
#ifdef CONFIG_CHIP_LIB_SHELL
	SwitchCommands::RegisterSwitchCommands();
#endif
	mLightSwitchEndpoint = kSwitchEndpointMin;
}

void LightSwitch::SetCurrentSwitchEndpoint(EndpointId lightSwitchEndpoint)
{
	if (IsValidSwitchEndpoint(lightSwitchEndpoint)) {
		mLightSwitchEndpoint = lightSwitchEndpoint;
	} else {
		LOG_ERR("Invalid switch endpoint: %u", lightSwitchEndpoint);
	}
}

void LightSwitch::InitiateActionSwitch(Action action)
{
	InitiateActionSwitch(mLightSwitchEndpoint, action);
}

void LightSwitch::InitiateActionSwitch(EndpointId lightSwitchEndpoint, Action action)
{
	if (!IsValidSwitchEndpoint(lightSwitchEndpoint)) {
		LOG_ERR("Invalid switch endpoint: %u", lightSwitchEndpoint);
		return;
	}

	Nrf::Matter::BindingHandler::BindingData *data =
		Platform::New<Nrf::Matter::BindingHandler::BindingData>();
	if (data) {
		data->EndpointId = lightSwitchEndpoint;
		data->ClusterId = Clusters::OnOff::Id;
		data->InvokeCommandFunc = SwitchChangedHandler;
		switch (action) {
		case Action::Toggle:
			data->CommandId = Clusters::OnOff::Commands::Toggle::Id;
			break;
		case Action::On:
			data->CommandId = Clusters::OnOff::Commands::On::Id;
			break;
		case Action::Off:
			data->CommandId = Clusters::OnOff::Commands::Off::Id;
			break;
		default:
			Platform::Delete(data);
			return;
		}
		Nrf::Matter::BindingHandler::RunBoundClusterAction(data);
	}
}

void LightSwitch::DimmerChangeBrightness()
{
	DimmerChangeBrightness(mLightSwitchEndpoint);
}

void LightSwitch::DimmerChangeBrightness(EndpointId lightSwitchEndpoint)
{
	if (!IsValidSwitchEndpoint(lightSwitchEndpoint)) {
		LOG_ERR("Invalid switch endpoint: %u", lightSwitchEndpoint);
		return;
	}

	const size_t index = static_cast<size_t>(lightSwitchEndpoint - kSwitchEndpointMin);
	Nrf::Matter::BindingHandler::BindingData *data =
		Platform::New<Nrf::Matter::BindingHandler::BindingData>();
	if (data) {
		data->EndpointId = lightSwitchEndpoint;
		data->CommandId = Clusters::LevelControl::Commands::MoveToLevel::Id;
		data->ClusterId = Clusters::LevelControl::Id;
		data->InvokeCommandFunc = SwitchChangedHandler;
		/* add to brightness 3 to approximate 1% step of brightness after each call dimmer change. */
		mBrightness[index] += kOnePercentBrightnessApproximation;
		if (mBrightness[index] > kMaximumBrightness) {
			mBrightness[index] = 0;
		}
		data->Value = static_cast<uint8_t>(mBrightness[index]);
		Nrf::Matter::BindingHandler::RunBoundClusterAction(data);
	}
}

void LightSwitch::SwitchChangedHandler(const EmberBindingTableEntry &binding, OperationalDeviceProxy *deviceProxy,
				       Nrf::Matter::BindingHandler::BindingData &bindingData)
{
	if (binding.type == MATTER_MULTICAST_BINDING) {
		switch (bindingData.ClusterId) {
		case Clusters::OnOff::Id:
			OnOffProcessCommand(bindingData.CommandId, binding, nullptr, bindingData);
			break;
		case Clusters::LevelControl::Id:
			LevelControlProcessCommand(bindingData.CommandId, binding, nullptr, bindingData);
			break;
		default:
			ChipLogError(NotSpecified, "Invalid binding group command data");
			break;
		}
	} else if (binding.type == MATTER_UNICAST_BINDING) {
		switch (bindingData.ClusterId) {
		case Clusters::OnOff::Id:
			OnOffProcessCommand(bindingData.CommandId, binding, deviceProxy, bindingData);
			break;
		case Clusters::LevelControl::Id:
			LevelControlProcessCommand(bindingData.CommandId, binding, deviceProxy, bindingData);
			break;
		default:
			ChipLogError(NotSpecified, "Invalid binding unicast command data");
			break;
		}
	}
}

void LightSwitch::OnOffProcessCommand(CommandId commandId, const EmberBindingTableEntry &binding,
				      OperationalDeviceProxy *device,
				      Nrf::Matter::BindingHandler::BindingData &bindingData)
{
	CHIP_ERROR ret = CHIP_NO_ERROR;

	auto onSuccess = [dataPointer = Platform::New<Nrf::Matter::BindingHandler::BindingData>(bindingData)](
				 const ConcreteCommandPath &commandPath, const StatusIB &status,
				 const auto &dataResponse) {
		Nrf::Matter::BindingHandler::OnInvokeCommandSucces(dataPointer);
	};

	auto onFailure = [dataPointer = Platform::New<Nrf::Matter::BindingHandler::BindingData>(bindingData)](
				 CHIP_ERROR aError) mutable {
		Nrf::Matter::BindingHandler::OnInvokeCommandFailure(dataPointer, aError);
	};

	if (device) {
		/* We are validating connection is ready once here instead of multiple times in each case
		 * statement below. */
		VerifyOrDie(device->ConnectionReady());
	}

	switch (commandId) {
	case Clusters::OnOff::Commands::Toggle::Id:
		Clusters::OnOff::Commands::Toggle::Type toggleCommand;
		if (device) {
			ret = Controller::InvokeCommandRequest(device->GetExchangeManager(),
							       device->GetSecureSession().Value(), binding.remote,
							       toggleCommand, onSuccess, onFailure);
		} else {
			Messaging::ExchangeManager &exchangeMgr = Server::GetInstance().GetExchangeManager();
			ret = Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId,
								    toggleCommand);
		}
		break;

	case Clusters::OnOff::Commands::On::Id:
		Clusters::OnOff::Commands::On::Type onCommand;
		if (device) {
			ret = Controller::InvokeCommandRequest(device->GetExchangeManager(),
							       device->GetSecureSession().Value(), binding.remote,
							       onCommand, onSuccess, onFailure);
		} else {
			Messaging::ExchangeManager &exchangeMgr = Server::GetInstance().GetExchangeManager();
			ret = Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId,
								    onCommand);
		}
		break;

	case Clusters::OnOff::Commands::Off::Id:
		Clusters::OnOff::Commands::Off::Type offCommand;
		if (device) {
			ret = Controller::InvokeCommandRequest(device->GetExchangeManager(),
							       device->GetSecureSession().Value(), binding.remote,
							       offCommand, onSuccess, onFailure);
		} else {
			Messaging::ExchangeManager &exchangeMgr = Server::GetInstance().GetExchangeManager();
			ret = Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId,
								    offCommand);
		}
		break;
	default:
		LOG_DBG("Invalid binding command data - commandId is not supported");
		break;
	}
	if (CHIP_NO_ERROR != ret) {
		LOG_ERR("Invoke OnOff Command Request ERROR: %s", ErrorStr(ret));
	}
}

void LightSwitch::LevelControlProcessCommand(CommandId commandId, const EmberBindingTableEntry &binding,
					     OperationalDeviceProxy *device,
					     Nrf::Matter::BindingHandler::BindingData &bindingData)
{
	auto onSuccess = [dataPointer = Platform::New<Nrf::Matter::BindingHandler::BindingData>(bindingData)](
				 const ConcreteCommandPath &commandPath, const StatusIB &status,
				 const auto &dataResponse) {
		Nrf::Matter::BindingHandler::OnInvokeCommandSucces(dataPointer);
	};

	auto onFailure = [dataPointer = Platform::New<Nrf::Matter::BindingHandler::BindingData>(bindingData)](
				 CHIP_ERROR aError) mutable {
		Nrf::Matter::BindingHandler::OnInvokeCommandFailure(dataPointer, aError);
	};

	CHIP_ERROR ret = CHIP_NO_ERROR;

	if (device) {
		/* We are validating connection is ready once here instead of multiple times in each case
		 * statement below. */
		VerifyOrDie(device->ConnectionReady());
	}

	switch (commandId) {
	case Clusters::LevelControl::Commands::MoveToLevel::Id: {
		Clusters::LevelControl::Commands::MoveToLevel::Type moveToLevelCommand;
		moveToLevelCommand.level = bindingData.Value;
		if (device) {
			ret = Controller::InvokeCommandRequest(device->GetExchangeManager(),
							       device->GetSecureSession().Value(), binding.remote,
							       moveToLevelCommand, onSuccess, onFailure);
		} else {
			Messaging::ExchangeManager &exchangeMgr = Server::GetInstance().GetExchangeManager();
			ret = Controller::InvokeGroupCommandRequest(&exchangeMgr, binding.fabricIndex, binding.groupId,
								    moveToLevelCommand);
		}
	} break;
	default:
		LOG_DBG("Invalid binding command data - commandId is not supported");
		break;
	}
	if (CHIP_NO_ERROR != ret) {
		LOG_ERR("Invoke Group Command Request ERROR: %s", ErrorStr(ret));
	}
}
