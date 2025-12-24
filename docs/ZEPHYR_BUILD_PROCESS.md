# Zephyr 构建流程：zephyr_pre0.elf 的作用

本文档详细解释 Zephyr 构建过程中 `zephyr_pre0.elf` 的作用和构建流程。

## 目录

1. [概述](#1-概述)
2. [多阶段链接流程](#2-多阶段链接流程)
3. [zephyr_pre0.elf 的作用](#3-zephyr_pre0elf-的作用)
4. [构建流程示例](#4-构建流程示例)
5. [相关配置文件](#5-相关配置文件)

---

## 1. 概述

Zephyr 构建系统使用**多阶段链接**（Multi-stage Linking）来生成最终的固件镜像。`zephyr_pre0.elf` 是第一个链接阶段的输出文件，它是一个**中间 ELF 文件**，用于生成后续构建阶段所需的信息。

### 1.1 为什么需要多阶段链接？

某些代码生成器需要知道：
- 内存段的最终大小和位置
- 中断服务例程（ISR）的地址
- 设备依赖关系
- 内核对象的位置

这些信息只有在第一次链接完成后才能确定，因此需要多阶段链接。

---

## 2. 多阶段链接流程

### 2.1 链接阶段

Zephyr 最多支持**三个阶段**的链接：

```
┌─────────────────────────────────────────────────────────────┐
│                   编译所有源文件                              │
│              (生成 .o 目标文件)                               │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  阶段 0: zephyr_pre0                                        │
│  ├── 链接所有目标文件                                        │
│  ├── 生成 zephyr_pre0.elf                                   │
│  ├── 段大小和地址可以调整                                     │
│  └── 用于生成 ISR 表、设备依赖等                             │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  阶段 1: zephyr_pre1 (可选)                                  │
│  ├── 使用 zephyr_pre0.elf 生成的信息                         │
│  ├── 段大小固定，地址不能改变                                 │
│  └── 生成 zephyr_pre1.elf                                   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  阶段 2: zephyr_final                                       │
│  ├── 最终链接                                               │
│  ├── 生成 zephyr.elf (最终镜像)                              │
│  └── 生成 zephyr.hex, zephyr.bin 等                         │
└─────────────────────────────────────────────────────────────┘
```

### 2.2 何时需要多阶段链接？

根据 `CMakeLists.txt` 的说明，多阶段链接在以下情况下需要：

| 条件 | 说明 | 需要的阶段 |
|------|------|-----------|
| `CONFIG_GEN_ISR_TABLES=y` | 生成 ISR 表 | pre0 → final |
| `CONFIG_DEVICE_DEPS=y` | 生成设备依赖结构体 | pre0 → pre1 → final |
| `CONFIG_USERSPACE=y` | 用户空间支持 | pre0 → pre1 → final |

**注意**：即使只需要一个链接阶段，Zephyr 也会创建 `zephyr_pre0`，然后将其映射到 `zephyr_final`。

---

## 3. zephyr_pre0.elf 的作用

### 3.1 主要用途

`zephyr_pre0.elf` 主要用于：

1. **生成 ISR 表** (`isr_tables.c`)
   - 扫描 ELF 文件中的 `.intList` 段
   - 提取中断服务例程的地址和参数
   - 生成中断向量表和软件 ISR 表

2. **生成设备依赖信息**
   - 分析设备树和驱动代码
   - 生成设备初始化顺序

3. **生成内核对象信息** (如果启用用户空间)
   - 生成内核对象类型哈希表
   - 生成应用程序内存分区信息

4. **生成链接器脚本**
   - 基于实际的内存布局生成最终的链接器脚本

### 3.2 ISR 表生成示例

`gen_isr_tables.py` 脚本使用 `zephyr_pre0.elf` 来生成 ISR 表：

```bash
python gen_isr_tables.py \
    --kernel zephyr_pre0.elf \
    --output-source isr_tables.c \
    --linker-output-files isr_tables_vt.ld isr_tables_swi.ld \
    --intlist-section .intList \
    --intlist-section intList \
    --sw-isr-table \
    --vector-table
```

**流程**：
1. 解析 `zephyr_pre0.elf` 文件
2. 提取 `.intList` 段中的中断信息
3. 生成 `isr_tables.c`（包含中断向量表和软件 ISR 表）
4. 生成链接器脚本片段（`isr_tables_vt.ld`, `isr_tables_swi.ld`）

### 3.3 内存布局

在 `zephyr_pre0` 阶段：
- ✅ 链接器段**可以调整大小**
- ✅ 内存地址**可以重定位**
- ⚠️ 这是为了给代码生成器提供足够的信息

在后续阶段：
- ❌ 段大小**固定**
- ❌ 地址**不能改变**

---

## 4. 构建流程示例

### 4.1 典型构建流程

以 `hello_world` 示例为例：

```bash
west build -b nrf52840dk_nrf52840 samples/hello_world
```

**实际执行的步骤**：

1. **编译阶段**
   ```bash
   # 编译所有源文件
   gcc -c main.c -o main.c.obj
   gcc -c kernel/init.c -o init.c.obj
   # ... 更多源文件
   ```

2. **第一次链接 (zephyr_pre0)**
   ```bash
   # 使用 linker_zephyr_pre0.cmd
   ld -T linker_zephyr_pre0.cmd \
      -o zephyr_pre0.elf \
      main.c.obj init.c.obj ... \
      -lzephyr -lapp
   ```

3. **生成 ISR 表**
   ```bash
   python gen_isr_tables.py \
       --kernel zephyr_pre0.elf \
       --output-source isr_tables.c
   ```

4. **编译生成的代码**
   ```bash
   gcc -c isr_tables.c -o isr_tables.c.obj
   ```

5. **最终链接 (zephyr_final)**
   ```bash
   # 使用 linker.cmd (基于 zephyr_pre0 的信息生成)
   ld -T linker.cmd \
      -o zephyr.elf \
      main.c.obj init.c.obj isr_tables.c.obj ... \
      -lzephyr -lapp
   ```

6. **生成最终镜像**
   ```bash
   objcopy -O ihex zephyr.elf zephyr.hex
   objcopy -O binary zephyr.elf zephyr.bin
   ```

### 4.2 构建输出文件

| 文件 | 说明 | 阶段 |
|------|------|------|
| `zephyr_pre0.elf` | 第一次链接的输出 | pre0 |
| `zephyr_pre0.map` | 第一次链接的内存映射 | pre0 |
| `linker_zephyr_pre0.cmd` | 第一次链接的链接器脚本 | pre0 |
| `isr_tables.c` | 生成的 ISR 表代码 | 生成 |
| `linker.cmd` | 最终链接器脚本 | final |
| `zephyr.elf` | 最终 ELF 文件 | final |
| `zephyr.hex` | Intel HEX 格式 | final |
| `zephyr.bin` | 二进制格式 | final |

---

## 5. 相关配置文件

### 5.1 链接器脚本

**第一次链接**：`linker_zephyr_pre0.cmd`
- 基于设备树和 Kconfig 生成
- 包含基本的内存布局
- 段大小和地址可以调整

**最终链接**：`linker.cmd`
- 基于 `zephyr_pre0.elf` 的实际布局生成
- 包含精确的内存地址
- 段大小和地址固定

### 5.2 CMake 配置

在 `CMakeLists.txt` 中：

```cmake
# 当前链接阶段
set(ZEPHYR_CURRENT_LINKER_PASS 0)
set(ZEPHYR_CURRENT_LINKER_CMD linker_zephyr_pre${ZEPHYR_CURRENT_LINKER_PASS}.cmd)
set(ZEPHYR_LINK_STAGE_EXECUTABLE zephyr_pre${ZEPHYR_CURRENT_LINKER_PASS})

# 确定需要多少个链接阶段
if(CONFIG_USERSPACE OR CONFIG_DEVICE_DEPS)
  set(ZEPHYR_PREBUILT_EXECUTABLE zephyr_pre1)
else()
  set(ZEPHYR_PREBUILT_EXECUTABLE zephyr_pre0)
endif()
```

### 5.3 构建日志示例

查看构建日志，可以看到：

```
[1/10] Linking C executable zephyr/zephyr_pre0.elf
[2/10] Generating isr_tables.c
[3/10] Compiling generated file isr_tables.c
[4/10] Linking C executable zephyr/zephyr.elf
[5/10] Generating zephyr.hex
[6/10] Generating zephyr.bin
```

---

## 6. 调试和故障排查

### 6.1 查看 zephyr_pre0.elf 的内容

```bash
# 查看符号表
arm-none-eabi-nm zephyr_pre0.elf

# 查看段信息
arm-none-eabi-objdump -h zephyr_pre0.elf

# 查看内存映射
cat zephyr_pre0.map
```

### 6.2 常见问题

**问题 1**：`zephyr_pre0.elf` 文件过大
- **原因**：某些平台（如 Intel ADSP）在 pre0 阶段会包含所有代码
- **解决**：这是正常的，最终镜像会优化

**问题 2**：ISR 表生成失败
- **原因**：`zephyr_pre0.elf` 中缺少 `.intList` 段
- **解决**：检查中断配置和驱动代码

**问题 3**：链接错误
- **原因**：内存不足或段重叠
- **解决**：检查 `zephyr_pre0.map` 查看内存布局

---

## 7. 总结

`zephyr_pre0.elf` 是 Zephyr 构建过程中的关键中间文件：

1. ✅ **第一个链接阶段的输出**
2. ✅ **用于生成 ISR 表、设备依赖等信息**
3. ✅ **为最终链接提供内存布局信息**
4. ✅ **在大多数情况下是必需的**（即使只有一个链接阶段）

理解 `zephyr_pre0.elf` 的作用有助于：
- 调试构建问题
- 优化内存布局
- 理解 Zephyr 的构建流程

---

## 参考资源

- [Zephyr CMakeLists.txt](`/opt/nordic/ncs/v3.1.1/zephyr/CMakeLists.txt`)
- [gen_isr_tables.py 脚本](`/opt/nordic/ncs/v3.1.1/zephyr/scripts/build/gen_isr_tables.py`)
- [Zephyr 构建系统文档](https://docs.zephyrproject.org/latest/build/index.html)

