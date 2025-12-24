# 为什么复位后的地址是 0x4698 而不是 0x4？

## 问题描述

在 GDB 中执行 `monitor reset halt` 后，PC（程序计数器）显示为 `0x4698`（`arch_cpu_idle+18`），而不是预期的 `0x4`（复位向量地址）。

## 关键理解

### 1. 向量表的结构

在 ARM Cortex-M 架构中，向量表的结构如下：

```
地址      内容                    说明
0x0000    MSP 初始值              主栈指针（MSP）的初始值
0x0004    复位向量地址            复位后跳转的地址（不是 0x4 本身！）
0x0008    NMI 向量地址            NMI 中断处理函数地址
0x000C    硬故障向量地址           硬故障处理函数地址
...
```

从你的 GDB 输出可以看到：

```gdb
(gdb) x /20xw 0x0
0x0 <_vector_table>:    0x20001900      0x000008d9      0x00004663      0x000008c5
```

- **0x0**: `0x20001900` - 初始栈指针（MSP）
- **0x4**: `0x000008d9` - **复位向量的地址**（不是 0x4！）
- **0x8**: `0x00004663` - NMI 向量地址
- **0xC**: `0x000008c5` - 硬故障向量地址

### 2. 复位流程

当 CPU 复位时，ARM Cortex-M 的复位流程是：

```
1. CPU 复位
   ↓
2. 从 0x0 读取 MSP 初始值 → 0x20001900
   ↓
3. 从 0x4 读取复位向量地址 → 0x000008d9
   ↓
4. 跳转到复位向量地址 → 0x8d9 (z_arm_reset/__start)
   ↓
5. 执行复位处理函数
   ↓
6. 初始化系统
   ↓
7. 进入主循环或 idle 循环
```

**重要**：CPU **不会**执行地址 `0x4` 处的代码，而是**读取** `0x4` 处的值（`0x8d9`），然后跳转到该地址。

### 3. 为什么 PC 是 0x4698？

从你的 GDB 输出可以看到：

```gdb
(gdb) monitor reset halt
[nrf52.cpu] halted due to debug-request, current mode: Thread 
xPSR: 0x41000000 pc: 0x00004698 psp: 0x200014e0
(gdb) x/8xw 0x4698
0x4698 <arch_cpu_idle+18>:      0xf3bfb662      0x47708f6f      0x2300b672      0x8811f383
```

PC 指向 `arch_cpu_idle+18`，说明：

1. **系统已经完成了初始化**
2. **已经进入了 idle 循环**

### 4. `monitor reset halt` 的行为

`monitor reset halt` 命令的行为取决于 OpenOCD 的配置：

#### 情况 1：软件复位（默认行为）

如果 `reset_config` 设置为软件复位（`srst_only`），`reset halt` 会：

1. 执行软件复位
2. CPU 从复位向量（0x8d9）开始执行
3. **但是**，如果系统初始化很快，可能在 GDB 暂停之前就已经执行到了 idle 循环

#### 情况 2：硬件复位

如果配置了硬件复位，`reset halt` 会：

1. 执行硬件复位
2. CPU 从复位向量开始执行
3. 立即暂停（halt）

### 5. 验证复位向量地址

从链接器映射文件可以看到：

```
0x00000000000008d8                z_arm_reset
0x00000000000008d8                __start
```

复位向量地址是 `0x8d8`（或 `0x8d9`，取决于对齐），这与向量表中 `0x4` 处的值 `0x000008d9` 一致。

## 如何查看真正的复位入口？

### 方法 1：设置断点在复位向量

```gdb
# 设置断点在复位向量地址
(gdb) break *0x8d8
(gdb) monitor reset halt
# 现在应该停在复位向量处
```

### 方法 2：使用硬件复位

修改 OpenOCD 配置，使用硬件复位：

```tcl
# openocd.cfg
reset_config srst_only srst_nogate connect_assert_srst
```

然后：

```gdb
(gdb) monitor reset halt
# 应该停在复位向量处
```

### 方法 3：查看启动流程

```gdb
# 设置多个断点跟踪启动流程
(gdb) break z_arm_reset
(gdb) break z_prep_c
(gdb) break z_cstart
(gdb) break arch_cpu_idle
(gdb) monitor reset halt
(gdb) continue
# 逐步执行，观察启动流程
```

## 启动流程详解

完整的启动流程：

```
复位
 ↓
0x8d8: z_arm_reset / __start (reset.S)
  ├─ 设置 MSP
  ├─ 设置 PSP
  ├─ 锁定中断
  └─ 跳转到 z_prep_c
       ↓
z_prep_c (prep_c.c)
  ├─ relocate_vector_table()
  ├─ z_arm_floating_point_init()
  ├─ z_bss_zero()
  ├─ z_data_copy()
  ├─ z_arm_interrupt_init()
  └─ 跳转到 z_cstart
       ↓
z_cstart (init.c)
  ├─ z_sys_init_run_level(INIT_LEVEL_EARLY)
  ├─ arch_kernel_init()
  ├─ z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_1)
  ├─ z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_2)
  ├─ z_sys_init_run_level(INIT_LEVEL_POST_KERNEL)
  ├─ z_sys_init_run_level(INIT_LEVEL_APPLICATION)
  └─ switch_to_main_thread()
       ↓
main() 函数
  └─ 执行应用代码
       ↓
idle 循环
  └─ arch_cpu_idle() ← 你看到的 0x4698
```

## 总结

1. **0x4 不是执行地址**：`0x4` 存储的是复位向量的**地址值**（`0x8d9`），不是要执行的代码。

2. **复位向量是 0x8d9**：CPU 复位后，从 `0x4` 读取复位向量地址，然后跳转到 `0x8d9`（`z_arm_reset`）。

3. **PC 在 0x4698 的原因**：
   - 系统已经完成了初始化
   - 已经进入了 idle 循环
   - `monitor reset halt` 可能不是真正的硬件复位，或者系统初始化太快

4. **如何查看复位入口**：
   - 在复位向量地址设置断点：`break *0x8d8`
   - 使用硬件复位配置
   - 逐步跟踪启动流程

## 参考

- [ARM Cortex-M 向量表](https://developer.arm.com/documentation/dui0552/a/the-cortex-m3-processor/exception-model/vector-table)
- [Zephyr 启动流程](`/opt/nordic/ncs/v3.1.1/zephyr/arch/arm/core/cortex_m/reset.S`)
- [OpenOCD 复位配置](http://openocd.org/doc/html/Reset-Configuration.html)

