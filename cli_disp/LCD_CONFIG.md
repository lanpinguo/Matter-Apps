# LCD配置说明

## 硬件连接

0.96寸IPS LCD (ST7735S控制器，160x80分辨率) 需要以下连接：

### SPI接口 (4线)
- **SCK (时钟)**: 连接到SPI的SCK引脚
- **MOSI (数据)**: 连接到SPI的MOSI引脚
- **CS (片选)**: 连接到GPIO引脚 (默认: P0.10)
- **DC (数据/命令)**: 连接到GPIO引脚 (默认: P0.11)
- **RST (复位)**: 连接到GPIO引脚 (默认: P0.12)
- **VCC**: 3.3V
- **GND**: GND
- **LED**: 背光控制 (可选，可连接到VCC或通过GPIO控制)

## 软件配置

### 1. 修改GPIO引脚定义

在 `src/lcd_task.c` 文件中，根据实际硬件连接修改以下引脚定义：

```c
#define LCD_CS_PIN         10   // 修改为实际使用的CS引脚号
#define LCD_DC_PIN         11   // 修改为实际使用的DC引脚号
#define LCD_RST_PIN        12   // 修改为实际使用的RST引脚号
```

### 2. 配置SPI设备

在设备树overlay文件中（例如 `boards/nrf52840dk_nrf52840.overlay`），确保SPI1已启用：

```dts
&spi1 {
	status = "okay";
	pinctrl-0 = <&spi1_default>;
	pinctrl-names = "default";
};
```

### 3. 配置项目

确保 `prj.conf` 中包含以下配置：

```
CONFIG_SPI=y
CONFIG_GPIO=y
```

## 使用方法

### Shell命令

LCD驱动提供了以下Shell命令：

1. **测试LCD显示**:
   ```
   lcd_test
   ```
   运行一系列测试图案，包括：
   - 单色填充测试（红、绿、蓝、白）
   - 彩色条纹测试
   - 彩色方块测试
   - 渐变效果测试

2. **清屏**:
   ```
   lcd_clear [color]
   ```
   可选颜色：`red`, `green`, `blue`, `white`, `black` (默认: black)

### 代码中使用

```c
#include "lcd_task.h"

// 在main函数中启用LCD
lcd_task_enable();

// 运行测试
lcd_test();
```

## 故障排除

1. **LCD无显示**:
   - 检查SPI和GPIO引脚连接
   - 确认引脚定义与实际硬件匹配
   - 检查电源连接（3.3V和GND）
   - 查看日志输出确认初始化是否成功

2. **编译错误**:
   - 确保已添加 `src/lcd_task.c` 到 `CMakeLists.txt`
   - 确保 `prj.conf` 中启用了SPI和GPIO配置

3. **显示异常**:
   - 检查SPI时钟频率（默认10MHz，可在代码中调整）
   - 确认ST7735S初始化序列是否正确
   - 检查LCD是否为160x80分辨率版本

## 技术规格

- **控制器**: ST7735S
- **分辨率**: 160x80像素
- **颜色深度**: RGB565 (16位)
- **接口**: 4线SPI
- **SPI频率**: 10MHz (可在代码中调整)
