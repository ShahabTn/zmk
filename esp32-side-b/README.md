# ESP32 Side-B Firmware

Initial Side-B bring-up firmware for the display half of the split macropad.

## Hardware Covered

- ESP32-S3 N16R8
- nRF52840 UART link
- TFT LCD SPI pin definitions and backlight control
- nRF24L01+ SPI pin definitions
- INA219/SHT40 I2C bus scan
- 16 WS2812-2121 LEDs on ESP GPIO2

## Pin Summary

### nRF Link

- ESP RX `GPIO47` receives from nRF TX `P0.22`
- ESP TX `GPIO21` sends to nRF RX `P0.24`
- ESP reset control for nRF: `GPIO6`

### nRF Shared I2C Link

- ESP `GPIO20` = SDA to nRF `P1.00`
- ESP `GPIO19` = SCL to nRF `P0.11`

### TFT LCD

- CS `GPIO46`
- DC `GPIO03`
- RST `GPIO08`
- MOSI/SDA `GPIO18`
- SCK/SCL `GPIO17`
- Backlight NMOS gate `GPIO14`

### nRF24L01+

- SCK `GPIO05`
- IRQ `GPIO41`
- MISO `GPIO42`
- CE `GPIO01`
- CSN `GPIO44`
- MOSI `GPIO43`

### Sensors

- INA219/SHT40 I2C SCL `GPIO38`
- INA219/SHT40 I2C SDA `GPIO40`

### Side-B LEDs

- 16x WS2812-2121
- DIN `GPIO02`
