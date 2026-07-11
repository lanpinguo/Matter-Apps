# Matter-Apps

基于 [nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/index.html) 的应用集合，目标板以 **nRF54L15 DK** 为主，涵盖 Matter 节点、BLE 工具，以及航模遥控链路（Xbox → Hub → ESB → PWM）。

SDK 版本：NCS **v3.1.1**（与本机 `/opt/nordic/ncs/v3.1.1` 对齐）。

## 航模遥控链路（当前主线）

三块 nRF54L15 DK + Xbox Series 手柄 + 可选手机 App（[ground-hub](https://github.com/lanpinguo)）：

```text
Xbox (BLE) ──► xbox_central (Hub) ──UART──► esb_ptx ──ESB 2.4GHz──► esb_prx ──PWM──► 舵机/电调
                      │
                      └── BLE ──► 手机遥测 App
```

| 工程 | 路径 | 角色 |
|------|------|------|
| Ground BLE Hub | [`bluetooth/xbox_central`](bluetooth/xbox_central/) | Xbox Central + 手机 Peripheral；UART 下发 CTRL |
| ESB PTX | [`esb/esb_ptx`](esb/esb_ptx/) | 接收 Hub UART，转发 ESB 控制帧；OTA 配对广播 |
| ESB PRX | [`esb/esb_prx`](esb/esb_prx/) | 接收 ESB；5 路 RC PWM；状态经 ACK 回传 |
| 公共协议 | [`esb/common`](esb/common/) | HDLC UART RC、ESB 帧、radio settings |

### 控制通道与 PWM

Hub 下发 6 路：`LX, LY, RX, RY, LT, RT`。

PRX PWM（仅 **Port P1**，50 Hz，脉宽 1000–2000 µs）：

| PWM | 管脚 | 来源 | 说明 |
|-----|------|------|------|
| CH0 | P1.11 | LX | 左摇杆 X（0..1000） |
| CH1 | P1.12 | LY | 左摇杆 Y |
| CH2 | P1.06 | RX | 右摇杆 X |
| CH3 | P1.07 | RY | 右摇杆 Y |
| CH4 | P1.10 | **RT** | **油门**；Hub 发原始 0..1023，PRX 侧归一化 |

失联 500 ms：摇杆回中、油门拉低。Hub 在 Xbox 已连接时每 100 ms 发 UART CTRL 心跳。

### Hub ↔ PTX 接线（115200，仅 TX/RX/GND）

```text
Hub uart30 TX (P0.00)  --->  PTX uart20 RX (P1.05)
Hub uart30 RX (P0.01)  <---  PTX uart20 TX (P1.04)
GND                    <-->  GND
```

### OTA 配对（简要）

1. PRX 进入 pair mode（无保存配置，或长按 PRX **Btn4** 5 s）
2. Hub UART 接 PTX，长按 Hub **Btn4** 1.5 s → PTX 广播 ESB `PAIR`
3. PRX 收包保存地址；PTX 收到 ACK 后进入 CTRL 转发

详情见各子目录 README。

### 编译与烧录

```bash
# Hub
cd apps/bluetooth/xbox_central
west build -b nrf54l15dk/nrf54l15/cpuapp -p && west flash

# PTX
cd apps/esb/esb_ptx
west build -b nrf54l15dk/nrf54l15/cpuapp -p && west flash

# PRX
cd apps/esb/esb_prx
west build -b nrf54l15dk/nrf54l15/cpuapp -p && west flash
```

## 其他应用（摘要）

| 目录 | 说明 |
|------|------|
| `bluetooth/peripheral_status` | BLE Peripheral 状态示例 |
| `light_bulb_mote` / `light_switch` / `dimmer-*` | Matter 灯具 / 开关 |
| `lock` / `lock_mote` | Matter 门锁 |
| `smart_plugs` / `smart_plugs_mote` / `plug_mote` | Matter 插座 |
| `window_covering` | Matter 窗帘 |
| `coprocessor` | OpenThread RCP 等协处理器相关 |
| `cli` / `cli_disp` | CLI / 带显示调试 |
| `coap_client` / `coap_server` | CoAP 示例 |
| `hello-world` / `example` / `with_mcuboot` / `zephyr_mcuboot` | 入门与 MCUboot |
| `radio_test` / `shell_factory_verify` / `openocd` | 射频 / 产测 / 调试辅助 |
| `docs` | 补充文档 |

各子工程以自身 `README` / `README.rst` 为准。

## 仓库说明

- GitHub：本仓库为应用源码树，通常作为 NCS workspace 下的 `apps/` 使用。
- 未跟踪的本地实验目录请勿误提交；推送前确认 `git status`。
