# 复位到正常启动状态指南

## 问题描述

执行 `monitor reset halt` 后，系统进入了 HardFault 状态而不是正常的启动状态：

```
[nrf52.cpu] halted due to debug-request, current mode: Handler HardFault
xPSR: 0x01000003 pc: 0x00098bfc msp: 0x20024a28
```

## 解决方案

### 方法 1：使用正常的复位序列（推荐）

在 GDB 中执行以下命令：

```gdb
# 1. 先执行正常复位（不立即 halt）
(gdb) monitor reset

# 2. 等待一小段时间让系统初始化
(gdb) shell sleep 0.1

# 3. 然后暂停
(gdb) halt

# 4. 检查状态
(gdb) info registers
(gdb) where
```

### 方法 2：使用 reset run 然后 attach

```gdb
# 1. 让系统正常运行
(gdb) monitor reset run

# 2. 等待系统启动
(gdb) shell sleep 1

# 3. 暂停系统
(gdb) interrupt

# 4. 检查当前状态
(gdb) where
(gdb) info registers
```

### 方法 3：清除 HardFault 状态并复位

```gdb
# 1. 如果已经在 HardFault 中，先清除状态
(gdb) monitor reset

# 2. 设置断点在复位向量
(gdb) break *0x8d8
# 或
(gdb) break z_arm_reset

# 3. 继续执行，应该停在复位向量
(gdb) continue

# 4. 现在系统应该在正常启动状态
(gdb) info registers
```

### 方法 4：使用硬件复位

如果软件复位有问题，尝试硬件复位：

```gdb
# 1. 执行硬件复位
(gdb) monitor reset halt

# 2. 如果还是 HardFault，尝试清除并重新复位
(gdb) monitor reset
(gdb) monitor reset halt

# 3. 检查向量表
(gdb) x/4xw 0x0
```

## 完整的复位和调试流程

### 步骤 1：复位到正常状态

```gdb
# 连接到目标
(gdb) target remote localhost:3333

# 加载符号表
(gdb) add-symbol-file build/mcuboot/zephyr/zephyr.elf 0x0
(gdb) add-symbol-file build/light_bulb_mote/zephyr/zephyr.elf 0xC200

# 执行正常复位
(gdb) monitor reset

# 等待初始化
(gdb) shell sleep 0.2

# 暂停
(gdb) halt

# 检查状态（应该不在 HardFault）
(gdb) info registers
(gdb) where
```

### 步骤 2：设置断点并开始调试

```gdb
# 设置断点在复位向量
(gdb) break *0x8d8
# 或
(gdb) break z_arm_reset

# 设置断点在 MCUboot main
(gdb) break main

# 设置断点在应用程序
(gdb) break AppTask::StartApp

# 继续执行
(gdb) continue
```

## 检查复位配置

### OpenOCD 配置

检查或创建 `openocd.cfg`：

```tcl
# openocd.cfg
source [find interface/jlink.cfg]
transport select swd
source [find target/nrf52.cfg]

set WORKAREASIZE 0x4000

# 复位配置 - 使用硬件复位
reset_config srst_only srst_nogate connect_assert_srst

# 适配器速度
adapter speed 1000

# 初始化
init
```

### 关键配置说明

- `srst_only`：只使用硬件复位（SRST）
- `srst_nogate`：复位时不门控调试接口
- `connect_assert_srst`：连接时断言复位信号

## 常见问题排查

### 1. 复位后总是进入 HardFault

**可能原因：**
- 复位配置不正确
- 系统状态损坏
- Flash 内容有问题

**解决方案：**
```gdb
# 1. 检查向量表
(gdb) x/4xw 0x0

# 2. 检查复位向量地址
(gdb) x/xw 0x4

# 3. 尝试硬件复位
(gdb) monitor reset
(gdb) monitor reset halt

# 4. 如果还是不行，可能需要重新烧录固件
```

### 2. 复位后 PC 不在预期位置

**解决方案：**
```gdb
# 1. 设置断点在复位向量
(gdb) break *0x8d8

# 2. 复位
(gdb) monitor reset halt

# 3. 应该停在复位向量
(gdb) info registers
```

### 3. 无法正常启动

**解决方案：**
```gdb
# 1. 检查 Flash 内容
(gdb) x/20xw 0x0

# 2. 检查应用程序入口
(gdb) x/20xw 0xC200

# 3. 尝试单步执行
(gdb) monitor reset halt
(gdb) stepi
(gdb) stepi
```

## 验证正常启动状态

### 检查寄存器状态

正常启动时，寄存器应该显示：

```gdb
(gdb) info registers
# PC 应该在复位向量或初始化代码中
# xPSR 应该是 Thread 模式，不是 HardFault
# MSP 应该有合理的值
```

### 检查调用栈

```gdb
(gdb) where
# 应该显示正常的调用栈，不是 HardFault 处理函数
```

### 检查向量表

```gdb
(gdb) x/4xw 0x0
# 应该显示：
# 0x0: MSP 初始值
# 0x4: 复位向量地址（如 0x8d9）
# 0x8: NMI 向量地址
# 0xC: HardFault 向量地址
```

## GDB 命令参考

### 复位相关命令

```gdb
# 软件复位并立即暂停
monitor reset halt

# 软件复位并运行
monitor reset run

# 软件复位（不指定运行/暂停）
monitor reset

# 硬件复位
monitor reset halt

# 暂停执行
halt
interrupt

# 继续执行
continue
c

# 单步执行
step
stepi
next
nexti
```

### 状态检查命令

```gdb
# 查看寄存器
info registers
info all-registers

# 查看调用栈
where
backtrace
bt

# 查看内存
x/10xw 0x0
x/10i $pc

# 查看变量
print variable_name
info locals
```

## 推荐的调试启动流程

```gdb
# 1. 连接到目标
(gdb) target remote localhost:3333

# 2. 加载符号表
(gdb) add-symbol-file build/mcuboot/zephyr/zephyr.elf 0x0
(gdb) add-symbol-file build/light_bulb_mote/zephyr/zephyr.elf 0xC200

# 3. 设置断点在复位向量
(gdb) break *0x8d8

# 4. 执行复位并停止
(gdb) monitor reset halt

# 5. 验证状态（应该停在复位向量）
(gdb) info registers
(gdb) where

# 6. 设置其他断点
(gdb) break main
(gdb) break AppTask::StartApp

# 7. 开始调试
(gdb) continue
```

## 如果仍然进入 HardFault

### 检查 Flash 内容

```gdb
# 检查向量表
(gdb) x/4xw 0x0

# 检查 MCUboot 入口
(gdb) x/20xw 0x0

# 检查应用程序入口
(gdb) x/20xw 0xC200
```

### 重新烧录固件

如果 Flash 内容损坏，需要重新烧录：

```bash
# 使用 west 烧录
west flash

# 或使用 nrfjprog
nrfjprog --program build/merged.hex --chiperase --reset
```

### 检查硬件连接

- 确认调试器连接正常
- 检查电源供应
- 确认复位引脚连接

## 总结

**正常复位到启动状态的步骤：**

1. 使用 `monitor reset` 而不是 `monitor reset halt`
2. 等待系统初始化
3. 然后 `halt` 暂停
4. 设置断点在复位向量
5. 继续执行并跟踪启动流程

**关键命令：**
```gdb
monitor reset          # 正常复位
shell sleep 0.1       # 等待初始化
halt                  # 暂停
break *0x8d8          # 在复位向量设置断点
continue              # 开始执行
```

