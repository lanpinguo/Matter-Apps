# 便携热成像仪（MLX90640 + nRF54L15 + Flutter）

MLX90640ESF-BAB（32×24）红外阵列 + Nordic nRF54L15 DK，经 BLE 把热成像帧传到手机显示。

| 组件 | 路径 |
|------|------|
| 固件 | `matter/nrf/apps/bluetooth/peripheral_thermal` |
| 手机 App | `ble_thermal_viewer` |

## 硬件接线

| MLX90640 | nRF54L15 DK |
|----------|-------------|
| VIN | 3V3 |
| GND | GND |
| SCL | **P1.12** |
| SDA | **P1.13** |

- I2C 地址：`0x33`（默认）
- 电平：3.3 V；BAB / BAA 寄存器兼容，同一套固件

## 固件编译烧录

```bash
cd apps/bluetooth/peripheral_thermal
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -d build --pristine
west flash -d build
```

串口应看到类似：

```
MLX90640: ready @ 0x33 (2 Hz, chess, 18-bit)
Advertising started as "ThermalCam"
Fixed passkey: 123456
```

配对 PIN：`123456`

## 手机 App

```bash
cd ble_thermal_viewer
flutter pub get
flutter run
```

扫描并连接名为 **ThermalCam** 的设备，即可看到伪彩热图、环境/最低/最高温度，并可切换刷新率。

## BLE 协议摘要

| 项 | UUID |
|----|------|
| Service | `5448524D-0001-1000-8000-00805F9B34FB` |
| Frame Notify | `5448524D-0002-1000-8000-00805F9B34FB` |
| Control Write | `5448524D-0003-1000-8000-00805F9B34FB` |

**帧分片**（每包）：

```
magic u32 BE='THRM' | ver u8 | flags u8 | seq u16 LE
chunk u8 | chunkCnt u8 | w=32 | h=24
Ta/Tmin/Tmax 各 i16 LE（单位 0.01°C）
pixels[] i16 LE（本分片像素，0.01°C）
```

完整一帧 = 768 像素，按 ATT MTU 分成多包 Notify。  
Control 写入 1 字节：Melexis 刷新率编码 `0=0.5Hz … 4=8Hz`（默认 `2` = 2 Hz）。

## 说明

- 温度由片上 Melexis 官方算法换算后下发，手机只做伪彩渲染
- 默认 2 Hz；提高刷新率会明显增加 I2C / BLE 负载
- 若 I2C 引脚与你的板子不同，改 `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` 中的 `pinctrl`
