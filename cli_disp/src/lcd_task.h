/**
 * @file
 * @defgroup lcd_task LCD Task API
 * @{
 */

/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef __LCD_TASK_H__
#define __LCD_TASK_H__

/** @brief Initialize and start LCD display.
 */
void lcd_task_enable(void);

/** @brief Test LCD display with various patterns.
 */
void lcd_test(void);

#endif

/**
 * @}
 */
