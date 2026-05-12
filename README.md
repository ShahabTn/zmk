# ZMK ESP Split Macropad

This repository contains the firmware and project notes for a custom split macropad built around two controllers on each side:

- nRF52840 for ZMK, key scanning, BLE HID, and split keyboard communication
- ESP32-S3 for LEDs, sensors, Wi-Fi/web GUI, display work, haptics, buzzer, and higher-level device features

The long-term goal is a programmable split macropad with a browser-based configuration interface, similar in spirit to QMK/VIA or ZMK Studio, but designed around this hardware. The GUI should eventually allow live editing of key labels, LED colors, layer behavior, smart-home actions, trading shortcuts, and other device-specific settings without reflashing for every change.

## Project Shape

The macropad has two halves:

- Side-A: the main key side. It has the key matrix, per-key RGB, indicator/underglow LEDs, battery measurement, buzzer, haptic driver, sensors, ESP32-S3, and nRF52840.
- Side-B: the display/sensor side. It has no switches. It has a TFT LCD, 16 WS2812 LEDs, ESP32-S3, nRF52840, SHT40, INA219, and nRF24L01+ hardware.

ZMK runs on the nRF52840 modules. The ESP32-S3 modules run separate PlatformIO/Arduino firmware. The two chips on each side communicate over UART and/or I2C depending on the feature.

## Repository Layout

- `boards/shields/zmk/`: ZMK shield overlays and keymaps for the split macropad.
- `ESP-A/`: ESP32-S3 firmware for Side-A.
- `ESP-B/`: ESP32-S3 firmware for Side-B.
- `config/`: ZMK config files used by GitHub Actions builds.
- `src/`: custom ZMK-side helper code for nRF/ESP communication experiments.
- `build.yaml`: GitHub Actions build matrix for the ZMK firmware.
- `PROJECT_STATE.md`: detailed recovery notes, pin maps, current status, and build commands.

## Current Status

Working/proven:

- Side-A nRF52840 matrix scanning.
- Side-A BLE HID typing through ZMK.
- Side-A ESP32-S3 LED control.
- UART communication path between Side-A nRF52840 and ESP32-S3.
- Side-B ESP32-S3 firmware scaffold.
- Side-B ZMK shield/keymap scaffold with no switches.

In progress:

- Browser GUI for configuration.
- Live remapping from GUI through ESP32-S3 to nRF52840.
- Final Side-B display, sensor, and nRF24L01+ behavior.

Important note: the Side-A GUI/remap code is intentionally semi-working for now. LED configuration tests worked, but true live key remapping still needs a cleaner implementation. The hardware path is not considered the blocker.

## Firmware Responsibilities

### nRF52840 / ZMK

- Key matrix scanning on Side-A
- BLE HID keyboard output
- Split communication between Side-A and Side-B
- Battery reporting
- Receiving simple commands from ESP32-S3 where needed

### ESP32-S3

- WS2812/SK6812 LED effects
- Local web GUI over Wi-Fi
- WebSocket configuration channel
- Sensor access
- Haptic and buzzer control
- TFT display work on Side-B
- Future smart-home/trading/network features

## Build Notes

ZMK firmware is built by GitHub Actions using `build.yaml`.

ESP-A firmware:

```sh
pio run -d ESP-A -e esp32-s3-devkitc-1
```

ESP-B firmware:

```sh
pio run -d ESP-B -e esp32-s3-devkitc-1
```

On the original development machine, PlatformIO was installed at:

```sh
/Users/shahabtanhaei/.platformio/penv/bin/pio
```

On a new laptop, installing VS Code and PlatformIO again is enough; the repository contains the source files.

## Recovery

If this project is moved to a new laptop, clone the repository from GitHub and start with `PROJECT_STATE.md`. That file records the current hardware pin maps and the most important design decisions so the project can continue without relying on local files from the old laptop.
