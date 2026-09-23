/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#pragma once

#define CHIP_CONFIG_CONTROLLER_MAX_ACTIVE_DEVICES 2

/* Enough entries for 3 switch endpoints (OnOff + LevelControl bindings each). */
#ifndef MATTER_BINDING_TABLE_SIZE
#define MATTER_BINDING_TABLE_SIZE 20
#endif
