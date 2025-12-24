# Zephyr 与 MCUboot 启动流程分析

本文档详细介绍 Zephyr RTOS 和 MCUboot 的启动流程，包括关键文件位置和函数调用链。

## 目录

1. [Zephyr 启动入口](#1-zephyr-启动入口)
2. [MCUboot 启动入口](#2-mcuboot-启动入口)
3. [External Flash 初始化](#3-external-flash-初始化)
4. [MCUboot 与 Zephyr 的关系](#4-mcuboot-与-zephyr-的关系)
5. [MCUboot 大小优化策略](#5-mcuboot-大小优化策略)

---

## 1. Zephyr 启动入口

### 1.1 复位向量入口（汇编）

**文件**：`zephyr/arch/arm/core/cortex_m/reset.S`

复位后 CPU 首先执行的代码：

```asm
SECTION_SUBSEC_FUNC(TEXT,_reset_section,z_arm_reset)
SECTION_SUBSEC_FUNC(TEXT,_reset_section,__start)

    ; 设置主栈指针 (MSP)
    ldr r0, =z_main_stack + CONFIG_MAIN_STACK_SIZE
    msr msp, r0

    ; 锁定中断
    cpsid i  ; (Cortex-M0/M0+)
    ; 或
    msr BASEPRI, r0  ; (Cortex-M3/M4/M7)

    ; 跳转到 C 语言准备阶段
    bl z_prep_c
```

### 1.2 C 语言准备阶段

**文件**：`zephyr/arch/arm/core/cortex_m/prep_c.c`

```c
void z_prep_c(void)
{
    relocate_vector_table();     // 重定位向量表
    z_arm_floating_point_init(); // FPU 初始化
    z_bss_zero();                // 清零 BSS 段
    z_data_copy();               // 复制初始化数据到 RAM
    z_arm_interrupt_init();      // 中断控制器初始化
    z_cstart();                  // 跳转到内核初始化
}
```

### 1.3 内核初始化入口

**文件**：`zephyr/kernel/init.c`

```c
FUNC_NORETURN void z_cstart(void)
{
    // 初始化级别: EARLY
    z_sys_init_run_level(INIT_LEVEL_EARLY);
    
    // 架构相关初始化
    arch_kernel_init();
    
    // 设备状态初始化
    z_device_state_init();
    
    // 初始化级别: PRE_KERNEL_1 和 PRE_KERNEL_2
    z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_1);
    z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_2);
    
    // 切换到主线程
    switch_to_main_thread(prepare_multithreading());
}
```

### 1.4 完整启动流程图

```
┌─────────────────────────────────────────────────────────────┐
│                        硬件复位                              │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  reset.S: z_arm_reset / __start                             │
│  ├── 设置 MSP (主栈指针)                                     │
│  ├── 设置 PSP (进程栈指针)                                   │
│  ├── 锁定中断                                                │
│  └── bl z_prep_c                                            │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  prep_c.c: z_prep_c()                                       │
│  ├── relocate_vector_table()                                │
│  ├── z_bss_zero()                                           │
│  ├── z_data_copy()                                          │
│  ├── z_arm_interrupt_init()                                 │
│  └── z_cstart()                                             │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  init.c: z_cstart()                                         │
│  ├── INIT_LEVEL_EARLY                                       │
│  ├── arch_kernel_init()                                     │
│  ├── z_device_state_init()                                  │
│  ├── INIT_LEVEL_PRE_KERNEL_1                                │
│  ├── INIT_LEVEL_PRE_KERNEL_2                                │
│  └── switch_to_main_thread()                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  main() ← 应用程序入口 (MCUboot 或用户应用)                   │
└─────────────────────────────────────────────────────────────┘
```

### 1.5 关键文件总结

| 阶段 | 文件路径 | 函数 |
|------|----------|------|
| 复位向量 | `arch/arm/core/cortex_m/reset.S` | `z_arm_reset` |
| C 准备 | `arch/arm/core/cortex_m/prep_c.c` | `z_prep_c()` |
| 内核初始化 | `kernel/init.c` | `z_cstart()` |

---

## 2. MCUboot 启动入口

### 2.1 主入口文件

**文件**：`bootloader/mcuboot/boot/zephyr/main.c`

```c
int main(void)
{
    struct boot_rsp rsp;
    FIH_DECLARE(fih_rc, FIH_FAILURE);

    MCUBOOT_WATCHDOG_SETUP();
    MCUBOOT_WATCHDOG_FEED();

    BOOT_LOG_INF("Starting bootloader");

    os_heap_init();
    
    mcuboot_status_change(MCUBOOT_STATUS_STARTUP);

    // 验证并选择镜像
    FIH_CALL(boot_go, fih_rc, &rsp);

    // 跳转到应用程序
    do_boot(&rsp);
}
```

### 2.2 跳转到应用程序

```c
static void do_boot(struct boot_rsp *rsp)
{
    static struct arm_vector_table *vt;

    // 获取应用程序向量表地址
    vt = (struct arm_vector_table *)(flash_base +
                                     rsp->br_image_off +
                                     rsp->br_hdr->ih_hdr_size);

    // 禁用系统时钟
    sys_clock_disable();

    // 设置向量表
    SCB->VTOR = (uint32_t)vt;

    // 设置主栈指针并跳转
    __set_MSP(vt->msp);
    ((void (*)(void))vt->reset)();
}
```

### 2.3 MCUboot 启动流程

```
┌─────────────────────────────────────────────────────────────┐
│                    MCUboot main()                           │
└─────────────────────────────────────────────────────────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│ Watchdog 设置  │    │  堆初始化     │    │  日志初始化   │
└───────────────┘    └───────────────┘    └───────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  boot_go(&rsp)                                              │
│  ├── 读取镜像头                                              │
│  ├── 验证签名                                                │
│  ├── 检查版本                                                │
│  └── 选择启动镜像 (primary/secondary)                        │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  do_boot(&rsp)                                              │
│  ├── 获取应用向量表地址                                       │
│  ├── 禁用外设 (USB, 定时器等)                                 │
│  ├── 设置 VTOR (向量表偏移)                                   │
│  ├── 设置 MSP (主栈指针)                                      │
│  └── 跳转到应用 Reset_Handler                                │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                   应用程序 (z_arm_reset)                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. External Flash 初始化

### 3.1 QSPI NOR Flash 驱动

**文件**：`zephyr/drivers/flash/nrf_qspi_nor.c`

#### 初始化函数

```c
static int qspi_nor_init(const struct device *dev)
{
    const struct qspi_nor_config *dev_config = dev->config;
    int rc;

    // 1. 配置引脚
    rc = pinctrl_apply_state(dev_config->pcfg, PINCTRL_STATE_DEFAULT);

    // 2. 连接中断
    IRQ_CONNECT(DT_IRQN(QSPI_NODE), DT_IRQ(QSPI_NODE, priority),
                nrfx_isr, nrfx_qspi_irq_handler, 0);

    // 3. 初始化 QSPI 外设
    qspi_clock_div_change();
    rc = qspi_init(dev);
    qspi_clock_div_restore();

    return rc;
}
```

#### 设备定义

```c
DEVICE_DT_INST_DEFINE(0, qspi_nor_init, PM_DEVICE_DT_INST_GET(0),
                      &qspi_nor_dev_data, &qspi_nor_dev_config,
                      POST_KERNEL, CONFIG_NORDIC_QSPI_NOR_INIT_PRIORITY,
                      &qspi_nor_api);
```

### 3.2 初始化时机

| 阶段 | 说明 |
|------|------|
| `POST_KERNEL` | 内核启动后 |
| `CONFIG_NORDIC_QSPI_NOR_INIT_PRIORITY` | 优先级（默认 80）|

### 3.3 Flash 初始化流程

```
Zephyr 内核启动
    │
    ├── POST_KERNEL 阶段
    │       │
    │       └── qspi_nor_init()
    │               ├── pinctrl_apply_state()  # 配置引脚
    │               ├── IRQ_CONNECT()          # 连接中断
    │               ├── qspi_init()            # QSPI 外设初始化
    │               └── configure_chip()       # 配置 flash 芯片
    │
    └── main() (MCUboot)
            │
            └── boot_go()
                    │
                    └── flash_area_open()  # 访问 external flash
```

### 3.4 MCUboot 配置

**文件**：`sysbuild/mcuboot/boards/nrf52840dk_nrf52840.conf`

```conf
# 启用 QSPI NOR for external flash
CONFIG_NORDIC_QSPI_NOR=y
CONFIG_NORDIC_QSPI_NOR_FLASH_LAYOUT_PAGE_SIZE=4096
CONFIG_NORDIC_QSPI_NOR_STACK_WRITE_BUFFER_SIZE=16
```

**文件**：`sysbuild/mcuboot/boards/nrf52840dk_nrf52840.overlay`

```dts
/ {
    chosen {
        nordic,pm-ext-flash = &mx25r64;
    };
};
```

---

## 4. MCUboot 与 Zephyr 的关系

### 4.1 架构概述

MCUboot 是跨平台 Bootloader，支持多个 RTOS：

```
bootloader/mcuboot/boot/
├── zephyr/      # Zephyr 平台移植
├── mynewt/      # Mynewt 平台移植
├── mbed/        # Mbed 平台移植
├── nuttx/       # NuttX 平台移植
├── espressif/   # ESP32 平台移植
└── cypress/     # Cypress 平台移植
```

### 4.2 为什么 MCUboot 使用 Zephyr？

#### 原因 1：MCUboot 作为 Zephyr 应用构建

```cmake
# CMakeLists.txt
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(NONE)
```

#### 原因 2：代码复用

MCUboot 利用 Zephyr 提供：
- CMake 构建系统
- 设备树（Device Tree）
- 驱动框架（Flash, GPIO 等）
- Kconfig 配置系统

#### 原因 3：OS 抽象层

```c
// os.c - Zephyr OS 抽象层
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>

int flash_device_base(uint8_t fd_id, uintptr_t *ret)
{
    // 使用 Zephyr flash API
}
```

### 4.3 工作流程

```
┌─────────────────────────────────────────────────────────────┐
│                       电源上电                               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  MCUboot (Zephyr 应用)                                      │
│  ├── Zephyr 启动 (reset.S → prep_c.c → init.c)              │
│  ├── 设备初始化 (包括 QSPI Flash)                            │
│  ├── main() - MCUboot 主逻辑                                 │
│  └── do_boot() - 跳转到应用                                  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  应用程序 (另一个 Zephyr 应用)                                │
│  ├── Zephyr 启动 (reset.S → prep_c.c → init.c)              │
│  ├── 设备初始化                                              │
│  └── main() - 应用主逻辑                                     │
└─────────────────────────────────────────────────────────────┘
```

---

## 5. MCUboot 大小优化策略

### 5.1 编译器优化

```conf
# LTO (Link Time Optimization)
CONFIG_LTO=y

# 大小优化 (-Os)
CONFIG_SIZE_OPTIMIZATIONS=y
```

### 5.2 禁用不必要的内核功能

```conf
# 禁用多线程
CONFIG_MULTITHREADING=n

# 禁用时间片调度
CONFIG_TIMESLICING=n

# 禁用 64 位超时
CONFIG_TIMEOUT_64BIT=n

# 禁用系统时钟
CONFIG_SYS_CLOCK_EXISTS=n
```

### 5.3 禁用外设驱动

```conf
CONFIG_BT=n
CONFIG_BT_CTLR=n
CONFIG_I2C=n
CONFIG_SPI=n
CONFIG_WATCHDOG=n
CONFIG_GPIO=n
```

### 5.4 最小化日志

```conf
CONFIG_LOG=n
# 或
CONFIG_LOG_MODE_MINIMAL=y
CONFIG_BOOT_BANNER=n
CONFIG_CONSOLE=n
CONFIG_UART_CONSOLE=n
```

### 5.5 典型 MCUboot 大小

| 配置 | 大小 |
|------|------|
| 完整功能 | ~50-60 KB |
| 优化后 | ~24-32 KB |
| 最小配置 | ~16-20 KB |

### 5.6 最小配置参考

**文件**：`prj_minimal.conf`

```conf
CONFIG_SIZE_OPTIMIZATIONS=y
CONFIG_BOOT_BANNER=n
CONFIG_LOG=n
CONFIG_CONSOLE=n
CONFIG_SERIAL=n
CONFIG_UART_CONSOLE=n
CONFIG_GPIO=n
CONFIG_TIMESLICING=n
CONFIG_TIMEOUT_64BIT=n
CONFIG_THREAD_RUNTIME_STATS=n
CONFIG_PRINTK=n
CONFIG_CBPRINTF_NANO=y
```

---

## 参考文件

| 组件 | 路径 |
|------|------|
| Zephyr 复位向量 | `zephyr/arch/arm/core/cortex_m/reset.S` |
| Zephyr C 准备 | `zephyr/arch/arm/core/cortex_m/prep_c.c` |
| Zephyr 内核初始化 | `zephyr/kernel/init.c` |
| MCUboot 主入口 | `bootloader/mcuboot/boot/zephyr/main.c` |
| QSPI NOR 驱动 | `zephyr/drivers/flash/nrf_qspi_nor.c` |
| MCUboot Flash Map | `bootloader/mcuboot/boot/zephyr/flash_map_extended.c` |

