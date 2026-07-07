# Xbox BLE Central

BLE Central sample for verifying Xbox Series X|S controller (model 1914)
connection and control data on nRF54L15 DK.

## What it does

1. Scans for devices advertising the HID service (0x1812).
2. Connects automatically and pairs (numeric comparison on DK buttons).
3. Discovers HIDS, subscribes to input reports.
4. Parses Xbox gamepad reports and prints sticks, triggers, D-pad, and buttons
   over UART at 10 Hz.

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
3. When the DK shows a passkey, confirm on the controller and press **Button 1**
   on the DK to accept (Button 2 to reject).
4. LED1 turns on when connected. Move sticks and press buttons to see parsed
   values on the serial console.

## Report format

The app uses a minimal HIDS client (`xbox_hids.c`) instead of Nordic `bt_hogp`,
because Xbox controllers do not expose the mandatory HIDS Control Point
characteristic.

## Next step

Feed parsed channel values into the ESB PTX app (`apps/esb/esb_ptx`) over UART
or another link. Do not run BLE Central and ESB TX on the same chip at the
same time.
