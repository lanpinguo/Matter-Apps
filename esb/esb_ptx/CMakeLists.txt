#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(esb_ptx)

target_include_directories(app PRIVATE ../common)

target_sources(app PRIVATE
  src/main.c
  src/rc_ptx_channels.c
  ../common/rc_link.c
  ../common/rc_channel_bank.c
)
