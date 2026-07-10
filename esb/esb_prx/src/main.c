/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <nrf.h>
#include <esb.h>
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <dk_buttons_and_leds.h>
#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif
#include <nrf_erratas.h>
#if NRF54L_ERRATA_20_PRESENT
#include <hal/nrf_power.h>
#endif /* NRF54L_ERRATA_20_PRESENT */
#if defined(NRF54LM20A_ENGA_XXAA)
#include <hal/nrf_clock.h>
#endif /* defined(NRF54LM20A_ENGA_XXAA) */

#include "rc_channel_bank.h"
#include "rc_esb_radio.h"
#include "rc_link.h"
#include "rc_prx_channels.h"
#include "rc_uart_bridge.h"

LOG_MODULE_REGISTER(esb_prx, CONFIG_ESB_PRX_APP_LOG_LEVEL);

#define KEY_RADIO_CLEAR            DK_BTN4_MSK
#define RADIO_CLEAR_HOLD_TIME_MS  5000

static uint8_t status_seq;
static uint8_t last_ctrl_seq;
static uint8_t peer_ctrl_channels;
static bool pairing_mode;
static bool radio_clear_armed;
static struct k_work_delayable radio_clear_work;
static struct k_work_delayable pair_apply_work;
static struct rc_link_pair_payload pending_pair_payload;
static bool pending_pair_valid;
static struct esb_payload rx_payload;
static struct esb_payload tx_payload;
static struct rc_link_frame status_frame;

/* Leave enough time for ESB ACK on the default address before switching. */
#define PAIR_APPLY_DELAY_MS 100

static void leds_update(uint8_t value)
{
	uint32_t leds_mask =
		(!(value % 8 > 0 && value % 8 <= 4) ? DK_LED1_MSK : 0) |
		(!(value % 8 > 1 && value % 8 <= 5) ? DK_LED2_MSK : 0) |
		(!(value % 8 > 2 && value % 8 <= 6) ? DK_LED3_MSK : 0) |
		(!(value % 8 > 3) ? DK_LED4_MSK : 0);

	dk_set_leds(leds_mask);
}

static int queue_status_payload(void)
{
	int err;
	int len;

	err = rc_link_frame_fill(&status_frame, RC_LINK_TYPE_STATUS, status_seq++,
				 RC_STATUS_FLAG_ARMED, &rc_prx_status_bank);
	if (err) {
		return err;
	}

	len = rc_link_pack(&status_frame, tx_payload.data, sizeof(tx_payload.data));
	if (len < 0) {
		return len;
	}

	tx_payload.length = (uint8_t)len;
	tx_payload.pipe = RC_LINK_PIPE;
	tx_payload.noack = false;

	err = esb_write_payload(&tx_payload);
	if (err) {
		LOG_ERR("Status payload write failed: %d", err);
	}

	return err;
}

static void pair_apply_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (!pending_pair_valid) {
		return;
	}

	pending_pair_valid = false;
	err = rc_esb_radio_apply_pair_payload(&pending_pair_payload, true);
	if (err != 0) {
		LOG_ERR("Pair apply/save failed: %d", err);
		pairing_mode = true;
		return;
	}

	pairing_mode = false;
	LOG_WRN("Paired from first valid pair frame and saved");
}

static void handle_pair_frame(const struct rc_link_frame *frame)
{
	struct rc_link_pair_payload pair_payload;
	int err;

	if (!pairing_mode) {
		LOG_DBG("Ignore PAIR (not in pair mode)");
		return;
	}

	err = rc_link_pair_decode(frame, &pair_payload);
	if (err != 0) {
		LOG_DBG("Ignore invalid pair frame: %d", err);
		return;
	}

	/*
	 * Delay address switch so the ESB ACK for this PAIR frame completes on
	 * the default listen address. k_work_submit was ~1ms and broke PTX ACK.
	 */
	if (pending_pair_valid) {
		return;
	}

	pending_pair_payload = pair_payload;
	pending_pair_valid = true;
	pairing_mode = false;
	LOG_WRN("PAIR frame RX — apply in %d ms (after ACK)", PAIR_APPLY_DELAY_MS);
	(void)k_work_schedule(&pair_apply_work, K_MSEC(PAIR_APPLY_DELAY_MS));
}

static void handle_ctrl_frame(const struct rc_link_frame *ctrl)
{
	if (ctrl->type != RC_LINK_TYPE_CTRL) {
		return;
	}

	last_ctrl_seq = ctrl->seq;

	if (peer_ctrl_channels != ctrl->channel_count) {
		peer_ctrl_channels = ctrl->channel_count;
		LOG_INF("Controller channel count: %u", peer_ctrl_channels);
	}

	rc_link_log_channels("Control", ctrl);

	if (ctrl->channel_count > 1U) {
		leds_update((uint8_t)ctrl->channels[1]);
	}
}

static void handle_rx_frame(const struct esb_payload *payload)
{
	struct rc_link_frame frame;
	int err;

	err = rc_link_unpack(payload->data, payload->length, &frame);
	if (err != 0) {
		LOG_DBG("Ignore invalid RX frame: %d", err);
		return;
	}

	if (frame.type == RC_LINK_TYPE_PAIR) {
		handle_pair_frame(&frame);
		return;
	}

	handle_ctrl_frame(&frame);
}

static void radio_clear_work_handler(struct k_work *work)
{
	int err;
	ARG_UNUSED(work);

	radio_clear_armed = false;

	err = rc_esb_radio_clear_saved_config();
	if (err != 0) {
		LOG_ERR("Radio config clear failed: %d", err);
		return;
	}

	pairing_mode = true;
	LOG_INF("Radio config cleared, re-enter pair mode");
}

static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	uint32_t changed = button_state & has_changed;

	if ((changed & KEY_RADIO_CLEAR) == 0U) {
		return;
	}

		if ((button_state & KEY_RADIO_CLEAR) != 0U) {
		radio_clear_armed = true;
		k_work_schedule(&radio_clear_work, K_MSEC(RADIO_CLEAR_HOLD_TIME_MS));
		LOG_INF("Hold button %lu to clear radio config (5s)", (unsigned long)KEY_RADIO_CLEAR);
	} else {
		radio_clear_armed = false;
		(void)k_work_cancel_delayable(&radio_clear_work);
	}
}

void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		(void)queue_status_payload();
		break;
	case ESB_EVENT_TX_FAILED:
		LOG_DBG("ACK with status failed");
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			handle_rx_frame(&rx_payload);
		}
		break;
	default:
		break;
	}
}

#if defined(CONFIG_CLOCK_CONTROL_NRF)
int clocks_start(void)
{
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		LOG_ERR("Unable to get the Clock manager");
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		LOG_ERR("Clock request failed: %d", err);
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err);

#if NRF54L_ERRATA_20_PRESENT
	if (nrf54l_errata_20()) {
		nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_CONSTLAT);
	}
#endif /* NRF54L_ERRATA_20_PRESENT */

#if defined(NRF54LM20A_ENGA_XXAA)
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif

	LOG_DBG("HF clock started");
	return 0;
}

#elif defined(CONFIG_CLOCK_CONTROL_NRF2)

int clocks_start(void)
{
	int err;
	int res;
	const struct device *radio_clk_dev =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client radio_cli;

	nrf_lrcconf_poweron_force_set(NRF_LRCCONF010, NRF_LRCCONF_POWER_DOMAIN_1, true);

	sys_notify_init_spinwait(&radio_cli.notify);

	err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);

	do {
		err = sys_notify_fetch_result(&radio_cli.notify, &res);
		if (!err && res) {
			LOG_ERR("Clock could not be started: %d", res);
			return res;
		}
	} while (err == -EAGAIN);

	nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
	nrf_lrcconf_task_trigger(NRF_LRCCONF000, NRF_LRCCONF_TASK_CLKSTART_0);

	LOG_DBG("HF clock started");

	return 0;
}

#else
BUILD_ASSERT(false, "No Clock Control driver");
#endif /* defined(CONFIG_CLOCK_CONTROL_NRF2) */

int esb_initialize(void)
{
	return rc_esb_radio_init(event_handler);
}

static void on_radio_applied(void)
{
	(void)queue_status_payload();
}

int main(void)
{
	int err;

	LOG_INF("ESB receiver with bidirectional telemetry");

	rc_prx_channels_bind_ctrl_seq(&last_ctrl_seq);
	rc_esb_radio_set_applied_cb(on_radio_applied);

	err = clocks_start();
	if (err) {
		return 0;
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs initialization failed, err %d", err);
		return 0;
	}

	k_work_init_delayable(&radio_clear_work, radio_clear_work_handler);
	k_work_init_delayable(&pair_apply_work, pair_apply_work_handler);
	radio_clear_armed = false;
	dk_buttons_init(button_handler);

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}
	pairing_mode = !rc_esb_radio_has_saved_config();
	if (pairing_mode) {
		LOG_WRN("No saved radio config — pair mode ON (waiting ESB PAIR)");
	} else {
		LOG_WRN("Saved radio config loaded — pair mode OFF (hold Btn4 5s to clear)");
	}

	err = rc_uart_bridge_init();
	if (err) {
		LOG_ERR("UART bridge init failed, err %d", err);
		return 0;
	}

	LOG_INF("Local status sources: %u (max)", (unsigned int)rc_prx_status_bank.slot_count);
	LOG_INF("Listening for control frames");

	return 0;
}
