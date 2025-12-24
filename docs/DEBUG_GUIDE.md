# Light Bulb Mote 应用程序调试指南

## 概述

本文档介绍如何使用 ELF 文件调试 light-bulb-mote 应用程序。支持多种调试方法：GDB + OpenOCD、GDB + J-Link，以及使用 west 命令。

## ELF 文件位置

构建完成后，ELF 文件位于：

- **应用程序 ELF：** `build/light_bulb_mote/zephyr/zephyr.elf`
- **MCUboot ELF：** `build/mcuboot/zephyr/zephyr.elf`
- **中间文件：** `build/light_bulb_mote/zephyr/zephyr_pre0.elf`

## 方法 1：使用 GDB + OpenOCD（推荐）

### 前置要求

1. 安装 OpenOCD
2. 安装 GDB（Zephyr SDK 自带）
3. 连接调试器（J-Link、CMSIS-DAP 等）

### 步骤

#### 1. 启动 OpenOCD

在项目根目录创建或使用现有的 `openocd.cfg`：

```tcl
# openocd.cfg - OpenOCD 配置文件 for nRF52840
source [find interface/jlink.cfg]
transport select swd
source [find target/nrf52.cfg]
set WORKAREASIZE 0x4000
reset_config srst_only srst_nogate connect_assert_srst
adapter speed 1000
init
```

启动 OpenOCD：

```bash
cd /Users/lanpinguo/work/matter/nrf/apps/light_bulb_mote
openocd -f openocd.cfg
```

OpenOCD 会在端口 3333 上启动 GDB 服务器。

#### 2. 启动 GDB

在另一个终端中：

```bash
cd /Users/lanpinguo/work/matter/nrf/apps/light_bulb_mote

# 使用 Zephyr SDK 的 GDB
/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb \
  build/light_bulb_mote/zephyr/zephyr.elf
```

#### 3. 在 GDB 中连接

```gdb
# 连接到 OpenOCD
(gdb) target remote localhost:3333

# 加载符号表
(gdb) file build/light_bulb_mote/zephyr/zephyr.elf

# 设置断点（例如在 main 函数）
(gdb) break main
(gdb) break app_task.cpp:53

# 继续执行
(gdb) continue
```

## 方法 2：使用 GDB + J-Link（最简单）

### 步骤

#### 1. 使用 west 命令启动调试

```bash
cd /Users/lanpinguo/work/matter/nrf/apps/light_bulb_mote
west debug --runner jlink
```

这会自动：
- 启动 J-Link GDB 服务器
- 启动 GDB 并连接到目标
- 加载 ELF 文件
- 设置断点在 `main` 函数

#### 2. 手动使用 J-Link GDB Server

```bash
# 启动 J-Link GDB Server
JLinkGDBServer -device nRF52840_xxAA -if SWD -speed 4000 -port 2331

# 在另一个终端启动 GDB
/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb \
  build/light_bulb_mote/zephyr/zephyr.elf

# 在 GDB 中连接
(gdb) target remote localhost:2331
(gdb) load
(gdb) break main
(gdb) continue
```

## 方法 3：使用 west flash 和 attach

### 步骤

```bash
# 1. 烧录固件
cd /Users/lanpinguo/work/matter/nrf/apps/light_bulb_mote
west flash

# 2. 附加调试器（attach 到正在运行的进程）
west attach --runner jlink
```

## 常用 GDB 命令

### 基本命令

```gdb
# 连接目标
(gdb) target remote localhost:3333

# 加载符号表
(gdb) file build/light_bulb_mote/zephyr/zephyr.elf

# 设置断点
(gdb) break main
(gdb) break app_task.cpp:53
(gdb) break gpio_pin_configure

# 查看断点
(gdb) info breakpoints

# 继续执行
(gdb) continue
(gdb) c

# 单步执行
(gdb) step
(gdb) s
(gdb) next
(gdb) n

# 查看调用栈
(gdb) backtrace
(gdb) bt

# 查看局部变量
(gdb) info locals

# 查看寄存器
(gdb) info registers

# 查看变量值
(gdb) print variable_name
(gdb) p pin
(gdb) p flags

# 查看内存
(gdb) x/10x 0x20000000

# 查看源代码
(gdb) list
(gdb) list app_task.cpp:50

# 反汇编
(gdb) disassemble
(gdb) disassemble /m main
```

### 调试 GPIO 断言失败

```gdb
# 在 GPIO 配置函数设置断点
(gdb) break gpio_pin_configure

# 运行到断点
(gdb) continue

# 查看参数
(gdb) print port
(gdb) print pin
(gdb) print flags

# 查看 port_pin_mask
(gdb) print cfg->port_pin_mask

# 单步执行到断言
(gdb) step
(gdb) step

# 查看断言失败的位置
(gdb) backtrace
```

### 调试启动问题

```gdb
# 在 main 函数设置断点
(gdb) break main

# 在应用程序初始化函数设置断点
(gdb) break AppTask::StartApp

# 在 GPIO 配置前设置断点
(gdb) break gpio_pin_configure

# 运行并查看调用栈
(gdb) continue
(gdb) backtrace
```

## 调试脚本示例

### GDB 初始化脚本（.gdbinit）

创建 `.gdbinit` 文件：

```gdb
# 自动连接到 OpenOCD
target remote localhost:3333

# 加载符号表
file build/light_bulb_mote/zephyr/zephyr.elf

# 设置常用断点
break main
break AppTask::StartApp

# 显示源代码
set listsize 20

# 美化打印
set print pretty on
set print array on
set print array-indexes on
```

### 调试启动脚本（debug.sh）

```bash
#!/bin/bash

# debug.sh - 启动调试会话

ELF_FILE="build/light_bulb_mote/zephyr/zephyr.elf"
GDB="/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"

# 检查 ELF 文件是否存在
if [ ! -f "$ELF_FILE" ]; then
    echo "错误: ELF 文件不存在: $ELF_FILE"
    echo "请先构建项目: west build -b nrf52840dk/nrf52840"
    exit 1
fi

# 启动 GDB
$GDB -ex "target remote localhost:3333" \
     -ex "file $ELF_FILE" \
     -ex "break main" \
     -ex "break AppTask::StartApp" \
     -ex "continue" \
     $ELF_FILE
```

使用方法：

```bash
chmod +x debug.sh
# 在一个终端启动 OpenOCD
openocd -f openocd.cfg
# 在另一个终端运行脚本
./debug.sh
```

## 调试 GPIO 断言失败的具体步骤

### 1. 设置断点

```gdb
# 在 GPIO 配置函数设置断点
(gdb) break gpio_pin_configure

# 在断言位置设置断点（如果可能）
(gdb) break gpio.h:1020
```

### 2. 运行到断点

```gdb
(gdb) continue
```

### 3. 检查参数

```gdb
# 查看函数参数
(gdb) info args
(gdb) print port
(gdb) print pin
(gdb) print flags

# 查看 GPIO 设备配置
(gdb) print cfg->port_pin_mask
(gdb) print cfg->ngpios

# 检查引脚是否在支持范围内
(gdb) print (cfg->port_pin_mask & (1 << pin))
```

### 4. 查看调用栈

```gdb
(gdb) backtrace
(gdb) backtrace full  # 显示所有帧的局部变量
```

### 5. 查看源代码

```gdb
# 查看当前代码
(gdb) list

# 查看调用者的代码
(gdb) frame 1
(gdb) list
```

## 使用 VS Code 调试

### 配置 launch.json

创建 `.vscode/launch.json`：

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug nRF52840 (OpenOCD)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/light_bulb_mote/zephyr/zephyr.elf",
            "cwd": "${workspaceFolder}",
            "MIMode": "gdb",
            "miDebuggerPath": "/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb",
            "miDebuggerServerAddress": "localhost:3333",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Set Disassembly Flavor to Intel",
                    "text": "-gdb-set disassembly-flavor intel",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "openocd"
        },
        {
            "name": "Debug nRF52840 (J-Link)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/light_bulb_mote/zephyr/zephyr.elf",
            "cwd": "${workspaceFolder}",
            "MIMode": "gdb",
            "miDebuggerPath": "/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb",
            "miDebuggerServerAddress": "localhost:2331",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ]
        }
    ]
}
```

### 配置 tasks.json

创建 `.vscode/tasks.json`：

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "openocd",
            "type": "shell",
            "command": "openocd",
            "args": [
                "-f", "openocd.cfg"
            ],
            "isBackground": true,
            "problemMatcher": []
        }
    ]
}
```

## 调试多镜像系统（MCUboot + Application）

### 方法 1：分别调试

```gdb
# 1. 加载 MCUboot 符号表
(gdb) add-symbol-file build/mcuboot/zephyr/zephyr.elf 0x0

# 2. 加载应用程序符号表
(gdb) add-symbol-file build/light_bulb_mote/zephyr/zephyr.elf 0xc000

# 3. 设置断点
(gdb) break main  # MCUboot main
(gdb) break AppTask::StartApp  # Application main
```

### 方法 2：使用合并的符号表

某些情况下，可以使用合并的 HEX 文件：

```gdb
(gdb) file build/merged.hex
```

## 常见问题排查

### 1. GDB 无法连接

**问题：** `target remote localhost:3333` 失败

**解决方案：**
- 确认 OpenOCD 正在运行
- 检查端口号是否正确（OpenOCD 默认 3333）
- 检查防火墙设置
- 尝试使用 `127.0.0.1:3333` 而不是 `localhost:3333`

### 2. 符号表不匹配

**问题：** 断点设置失败或显示错误地址

**解决方案：**
- 确认使用最新构建的 ELF 文件
- 重新构建项目：`west build -b nrf52840dk/nrf52840 -p`
- 检查 ELF 文件路径是否正确

### 3. 无法设置断点

**问题：** `Breakpoint 1 (main) pending`

**解决方案：**
- 确认代码已加载到目标
- 使用 `load` 命令加载固件
- 检查地址是否正确

### 4. 调试器连接失败

**问题：** OpenOCD 无法连接到目标

**解决方案：**
- 检查硬件连接
- 确认调试器驱动已安装
- 降低适配器速度：`adapter speed 1000`
- 检查复位配置

## 高级调试技巧

### 1. 条件断点

```gdb
# 只在特定条件下触发
(gdb) break gpio_pin_configure if pin == 14
(gdb) break app_task.cpp:53 if err != CHIP_NO_ERROR
```

### 2. 观察点（Watchpoint）

```gdb
# 监视变量变化
(gdb) watch variable_name
(gdb) watch pin
```

### 3. 命令序列

```gdb
# 在断点处自动执行命令
(gdb) commands 1
> print pin
> print flags
> continue
> end
```

### 4. 内存转储

```gdb
# 转储内存到文件
(gdb) dump binary memory dump.bin 0x20000000 0x20004000
```

### 5. 寄存器检查

```gdb
# 查看所有寄存器
(gdb) info registers

# 查看特定寄存器
(gdb) print $r0
(gdb) print $pc
(gdb) print $sp
```

## 相关文件路径

- **ELF 文件：** `build/light_bulb_mote/zephyr/zephyr.elf`
- **MCUboot ELF：** `build/mcuboot/zephyr/zephyr.elf`
- **OpenOCD 配置：** `openocd.cfg`（项目根目录）
- **GDB：** `/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb`
- **源代码：** `src/app_task.cpp`, `src/main.cpp`

## 参考资源

- [GDB 文档](https://sourceware.org/gdb/documentation/)
- [OpenOCD 文档](http://openocd.org/doc/html/index.html)
- [Zephyr 调试指南](https://docs.zephyrproject.org/latest/develop/debugging/index.html)
- [nRF Connect SDK 调试](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/ug_debugging.html)

