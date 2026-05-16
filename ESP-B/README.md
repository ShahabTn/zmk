# ESP-B Firmware

Initial ESP32-S3 bring-up firmware for Side-B, the display half of the split macropad.

## Hardware Covered

- ESP32-S3 N16R8
- nRF52840 UART link
- TFT LCD SPI pin definitions and backlight control
- nRF24L01+ SPI pin definitions
- MicroSD local NAS over Wi-Fi AP
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

- CS `GPIO09`
- DC `GPIO13`
- RST `GPIO08`
- MOSI/SDA `GPIO18`
- SCK/SCL `GPIO17`
- Backlight NMOS gate `GPIO14`

### nRF24L01+

- SCK `GPIO05`
- IRQ `GPIO39`
- MISO `GPIO12`
- CE `GPIO01`
- CSN `GPIO11`
- MOSI `GPIO10`

### MicroSD NAS

- CS `GPIO35`
- MOSI `GPIO15`
- CLK `GPIO07`
- MISO `GPIO04`
- VCC `3.3V`
- GND `GND`

The current ESP-B bring-up firmware mounts the MicroSD card and starts a local Wi-Fi AP:

- SSID: `ZMK-SideB-NAS`
- Password: `12345678`
- URL: `http://192.168.4.1/`

The browser UI supports file listing, upload, download, folder creation, and delete.

For this NAS bring-up build, LCD and Xiaomi nRF24 pins are kept in safe idle states but their SPI devices are not initialized. This avoids SPI host conflicts while the MicroSD storage path is being validated.

### Sensors

- INA219/SHT40 I2C SCL `GPIO38`
- INA219/SHT40 I2C SDA `GPIO40`

### Side-B LEDs

- 16x WS2812-2121
- DIN `GPIO02`
