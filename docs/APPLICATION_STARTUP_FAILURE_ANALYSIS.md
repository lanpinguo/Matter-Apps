# 应用程序启动失败分析文档

## 错误现象

MCUboot 成功启动并验证镜像后，应用程序启动时发生断言失败：

```
I: Starting bootloader
D: context_boot_go
D: Non-optimal sector distribution, slot0 has 239 usable sectors (240 assigned) but slot1 has 240 assigned
I: Primary image: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
I: Secondary image: magic=unset, swap_type=0x1, copy_done=0x3, image_ok=0x3
I: Boot source: none
I: Image index: 0, Swap type: none
D: boot_validate_slot: slot 0, expected_swap_type 0
D: bootutil_img_validate: flash area 0x7528
D: bootutil_img_hash
D: bootutil_tlv_iter_begin: type 65535, prot == 0
D: bootutil_img_validate: TLV off 811336, end 811482
D: bootutil_tlv_iter_next: searching for 65535 (65535 is any) starting at 811336 ending at 811482
D: bootutil_tlv_iter_next: TLV 16 found at 811340 (size 32)
D: bootutil_img_validate: EXPECTED_HASH_TLV == 16
D: bootutil_tlv_iter_next: searching for 65535 (65535 is any) starting at 811372 ending at 811482
D: bootutil_tlv_iter_next: TLV 1 found at 811376 (size 32)
D: bootutil_img_validate: EXPECTED_KEY_TLV == 1
D: bootutil_find_key
D: bootutil_tlv_iter_next: searching for 65535 (65535 is any) starting at 811408 ending at 811482
D: bootutil_tlv_iter_next: TLV 34 found at 811412 (size 70)
D: bootutil_img_validate: EXPECTED_SIG_TLV == 34
D: bootutil_verify_sig: ECDSA builtin key 0
D: bootutil_tlv_iter_next: searching for 65535 (65535 is any) starting at 811482 ending at 811482
D: bootutil_tlv_iter_next: TLV 65535 not found
D: Left boot_go with success == 1
I: Bootloader chainload address offset: 0xc000
I: Image version: v3.1.1
*** Booting My Application v3.1.1-c30fc4bddb29 ***
*** Using nRF Connect SDK v3.1.1-e2a97fe2578a ***
*** Using Zephyr OS v4.1.99-ff8f0c579eeb ***
D: 29 [DL]Boot reason: 1
uart:~$ I: 33 [DL]BLE address: F0:6B:E3:A5:AF:B2
ASSERTION FAIL @ gpio.h:1020
E: r0/a1:  0x00000004  r1/a2:  0x000003fc  r2/a3:  0x00000001
E: r3/a4:  0x00000004 r12/ip:  0x00000000 r14/lr:  0x0002b2f9
E:  xpsr:  0x01000000
E: Faulting instruction address (r15/pc): 0x00084686
E: >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0
E: Current thread: 0x2000acf0 (main)
E: Halting system
```

## 错误分析

### 1. MCUboot 启动成功

- ✅ MCUboot 成功启动
- ✅ 镜像验证通过（ECDSA 签名验证成功）
- ✅ 成功跳转到应用程序（`Bootloader chainload address offset: 0xc000`）

### 2. 应用程序启动阶段

- ✅ 应用程序开始启动（显示了版本信息）
- ✅ BLE 地址初始化成功
- ❌ **在 GPIO 配置时发生断言失败**

### 3. 断言失败详情

**位置：** `gpio.h:1020`

**断言代码：**
```c
__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
         "Unsupported pin");
```

**含义：** 应用程序尝试配置一个不支持的 GPIO 引脚。该引脚不在 GPIO 控制器的 `port_pin_mask` 中。

**寄存器信息：**
- `r0/a1: 0x00000004` - 可能是错误代码或参数
- `r1/a2: 0x000003fc` - 可能是 GPIO 配置标志
- `r2/a3: 0x00000001` - 可能是引脚号
- `r3/a4: 0x00000004` - 可能是设备指针或参数
- `pc: 0x00084686` - 故障指令地址

### 4. 警告信息

**MCUboot 警告：**
```
D: Non-optimal sector distribution, slot0 has 239 usable sectors (240 assigned) but slot1 has 240 assigned
```

**含义：** Primary slot 有 239 个可用扇区（分配了 240 个），但 secondary slot 分配了 240 个扇区。这不是致命错误，但可能导致交换操作不够优化。

## 可能的原因

### 1. GPIO 引脚配置错误

**可能原因：**
- 设备树中配置的 GPIO 引脚超出了硬件支持的范围
- GPIO 引脚被保留（reserved）但应用程序仍尝试使用
- GPIO 设备树配置与硬件不匹配

**检查点：**
- 查看 `nrf52840dk_nrf52840.overlay` 中的 GPIO 配置
- 检查设备树中的 `gpio-reserved-ranges`
- 确认应用程序使用的 GPIO 引脚是否在保留范围内

### 2. 设备树配置问题

**nRF52840DK 的 GPIO 保留范围：**
```dts
gpio-reserved-ranges = <0 2>, <6 1>, <8 3>, <17 7>;
```

这意味着以下引脚被保留：
- 引脚 0-1（XL1, XL2）
- 引脚 6（TXD）
- 引脚 8-10（RXD, NFC1, NFC2）
- 引脚 17-23（QSPI 相关引脚）

**如果应用程序尝试使用这些保留的引脚，会导致断言失败。**

### 3. PWM 配置问题

从 overlay 文件可以看到，应用程序配置了 PWM LED：
```dts
pwm_led1: pwm_led_1 {
    pwms = <&pwm0 1 PWM_MSEC(20) PWM_POLARITY_INVERTED>;
};
```

PWM 通道 1 使用 GPIO 引脚 14（LED2）。如果该引脚配置有问题，可能导致 GPIO 断言失败。

### 4. 应用程序代码问题

应用程序可能在初始化时尝试配置一个无效的 GPIO 引脚。

## 排查步骤

### 1. 检查设备树配置

```bash
# 查看生成的设备树
cat build/light_bulb_mote/zephyr/zephyr.dts | grep -A 20 "gpio0"
```

### 2. 检查 GPIO 保留范围

确认应用程序使用的 GPIO 引脚不在保留范围内。

### 3. 检查应用程序代码

查找应用程序中所有 `gpio_pin_configure` 调用，确认使用的引脚号。

### 4. 启用更详细的日志

在 `prj.conf` 中启用 GPIO 驱动日志：
```conf
CONFIG_GPIO_LOG_LEVEL_DBG=y
```

### 5. 使用 GDB 调试

使用 GDB 连接到设备，在断言失败时检查：
- 哪个 GPIO 引脚被配置
- GPIO 设备的 `port_pin_mask` 值
- 调用栈信息

## 解决方案

### 方案 1：检查并修复 GPIO 配置

1. **检查设备树 overlay：**
   - 确认所有 GPIO 引脚配置正确
   - 确保没有使用保留的 GPIO 引脚

2. **检查应用程序代码：**
   - 查找所有 GPIO 配置调用
   - 确认引脚号在有效范围内

### 方案 2：调整 GPIO 保留范围

如果确实需要使用某些保留的引脚，可以修改设备树中的 `gpio-reserved-ranges`。

### 方案 3：检查 PWM 配置

如果问题与 PWM LED 相关，检查：
- PWM 通道配置
- GPIO 引脚映射
- 引脚复用配置

## 相关文件

- **设备树 Overlay：** `boards/nrf52840dk_nrf52840.overlay`
- **应用程序配置：** `prj.conf`
- **生成的设备树：** `build/light_bulb_mote/zephyr/zephyr.dts`
- **GPIO 驱动：** `/opt/nordic/ncs/v3.1.1/zephyr/include/zephyr/drivers/gpio.h`

## 参考信息

### GPIO 断言失败位置

```c
// zephyr/include/zephyr/drivers/gpio.h:1020
__ASSERT((cfg->port_pin_mask & (gpio_port_pins_t)BIT(pin)) != 0U,
         "Unsupported pin");
```

### nRF52840 GPIO 保留范围

- 引脚 0-1：XL1, XL2（32.768 kHz 晶振）
- 引脚 6：TXD（UART）
- 引脚 8-10：RXD, NFC1, NFC2
- 引脚 17-23：QSPI 相关引脚

### 错误代码

- `K_ERR_KERNEL_PANIC (4)`：内核 panic，表示不可恢复的错误

## 下一步行动

1. ✅ 检查设备树配置中的 GPIO 引脚使用
2. ✅ 检查应用程序代码中的 GPIO 配置
3. ✅ 使用 GDB 调试确定具体的失败引脚
4. ✅ 修复 GPIO 配置或调整设备树

## 总结

**问题根源：** 应用程序尝试配置一个不支持的 GPIO 引脚，导致断言失败。

**关键信息：**
- MCUboot 启动和镜像验证正常
- 应用程序开始启动但 GPIO 配置失败
- 断言位置：`gpio.h:1020` - "Unsupported pin"
- 需要检查 GPIO 引脚配置和设备树设置

