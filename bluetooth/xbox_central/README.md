# Ground BLE Hub

Single-chip BLE hub on nRF54L15:

- BLE Central to Xbox Series X|S controller (model 1914)
- BLE Peripheral to phone for telemetry and runtime configuration

## What it does

1. Scans for devices advertising the HID service (0x1812).
2. Connects automatically and pairs (numeric comparison on DK buttons).
3. Discovers HIDS, subscribes to input reports.
4. Parses Xbox gamepad reports and prints sticks, triggers, D-pad, and buttons.
5. Advertises a custom GATT service for phone apps:
   - Telemetry characteristic (read + notify)
   - Config characteristic (read + write)
6. Sends control channels to ESB module over UART and accepts status feedback.

## Hardware

- Board: `nrf54l15dk/nrf54l15/cpuapp`
- Xbox Wireless Controller in **Bluetooth mode** (hold Sync to pair)
- Status LED on **P2.07**（`status-led`，**低电平点亮**）：慢闪=运行中，快闪=Xbox 已连接；板载 LED1 仍表示 Xbox 连接

## Build and flash

```bash
cd apps/bluetooth/xbox_central
west build -b nrf54l15dk/nrf54l15/cpuapp -p
west flash
```

Open a serial terminal at 115200 baud.

## Pairing

1. Flash the DK and reset.
2. Put the Xbox controller in pairing mode (hold the Sync button until the Xbox
   button blinks rapidly).
3. The hub auto-confirms the BLE passkey; confirm on the controller if prompted.
   Button 1 / Button 2 can still reject an in-progress manual pairing request.
4. LED1 turns on when Xbox is connected. Move sticks and press buttons to see
   parsed values on the serial console.

## Phone GATT service

- Service UUID: `57a71000-9350-11ed-a1eb-0242ac120002`
- Telemetry Char UUID: `57a71001-9350-11ed-a1eb-0242ac120002`
- Config Char UUID: `57a71002-9350-11ed-a1eb-0242ac120002`
- Power Char UUID: `57a71003-9350-11ed-a1eb-0242ac120002`
  - VBUS 在位/充电中约 **1 Hz** notify；空闲约 **30 s**；INT 仍立即采样

- LogCtrl Char UUID: `57a71004-9350-11ed-a1eb-0242ac120002` (write)
- LogData Char UUID: `57a71005-9350-11ed-a1eb-0242ac120002` (notify)

### Telemetry payload (notify/read)

Packed struct:

`version(1), seq(1), roll(i16), pitch(i16), yaw(i16), lt(u16), rt(u16), buttons(u16), dpad(u8), flags(u8)`

### Config payload (read)

`version(1), telemetry_interval_ms(u16)`

### Config write command

`param_id(1), value_le16(2)`

- `param_id = 1`: set telemetry interval in milliseconds (`20..500`)

### Flash log over BLE

1. Subscribe LogData notify.
2. Write LogCtrl START (LE):

`op=0x01, flags, limit_u16, from_u16, mod_mask_u32, level_max_u8, kw_len, kw[]`

- `flags bit0`: take_tail（取最近 N 条）
- `level_max=0xFF`: 全部级别；否则仅 `level <= level_max`
- `mod_mask=0`: 固件按全部模块处理

3. 设备分片 notify：
   - `BEGIN 0x01 | total_u16 | reserved_u32`
   - `REC 0x02 | idx_u16 | boot_id_u32 | uptime_ms_u32 | mod | level | text_len | text[]`
   - `END 0x03 | status`（0=ok，1=abort，2=error）
4. 可写 `op=0x02` STOP 中止。

单次导出匹配上限 256 条（与 `flog show` 相同）。
## UART link to ESB module (HUART-RC)

Link layer uses **HDLC** framing (same style as OpenThread Spinel RCP):

`0x7E | escaped( type | len | payload | fcs16_le ) | 0x7E`

- Flag: `0x7E`; escape: `0x7D` + `(byte ^ 0x20)` for `0x7E`/`0x7D`
- FCS-16: PPP/HDLC CRC over `type | len | payload`, XOR `0xFFFF`, little-endian on wire
- Full definition: `apps/esb/common/uart_rc_link.h`

Wiring (nRF54L15 DK, 115200 baud, **TX/RX/GND only — no RTS/CTS**):

```text
Hub uart30 TX (P0.00)  --->  ESB PTX uart20 RX (P1.05)
Hub uart30 RX (P0.01)  <---  ESB PTX uart20 TX (P1.04)
GND                    <-->  GND
```

Hardware flow control is not used; HDLC framing handles reliability at 115200.

Message types (application payload inside HDLC):

- `type=0x01` CTRL (Hub -> ESB): `seq(1), channel_count(1), channels[]` (LE u16)
  - channel_count is 9: `LX, LY, RX, RY, LT, RT, AUX0(A), AUX1(B), AUX2(drive)`
  - sticks/AUX0–1 are 0..1000; triggers are raw 10-bit 0..1023 (normalized on esb_prx)
  - **AUX2 / CH8**: combined drive for single-channel FWD/REV cars —
    RT maps to upper half (500..1000), LT to lower half (500..0), idle=500
  - **RT** also remains on CH4; **LT** on CH5
  - While Xbox is connected, Hub also sends CTRL every **100 ms** (heartbeat) so
    idle sticks still keep the ESB / PWM link alive; HID reports still push
    immediate CTRL for low latency.
- `type=0x02` STATUS (ESB -> Hub): `seq(1), roll(i16), pitch(i16), yaw(i16), batt(u16), flags(1)`
- `type=0x03/0x04` ESB_REQ/RSP: radio config, pair, apply (SAVE is PRX-only / PTX no-op)
- `type=0x05/0x06` DEBUG_CTRL/LOG: Btn3 long press toggles log forwarding from ESB PTX

**ESB pairing persistence (Hub-owned):**

- Successful OTA pair addresses are saved on **xbox_central** (`xbox_hub/esb_radio`)
- **esb_ptx** does **not** store pair config; Hub pushes `SET_RADIO`/`SET_ADDR`/`APPLY` at boot
- **esb_prx** still persists OTA pair locally (`esb_prx/radio`) for standalone rejoin

Buttons:

- **Btn1 (P1.02)** hold 1.5 s: ``PAIR`` on **esb_ptx** — generate addresses, OTA broadcast until **esb_prx** ACKs (max 30 s); Hub saves config on success
- **Btn3** short press: re-push Hub-saved ESB config to the UART device (SET_RADIO/SET_ADDR/APPLY)
- **Btn3** hold 1.5 s: toggle ESB debug log forwarding to Hub console

OTA pair checklist: PRX in pair mode → Hub UART to PTX → hold Btn1 (P1.02) 1.5s → wait for PRX
``Paired from first valid pair frame``. After reboot, Hub restores PTX automatically.

## ESB / PTX shell debug (console uart20 @ 115200)

```text
hub> esb status      # Hub cfg / pair / **PTX present ping** / last STATUS
hub> esb ping        # 主动检测 esb_ptx 是否在 UART 上应答（GET_CONFIG，300 ms）
hub> esb cfg         # dump Hub-saved addresses
hub> esb get         # GET_CONFIG from PTX (waits ~800 ms)
hub> esb push        # SET_RADIO/SET_ADDR/APPLY Hub cfg → PTX
hub> esb pair        # force OTA PAIR (same as Btn1 hold 1.5 s)
hub> esb log on      # forward PTX logs to Hub console
hub> esb clear       # delete Hub flash xbox_hub/esb_radio
```

Pair tip: PRX in pair mode → `esb pair` (or Btn1 hold) → wait for ACK → Hub saves.
After reboot Hub auto-pushes; `esb push` re-applies manually. Use `flog mirror on`
if you want HUB_* lines on UART while pairing.

## BQ25895 shell debug (console uart20 @ 115200)

STAT 约 1 Hz 闪烁通常表示故障锁存。烧录后：

1. **按 DK Button1** 可随时 dump 寄存器
2. 交互 Shell（轮询模式）：回车出现 `hub>` 后输入：

```text
hub> bq dump
hub> bq status
hub> bq read 0x0c
```

优先看 **REG0C**（NTC/输入/看门狗）和 **REG0B**（VBUS/充电状态）。

### 电池充电参数（LiPo）

当前默认 **200 mAh** 单节锂聚合物，约 **0.5C** 快充（BQ25895 按 64 mA 步进圆整后约为 **ICHG=128 mA**，ITERM/IPRECHG=64 mA 芯片下限）。

换更大容量电池时，在 `prj.conf` 改一项即可（或 `menuconfig`）：

```text
CONFIG_BQ25895_BATT_CAPACITY_MAH=1000   # 例：1000 mAh → ICHG≈512 mA
# CONFIG_BQ25895_CHARGE_RATE_MILLIC=500  # 可选，默认 0.5C
```

烧录后用 `bq status` / 启动日志确认 `ICHG=` 是否符合预期。

## 应用日志（外部 Flash，按模块）

模块：`sys` `xbox` `hid` `phone` `uart` `esb` `bq` `input`  
默认写入 MX25R64 前 **1 MiB** **统一公共环形区**（4KB 扇区擦除；末页为 scratch）。  
回收时丢弃**同级别**条目，其它级别经 scratch **压缩写回**页首（近似按条目回收）。  
记录带 `flags` 软删；dump 跳过已删条目。GC 窗口断电可能丢该页上本应保留的条目。  
每条记录时间戳：`boot_id` + 启动后相对时间，显示为 `b3+0:01:23.456`。  
各模块默认 `inf`（`input` 默认 `wrn`）。

```text
hub> loglevel
hub> loglevel xbox dbg
hub> flog status
hub> flog show                      # 最近 50 行（带 #行号）
hub> flog show 100
hub> flog show from 200             # 从匹配行 #200 起看 50 行
hub> flog show from 200 20          # 从 #200 起看 20 行
hub> flog show /0x3e 50             # 关键词（不区分大小写）
hub> flog show kw:fail !bq
hub> flog find disconnect           # 等价于带关键词的查找
hub> flog find Security 20 xbox
hub> flog show 50 !bq !input
hub> flog clear
```

行号 `#idx` 是**过滤后**从最旧到最新的序号，可用 `from <idx>` 接着往下看。
`/keyword` 与 `kw:keyword`、`flog find` 均为正文子串匹配（忽略大小写）。

`flog show` 的等级过滤为 **≤ 指定等级**（`err` ⊂ `wrn` ⊂ `inf` ⊂ `dbg`）。  
升级后旧 Flash 日志格式会自动清空重建（当前 meta v5：4KB + 软删/GC）。

DK Flash：SCK P2.01、MOSI P2.02、MISO P2.04、CS P2.05。

## 串口

在 **CP2104 `/dev/ttyUSB0`**（或 DK VCOM）上打开**双向**终端：

```bash
screen -x
# 或
screen /dev/ttyUSB0 115200
```

接线：MCU `uart20` **P1.04 TX → CP2104 RX**，**P1.05 RX ← CP2104 TX**，共地。不要接 RTS/CTS。

Button1 / `bq dump` 不受过滤。

## Report format

The app uses a minimal HIDS client (`xbox_hids.c`) instead of Nordic `bt_hogp`,
because Xbox controllers do not expose the mandatory HIDS Control Point
characteristic.

## Next step

Feed parsed channel values into the ESB PTX app (`apps/esb/esb_ptx`) over UART.
Do not run BLE and ESB TX on the same chip at the same time.
