/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "network_watchdog.h"

#include "app/task_executor.h"

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/ConnectivityManager.h>
#include <platform/nrfconnect/ConnectivityManagerImpl.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::DeviceLayer;

namespace NetworkWatchdog
{
namespace
{

k_timer sCheckTimer;
int64_t sOfflineSinceMs = 0;

bool IsNetworkConnected()
{
#if defined(CONFIG_NET_L2_OPENTHREAD)
	return ConnectivityMgr().IsThreadAttached();
#elif defined(CONFIG_CHIP_WIFI)
	return ConnectivityMgr().IsWiFiStationConnected();
#else
	return true;
#endif
}

bool ShouldMonitorConnectivity()
{
	if (Server::GetInstance().GetFabricTable().FabricCount() == 0) {
		return false;
	}

	return ConnectivityMgrImpl().IsIPv6NetworkProvisioned();
}

void CheckConnectivity()
{
	const int64_t nowMs = k_uptime_get();
	const int64_t bootGraceMs =
		static_cast<int64_t>(CONFIG_APP_NETWORK_WATCHDOG_BOOT_GRACE_SEC) * 1000;

	if (nowMs < bootGraceMs) {
		sOfflineSinceMs = 0;
		return;
	}

	if (!ShouldMonitorConnectivity()) {
		sOfflineSinceMs = 0;
		return;
	}

	if (IsNetworkConnected()) {
		if (sOfflineSinceMs != 0) {
			LOG_INF("Network connectivity restored");
			sOfflineSinceMs = 0;
		}
		return;
	}

	if (sOfflineSinceMs == 0) {
		sOfflineSinceMs = nowMs;
		LOG_WRN("Network offline; reboot after %d s if not restored",
			CONFIG_APP_NETWORK_WATCHDOG_TIMEOUT_SEC);
		return;
	}

	const int64_t offlineMs = nowMs - sOfflineSinceMs;
	const int64_t timeoutMs =
		static_cast<int64_t>(CONFIG_APP_NETWORK_WATCHDOG_TIMEOUT_SEC) * 1000;

	if (offlineMs < timeoutMs) {
		return;
	}

	LOG_ERR("Network offline for %lld s; rebooting", offlineMs / 1000);
	k_msleep(100);
	sys_reboot(SYS_REBOOT_COLD);
}

void TimerHandler(k_timer * /* timer */)
{
	Nrf::PostTask([] { CheckConnectivity(); });
}

} /* namespace */

CHIP_ERROR Init()
{
	k_timer_init(&sCheckTimer, TimerHandler, nullptr);
	k_timer_start(&sCheckTimer, K_SECONDS(CONFIG_APP_NETWORK_WATCHDOG_CHECK_INTERVAL_SEC),
		      K_SECONDS(CONFIG_APP_NETWORK_WATCHDOG_CHECK_INTERVAL_SEC));

	LOG_INF("Network watchdog started (timeout %d s, check every %d s, boot grace %d s)",
		CONFIG_APP_NETWORK_WATCHDOG_TIMEOUT_SEC, CONFIG_APP_NETWORK_WATCHDOG_CHECK_INTERVAL_SEC,
		CONFIG_APP_NETWORK_WATCHDOG_BOOT_GRACE_SEC);

	return CHIP_NO_ERROR;
}

} /* namespace NetworkWatchdog */
