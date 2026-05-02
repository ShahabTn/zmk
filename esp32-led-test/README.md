# ESP32 LED Test

Temporary firmware for testing a 10 LED strip from the ESP32-S3 before the PCB arrives.

## Wiring

- LED DIN: ESP32-S3 GPIO41
- LED power and ground must match the strip requirements
- ESP32 ground and LED power ground must be common

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
