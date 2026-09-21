/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#include <lib/core/CHIPError.h>

namespace NetworkWatchdog
{

/**
 * Start periodic connectivity checks. Call after the Matter server is ready.
 * When commissioned + provisioned but offline longer than the configured
 * timeout, the device reboots.
 */
CHIP_ERROR Init();

} /* namespace NetworkWatchdog */
