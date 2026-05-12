# ZMK ESP Split Macropad Project State

Last updated: 2026-05-12

This file is the recovery note for moving to a new laptop. The source of truth is this GitHub repository on `main`.

## Repository Layout

- `ESP-A/`: Side-A ESP32-S3 firmware. Handles LEDs, sensors, Wi-Fi GUI, and UART link to Side-A nRF52840.
- `ESP-B/`: Side-B ESP32-S3 bring-up firmware. Handles TFT pins, 16 WS2812 LEDs, sensors, nRF24L01 pins, and UART/I2C links to Side-B nRF52840.
- `boards/shields/zmk/force_ble_left.*`: Side-A ZMK shield/keymap.
- `boards/shields/zmk/force_ble_right.*`: Side-B ZMK shield/keymap. Side-B currently has no switches.
- `build.yaml`: Builds both `force_ble_left` and `force_ble_right`.

## Current Status

- Side-A basic nRF matrix, BLE HID, and ESP LED communication were proven.
- Side-A GUI/remap is intentionally left semi-working for later. LED changes work, but live key remap still needs a cleaner architecture.
- Side-B firmware scaffold is added and compiles.
- Folder names are now final:
  - `ESP-A`
  - `ESP-B`

## Side-A Final Hardware Notes

Side-A has 28 keys, ESP32-S3, nRF52840, LEDs, buzzer, DRV2605L, INA219, BH1750, ATECC608A.

Final nRF52840 pins:

- `P0.06`: C1
- `P0.08`: R4
- `P0.17`: C3
- `P0.20`: C2
- `P0.22`: C4
- `P0.24`: C5
- `P1.00`: R0
- `P0.11`: R1
- `P1.01-LF`: R2
- `P1.02-LF`: R3
- `P0.31-LF`: C0
- `P0.02-LF`: UART link to ESP32 GPIO13
- `P1.15-LF`: UART link to ESP32 GPIO12
- `P1.11-LF`: battery measurement
- `P0.29`: I2C link to ESP32 GPIO14
- `P1.13`: I2C link to ESP32 GPIO11

Final Side-A ESP32 pins:

- `GPIO13`: UART RX from nRF
- `GPIO12`: UART TX to nRF
- `GPIO10`: I2C SDA for INA219
- `GPIO09`: I2C SCL for INA219
- `GPIO04`: buzzer
- `GPIO21`: LED DIN
- `GPIO47`: DRV2605L IN/TRIG
- `GPIO38`: shared I2C SDA for DRV2605L, BH1750, ATECC608A
- `GPIO39`: shared I2C SCL for DRV2605L, BH1750, ATECC608A

Side-A LED chain:

- 1 battery LED
- 28 per-key LEDs on final PCB
- 16 indicator LEDs
- 7 underglow LEDs

Software may limit LED brightness to protect the 3.3 V rail.

## Side-B Final Hardware Notes

Side-B has no switches. It has TFT LCD, nRF52840, ESP32-S3 N16R8, nRF24L01+, SHT40, INA219, and 16 WS2812-2121 LEDs.

Side-B nRF52840 pins:

- `RST`: connected to ESP32 GPIO6 through 1 kOhm resistor and manual reset contact
- `P0.22`: UART TX to ESP32 GPIO47
- `P0.24`: UART RX from ESP32 GPIO21
- `P1.00`: SDA to ESP32 GPIO20
- `P0.11`: SCL to ESP32 GPIO19
- `P0.31-LF`: battery monitoring

Side-B ESP32 pins:

- `GPIO47`: UART RX from nRF P0.22
- `GPIO21`: UART TX to nRF P0.24
- `GPIO20`: I2C SDA to nRF P1.00
- `GPIO19`: I2C SCL to nRF P0.11
- `GPIO6`: nRF reset through 1 kOhm resistor. Firmware should keep this high-Z/input normally and drive low only briefly for reset.
- `GPIO46`: LCD CS
- `GPIO03`: LCD DC
- `GPIO08`: LCD reset
- `GPIO18`: LCD SDA/MOSI
- `GPIO17`: LCD SCL/SCK
- `GPIO14`: LCD backlight NMOS gate
- `GPIO05`: nRF24L01 SCK
- `GPIO41`: nRF24L01 IRQ
- `GPIO42`: nRF24L01 MISO
- `GPIO01`: nRF24L01 CE
- `GPIO44`: nRF24L01 CSN
- `GPIO43`: nRF24L01 MOSI
- `GPIO02`: DIN for 16 WS2812-2121 LEDs
- `GPIO38`: I2C SCL for INA219/SHT40
- `GPIO40`: I2C SDA for INA219/SHT40

## Build Commands

ESP-A:

```sh
/Users/shahabtanhaei/.platformio/penv/bin/pio run -d ESP-A -e esp32-s3-devkitc-1
```

ESP-B:

```sh
/Users/shahabtanhaei/.platformio/penv/bin/pio run -d ESP-B -e esp32-s3-devkitc-1
```

ZMK is built by GitHub Actions from `build.yaml`.

## Important Reminder

After moving to a new laptop, clone the repository from GitHub and install PlatformIO/VS Code again. The local untracked files `.DS_Store`, `.vscode/`, and `ESP-A/.vscode/` are not required.
