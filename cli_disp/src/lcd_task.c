/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/shell/shell.h>
#include <stdio.h>
#include <string.h>

#include "lcd_task.h"

LOG_MODULE_REGISTER(lcd_task, LOG_LEVEL_DBG);

/* LCD尺寸定义 (逻辑宽高，与 MADCTL 方向一致) */
#define LCD_WIDTH  160
#define LCD_HEIGHT 80

/* ST7735S 160x80 常见模块：控制器 80 列 x 160 行，MV=1 时显示为 160x80。
 * 列/行偏移：多数 0.96 寸模块为 col_offset=26, row_offset=1，仅右半屏时请试 26/1 或 24/0。 */
#define LCD_COL_OFFSET  26
#define LCD_ROW_OFFSET  1

/* ST7735S命令定义 */
#define ST7735_NOP        0x00
#define ST7735_SWRESET    0x01
#define ST7735_RDDID      0x04
#define ST7735_RDDST      0x09
#define ST7735_SLPIN      0x10
#define ST7735_SLPOUT     0x11
#define ST7735_PTLON      0x12
#define ST7735_NORON      0x13
#define ST7735_INVOFF     0x20
#define ST7735_INVON      0x21
#define ST7735_DISPOFF    0x28
#define ST7735_DISPON     0x29
#define ST7735_CASET      0x2A
#define ST7735_RASET      0x2B
#define ST7735_RAMWR      0x2C
#define ST7735_RAMRD      0x2E
#define ST7735_PTLAR      0x30
#define ST7735_COLMOD     0x3A
#define ST7735_MADCTL     0x36
#define ST7735_FRMCTR1    0xB1
#define ST7735_FRMCTR2    0xB2
#define ST7735_FRMCTR3    0xB3
#define ST7735_INVCTR     0xB4
#define ST7735_DISSET5    0xB6
#define ST7735_PWCTR1     0xC0
#define ST7735_PWCTR2     0xC1
#define ST7735_PWCTR3     0xC2
#define ST7735_PWCTR4     0xC3
#define ST7735_PWCTR5     0xC4
#define ST7735_VMCTR1     0xC5
#define ST7735_RDID1      0xDA
#define ST7735_RDID2      0xDB
#define ST7735_RDID3      0xDC
#define ST7735_RDID4      0xDD
#define ST7735_PWCTR6     0xFC
#define ST7735_GMCTRP1    0xE0
#define ST7735_GMCTRN1    0xE1

/* 颜色定义 (RGB565格式) */
#define COLOR_BLACK       0x0000
#define COLOR_BLUE        0x001F
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_YELLOW      0xFFE0
#define COLOR_WHITE       0xFFFF

/* 设备树节点定义 - 需要在overlay文件中配置 */
/* 对于nRF54L15使用spi20，对于nRF52840使用spi1 */
#if DT_NODE_EXISTS(DT_NODELABEL(spi21))
#define SPI_NODE           DT_NODELABEL(spi21)
#elif DT_NODE_EXISTS(DT_NODELABEL(spi1))
#define SPI_NODE           DT_NODELABEL(spi1)
#else
#error "No suitable SPI device found. Please configure SPI in device tree."
#endif
/* GPIO节点 - 对于nRF54L15使用gpio00，对于nRF52840使用gpio0 */
#if DT_NODE_EXISTS(DT_NODELABEL(gpio1))
#define GPIO_NODE          DT_NODELABEL(gpio1)
#elif DT_NODE_EXISTS(DT_NODELABEL(gpio0))
#define GPIO_NODE          DT_NODELABEL(gpio0)
#else
#error "No suitable GPIO device found."
#endif

/* GPIO引脚定义 - 需要在overlay文件中配置，这里使用默认值 */
/* 用户需要根据实际硬件连接修改这些引脚号 */
#define LCD_CS_PIN         6
#define LCD_DC_PIN         10
#define LCD_RST_PIN        11

static const struct device *spi_dev = DEVICE_DT_GET(SPI_NODE);
static const struct device *gpio_dev = DEVICE_DT_GET(GPIO_NODE);

/* ST7735S 4-line SPI: TSCYCW min 66ns -> max 15MHz (Section 8.4 Table 7);
 * SDA sampled at SCL rising edge -> Mode 0 (CPOL=0, CPHA=0), MSB first.
 */
static struct spi_config spi_cfg = {
	.frequency = 8000000,  /* 15 MHz, datasheet max for write (1/66ns) */
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
};

static bool lcd_initialized = false;

/* 发送命令 */
static void lcd_write_cmd(uint8_t cmd)
{
	struct spi_buf tx_buf = {
		.buf = &cmd,
		.len = 1,
	};
	struct spi_buf_set tx_buf_set = {
		.buffers = &tx_buf,
		.count = 1,
	};

	/* CS拉低 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 0);
	/* DC = 0 for command */
	gpio_pin_set_raw(gpio_dev, LCD_DC_PIN, 0);
	
	spi_write(spi_dev, &spi_cfg, &tx_buf_set);
	
	/* CS拉高 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 1);
}

/* 发送数据 */
static void lcd_write_data(uint8_t data)
{
	struct spi_buf tx_buf = {
		.buf = &data,
		.len = 1,
	};
	struct spi_buf_set tx_buf_set = {
		.buffers = &tx_buf,
		.count = 1,
	};

	/* CS拉低 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 0);
	/* DC = 1 for data */
	gpio_pin_set_raw(gpio_dev, LCD_DC_PIN, 1);
	
	spi_write(spi_dev, &spi_cfg, &tx_buf_set);
	
	/* CS拉高 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 1);
}

/* 发送多个数据字节 */
static void lcd_write_data_buf(const uint8_t *data, size_t len)
{
	struct spi_buf tx_buf = {
		.buf = (void *)data,
		.len = len,
	};
	struct spi_buf_set tx_buf_set = {
		.buffers = &tx_buf,
		.count = 1,
	};

	/* CS拉低 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 0);
	/* DC = 1 for data */
	gpio_pin_set_raw(gpio_dev, LCD_DC_PIN, 1);
	
	spi_write(spi_dev, &spi_cfg, &tx_buf_set);
	
	/* CS拉高 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 1);
}

/* 设置显示窗口。MADCTL 0xC8 含 MV=1，逻辑 x=控制器行、y=控制器列，故 CASET 用 y、RASET 用 x，并加偏移。 */
static void lcd_set_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
	uint8_t cs0 = LCD_COL_OFFSET + y0;
	uint8_t cs1 = LCD_COL_OFFSET + y1;
	uint8_t rs0 = LCD_ROW_OFFSET + x0;
	uint8_t rs1 = LCD_ROW_OFFSET + x1;

	lcd_write_cmd(ST7735_CASET);
	lcd_write_data(0x00);
	lcd_write_data(cs0);
	lcd_write_data(0x00);
	lcd_write_data(cs1);

	lcd_write_cmd(ST7735_RASET);
	lcd_write_data(0x00);
	lcd_write_data(rs0);
	lcd_write_data(0x00);
	lcd_write_data(rs1);

	lcd_write_cmd(ST7735_RAMWR);
}

/* 填充矩形区域 */
static void lcd_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color)
{
	uint8_t x1 = x + w - 1;
	uint8_t y1 = y + h - 1;
	uint32_t pixel_count = w * h;
	uint8_t color_bytes[2] = {(color >> 8) & 0xFF, color & 0xFF};

	if (x1 >= LCD_WIDTH || y1 >= LCD_HEIGHT) {
		return;
	}

	lcd_set_window(x, y, x1, y1);

	/* 使用静态缓冲区，避免动态分配内存 */
	#define MAX_BUF_SIZE 320  /* 160像素 * 2字节 */
	static uint8_t buffer[MAX_BUF_SIZE];
	uint32_t bytes_to_send = pixel_count * 2;
	uint32_t offset = 0;

	/* 分批发送数据，避免缓冲区溢出 */
	while (offset < bytes_to_send) {
		uint32_t chunk_size = (bytes_to_send - offset > MAX_BUF_SIZE) ? 
		                      MAX_BUF_SIZE : (bytes_to_send - offset);
		uint32_t pixels_in_chunk = chunk_size / 2;

		/* 填充缓冲区 */
		for (uint32_t i = 0; i < pixels_in_chunk; i++) {
			buffer[i * 2] = color_bytes[0];
			buffer[i * 2 + 1] = color_bytes[1];
		}

		/* 发送数据 */
		lcd_write_data_buf(buffer, chunk_size);
		offset += chunk_size;
	}
}

/* 清屏 */
static void lcd_clear(uint16_t color)
{
	lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

/* ST7735S初始化序列 */
static void lcd_init_sequence(void)
{
	/* 硬件复位 */
	gpio_pin_set_raw(gpio_dev, LCD_RST_PIN, 0);
	k_msleep(10);
	gpio_pin_set_raw(gpio_dev, LCD_RST_PIN, 1);
	k_msleep(120);

	/* 软件复位 */
	lcd_write_cmd(ST7735_SWRESET);
	k_msleep(150);

	/* 退出睡眠模式 */
	lcd_write_cmd(ST7735_SLPOUT);
	k_msleep(120);

	/* 帧率控制 */
	lcd_write_cmd(ST7735_FRMCTR1);
	lcd_write_data(0x01);
	lcd_write_data(0x2C);
	lcd_write_data(0x2D);

	lcd_write_cmd(ST7735_FRMCTR2);
	lcd_write_data(0x01);
	lcd_write_data(0x2C);
	lcd_write_data(0x2D);

	lcd_write_cmd(ST7735_FRMCTR3);
	lcd_write_data(0x01);
	lcd_write_data(0x2C);
	lcd_write_data(0x2D);
	lcd_write_data(0x01);
	lcd_write_data(0x2C);
	lcd_write_data(0x2D);

	/* 显示反转控制 */
	lcd_write_cmd(ST7735_INVCTR);
	lcd_write_data(0x07);

	/* 电源控制 */
	lcd_write_cmd(ST7735_PWCTR1);
	lcd_write_data(0xA2);
	lcd_write_data(0x02);
	lcd_write_data(0x84);

	lcd_write_cmd(ST7735_PWCTR2);
	lcd_write_data(0xC5);

	lcd_write_cmd(ST7735_PWCTR3);
	lcd_write_data(0x0A);
	lcd_write_data(0x00);

	lcd_write_cmd(ST7735_PWCTR4);
	lcd_write_data(0x8A);
	lcd_write_data(0x2A);

	lcd_write_cmd(ST7735_PWCTR5);
	lcd_write_data(0x8A);
	lcd_write_data(0xEE);

	/* VCOM控制 */
	lcd_write_cmd(ST7735_VMCTR1);
	lcd_write_data(0x0E);

	/* 关闭反转 */
	lcd_write_cmd(ST7735_INVOFF);

	/* 颜色模式 - RGB565 */
	lcd_write_cmd(ST7735_COLMOD);
	lcd_write_data(0x05);

	/* 内存访问控制 */
	lcd_write_cmd(ST7735_MADCTL);
	lcd_write_data(0xC8);

	/* 列/行地址：160x80 时控制器为 80 列 x 160 行(MV=1)，加偏移使可见区居中 */
	lcd_write_cmd(ST7735_CASET);
	lcd_write_data(0x00);
	lcd_write_data(LCD_COL_OFFSET);
	lcd_write_data(0x00);
	lcd_write_data(LCD_COL_OFFSET + LCD_HEIGHT - 1);  /* 80 列: 26..105 */

	lcd_write_cmd(ST7735_RASET);
	lcd_write_data(0x00);
	lcd_write_data(LCD_ROW_OFFSET);
	lcd_write_data(0x00);
	lcd_write_data(LCD_ROW_OFFSET + LCD_WIDTH - 1);   /* 160 行: 1..160 */

	/* Gamma设置 */
	lcd_write_cmd(ST7735_GMCTRP1);
	lcd_write_data(0x02);
	lcd_write_data(0x1C);
	lcd_write_data(0x07);
	lcd_write_data(0x12);
	lcd_write_data(0x37);
	lcd_write_data(0x32);
	lcd_write_data(0x29);
	lcd_write_data(0x2D);
	lcd_write_data(0x29);
	lcd_write_data(0x25);
	lcd_write_data(0x2B);
	lcd_write_data(0x39);
	lcd_write_data(0x00);
	lcd_write_data(0x01);
	lcd_write_data(0x03);
	lcd_write_data(0x10);

	lcd_write_cmd(ST7735_GMCTRN1);
	lcd_write_data(0x03);
	lcd_write_data(0x1D);
	lcd_write_data(0x07);
	lcd_write_data(0x06);
	lcd_write_data(0x2E);
	lcd_write_data(0x2C);
	lcd_write_data(0x29);
	lcd_write_data(0x2D);
	lcd_write_data(0x2E);
	lcd_write_data(0x2E);
	lcd_write_data(0x37);
	lcd_write_data(0x3F);
	lcd_write_data(0x00);
	lcd_write_data(0x00);
	lcd_write_data(0x02);
	lcd_write_data(0x10);

	/* 正常显示模式 */
	lcd_write_cmd(ST7735_NORON);
	k_msleep(10);

	/* 显示开启 */
	lcd_write_cmd(ST7735_DISPON);
	k_msleep(100);
}

/* 初始化LCD */
static int lcd_init(void)
{
	int ret;

	/* 检查SPI设备 */
	if (!device_is_ready(spi_dev)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}
	
	/* 检查GPIO设备 */
	if (!device_is_ready(gpio_dev)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	/* 配置GPIO */
	ret = gpio_pin_configure(gpio_dev, LCD_CS_PIN, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure CS GPIO: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure(gpio_dev, LCD_DC_PIN, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure DC GPIO: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure(gpio_dev, LCD_RST_PIN, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure RST GPIO: %d", ret);
		return ret;
	}

	/* 初始状态：CS高电平，DC低电平，RST高电平 */
	gpio_pin_set_raw(gpio_dev, LCD_CS_PIN, 1);
	gpio_pin_set_raw(gpio_dev, LCD_DC_PIN, 0);
	gpio_pin_set_raw(gpio_dev, LCD_RST_PIN, 1);

	/* 初始化序列 */
	lcd_init_sequence();

	lcd_initialized = true;
	LOG_INF("LCD initialized successfully");
	return 0;
}

/* LCD测试函数 */
void lcd_test(void)
{
	if (!lcd_initialized) {
		LOG_ERR("LCD not initialized. Call lcd_task_enable() first.");
		return;
	}

	LOG_INF("Starting LCD test...");

	/* 测试1: 清屏 - 红色 */
	LOG_INF("Test 1: Fill screen with RED");
	lcd_clear(COLOR_RED);
	k_msleep(1000);

	/* 测试2: 清屏 - 绿色 */
	LOG_INF("Test 2: Fill screen with GREEN");
	lcd_clear(COLOR_GREEN);
	k_msleep(1000);

	/* 测试3: 清屏 - 蓝色 */
	LOG_INF("Test 3: Fill screen with BLUE");
	lcd_clear(COLOR_BLUE);
	k_msleep(1000);

	/* 测试4: 清屏 - 白色 */
	LOG_INF("Test 4: Fill screen with WHITE");
	lcd_clear(COLOR_WHITE);
	k_msleep(1000);

	/* 测试5: 彩色条纹 */
	LOG_INF("Test 5: Color stripes");
	lcd_fill_rect(0, 0, LCD_WIDTH, 16, COLOR_RED);
	lcd_fill_rect(0, 16, LCD_WIDTH, 16, COLOR_GREEN);
	lcd_fill_rect(0, 32, LCD_WIDTH, 16, COLOR_BLUE);
	lcd_fill_rect(0, 48, LCD_WIDTH, 16, COLOR_YELLOW);
	lcd_fill_rect(0, 64, LCD_WIDTH, 16, COLOR_CYAN);
	k_msleep(2000);

	/* 测试6: 彩色方块 */
	LOG_INF("Test 6: Color squares");
	uint8_t square_size = 40;
	uint8_t x_offset = (LCD_WIDTH - square_size * 4) / 2;
	uint8_t y_offset = (LCD_HEIGHT - square_size * 2) / 2;
	
	lcd_fill_rect(x_offset, y_offset, square_size, square_size, COLOR_RED);
	lcd_fill_rect(x_offset + square_size, y_offset, square_size, square_size, COLOR_GREEN);
	lcd_fill_rect(x_offset + square_size * 2, y_offset, square_size, square_size, COLOR_BLUE);
	lcd_fill_rect(x_offset + square_size * 3, y_offset, square_size, square_size, COLOR_YELLOW);
	
	lcd_fill_rect(x_offset, y_offset + square_size, square_size, square_size, COLOR_CYAN);
	lcd_fill_rect(x_offset + square_size, y_offset + square_size, square_size, square_size, COLOR_MAGENTA);
	lcd_fill_rect(x_offset + square_size * 2, y_offset + square_size, square_size, square_size, COLOR_WHITE);
	lcd_fill_rect(x_offset + square_size * 3, y_offset + square_size, square_size, square_size, COLOR_BLACK);
	k_msleep(2000);

	/* 测试7: 渐变效果 */
	LOG_INF("Test 7: Gradient effect");
	for (int i = 0; i < LCD_WIDTH; i++) {
		uint16_t color = ((i * 31 / LCD_WIDTH) << 11) | ((i * 63 / LCD_WIDTH) << 5) | (i * 31 / LCD_WIDTH);
		lcd_fill_rect(i, 0, 1, LCD_HEIGHT, color);
	}
	k_msleep(2000);

	/* 测试8: 最终清屏 - 黑色 */
	LOG_INF("Test 8: Clear screen to BLACK");
	lcd_clear(COLOR_BLACK);
	
	LOG_INF("LCD test completed!");
}

/* Shell命令：LCD测试 */
static int cmd_lcd_test(const struct shell *sh, size_t argc, char **argv)
{
	if (!lcd_initialized) {
		shell_print(sh, "LCD not initialized. Please enable LCD first.");
		return -1;
	}

	lcd_test();
	shell_print(sh, "LCD test completed!");
	return 0;
}

SHELL_CMD_REGISTER(lcd_test, NULL, "Test LCD display", cmd_lcd_test);

/* Shell命令：LCD清屏 */
static int cmd_lcd_clear(const struct shell *sh, size_t argc, char **argv)
{
	uint16_t color = COLOR_BLACK;

	if (!lcd_initialized) {
		shell_print(sh, "LCD not initialized. Please enable LCD first.");
		return -1;
	}

	if (argc > 1) {
		if (strcmp(argv[1], "red") == 0) {
			color = COLOR_RED;
		} else if (strcmp(argv[1], "green") == 0) {
			color = COLOR_GREEN;
		} else if (strcmp(argv[1], "blue") == 0) {
			color = COLOR_BLUE;
		} else if (strcmp(argv[1], "white") == 0) {
			color = COLOR_WHITE;
		} else if (strcmp(argv[1], "black") == 0) {
			color = COLOR_BLACK;
		}
	}

	lcd_clear(color);
	shell_print(sh, "LCD cleared with color: 0x%04X", color);
	return 0;
}

SHELL_CMD_REGISTER(lcd_clear, NULL, "Clear LCD screen [red|green|blue|white|black]", cmd_lcd_clear);

void lcd_task_enable(void)
{
	int ret;

	if (lcd_initialized) {
		LOG_INF("LCD already initialized");
		return;
	}

	ret = lcd_init();
	if (ret < 0) {
		LOG_ERR("Failed to initialize LCD: %d", ret);
		return;
	}

	LOG_INF("LCD task enabled");
}
