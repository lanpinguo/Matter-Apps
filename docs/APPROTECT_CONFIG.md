# APPROTECT 配置指南

本文档介绍如何在构建时配置关闭 APPROTECT，以便在开发阶段进行调试和烧录。

## 目录

1. [APPROTECT 概述](#1-appprotect-概述)
2. [不同芯片系列的配置方法](#2-不同芯片系列的配置方法)
3. [配置位置](#3-配置位置)
4. [验证配置](#4-验证配置)
5. [注意事项](#5-注意事项)

---

## 1. APPROTECT 概述

### 1.1 什么是 APPROTECT？

**APPROTECT** (Application Protection) 是 Nordic 芯片的一个安全特性，用于防止未授权访问 Flash 和 RAM。当 APPROTECT 启用时：

- ✅ 防止通过调试接口（SWD/JTAG）访问 Flash
- ✅ 防止通过调试接口读取 RAM 内容
- ✅ 防止未授权的固件更新

### 1.2 为什么需要关闭 APPROTECT？

在**开发阶段**，关闭 APPROTECT 可以：

- 🔧 允许通过调试器（J-Link, OpenOCD）进行调试
- 🔧 允许通过 `nrfjprog` 等工具烧录固件
- 🔧 允许读取 Flash 内容进行调试

⚠️ **警告**：生产环境应启用 APPROTECT 以保护固件安全。

---

## 2. 不同芯片系列的配置方法

### 2.1 nRF52 系列 (nRF52832, nRF52840 等)

**配置方法**：在 `prj.conf` 或子镜像配置文件中添加：

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

**说明**：
- `CONFIG_NRF_APPROTECT_USE_UICR=n`：禁用通过 UICR 配置 APPROTECT
- `# CONFIG_NRF_APPROTECT_LOCK is not set`：确保不锁定 APPROTECT

### 2.2 nRF53 系列 (nRF5340)

**配置方法**：与 nRF52 相同

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set

# Disable Secure APPROTECT (if using secure domain)
CONFIG_NRF_SECURE_APPROTECT_USE_UICR=n
# CONFIG_NRF_SECURE_APPROTECT_LOCK is not set
```

### 2.3 nRF54 系列 (nRF54L15, nRF54H20 等)

**配置方法**：使用 `CONFIG_NRF_APPROTECT_DISABLE`

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_DISABLE=y

# Disable Secure APPROTECT (if using secure domain)
CONFIG_NRF_SECURE_APPROTECT_DISABLE=y
```

### 2.4 nRF91 系列 (nRF9160)

**配置方法**：与 nRF52/nRF53 相同

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

---

## 3. 配置位置

### 3.1 应用程序配置

**文件**：`prj.conf` 或 `prj_release.conf`

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

**示例**：`apps/light_bulb_mote/prj.conf`

```46:48:apps/light_bulb_mote/prj.conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

### 3.2 MCUboot 配置

**文件**：`sysbuild/mcuboot/prj.conf`

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

**示例**：`apps/light_bulb_mote/sysbuild/mcuboot/prj.conf`

```43:45:apps/light_bulb_mote/sysbuild/mcuboot/prj.conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

### 3.3 Sysbuild 级别配置（可选）

**文件**：`sysbuild.conf` 或 `Kconfig.sysbuild`

如果使用 sysbuild，可以在 sysbuild 级别统一配置：

```kconfig
# 在 Kconfig.sysbuild 中
config APPROTECT_HANDLING
    default APPROTECT_NO_SYSBUILD
```

或者在 `sysbuild.conf` 中：

```conf
# 不启用 sysbuild 级别的 APPROTECT 配置
CONFIG_APPROTECT_NO_SYSBUILD=y
```

---

## 4. 验证配置

### 4.1 检查生成的配置文件

构建后，检查生成的配置文件：

```bash
# 检查应用程序配置
grep APPROTECT build/light_bulb_mote/zephyr/include/generated/zephyr/autoconf.h

# 检查 MCUboot 配置
grep APPROTECT build/mcuboot/zephyr/include/generated/zephyr/autoconf.h
```

**期望输出**（nRF52/nRF53）：
```c
#define CONFIG_NRF_APPROTECT_USE_UICR 1  // 如果设置为 n，这里应该是 0 或不存在
// CONFIG_NRF_APPROTECT_LOCK is not set
```

**期望输出**（nRF54）：
```c
#define CONFIG_NRF_APPROTECT_DISABLE 1
```

### 4.2 使用 nrfjprog 验证

```bash
# 读取 UICR 寄存器（nRF52/nRF53）
nrfjprog --memrd 0x10001208 --n 1

# 如果 APPROTECT 已禁用，应该看到 0xFFFFFFFE 或类似值
# 如果 APPROTECT 已启用，应该看到 0xFFFFFF00
```

### 4.3 使用 J-Link Commander 验证

```bash
# 启动 J-Link Commander
JLinkExe -device nRF52840 -if SWD -speed 4000

# 在 J-Link 命令行中
> connect
> mem32 0x10001208 1

# 检查返回值
```

---

## 5. 注意事项

### 5.1 开发 vs 生产环境

| 环境 | APPROTECT 配置 | 说明 |
|------|---------------|------|
| **开发** | 禁用 | 允许调试和烧录 |
| **生产** | 启用 | 保护固件安全 |

### 5.2 配置一致性

在使用 **sysbuild** 构建多个镜像时，确保所有镜像的 APPROTECT 配置一致：

- ✅ 应用程序 (`prj.conf`)
- ✅ MCUboot (`sysbuild/mcuboot/prj.conf`)
- ✅ 网络核心（如果使用 nRF53/nRF91）

### 5.3 已锁定的芯片

如果芯片的 APPROTECT 已经被锁定（通过 UICR），**无法通过软件配置解除**。需要：

1. 使用 `nrfjprog --recover` 恢复芯片（会擦除所有内容）
2. 或者使用特殊的恢复流程（参考 Nordic 文档）

### 5.4 配置选项说明

| 配置选项 | 说明 | 适用芯片 |
|---------|------|---------|
| `CONFIG_NRF_APPROTECT_USE_UICR=n` | 禁用 UICR 配置 APPROTECT | nRF52, nRF53, nRF91 |
| `CONFIG_NRF_APPROTECT_DISABLE=y` | 直接禁用 APPROTECT | nRF54 |
| `CONFIG_NRF_APPROTECT_LOCK` | 锁定 APPROTECT（生产用） | 所有系列 |
| `CONFIG_NRF_APPROTECT_USER_HANDLING` | 允许用户自定义处理 | nRF53, nRF54, nRF91 |

### 5.5 完整配置示例

**nRF52840 开发配置** (`prj.conf`):

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set
```

**nRF5340 开发配置** (`prj.conf`):

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_USE_UICR=n
# CONFIG_NRF_APPROTECT_LOCK is not set

# Disable Secure APPROTECT
CONFIG_NRF_SECURE_APPROTECT_USE_UICR=n
# CONFIG_NRF_SECURE_APPROTECT_LOCK is not set
```

**nRF54L15 开发配置** (`prj.conf`):

```conf
# Disable APPROTECT for development
CONFIG_NRF_APPROTECT_DISABLE=y
CONFIG_NRF_SECURE_APPROTECT_DISABLE=y
```

---

## 6. 故障排查

### 6.1 仍然无法调试

如果配置后仍然无法调试，检查：

1. **清理并重新构建**：
   ```bash
   rm -rf build
   west build -b <board>
   ```

2. **检查所有镜像配置**：
   - 应用程序配置
   - MCUboot 配置
   - 网络核心配置（nRF53）

3. **验证 UICR 寄存器**：
   ```bash
   nrfjprog --memrd 0x10001208 --n 1
   ```

### 6.2 配置不生效

如果配置不生效，可能原因：

1. **缓存问题**：清理构建目录
2. **配置冲突**：检查是否有其他配置文件覆盖了设置
3. **芯片已锁定**：使用 `nrfjprog --recover` 恢复

---

## 参考资源

- [Nordic APPROTECT 文档](https://infocenter.nordicsemi.com/)
- [nRF Connect SDK 文档](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/)
- [Kconfig.approtect 源码](`/opt/nordic/ncs/v3.1.1/nrf/sysbuild/Kconfig.approtect`)

