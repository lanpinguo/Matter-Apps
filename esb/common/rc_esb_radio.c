/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "rc_esb_radio.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "rc_link.h"

LOG_MODULE_REGISTER(rc_esb_radio, CONFIG_LOG_DEFAULT_LEVEL);

#ifndef ESB_RADIO_SUBTREE
#error "Define ESB_RADIO_SUBTREE to esb_ptx or esb_prx"
#endif

#ifndef ESB_RADIO_MODE
#error "Define ESB_RADIO_MODE to ESB_MODE_PTX or ESB_MODE_PRX"
#endif

#define RC_ESB_SETTINGS_KEY ESB_RADIO_SUBTREE "/radio"
#define RC_ESB_STORE_VERSION 1U

struct rc_esb_store {
	uint16_t version;
	struct uart_rc_esb_config cfg;
} __packed;

static struct uart_rc_esb_config staged_cfg;
static rc_esb_event_handler_t event_handler;
static rc_esb_radio_applied_cb_t applied_cb;
static bool staged_valid;
static bool has_saved_config;
static bool esb_has_been_initialized;
static int64_t pair_broadcast_until_ms;

#define RC_ESB_PAIR_BROADCAST_MS 30000U

static void rc_esb_radio_defaults(struct uart_rc_esb_config *cfg)
{
	static const uint8_t base0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
	static const uint8_t base1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
	static const uint8_t prefixes[8] = {0xE7, 0xC2, 0xC3, 0xC4,
					    0xC5, 0xC6, 0xC7, 0xC8};

	cfg->bitrate = ESB_BITRATE_2MBPS;
	cfg->tx_power = 8;
	cfg->retransmit_delay = 600U;
	cfg->pipe = RC_LINK_PIPE;
	memcpy(cfg->base0, base0, sizeof(base0));
	memcpy(cfg->base1, base1, sizeof(base1));
	memcpy(cfg->prefixes, prefixes, sizeof(prefixes));
}

static int rc_esb_radio_hw_apply(const struct uart_rc_esb_config *cfg)
{
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	if (cfg == NULL) {
		return -EINVAL;
	}

	/*
	 * ESB disable/uninit must only happen after a successful esb_init().
	 * Calling esb_disable too early can lead to nrfx_timer_uninit assertions
	 * (observed panic in nrfx_timer.c:188).
	 */
	if (esb_has_been_initialized) {
		esb_disable();
	}

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.retransmit_delay = cfg->retransmit_delay;
	config.bitrate = (enum esb_bitrate)cfg->bitrate;
	config.event_handler = event_handler;
	config.mode = ESB_RADIO_MODE;
	config.selective_auto_ack = true;
	config.tx_output_power = cfg->tx_power;
	if (IS_ENABLED(CONFIG_ESB_FAST_SWITCHING)) {
		config.use_fast_ramp_up = true;
	}

	err = esb_init(&config);
	if (err != 0) {
		return err;
	}

	esb_has_been_initialized = true;

	err = esb_set_base_address_0((uint8_t *)cfg->base0);
	if (err != 0) {
		return err;
	}

	err = esb_set_base_address_1((uint8_t *)cfg->base1);
	if (err != 0) {
		return err;
	}

	err = esb_set_prefixes((uint8_t *)cfg->prefixes, ARRAY_SIZE(cfg->prefixes));
	if (err != 0) {
		return err;
	}

	if (ESB_RADIO_MODE == ESB_MODE_PRX) {
		err = esb_start_rx();
	}

	if (err == 0 && applied_cb != NULL) {
		applied_cb();
	}

	return err;
}

static int rc_esb_radio_store_save(void)
{
	struct rc_esb_store store = {
		.version = RC_ESB_STORE_VERSION,
	};

	if (!staged_valid) {
		return -EINVAL;
	}

	store.cfg = staged_cfg;
	return settings_save_one(RC_ESB_SETTINGS_KEY, &store, sizeof(store));
}

static int rc_esb_radio_settings_set(const char *name, size_t len,
				     settings_read_cb read_cb, void *cb_arg)
{
	struct rc_esb_store store;
	ssize_t rd;

	if (strcmp(name, "radio") != 0) {
		return -ENOENT;
	}

	if (len != sizeof(store)) {
		return -EINVAL;
	}

	rd = read_cb(cb_arg, &store, sizeof(store));
	if (rd != (ssize_t)sizeof(store)) {
		return -EIO;
	}

	if (store.version != RC_ESB_STORE_VERSION) {
		return -ENOTSUP;
	}

	staged_cfg = store.cfg;
	staged_valid = true;
	has_saved_config = true;
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(rc_esb_radio, ESB_RADIO_SUBTREE, NULL,
			       rc_esb_radio_settings_set, NULL, NULL);

void rc_esb_radio_set_applied_cb(rc_esb_radio_applied_cb_t cb)
{
	applied_cb = cb;
}

bool rc_esb_radio_has_saved_config(void)
{
	return has_saved_config;
}

int rc_esb_radio_load_settings(void)
{
	return settings_load_subtree(ESB_RADIO_SUBTREE);
}

int rc_esb_radio_get_config(struct uart_rc_esb_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	if (!staged_valid) {
		rc_esb_radio_defaults(&staged_cfg);
		staged_valid = true;
	}

	*cfg = staged_cfg;
	return 0;
}

int rc_esb_radio_set_radio(uint8_t bitrate, int8_t tx_power, uint16_t retransmit_delay,
			   uint8_t pipe)
{
	if (!staged_valid) {
		rc_esb_radio_get_config(&staged_cfg);
	}

	staged_cfg.bitrate = bitrate;
	staged_cfg.tx_power = tx_power;
	staged_cfg.retransmit_delay = retransmit_delay;
	staged_cfg.pipe = pipe;
	staged_valid = true;
	return 0;
}

int rc_esb_radio_set_addr(const uint8_t base0[4], const uint8_t base1[4],
			  const uint8_t prefixes[8])
{
	if (base0 == NULL || base1 == NULL || prefixes == NULL) {
		return -EINVAL;
	}

	if (!staged_valid) {
		rc_esb_radio_get_config(&staged_cfg);
	}

	memcpy(staged_cfg.base0, base0, 4U);
	memcpy(staged_cfg.base1, base1, 4U);
	memcpy(staged_cfg.prefixes, prefixes, 8U);
	staged_valid = true;
	return 0;
}

int rc_esb_radio_pair(struct uart_rc_esb_config *out_cfg)
{
	uint8_t rnd[13];
	int err;

	err = sys_csrand_get(rnd, sizeof(rnd));
	if (err != 0) {
		return err;
	}

	if (!staged_valid) {
		rc_esb_radio_get_config(&staged_cfg);
	}

	memcpy(staged_cfg.base0, &rnd[0], 4U);
	memcpy(staged_cfg.base1, &rnd[4], 4U);
	memcpy(staged_cfg.prefixes, &rnd[5], 8U);
	staged_cfg.prefixes[0] = rnd[12];
	staged_valid = true;

	if (out_cfg != NULL) {
		*out_cfg = staged_cfg;
	}

	LOG_DBG("Generated new ESB pairing addresses");
	return 0;
}

int rc_esb_radio_export_pair_payload(struct rc_link_pair_payload *pair)
{
	if (pair == NULL) {
		return -EINVAL;
	}

	if (!staged_valid) {
		(void)rc_esb_radio_get_config(&staged_cfg);
	}

	memcpy(pair->base0, staged_cfg.base0, sizeof(pair->base0));
	memcpy(pair->base1, staged_cfg.base1, sizeof(pair->base1));
	memcpy(pair->prefixes, staged_cfg.prefixes, sizeof(pair->prefixes));

	return 0;
}

int rc_esb_radio_apply_pair_payload(const struct rc_link_pair_payload *pair, bool save_to_flash)
{
	int err;

	if (pair == NULL) {
		return -EINVAL;
	}

	err = rc_esb_radio_set_addr(pair->base0, pair->base1, pair->prefixes);
	if (err != 0) {
		return err;
	}

	err = rc_esb_radio_apply();
	if (err != 0) {
		return err;
	}

	if (!save_to_flash) {
		return 0;
	}

	err = rc_esb_radio_save();
	if (err == 0) {
		has_saved_config = true;
	}

	return err;
}

int rc_esb_radio_apply_cfg(const struct uart_rc_esb_config *cfg)
{
	if (cfg == NULL) {
		return -EINVAL;
	}

	return rc_esb_radio_hw_apply(cfg);
}

int rc_esb_radio_apply_pair_listen(void)
{
	struct uart_rc_esb_config listen_cfg;

	memset(&listen_cfg, 0, sizeof(listen_cfg));
	rc_esb_radio_defaults(&listen_cfg);

	return rc_esb_radio_hw_apply(&listen_cfg);
}

void rc_esb_radio_begin_pair_broadcast(uint32_t duration_ms)
{
	if (duration_ms == 0U) {
		duration_ms = RC_ESB_PAIR_BROADCAST_MS;
	}

	pair_broadcast_until_ms = k_uptime_get() + (int64_t)duration_ms;
	LOG_WRN("PAIR broadcast up to %u ms (ends early on PRX ACK)", duration_ms);
}

void rc_esb_radio_end_pair_broadcast(void)
{
	if (pair_broadcast_until_ms == 0) {
		return;
	}

	pair_broadcast_until_ms = 0;
	LOG_WRN("PAIR broadcast ended — UART CTRL forward mode");
}

bool rc_esb_radio_pair_broadcast_active(void)
{
	if (pair_broadcast_until_ms == 0) {
		return false;
	}

	if (k_uptime_get() >= pair_broadcast_until_ms) {
		pair_broadcast_until_ms = 0;
		LOG_WRN("PAIR broadcast timed out — UART CTRL forward mode");
		return false;
	}

	return true;
}

int rc_esb_radio_clear_saved_config(void)
{
	int err;

	/* Delete persisted settings and reset to defaults (pair-listen mode). */
	err = settings_delete(RC_ESB_SETTINGS_KEY);
	if (err != 0 && err != -ENOENT) {
		return err;
	}

	staged_valid = false;
	has_saved_config = false;

	/* Make sure the radio still runs and listens on the known pairing address. */
	rc_esb_radio_defaults(&staged_cfg);
	staged_valid = true;

	return rc_esb_radio_apply();
}

int rc_esb_radio_apply(void)
{
	if (!staged_valid) {
		rc_esb_radio_get_config(&staged_cfg);
	}

	return rc_esb_radio_hw_apply(&staged_cfg);
}

int rc_esb_radio_save(void)
{
	int err = rc_esb_radio_store_save();

	if (err == 0) {
		has_saved_config = true;
	}

	return err;
}

int rc_esb_radio_init(rc_esb_event_handler_t handler)
{
	int err;

	event_handler = handler;
	staged_valid = false;
	has_saved_config = false;
	esb_has_been_initialized = false;

	err = settings_subsys_init();
	if (err != 0) {
		LOG_WRN("Settings subsys init failed: %d", err);
	}

	err = rc_esb_radio_load_settings();
	if (err != 0) {
		LOG_WRN("Settings load failed: %d", err);
	}

	if (!staged_valid) {
		rc_esb_radio_defaults(&staged_cfg);
		staged_valid = true;
	}

	return rc_esb_radio_apply();
}

int rc_esb_radio_handle_req(const struct uart_rc_esb_req *req,
			    struct uart_rc_esb_rsp *rsp)
{
	struct uart_rc_esb_config cfg;
	int err = 0;

	if (req == NULL || rsp == NULL) {
		return -EINVAL;
	}

	memset(rsp, 0, sizeof(*rsp));
	rsp->seq = req->seq;
	rsp->cmd = req->cmd;

	switch (req->cmd) {
	case UART_RC_ESB_CMD_GET_CONFIG:
		err = rc_esb_radio_get_config(&cfg);
		if (err == 0) {
			rsp->data_len = (uint8_t)sizeof(cfg);
			memcpy(rsp->data, &cfg, sizeof(cfg));
		}
		break;
	case UART_RC_ESB_CMD_SET_RADIO:
		if (req->data_len < 5U) {
			err = -EINVAL;
			break;
		}
		err = rc_esb_radio_set_radio(req->data[0], (int8_t)req->data[1],
					     sys_get_le16(&req->data[2]), req->data[4]);
		break;
	case UART_RC_ESB_CMD_SET_ADDR:
		if (req->data_len < 16U) {
			err = -EINVAL;
			break;
		}
		err = rc_esb_radio_set_addr(&req->data[0], &req->data[4], &req->data[8]);
		break;
	case UART_RC_ESB_CMD_PAIR:
		err = rc_esb_radio_pair(&cfg);
		if (err == 0) {
			err = rc_esb_radio_apply();
		}
		if (err == 0) {
			err = rc_esb_radio_save();
		}
		if (err == 0) {
			rsp->data_len = (uint8_t)sizeof(cfg);
			memcpy(rsp->data, &cfg, sizeof(cfg));
			rc_esb_radio_begin_pair_broadcast(RC_ESB_PAIR_BROADCAST_MS);
		}
		break;
	case UART_RC_ESB_CMD_APPLY:
		err = rc_esb_radio_apply();
		break;
	case UART_RC_ESB_CMD_SAVE:
		err = rc_esb_radio_save();
		break;
	default:
		err = -ENOTSUP;
		break;
	}

	rsp->status = (int8_t)((err == 0) ? 0 : err);
	LOG_WRN("ESB req cmd=0x%02x -> status=%d data_len=%u", req->cmd, rsp->status,
		rsp->data_len);
	return 0;
}
