# ESP32 LED Test

Temporary firmware for testing a 10 LED strip from the ESP32-S3 before the PCB arrives.

## Wiring

- LED DIN: ESP32-S3 GPIO39
- nRF P1.15 TX: ESP32-S3 GPIO10 RX
- nRF P0.02 RX: ESP32-S3 GPIO11 TX
- LED power and ground must match the strip requirements
- nRF, ESP32, and LED power grounds must be common

## Temporary LED Allocation

- LED 0: battery indicator
- LEDs 1-5: temporary per-key RGB
- LEDs 6-7: indicators
- LEDs 8-9: underglow

## Build

This folder is a PlatformIO Arduino project:

```sh
pio run
pio run --target upload
pio device monitor
```
