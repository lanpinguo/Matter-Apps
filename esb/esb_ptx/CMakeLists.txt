#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esb_ptx)

target_include_directories(app PRIVATE ../common)

target_compile_definitions(app PRIVATE
  ESB_RADIO_SUBTREE=\"esb_ptx\"
  ESB_RADIO_MODE=ESB_MODE_PTX
)

target_sources(app PRIVATE
  src/main.c
  src/rc_ptx_channels.c
  src/rc_ptx_uart_channels.c
  src/rc_uart_bridge.c
  ../common/rc_link.c
  ../common/rc_channel_bank.c
  ../common/uart_rc_link.c
  ../common/rc_esb_radio.c
  ../common/rc_uart_esb_req_work.c
  ../common/rc_uart_log_fwd.c
)
