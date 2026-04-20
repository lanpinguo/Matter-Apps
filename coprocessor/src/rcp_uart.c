/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rcp_uart.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rcp_uart_module, CONFIG_OT_COPROCESSOR_LOG_LEVEL);

static const struct device *const ot_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_ot_uart));

void run_uart(void)
{
	__ASSERT(ot_uart, "OpenThread UART device is NULL");

	if (!device_is_ready(ot_uart)) {
		LOG_ERR("OpenThread UART %s is not ready", ot_uart->name);
		return;
	}

#if DT_NODE_HAS_PROP(DT_CHOSEN(zephyr_ot_uart), current_speed)
	LOG_INF("RCP UART mode enabled: dev=%s speed=%d", ot_uart->name,
		DT_PROP(DT_CHOSEN(zephyr_ot_uart), current_speed));
#else
	LOG_INF("RCP UART mode enabled: dev=%s", ot_uart->name);
#endif

	/* OpenThread handles Spinel traffic on zephyr,ot-uart in its own context. */
	while (1) {
		k_sleep(K_FOREVER);
	}
}
