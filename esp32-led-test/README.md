# ESP32 LED Test

Temporary firmware for testing a 10 LED strip from the ESP32-S3 before the PCB arrives.

## Wiring

- LED DIN: ESP32-S3 GPIO21
- nRF P0.02 TX: ESP32-S3 GPIO13 RX
- nRF P1.15 RX: ESP32-S3 GPIO12 TX
- INA219 I2C SDA: ESP32-S3 GPIO10
- INA219 I2C SCL: ESP32-S3 GPIO9
- Shared I2C SDA: ESP32-S3 GPIO38
- Shared I2C SCL: ESP32-S3 GPIO39
- DRV2605L IN/TRIG: ESP32-S3 GPIO47
- Buzzer: ESP32-S3 GPIO4
- LED power and ground must match the strip requirements
- nRF, ESP32, and LED power grounds must be common

## Shared I2C Devices

- INA219: `0x40`
- DRV2605L: `0x5A`
- ATECC608A: `0x60`
- BH1750: `0x23`

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
