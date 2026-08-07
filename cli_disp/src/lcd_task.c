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
#include "adc_task.h"

LOG_MODULE_REGISTER(lcd_task, LOG_LEVEL_DBG);

/* 5x7 字体：每字符 5 列 x 7 行，5 字节/字符(列主序，每字节 bit0=上)。仅含 0-9 空格 C h : m V . - */
#define FONT_W  5
#define FONT_H  7
static const uint8_t font_5x7[][5] = {
	{ 0x00, 0x00, 0x00, 0x00, 0x00 }, /* space */
	{ 0x7B, 0x45, 0x45, 0x45, 0x7B }, /* 0 */
	{ 0x00, 0x42, 0x7F, 0x40, 0x00 }, /* 1 */
	{ 0x62, 0x51, 0x49, 0x49, 0x46 }, /* 2 */
	{ 0x22, 0x49, 0x49, 0x49, 0x36 }, /* 3 */
	{ 0x18, 0x14, 0x12, 0x7F, 0x10 }, /* 4 */
	{ 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 5 */
	{ 0x3E, 0x49, 0x49, 0x49, 0x32 }, /* 6 */
	{ 0x01, 0x71, 0x09, 0x05, 0x03 }, /* 7 */
	{ 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 8 */
	{ 0x26, 0x49, 0x49, 0x49, 0x3E }, /* 9 */
	{ 0x3E, 0x41, 0x41, 0x41, 0x22 }, /* C */
	{ 0x7F, 0x08, 0x04, 0x04, 0x78 }, /* h */
	{ 0x00, 0x36, 0x36, 0x00, 0x00 }, /* : */
	{ 0x7C, 0x04, 0x78, 0x04, 0x78 }, /* m */
	{ 0x1C, 0x20, 0x40, 0x20, 0x1C }, /* V */
	{ 0x00, 0x60, 0x60, 0x00, 0x00 }, /* . */
	{ 0x08, 0x08, 0x08, 0x08, 0x08 }, /* - */
};
#define FONT_INDEX_SPACE 0
#define FONT_INDEX_0     1
#define FONT_INDEX_9     10
#define FONT_INDEX_C    11
#define FONT_INDEX_h    12
#define FONT_INDEX_COL  13
#define FONT_INDEX_m    14
#define FONT_INDEX_V    15
#define FONT_INDEX_DOT  16
#define FONT_INDEX_MINUS 17

static uint8_t char_to_font_index(char c)
{
	if (c == ' ') return FONT_INDEX_SPACE;
	if (c >= '0' && c <= '9') return FONT_INDEX_0 + (c - '0');
	if (c == 'C') return FONT_INDEX_C;
	if (c == 'h') return FONT_INDEX_h;
	if (c == ':') return FONT_INDEX_COL;
	if (c == 'm') return FONT_INDEX_m;
	if (c == 'V') return FONT_INDEX_V;
	if (c == '.') return FONT_INDEX_DOT;
	if (c == '-') return FONT_INDEX_MINUS;
	return FONT_INDEX_SPACE;
}

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

/* 画单字符 5x7，左上角 (x,y)，fg/bg 为 RGB565。
 * MADCTL 0xC8：MX=1 使逻辑列小的一侧显示在右，故对“绘制列位置”做镜像；MY=1 用反序送行补偿。 */
static void lcd_draw_char(uint8_t x, uint8_t y, char c, uint16_t fg, uint16_t bg)
{
	uint8_t idx = char_to_font_index(c);
	static uint8_t col_buf[FONT_H * 2];

	for (uint8_t col = 0; col < FONT_W; col++) {
		uint8_t bits = font_5x7[idx][col];  /* 字模列按正常顺序读 */
		/* 左右镜像：MX=1 时小 x 显示在右，故字模左列(col=0)画到 x+4，右列(col=4)画到 x+0 */
		uint8_t draw_col = col;
		/* 上下镜像：MY=1 时先送像素显示在下，送像素顺序为 row 6..0 */
		for (uint8_t i = 0; i < FONT_H; i++) {
			uint8_t row = FONT_H - 1 - i;
			uint16_t color = (bits & (1U << row)) ? fg : bg;
			col_buf[i * 2] = (color >> 8) & 0xFF;
			col_buf[i * 2 + 1] = color & 0xFF;
		}
		lcd_set_window(x + draw_col, y, x + draw_col, y + FONT_H - 1);
		lcd_write_data_buf(col_buf, sizeof(col_buf));
	}
}

/* 画字符串，左上角 (x,y)，fg/bg RGB565，字符间距 1 像素 */
static void lcd_draw_string(uint8_t x, uint8_t y, const char *str, uint16_t fg, uint16_t bg)
{
	while (*str && x + FONT_W <= LCD_WIDTH) {
		lcd_draw_char(x, y, *str++, fg, bg);
		x += FONT_W + 1;
	}
}

/* ADC 显示区域：物理顶部 22 像素，两行文字。MADCTL MY=1 时逻辑 y 小在屏下方，故用偏移使区域在屏顶 */
#define ADC_DISPLAY_HEIGHT  22
#define ADC_LINE0_Y         2
#define ADC_LINE1_Y         12
#define ADC_DISPLAY_Y_OFFSET  (LCD_HEIGHT - ADC_DISPLAY_HEIGHT)  /* 58：区域放在逻辑顶部=物理底部之上 */

/* 将当前 ADC 采样值显示到 LCD 物理顶部 */
void lcd_show_adc(void)
{
	int16_t raw[ADC_TASK_CHANNEL_COUNT];
	int32_t mv[ADC_TASK_CHANNEL_COUNT];
	bool valid[ADC_TASK_CHANNEL_COUNT];
	char buf[20];
	static const uint8_t channel_ids[] = { 4, 5 }; /* 与 overlay io-channels 一致 */

	if (!lcd_initialized) {
		return;
	}

	adc_task_get_values(raw, mv, valid, ADC_TASK_CHANNEL_COUNT);

	/* 清空显示区域（在物理顶部，即逻辑 y 大的一端） */
	lcd_fill_rect(0, ADC_DISPLAY_Y_OFFSET, LCD_WIDTH, ADC_DISPLAY_HEIGHT, COLOR_BLACK);

	for (size_t i = 0; i < ADC_TASK_CHANNEL_COUNT; i++) {
		uint8_t line_y = (i == 0) ? ADC_LINE0_Y : ADC_LINE1_Y;
		uint8_t y = ADC_DISPLAY_Y_OFFSET + line_y;
		if (valid[i]) {
			snprintf(buf, sizeof(buf), "Ch%u: %d mV", (unsigned)channel_ids[i], (int)mv[i]);
		} else {
			snprintf(buf, sizeof(buf), "Ch%u: -- mV", (unsigned)channel_ids[i]);
		}
		lcd_draw_string(2, y, buf, COLOR_WHITE, COLOR_BLACK);
	}
}

static void adc_display_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(adc_display_work, adc_display_work_handler);

static void adc_display_work_handler(struct k_work *work)
{
	if (lcd_initialized) {
		lcd_show_adc();
	}
	k_work_schedule(&adc_display_work, K_MSEC(1000));
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
	lcd_write_data(0x08);

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

	/* 启动 ADC 值定时刷新到 LCD（每 1s） */
	k_work_schedule(&adc_display_work, K_MSEC(500));

	LOG_INF("LCD task enabled");
}
