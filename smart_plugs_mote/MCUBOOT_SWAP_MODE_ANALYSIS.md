# MCUboot 交换模式分析文档

## 检查结果

**`MCUBOOT_SWAP_USING_SCRATCH` 特性未启用**

当前系统使用的是 **`BOOT_SWAP_USING_MOVE`** 模式，而不是 `SWAP_USING_SCRATCH` 模式。

## 当前配置状态

### 1. 构建配置

从构建配置文件 `.config` 中可以看到：

```conf
CONFIG_BOOT_SWAP_USING_MOVE=y
```

### 2. 模式选择逻辑

MCUboot 的模式选择逻辑（来自 `bootutil_priv.h`）：

```c
// 如果定义了其他模式，就不会使用 SCRATCH
#if !defined(MCUBOOT_OVERWRITE_ONLY) && \
    !defined(MCUBOOT_SWAP_USING_MOVE) && \
    !defined(MCUBOOT_SWAP_USING_OFFSET) && \
    !defined(MCUBOOT_DIRECT_XIP) && \
    !defined(MCUBOOT_RAM_LOAD) && \
    !defined(MCUBOOT_SINGLE_APPLICATION_SLOT) && \
    !defined(MCUBOOT_FIRMWARE_LOADER)
#define MCUBOOT_SWAP_USING_SCRATCH 1
#endif
```

**说明：** `MCUBOOT_SWAP_USING_SCRATCH` 是默认模式，只有在没有定义其他任何模式时才会启用。由于当前配置中启用了 `CONFIG_BOOT_SWAP_USING_MOVE`，因此不会使用 SCRATCH 模式。

### 3. 分区配置

检查 `pm_static_nrf52840dk_nrf52840.yml`：

- ❌ **没有** `mcuboot_scratch` 分区定义
- ✅ 构建配置中有 `CONFIG_PM_PARTITION_SIZE_MCUBOOT_SCRATCH=0x1e000`，但这是 Partition Manager 的默认分配，实际未使用

## MCUboot 交换模式对比

| 模式 | Kconfig 配置选项 | 是否需要 Scratch 分区 | 特点 | 适用场景 |
|------|-----------------|---------------------|------|---------|
| **SWAP_USING_SCRATCH** | `CONFIG_BOOT_SWAP_USING_SCRATCH` | ✅ **需要** | • 最保守的交换模式<br>• 支持异构 Flash 页布局<br>• 需要额外的 scratch 分区 | • Flash 页大小不一致<br>• 需要最安全的交换机制 |
| **SWAP_USING_MOVE** | `CONFIG_BOOT_SWAP_USING_MOVE` | ❌ **不需要** | • 不需要 scratch 分区<br>• 要求所有扇区大小相同<br>• 两步交换过程 | • 同构 Flash 布局<br>• 节省 Flash 空间（不需要 scratch） |
| **SWAP_USING_OFFSET** | `CONFIG_BOOT_SWAP_USING_OFFSET` | ❌ **不需要** | • 最快的交换模式<br>• 不需要 scratch 分区<br>• 要求所有扇区大小相同 | • 性能要求高<br>• 同构 Flash 布局 |
| **OVERWRITE_ONLY** | `CONFIG_BOOT_UPGRADE_ONLY` | ❌ **不需要** | • 直接覆盖 primary slot<br>• 不支持回退<br>• 最简单的代码路径 | • 不需要回退功能<br>• 节省 Flash 空间 |
| **SINGLE_APPLICATION_SLOT** | `CONFIG_SINGLE_APPLICATION_SLOT` | ❌ **不需要** | • 单 slot 模式<br>• 不支持 DFU | • 不需要固件升级功能 |

## 当前使用的模式：SWAP_USING_MOVE

### 工作原理

`SWAP_USING_MOVE` 模式执行两步交换过程：

1. **第一步：** 将 primary slot 的每个扇区向上移动一个扇区位置
2. **第二步：** 对于 secondary slot 中的每个扇区 X：
   - 将扇区 X 移动到 primary slot 的索引 X
   - 将 primary slot 中索引 X+1 的扇区移动到 secondary slot 的索引 X

### 优点

- ✅ 不需要额外的 scratch 分区，节省 Flash 空间
- ✅ 支持回退功能
- ✅ 适用于同构 Flash 布局（所有扇区大小相同）

### 限制

- ❌ 要求 primary 和 secondary slot 的所有扇区大小必须相同
- ❌ 不支持异构 Flash 页布局（例如：内部 Flash 和外部 Flash 扇区大小不同）

## 如何启用 SWAP_USING_SCRATCH 模式

如果需要启用 `SWAP_USING_SCRATCH` 模式，需要执行以下步骤：

### 1. 修改 MCUboot 配置

在 `sysbuild/mcuboot/prj.conf` 或 `sysbuild/mcuboot/boards/nrf52840dk_nrf52840.conf` 中：

```conf
# 禁用 SWAP_USING_MOVE
# CONFIG_BOOT_SWAP_USING_MOVE is not set

# 启用 SWAP_USING_SCRATCH
CONFIG_BOOT_SWAP_USING_SCRATCH=y
```

### 2. 添加 Scratch 分区

在 `pm_static_nrf52840dk_nrf52840.yml` 中添加 scratch 分区：

```yaml
mcuboot_scratch:
  address: 0x100000  # 根据实际 Flash 布局调整
  size: 0x20000      # 建议大小：至少是最大扇区大小的 3 倍
  region: flash_primary  # 或 external_flash，取决于布局
```

**Scratch 分区大小建议：**
- 最小大小：`max(primary_slot_sector_size, secondary_slot_sector_size) * 3`
- 例如：如果最大扇区是 4KB，scratch 分区至少需要 12KB
- 更大的 scratch 分区可以减少 Flash 擦写次数，延长 Flash 寿命

### 3. 重新构建

```bash
west build -b nrf52840dk/nrf52840 -p
```

## 模式选择建议

### 选择 SWAP_USING_SCRATCH 的情况

- ✅ Primary 和 secondary slot 的扇区大小不同（异构 Flash）
- ✅ 需要最安全的交换机制
- ✅ 有足够的 Flash 空间用于 scratch 分区

### 选择 SWAP_USING_MOVE 的情况（当前模式）

- ✅ Primary 和 secondary slot 的扇区大小相同（同构 Flash）
- ✅ Flash 空间有限，需要节省空间
- ✅ 性能要求不是特别高

### 选择 SWAP_USING_OFFSET 的情况

- ✅ Primary 和 secondary slot 的扇区大小相同
- ✅ 需要最快的交换速度
- ✅ Flash 空间有限

### 选择 OVERWRITE_ONLY 的情况

- ✅ 不需要回退功能
- ✅ Flash 空间非常有限
- ✅ 可以接受直接覆盖的风险

## 相关文件

- **MCUboot 配置：** `sysbuild/mcuboot/prj.conf`
- **板级配置：** `sysbuild/mcuboot/boards/nrf52840dk_nrf52840.conf`
- **分区配置：** `pm_static_nrf52840dk_nrf52840.yml`
- **模式定义：** `/opt/nordic/ncs/v3.1.1/bootloader/mcuboot/boot/bootutil/src/bootutil_priv.h`
- **Kconfig 选项：** `/opt/nordic/ncs/v3.1.1/bootloader/mcuboot/boot/zephyr/Kconfig`

## 参考文档

- [MCUboot 设计文档 - Swap 模式](https://mcuboot.com/mcuboot/design.html)
- [MCUboot Zephyr 文档](https://mcuboot.com/mcuboot/readme-zephyr.html)
- [nRF Connect SDK MCUboot 文档](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/ug_mcuboot.html)

## 总结

- **当前模式：** `BOOT_SWAP_USING_MOVE`
- **SCRATCH 模式状态：** ❌ 未启用
- **原因：** 已启用 `CONFIG_BOOT_SWAP_USING_MOVE`，因此不会使用默认的 SCRATCH 模式
- **建议：** 当前配置适合同构 Flash 布局，如果 Flash 扇区大小一致，无需更改

