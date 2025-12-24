# 通过 MCUboot 间接启动的应用程序调试指南

## 概述

当应用程序通过 MCUboot 间接启动时，需要同时加载 MCUboot 和应用程序的符号表，并在正确的地址设置断点。本文档详细介绍多镜像调试的方法。

## 内存布局

根据 `pm_static_nrf52840dk_nrf52840.yml`：

```
Flash 布局：
- MCUboot:     0x0000 - 0xC000  (48 KB)
- MCUboot Pad: 0xC000 - 0xC200  (512 B)
- Application: 0xC200 - 0xF0000 (应用程序起始地址)
```

**关键地址：**
- MCUboot 入口：`0x0000`（复位向量）
- 应用程序入口：`0xC200`（Flash 中的地址）
- 应用程序执行地址：`0xC000`（MCUboot chainload 地址，包含镜像头）

## 方法 1：使用 GDB 多符号表调试（推荐）

### 步骤

#### 1. 启动 OpenOCD 或 J-Link GDB Server

**使用 OpenOCD：**
```bash
cd /Users/lanpinguo/work/matter/nrf/apps/light_bulb_mote
openocd -f openocd.cfg
```

**使用 J-Link：**
```bash
JLinkGDBServer -device nRF52840_xxAA -if SWD -speed 4000 -port 2331
```

#### 2. 启动 GDB 并加载多个符号表

```bash
/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb
```

在 GDB 中：

```gdb
# 1. 连接到调试器
(gdb) target remote localhost:3333  # OpenOCD
# 或
(gdb) target remote localhost:2331  # J-Link

# 2. 加载 MCUboot 符号表（地址 0x0）
(gdb) add-symbol-file build/mcuboot/zephyr/zephyr.elf 0x0

# 3. 加载应用程序符号表（地址 0xC200）
# 注意：应用程序在 Flash 中的地址是 0xC200，但执行地址是 0xC000
(gdb) add-symbol-file build/light_bulb_mote/zephyr/zephyr.elf 0xC200

# 4. 设置断点
# MCUboot 入口
(gdb) break main
(gdb) break boot_go

# 应用程序入口（在 main 函数）
(gdb) break AppTask::StartApp
(gdb) break main

# GPIO 配置函数（用于调试断言失败）
(gdb) break gpio_pin_configure

# 5. 复位并运行
(gdb) monitor reset halt
(gdb) continue
```

### 调试启动流程

```gdb
# 1. 在 MCUboot main 函数设置断点
(gdb) break main
(gdb) break boot_go

# 2. 运行到 MCUboot
(gdb) monitor reset halt
(gdb) continue

# 3. 当 MCUboot 准备跳转到应用程序时，设置应用程序断点
(gdb) break AppTask::StartApp
(gdb) break main

# 4. 继续执行，MCUboot 会跳转到应用程序
(gdb) continue

# 5. 现在可以调试应用程序了
(gdb) backtrace
(gdb) list
```

## 方法 2：使用 GDB 脚本自动化

### 创建 GDB 初始化脚本（.gdbinit-mcuboot）

```gdb
# .gdbinit-mcuboot - MCUboot + Application 调试脚本

# 连接到调试器
target remote localhost:3333

# 加载 MCUboot 符号表
add-symbol-file build/mcuboot/zephyr/zephyr.elf 0x0

# 加载应用程序符号表
add-symbol-file build/light_bulb_mote/zephyr/zephyr.elf 0xC200

# 设置 MCUboot 断点
break main
break boot_go
break bootutil_img_validate

# 设置应用程序断点
break AppTask::StartApp
break main
break gpio_pin_configure

# 显示源代码
set listsize 20
set print pretty on

# 复位并停止
monitor reset halt

# 显示当前状态
info breakpoints
```

### 使用方法

```bash
# 启动 OpenOCD
openocd -f openocd.cfg &

# 启动 GDB 并加载脚本
arm-zephyr-eabi-gdb -x .gdbinit-mcuboot
```

## 方法 3：使用 west debug（最简单）

west 命令会自动处理多镜像调试：

```bash
cd /Users/lanpinguo/work/matter/nrf/apps/light_bulb_mote
west debug --runner jlink
```

这会：
1. 自动加载合并的符号表
2. 设置断点在应用程序的 main 函数
3. 连接到目标并开始调试

## 调试 GPIO 断言失败的具体步骤

### 1. 设置断点

```gdb
# 连接到目标
(gdb) target remote localhost:3333

# 加载符号表
(gdb) add-symbol-file build/mcuboot/zephyr/zephyr.elf 0x0
(gdb) add-symbol-file build/light_bulb_mote/zephyr/zephyr.elf 0xC200

# 在 GPIO 配置函数设置断点
(gdb) break gpio_pin_configure

# 在应用程序初始化设置断点
(gdb) break AppTask::StartApp
(gdb) break main
```

### 2. 运行并跟踪启动流程

```gdb
# 复位并停止
(gdb) monitor reset halt

# 运行到 MCUboot
(gdb) continue

# MCUboot 会验证镜像并跳转到应用程序
# 当到达应用程序断点时，检查调用栈
(gdb) backtrace

# 继续运行到 GPIO 配置
(gdb) continue

# 当 GPIO 断言失败时，检查参数
(gdb) print pin
(gdb) print flags
(gdb) print cfg->port_pin_mask
(gdb) print (cfg->port_pin_mask & (1 << pin))

# 查看调用栈，找到是谁调用了 GPIO 配置
(gdb) backtrace full
```

### 3. 检查启动流程

```gdb
# 查看 MCUboot 的启动流程
(gdb) break boot_go
(gdb) continue
(gdb) step
(gdb) step

# 查看镜像验证过程
(gdb) break bootutil_img_validate
(gdb) continue
(gdb) print rsp.br_image_off  # 应该显示 0xC000

# 查看跳转到应用程序的代码
(gdb) break z_arm_do_system_call
(gdb) continue
```

## 方法 4：使用合并的 HEX 文件

某些情况下，可以使用合并的 HEX 文件，它包含 MCUboot 和应用程序：

```gdb
# 加载合并的 HEX 文件（如果可用）
(gdb) file build/merged.hex

# 或使用单独的 ELF 文件，但需要正确设置地址
(gdb) file build/light_bulb_mote/zephyr/zephyr.elf
(gdb) symbol-file build/light_bulb_mote/zephyr/zephyr.elf -o 0xC200
```

## 调试脚本示例

### debug-mcuboot.sh

```bash
#!/bin/bash

# debug-mcuboot.sh - MCUboot + Application 调试脚本

MCUBOOT_ELF="build/mcuboot/zephyr/zephyr.elf"
APP_ELF="build/light_bulb_mote/zephyr/zephyr.elf"
GDB="/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"
DEBUGGER="${1:-openocd}"

# 检查文件是否存在
if [ ! -f "$MCUBOOT_ELF" ]; then
    echo "错误: MCUboot ELF 文件不存在: $MCUBOOT_ELF"
    exit 1
fi

if [ ! -f "$APP_ELF" ]; then
    echo "错误: 应用程序 ELF 文件不存在: $APP_ELF"
    exit 1
fi

case "$DEBUGGER" in
    openocd)
        echo "请确保 OpenOCD 正在运行: openocd -f openocd.cfg"
        echo "按 Enter 继续..."
        read
        
        $GDB -ex "target remote localhost:3333" \
             -ex "add-symbol-file $MCUBOOT_ELF 0x0" \
             -ex "add-symbol-file $APP_ELF 0xC200" \
             -ex "break main" \
             -ex "break boot_go" \
             -ex "break AppTask::StartApp" \
             -ex "break gpio_pin_configure" \
             -ex "monitor reset halt" \
             -ex "set listsize 20" \
             -ex "set print pretty on" \
             $APP_ELF
        ;;
    
    jlink)
        echo "启动 J-Link GDB Server..."
        JLinkGDBServer -device nRF52840_xxAA -if SWD -speed 4000 -port 2331 &
        JLINK_PID=$!
        sleep 2
        
        echo "按 Enter 启动 GDB..."
        read
        
        $GDB -ex "target remote localhost:2331" \
             -ex "add-symbol-file $MCUBOOT_ELF 0x0" \
             -ex "add-symbol-file $APP_ELF 0xC200" \
             -ex "break main" \
             -ex "break boot_go" \
             -ex "break AppTask::StartApp" \
             -ex "break gpio_pin_configure" \
             -ex "monitor reset halt" \
             -ex "set listsize 20" \
             -ex "set print pretty on" \
             $APP_ELF
        
        kill $JLINK_PID 2>/dev/null || true
        ;;
    
    *)
        echo "用法: $0 [openocd|jlink]"
        exit 1
        ;;
esac
```

## 关键调试技巧

### 1. 理解地址映射

- **MCUboot Flash 地址：** `0x0000`
- **应用程序 Flash 地址：** `0xC200`
- **应用程序执行地址：** `0xC000`（MCUboot chainload 地址）

### 2. 设置条件断点

```gdb
# 只在应用程序中触发
(gdb) break gpio_pin_configure if $pc > 0xC000

# 只在特定线程中触发
(gdb) break gpio_pin_configure
(gdb) condition 1 $_thread == 0x2000acf0
```

### 3. 查看内存映射

```gdb
# 查看 Flash 内容
(gdb) x/20x 0x0        # MCUboot 区域
(gdb) x/20x 0xC200     # 应用程序区域

# 查看应用程序入口
(gdb) x/10i 0xC200
```

### 4. 跟踪启动流程

```gdb
# 在 MCUboot 跳转前设置断点
(gdb) break boot_go
(gdb) commands
> print rsp.br_image_off
> print rsp.br_hdr->ih_hdr_size
> continue
> end

# 在应用程序入口设置断点
(gdb) break *0xC200
```

## 常见问题

### 1. 符号表地址不匹配

**问题：** 断点设置失败或显示错误地址

**解决方案：**
- 确认使用正确的加载地址
- MCUboot: `0x0`
- Application: `0xC200`（Flash 地址）

### 2. 无法在应用程序中设置断点

**问题：** 应用程序断点无法设置

**解决方案：**
```gdb
# 先让 MCUboot 运行并跳转到应用程序
(gdb) break boot_go
(gdb) continue
# 等待 MCUboot 跳转后，再设置应用程序断点
(gdb) break AppTask::StartApp
(gdb) continue
```

### 3. 调用栈显示错误

**问题：** 调用栈混合了 MCUboot 和应用程序的地址

**解决方案：**
- 确保两个符号表都已加载
- 使用 `info symbol <address>` 查看地址对应的符号

## VS Code 配置

### launch.json（多符号表）

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug MCUboot + App (OpenOCD)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/light_bulb_mote/zephyr/zephyr.elf",
            "cwd": "${workspaceFolder}",
            "MIMode": "gdb",
            "miDebuggerPath": "/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb",
            "miDebuggerServerAddress": "localhost:3333",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Load MCUboot symbols",
                    "text": "add-symbol-file ${workspaceFolder}/build/mcuboot/zephyr/zephyr.elf 0x0",
                    "ignoreFailures": false
                },
                {
                    "description": "Load Application symbols",
                    "text": "add-symbol-file ${workspaceFolder}/build/light_bulb_mote/zephyr/zephyr.elf 0xC200",
                    "ignoreFailures": false
                },
                {
                    "description": "Set breakpoints",
                    "text": "-break-insert main",
                    "ignoreFailures": true
                },
                {
                    "description": "Set breakpoint in AppTask",
                    "text": "-break-insert AppTask::StartApp",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "openocd"
        }
    ]
}
```

## 总结

调试通过 MCUboot 间接启动的应用程序的关键点：

1. **加载多个符号表：** MCUboot (`0x0`) 和应用程序 (`0xC200`)
2. **理解地址映射：** Flash 地址 vs 执行地址
3. **设置断点：** 在 MCUboot 和应用程序的关键位置
4. **跟踪启动流程：** 从 MCUboot 到应用程序的跳转

使用 `west debug` 是最简单的方法，它会自动处理这些细节。

