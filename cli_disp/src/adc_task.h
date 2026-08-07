/**
 * @file
 * @defgroup adc_task ADC Task API
 * @{
 */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __ADC_TASK_H__
#define __ADC_TASK_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ADC_TASK_CHANNEL_COUNT 2

/** @brief Initialize and start ADC polling task.
 */
void adc_task_enable(void);

/** @brief Get latest ADC values (raw, mV, valid). Caller provides arrays of at least max_count.
 *  @return Number of channels copied (min of max_count and ADC_TASK_CHANNEL_COUNT).
 */
size_t adc_task_get_values(int16_t *raw, int32_t *mv, bool *valid, size_t max_count);

#endif

/**
 * @}
 */
