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

### Telemetry payload (notify/read)

Packed struct:

`version(1), seq(1), roll(i16), pitch(i16), yaw(i16), lt(u16), rt(u16), buttons(u16), dpad(u8), flags(u8)`

### Config payload (read)

`version(1), telemetry_interval_ms(u16)`

### Config write command

`param_id(1), value_le16(2)`

- `param_id = 1`: set telemetry interval in milliseconds (`20..500`)

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
  - channel_count is 6: `LX, LY, RX, RY, LT, RT` (0..1000)
  - While Xbox is connected, Hub also sends CTRL every **100 ms** (heartbeat) so
    idle sticks still keep the ESB / PWM link alive; HID reports still push
    immediate CTRL for low latency.
- `type=0x02` STATUS (ESB -> Hub): `seq(1), roll(i16), pitch(i16), yaw(i16), batt(u16), flags(1)`
- `type=0x03/0x04` ESB_REQ/RSP: radio config, pair, apply, save
- `type=0x05/0x06` DEBUG_CTRL/LOG: Btn3 long press toggles log forwarding from ESB PTX

Buttons:

- **Btn4** hold 1.5 s: ``PAIR`` on **esb_ptx** — generate/save addresses and broadcast OTA PAIR
  until **esb_prx** ACKs (max 30 s; PRX must be in pair mode)
- **Btn3** short press: optional UART sync of cached addresses to **esb_prx** (rewire Hub UART)
- **Btn3** hold 1.5 s: toggle ESB debug log forwarding to Hub console

OTA pair checklist: PRX in pair mode → Hub UART to PTX → hold Btn4 1.5s → wait for PRX
``Paired from first valid pair frame``.

## Report format

The app uses a minimal HIDS client (`xbox_hids.c`) instead of Nordic `bt_hogp`,
because Xbox controllers do not expose the mandatory HIDS Control Point
characteristic.

## Next step

Feed parsed channel values into the ESB PTX app (`apps/esb/esb_ptx`) over UART.
Do not run BLE and ESB TX on the same chip at the same time.
