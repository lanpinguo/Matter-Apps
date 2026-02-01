/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/adc/nrf-saadc.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>

#include "adc_task.h"

LOG_MODULE_REGISTER(adc_task, LOG_LEVEL_DBG);

#define ADC_NODE				DT_NODELABEL(adc)
#define ADC_RESOLUTION			12
#define ADC_GAIN				ADC_GAIN_1_3
#define ADC_REFERENCE			ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME	ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_4			3
#define ADC_CHANNEL_5			5
#define ADC_POLL_INTERVAL_MS	1000
#define CHANNEL_COUNT			2

static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

static struct adc_channel_cfg channel_cfgs[CHANNEL_COUNT] = {
	{
		.gain = ADC_GAIN,
		.reference = ADC_REFERENCE,
		.acquisition_time = ADC_ACQUISITION_TIME,
		.channel_id = ADC_CHANNEL_4,
		.differential = 0,
#ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
		.input_positive = NRF_SAADC_AIN4,
#endif
	},
	{
		.gain = ADC_GAIN,
		.reference = ADC_REFERENCE,
		.acquisition_time = ADC_ACQUISITION_TIME,
		.channel_id = ADC_CHANNEL_5,
		.differential = 0,
#ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
		.input_positive = NRF_SAADC_AIN5,
#endif
	}
};

static uint32_t vrefs_mv[CHANNEL_COUNT] = {0, 0};

static int16_t channel_reading[CHANNEL_COUNT];

/* Latest ADC values for shell command access */
static struct {
	int16_t raw[CHANNEL_COUNT];
	int32_t mv[CHANNEL_COUNT];
	bool valid[CHANNEL_COUNT];
	struct k_mutex mutex;
} adc_values;

static const struct adc_sequence_options options = {
	.extra_samplings = 0,
	.interval_us = 0,
};

static struct adc_sequence sequence = {
	.buffer = channel_reading,
	.buffer_size = sizeof(channel_reading),
	.resolution = ADC_RESOLUTION,
	.options = &options,
};

static void adc_task_thread(void *p1, void *p2, void *p3)
{
	int ret;
	uint32_t count = 0;

	if (!device_is_ready(adc_dev)) {
		printf("ADC controller device %s not ready\n", adc_dev->name);
		return;
	}

	/* Configure channels individually prior to sampling. */
	sequence.channels = 0;
	for (size_t i = 0U; i < CHANNEL_COUNT; i++) {
		sequence.channels |= BIT(channel_cfgs[i].channel_id);
		
		printf("Setting up ADC channel %d (index %u)...\n", 
		       channel_cfgs[i].channel_id, (unsigned int)i);
		
		ret = adc_channel_setup(adc_dev, &channel_cfgs[i]);
		if (ret != 0) {
			printf("Failed to setup channel %d (index %u), error: %d\n", 
			       channel_cfgs[i].channel_id, (unsigned int)i, ret);
			return;
		}
		
		printf("Channel %d setup successful\n", channel_cfgs[i].channel_id);
		
		if ((vrefs_mv[i] == 0) && (channel_cfgs[i].reference == ADC_REF_INTERNAL)) {
			vrefs_mv[i] = adc_ref_internal(adc_dev);
			printf("Internal reference voltage for channel %d: %u mV\n",
			       channel_cfgs[i].channel_id, vrefs_mv[i]);
		}
	}

	printf("ADC task started, polling channels 4 and 5 every %d ms\n",
		ADC_POLL_INTERVAL_MS);
	printf("ADC device: %s\n", adc_dev->name);

	while (1) {
		// printf("ADC sequence reading [%u]:\n", count++);
		k_msleep(ADC_POLL_INTERVAL_MS);

		ret = adc_read(adc_dev, &sequence);
		if (ret < 0) {
			printf("Could not read (%d)\n", ret);
			continue;
		}

		/* Update ADC values for shell command access */
		k_mutex_lock(&adc_values.mutex, K_FOREVER);
		for (size_t channel_index = 0U; channel_index < CHANNEL_COUNT; channel_index++) {
			int32_t val_mv = channel_reading[channel_index];

			/* Store raw value */
			adc_values.raw[channel_index] = channel_reading[channel_index];

			ret = adc_raw_to_millivolts(vrefs_mv[channel_index],
						    channel_cfgs[channel_index].gain,
						    ADC_RESOLUTION, &val_mv);

			/* Store converted value if available */
			if ((ret >= 0) && (vrefs_mv[channel_index] != 0)) {
				adc_values.mv[channel_index] = val_mv;
				adc_values.valid[channel_index] = true;
			} else {
				adc_values.valid[channel_index] = false;
			}
		}
		k_mutex_unlock(&adc_values.mutex);
	}
}

#define ADC_TASK_STACK_SIZE 1024
#define ADC_TASK_PRIORITY 5

/* Thread stack and control block */
K_THREAD_STACK_DEFINE(adc_task_stack, ADC_TASK_STACK_SIZE);
static struct k_thread adc_task_thread_data;
static bool adc_task_started = false;

static int cmd_adc_show(const struct shell *sh, size_t argc, char **argv)
{
	k_mutex_lock(&adc_values.mutex, K_FOREVER);
	
	shell_print(sh, "ADC Channel Values:");
	shell_print(sh, "===================");
	
	for (size_t i = 0U; i < CHANNEL_COUNT; i++) {
		shell_print(sh, "Channel %d:", channel_cfgs[i].channel_id);
		shell_print(sh, "  Raw value: %d", adc_values.raw[i]);
		
		if (adc_values.valid[i]) {
			shell_print(sh, "  Voltage: %d mV", adc_values.mv[i]);
		} else {
			shell_print(sh, "  Voltage: (not available)");
		}
	}
	
	k_mutex_unlock(&adc_values.mutex);
	
	thread_analyzer_print();
	return 0;
}

SHELL_CMD_REGISTER(adc, NULL, "Show ADC channel values", cmd_adc_show);

void adc_task_enable(void)
{
	if (adc_task_started) {
		printf("ADC task already started\n");
		return;
	}

	/* Initialize mutex for ADC values */
	k_mutex_init(&adc_values.mutex);
	
	/* Initialize ADC values */
	for (size_t i = 0U; i < CHANNEL_COUNT; i++) {
		adc_values.valid[i] = false;
		adc_values.raw[i] = 0;
		adc_values.mv[i] = 0;
	}

	/* Create and start the ADC task thread */
	k_thread_create(&adc_task_thread_data, adc_task_stack,
			K_THREAD_STACK_SIZEOF(adc_task_stack),
			adc_task_thread, NULL, NULL, NULL,
			ADC_TASK_PRIORITY, 0, K_NO_WAIT);
	
	k_thread_name_set(&adc_task_thread_data, "adc_task");
	adc_task_started = true;
	printf("ADC task enabled and started\n");
}
