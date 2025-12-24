/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include "board/board.h"
#include "pwm/pwm_device.h"

#include <platform/CHIPDeviceLayer.h>
#include "io_relay.h"

#include <platform/DiagnosticDataProvider.h>
#include <platform/nrfconnect/DiagnosticDataProviderImplNrf.h>
#include <platform/Zephyr/DiagnosticDataProviderImpl.h>


struct k_timer;
struct Identify;

using BootReasonType = chip::DeviceLayer::BootReasonType;

enum class LightingActor : uint8_t { Remote, Button };

struct LightingEvent {
	uint8_t Action;
	LightingActor Actor;
};

class AppTask {
public:
	static AppTask &Instance()
	{
		static AppTask sAppTask;
		return sAppTask;
	};

	CHIP_ERROR StartApp();

	void UpdateClusterState();
	void InitPWMDDevice();
	Nrf::PWMDevice &GetPWMDevice() { return mPWMDevice; }
	IO_Relay &GetRelayDevice() { return mIO_RelayDevice; }
    BootReasonType ResetReasonGet(void);

	static void IdentifyStartHandler(Identify *);
	static void IdentifyStopHandler(Identify *);
	static void TriggerIdentifyEffectHandler(Identify *);
	static void TriggerEffectTimerTimeoutCallback(k_timer *timer);

private:
	CHIP_ERROR Init();

	static void LightingActionEventHandler(const LightingEvent &event);
	static void ButtonEventHandler(Nrf::ButtonState state, Nrf::ButtonMask hasChanged);

	static void ActionInitiated(Nrf::PWMDevice::Action_t action, int32_t actor);
	static void ActionCompleted(Nrf::PWMDevice::Action_t action, int32_t actor);

#ifdef CONFIG_AWS_IOT_INTEGRATION
	static bool AWSIntegrationCallback(struct aws_iot_integration_cb_data *data);
#endif
	IO_Relay mIO_RelayDevice;
	Nrf::PWMDevice mPWMDevice;
};
