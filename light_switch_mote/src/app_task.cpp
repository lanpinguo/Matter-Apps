/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "app_task.h"

#include "light_switch.h"

#include "app/matter_init.h"
#include "app/task_executor.h"
#include "board/board.h"

#include <app/clusters/identify-server/identify-server.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::app;
using namespace ::chip::DeviceLayer;

namespace
{
constexpr uint32_t kDimmerTriggeredTimeout = 500;
constexpr uint32_t kDimmerInterval = 300;

k_timer sDimmerPressKeyTimer;
k_timer sDimmerTimer;

Identify sIdentify1 = { LightSwitch::kSwitchEndpointMin, AppTask::IdentifyStartHandler, AppTask::IdentifyStopHandler,
			Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator };
Identify sIdentify2 = { static_cast<EndpointId>(LightSwitch::kSwitchEndpointMin + 1), AppTask::IdentifyStartHandler,
			AppTask::IdentifyStopHandler, Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator };
Identify sIdentify3 = { LightSwitch::kSwitchEndpointMax, AppTask::IdentifyStartHandler, AppTask::IdentifyStopHandler,
			Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator };

bool sWasDimmerTriggered = false;
EndpointId sActiveSwitchEndpoint = LightSwitch::kSwitchEndpointMin;

/*
 * P1.08 -> DK_BTN1 / button0 : factory reset (Board), same pin as light_bulb_mote
 * snr1 P1.12 -> DK_BTN2 / button1 -> Matter endpoint 1
 * snr2 P1.11 -> DK_BTN3 / button2 -> Matter endpoint 2
 * snr3 P0.02 -> DK_BTN4 / button3 -> Matter endpoint 3
 */
#define SWITCH1_BUTTON_MASK DK_BTN2_MSK
#define SWITCH2_BUTTON_MASK DK_BTN3_MSK
#define SWITCH3_BUTTON_MASK DK_BTN4_MSK
#define SWITCH_BUTTONS_MASK (SWITCH1_BUTTON_MASK | SWITCH2_BUTTON_MASK | SWITCH3_BUTTON_MASK)

EndpointId EndpointFromButtonMask(Nrf::ButtonMask mask)
{
	if (mask & SWITCH1_BUTTON_MASK) {
		return 1;
	}
	if (mask & SWITCH2_BUTTON_MASK) {
		return 2;
	}
	if (mask & SWITCH3_BUTTON_MASK) {
		return 3;
	}
	return LightSwitch::kSwitchEndpointMin;
}
} /* namespace */

void AppTask::DimmerTriggerEventHandler()
{
	LightSwitch::GetInstance().SetCurrentSwitchEndpoint(sActiveSwitchEndpoint);

	if (!sWasDimmerTriggered) {
		LightSwitch::GetInstance().InitiateActionSwitch(sActiveSwitchEndpoint, LightSwitch::Action::Toggle);
	}

	Instance().CancelTimer(Timer::Dimmer);
	Instance().CancelTimer(Timer::DimmerTrigger);
	sWasDimmerTriggered = false;
}

void AppTask::TimerEventHandler(const Timer &timerType)
{
	switch (timerType) {
	case Timer::DimmerTrigger:
		LOG_INF("Dimming started on endpoint %u...", sActiveSwitchEndpoint);
		sWasDimmerTriggered = true;
		LightSwitch::GetInstance().SetCurrentSwitchEndpoint(sActiveSwitchEndpoint);
		LightSwitch::GetInstance().InitiateActionSwitch(sActiveSwitchEndpoint, LightSwitch::Action::On);
		Instance().StartTimer(Timer::Dimmer, kDimmerInterval);
		Instance().CancelTimer(Timer::DimmerTrigger);
		break;
	case Timer::Dimmer:
		LightSwitch::GetInstance().DimmerChangeBrightness(sActiveSwitchEndpoint);
		break;
	default:
		break;
	}
}

void AppTask::IdentifyStartHandler(Identify *)
{
	Nrf::PostTask(
		[] { Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED2).Blink(Nrf::LedConsts::kIdentifyBlinkRate_ms); });
}

void AppTask::IdentifyStopHandler(Identify *)
{
	Nrf::PostTask([] { Nrf::GetBoard().GetLED(Nrf::DeviceLeds::LED2).Set(false); });
}

void AppTask::ButtonEventHandler(Nrf::ButtonState state, Nrf::ButtonMask hasChanged)
{
	const Nrf::ButtonMask switchChanged = hasChanged & SWITCH_BUTTONS_MASK;
	if (!switchChanged) {
		return;
	}

	const EndpointId endpoint = EndpointFromButtonMask(switchChanged);
	sActiveSwitchEndpoint = endpoint;
	LightSwitch::GetInstance().SetCurrentSwitchEndpoint(endpoint);

	if (switchChanged & state) {
		LOG_INF("snr%u (endpoint %u) pressed; hold >= 500 ms to dim bound lights.", endpoint, endpoint);
		Instance().StartTimer(Timer::DimmerTrigger, kDimmerTriggeredTimeout);
	} else {
		Nrf::PostTask([] { DimmerTriggerEventHandler(); });
	}
}

void AppTask::StartTimer(Timer timer, uint32_t timeoutMs)
{
	switch (timer) {
	case Timer::DimmerTrigger:
		k_timer_start(&sDimmerPressKeyTimer, K_MSEC(timeoutMs), K_NO_WAIT);
		break;
	case Timer::Dimmer:
		k_timer_start(&sDimmerTimer, K_MSEC(timeoutMs), K_MSEC(timeoutMs));
		break;
	default:
		break;
	}
}

void AppTask::CancelTimer(Timer timer)
{
	switch (timer) {
	case Timer::DimmerTrigger:
		k_timer_stop(&sDimmerPressKeyTimer);
		break;
	case Timer::Dimmer:
		k_timer_stop(&sDimmerTimer);
		break;
	default:
		break;
	}
}

void AppTask::UserTimerTimeoutCallback(k_timer *timer)
{
	if (!timer) {
		return;
	}
	Timer timerType;

	if (timer == &sDimmerPressKeyTimer) {
		timerType = Timer::DimmerTrigger;
	} else if (timer == &sDimmerTimer) {
		timerType = Timer::Dimmer;
	} else {
		return;
	}

	Nrf::PostTask([timerType]() { TimerEventHandler(timerType); });
}

CHIP_ERROR AppTask::Init()
{
	/* Keep Identify instances alive for endpoints 1–3. */
	(void)sIdentify1;
	(void)sIdentify2;
	(void)sIdentify3;

	/* Initialize Matter stack */
	ReturnErrorOnFailure(Nrf::Matter::PrepareServer(Nrf::Matter::InitData{ .mPostServerInitClbk = [] {
		LightSwitch::GetInstance().Init();
		return CHIP_NO_ERROR;
	} }));

	/* Initialize application timers */
	k_timer_init(&sDimmerPressKeyTimer, AppTask::UserTimerTimeoutCallback, nullptr);
	k_timer_init(&sDimmerTimer, AppTask::UserTimerTimeoutCallback, nullptr);

	if (!Nrf::GetBoard().Init(ButtonEventHandler)) {
		LOG_ERR("User interface initialization failed.");
		return CHIP_ERROR_INCORRECT_STATE;
	}

	/* Register Matter event handler that controls the connectivity status LED based on the captured Matter network
	 * state. */
	ReturnErrorOnFailure(Nrf::Matter::RegisterEventHandler(Nrf::Board::DefaultMatterEventHandler, 0));

	return Nrf::Matter::StartServer();
}

CHIP_ERROR AppTask::StartApp()
{
	ReturnErrorOnFailure(Init());

	while (true) {
		Nrf::DispatchNextTask();
	}

	return CHIP_NO_ERROR;
}
