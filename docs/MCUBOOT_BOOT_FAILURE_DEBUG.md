# MCUboot 启动失败排查文档

## 错误现象

MCUboot 启动时出现以下错误：

```
I: Starting bootloader
D: context_boot_go
E: Failed to open flash area ID 1 (image 0 slot 1): -19, cannot continue
```

**错误代码说明：**
- `-19` = `-ENODEV`：设备不存在或无法访问
- Flash area ID 1 对应 secondary slot（image 0 slot 1）

## 排查过程

### 1. 检查分区配置

首先检查了分区管理器配置文件 `pm_static_nrf52840dk_nrf52840.yml`：

```yaml
mcuboot_secondary:
  address: 0x0
  size: 0xf0000
  device: MX25R64
  region: external_flash
```

**发现：** Secondary slot 配置在外部 Flash（MX25R64）上。

### 2. 检查 MCUboot 配置

检查了 MCUboot 的配置文件：

**`sysbuild/mcuboot/boards/nrf52840dk_nrf52840.conf`：**
```conf
# Enable QSPI NOR for external flash access (required for MCUboot secondary)
CONFIG_NORDIC_QSPI_NOR=y
CONFIG_NORDIC_QSPI_NOR_FLASH_LAYOUT_PAGE_SIZE=4096
CONFIG_NORDIC_QSPI_NOR_STACK_WRITE_BUFFER_SIZE=16
```

**`sysbuild/mcuboot/boards/nrf52840dk_nrf52840.overlay`：**
```dts
/ {
	chosen {
		zephyr,code-partition = &boot_partition;
		nordic,pm-ext-flash = &mx25r64;
	};
};
```

**发现：** MCUboot 已配置为使用外部 Flash，设备树中指定了 `mx25r64` 作为外部 Flash。

### 3. 检查 Partition Manager 配置

检查了生成的 `pm_config.h`：

```c
#define PM_MCUBOOT_SECONDARY_DEV DT_CHOSEN(nordic_pm_ext_flash)
#define PM_MCUBOOT_SECONDARY_DEFAULT_DRIVER_KCONFIG CONFIG_PM_EXTERNAL_FLASH_HAS_DRIVER
```

**发现：** Secondary slot 的设备节点指向 `nordic_pm_ext_flash`（即 `mx25r64`）。

### 4. 检查设备树配置

检查了生成的设备树文件 `zephyr.dts`：

```dts
chosen {
    nordic,pm-ext-flash = &mx25r64;
};

qspi: qspi@40029000 {
    status = "okay";
    mx25r64: mx25r6435f@0 {
        compatible = "nordic,qspi-nor";
        reg = <0x0>;
        // ...
    };
};
```

**发现：** 设备树中 QSPI 和外部 Flash 节点都已配置为 `okay`。

### 5. 检查驱动配置

检查了构建配置：

```conf
CONFIG_NORDIC_QSPI_NOR=y
CONFIG_PM_EXTERNAL_FLASH_HAS_DRIVER=y
CONFIG_PM_EXTERNAL_FLASH_ENABLED=y
```

**发现：** QSPI NOR 驱动已启用，Partition Manager 也检测到外部 Flash 驱动。

### 6. 尝试添加驱动检查覆盖

尝试添加了 `CONFIG_PM_OVERRIDE_EXTERNAL_DRIVER_CHECK=y` 配置：

```conf
# Override external driver check to ensure external flash is available
CONFIG_PM_OVERRIDE_EXTERNAL_DRIVER_CHECK=y
```

**结果：** 问题仍然存在，说明不是驱动检查的问题。

### 7. 确认硬件状态

**最终确认：** 外部 Flash（MX25R64）**未焊接**在开发板上。

## 问题根源

1. **硬件问题：** 外部 Flash 芯片未焊接，导致 MCUboot 无法访问 secondary slot
2. **配置不匹配：** 分区配置将 secondary slot 放在外部 Flash 上，但硬件上不存在该设备
3. **错误传播：** `flash_area_open()` 尝试打开外部 Flash 设备时返回 `-ENODEV`

## 解决方案

### 方案 1：将 Secondary Slot 移到内部 Flash（推荐）

修改 `pm_static_nrf52840dk_nrf52840.yml`，将 secondary slot 从外部 Flash 移到内部 Flash：

```yaml
mcuboot_secondary:
  address: 0x0
  size: 0xf0000
  region: flash_primary  # 改为内部 Flash
  # 移除 device: MX25R64
```

**注意：** 这需要调整内部 Flash 的分区布局，可能需要减小其他分区的大小。

### 方案 2：使用单 Slot 模式

如果不需要 DFU 功能，可以启用单 slot 模式：

在 `sysbuild/mcuboot/prj.conf` 或 `sysbuild/mcuboot/boards/nrf52840dk_nrf52840.conf` 中添加：

```conf
CONFIG_SINGLE_APPLICATION_SLOT=y
```

**注意：** 单 slot 模式不支持固件升级功能。

### 方案 3：焊接外部 Flash

如果确实需要外部 Flash，可以：
1. 焊接 MX25R64 芯片
2. 确保硬件连接正确
3. 保持当前配置不变

## 相关文件

- **分区配置：** `pm_static_nrf52840dk_nrf52840.yml`
- **MCUboot 配置：** `sysbuild/mcuboot/boards/nrf52840dk_nrf52840.conf`
- **设备树覆盖：** `sysbuild/mcuboot/boards/nrf52840dk_nrf52840.overlay`
- **MCUboot 主配置：** `sysbuild/mcuboot/prj.conf`
- **Sysbuild 配置：** `sysbuild_internal.conf`

## 参考文档

- [MCUboot 文档](https://mcuboot.com/)
- [nRF Connect SDK Partition Manager 文档](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/scripts/partition_manager/partition_manager.html)
- [Zephyr Flash Map API](https://docs.zephyrproject.org/latest/services/storage/flash_map.html)

## 总结

**关键教训：**
1. 配置分区时，必须确保硬件上存在对应的 Flash 设备
2. 外部 Flash 未焊接时，不能将分区配置在外部 Flash 上
3. 错误代码 `-ENODEV` 通常表示设备不存在或无法访问
4. 在开发阶段，应该先确认硬件配置，再配置软件分区

**下一步：**
根据实际需求选择上述解决方案之一，修改配置后重新构建和测试。

