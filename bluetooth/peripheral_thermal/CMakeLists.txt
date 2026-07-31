#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

# Custom DT bindings under ./dts/bindings
list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(peripheral_thermal)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE
  src/main.c
  src/mlx90640_sensor.c
  src/mlx90640_i2c.c
  src/MLX90640_API.c
)
