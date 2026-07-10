/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <nrf.h>
#include <esb.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
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
#include "rc_ptx_uart_channels.h"
#include "rc_uart_bridge.h"

LOG_MODULE_REGISTER(esb_ptx, CONFIG_ESB_PTX_APP_LOG_LEVEL);

#define TX_INTERVAL_MS 100
#define PAIR_TX_INTERVAL_MS 1000

static bool ready = true;
static uint8_t ctrl_seq;
static uint8_t pair_seq;
static uint8_t peer_status_channels;
static struct esb_payload rx_payload;
static struct esb_payload tx_payload;
static struct rc_link_frame ctrl_frame;
static int64_t next_pair_tx_ms;
static bool pending_pair_restore;
static bool pair_probe_pending;
static struct uart_rc_esb_config saved_paired_cfg;

static void pair_complete(const char *reason)
{
	pending_pair_restore = false;
	pair_probe_pending = false;
	(void)rc_esb_radio_apply_cfg(&saved_paired_cfg);
	rc_esb_radio_end_pair_broadcast();
	LOG_WRN("%s — enter UART CTRL forward", reason);
}

static void restore_paired_radio_cfg(void)
{
	if (!pending_pair_restore) {
		return;
	}

	(void)rc_esb_radio_apply_cfg(&saved_paired_cfg);
	pending_pair_restore = false;
}

static void leds_update(uint8_t value)
{
	uint32_t leds_mask =
		(!(value % 8 > 0 && value % 8 <= 4) ? DK_LED1_MSK : 0) |
		(!(value % 8 > 1 && value % 8 <= 5) ? DK_LED2_MSK : 0) |
		(!(value % 8 > 2 && value % 8 <= 6) ? DK_LED3_MSK : 0) |
		(!(value % 8 > 3) ? DK_LED4_MSK : 0);

	dk_set_leds(leds_mask);
}

static void handle_status_frame(const struct esb_payload *payload)
{
	struct rc_link_frame status;
	int err;

	err = rc_link_unpack(payload->data, payload->length, &status);
	if (err || status.type != RC_LINK_TYPE_STATUS) {
		LOG_DBG("Ignore invalid status frame: %d", err);
		return;
	}

	if (peer_status_channels != status.channel_count) {
		peer_status_channels = status.channel_count;
		LOG_DBG("Aircraft status channel count: %u", peer_status_channels);
	}

	rc_uart_bridge_on_esb_status(&status);
}

void event_handler(struct esb_evt const *event)
{
	static uint16_t pair_tx_ok;
	static uint16_t pair_tx_fail;

	ready = true;

	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		if (rc_esb_radio_pair_broadcast_active()) {
			if (pending_pair_restore) {
				/* ACK on default listen address. */
				pair_tx_ok++;
				restore_paired_radio_cfg();
				pair_complete("PRX ACK on PAIR");
				LOG_WRN("pair stats ok=%u fail=%u", pair_tx_ok, pair_tx_fail);
				pair_tx_ok = 0;
				pair_tx_fail = 0;
			} else if (pair_probe_pending) {
				/*
				 * ACK on the new paired address — PRX already
				 * applied even if the default-addr ACK was lost.
				 */
				pair_probe_pending = false;
				pair_complete("PRX reachable on new addr");
				pair_tx_ok = 0;
				pair_tx_fail = 0;
			} else {
				restore_paired_radio_cfg();
			}
		} else {
			restore_paired_radio_cfg();
			LOG_DBG("Control frame sent");
		}
		break;
	case ESB_EVENT_TX_FAILED:
		if (pending_pair_restore && rc_esb_radio_pair_broadcast_active()) {
			pair_tx_fail++;
			LOG_WRN("PAIR TX no ACK (fail=%u) — probe new addr next",
				pair_tx_fail);
			restore_paired_radio_cfg();
			/* PRX may already have paired; probe on new addresses. */
			pair_probe_pending = true;
			pending_pair_restore = false;
		} else if (pair_probe_pending && rc_esb_radio_pair_broadcast_active()) {
			LOG_WRN("New-addr probe no ACK — resume PAIR broadcast");
			pair_probe_pending = false;
		} else {
			restore_paired_radio_cfg();
			LOG_DBG("Control/PAIR frame failed");
		}
		break;
	case ESB_EVENT_RX_RECEIVED:
		while (esb_read_rx_payload(&rx_payload) == 0) {
			handle_status_frame(&rx_payload);
		}
		/*
		 * STATUS on the new address during pair window also proves PRX
		 * applied the paired config.
		 */
		if (rc_esb_radio_pair_broadcast_active() && !pending_pair_restore) {
			pair_probe_pending = false;
			pair_complete("STATUS RX on new addr");
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

static int pack_ctrl_payload(struct esb_payload *payload)
{
	int len;

	len = rc_link_pack(&ctrl_frame, payload->data, sizeof(payload->data));
	if (len < 0) {
		return len;
	}

	payload->length = (uint8_t)len;
	payload->pipe = RC_LINK_PIPE;
	payload->noack = false;

	return 0;
}

static int pack_pair_payload(struct esb_payload *payload)
{
	struct rc_link_pair_payload pair_payload;
	struct rc_link_frame pair_frame;
	int err;
	int len;

	err = rc_esb_radio_export_pair_payload(&pair_payload);
	if (err != 0) {
		return err;
	}

	err = rc_link_pair_encode(&pair_payload, pair_seq++, &pair_frame);
	if (err != 0) {
		return err;
	}

	len = rc_link_pack(&pair_frame, payload->data, sizeof(payload->data));
	if (len < 0) {
		return len;
	}

	payload->length = (uint8_t)len;
	payload->pipe = RC_LINK_PIPE;
	payload->noack = false;

	return 0;
}

int main(void)
{
	int err;

	LOG_WRN("ESB PTX ready (UART RC forward; Btn4 PAIR until PRX ACK)");

	err = clocks_start();
	if (err) {
		return 0;
	}

	err = dk_leds_init();
	if (err) {
		LOG_ERR("LEDs initialization failed, err %d", err);
		return 0;
	}

	err = esb_initialize();
	if (err) {
		LOG_ERR("ESB initialization failed, err %d", err);
		return 0;
	}

	err = rc_uart_bridge_init();
	if (err) {
		LOG_ERR("UART RC bridge init failed, err %d", err);
		return 0;
	}

	next_pair_tx_ms = 0;

	while (1) {
		if (ready) {
			ready = false;
			esb_flush_tx();

			struct uart_rc_esb_config paired_cfg;
			const bool pair_window = rc_esb_radio_pair_broadcast_active();

			if (pair_window && pair_probe_pending) {
				/*
				 * After a lost default-addr ACK, PRX may already be on
				 * the new addresses. Probe there before more PAIR TX.
				 */
				(void)rc_esb_radio_get_config(&paired_cfg);
				saved_paired_cfg = paired_cfg;
				err = pack_pair_payload(&tx_payload);
				if (err) {
					LOG_ERR("Pack probe frame failed: %d", err);
					pair_probe_pending = false;
					ready = true;
					continue;
				}
				pending_pair_restore = false;
				LOG_WRN("Probe TX on new paired addr (len=%u)", tx_payload.length);
			} else if (pair_window && k_uptime_get() >= next_pair_tx_ms) {
				next_pair_tx_ms = k_uptime_get() + PAIR_TX_INTERVAL_MS;

				(void)rc_esb_radio_get_config(&paired_cfg);
				err = pack_pair_payload(&tx_payload);
				if (err) {
					LOG_ERR("Pack pair frame failed: %d", err);
					ready = true;
					continue;
				}

				err = rc_esb_radio_apply_pair_listen();
				if (err != 0) {
					LOG_ERR("Apply pair listen failed: %d", err);
					ready = true;
					continue;
				}
				saved_paired_cfg = paired_cfg;
				pending_pair_restore = true;
				pair_probe_pending = false;
				LOG_WRN("PAIR TX on default addr (seq payload len=%u)",
					tx_payload.length);
			} else if (!pair_window) {
				const struct rc_channel_bank *bank =
					rc_ptx_get_active_control_bank();

				if (bank == NULL) {
					ready = true;
					continue;
				}

				err = rc_link_frame_fill(&ctrl_frame, RC_LINK_TYPE_CTRL, ctrl_seq++, 0,
							 bank);
				if (err) {
					LOG_ERR("Prepare control frame failed: %d", err);
					ready = true;
					continue;
				}

				if (ctrl_frame.channel_count > 1U) {
					leds_update((uint8_t)ctrl_frame.channels[1]);
				}

				err = pack_ctrl_payload(&tx_payload);
			} else {
				/* Pair window active but waiting for next PAIR slot. */
				ready = true;
				continue;
			}
			if (err) {
				LOG_ERR("Pack TX frame failed: %d", err);
				ready = true;
				continue;
			}

			err = esb_write_payload(&tx_payload);
			if (err) {
				LOG_ERR("Payload write failed, err %d", err);
				restore_paired_radio_cfg();
				ready = true;
			}
		}
		k_sleep(K_MSEC(TX_INTERVAL_MS));
	}
}
