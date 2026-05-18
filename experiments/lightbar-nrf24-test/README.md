# Xiaomi MJGJD01YL Lightbar nRF24 Test

The photo confirms the target model is `MJGJD01YL`, the non-BLE Xiaomi monitor light bar. This is the variant that can be controlled with an ESP32 and an nRF24L01+ module.

## Wiring

Prototype wiring:

| nRF24L01+ | ESP32 |
| --- | --- |
| VCC | 5V or 3V3 when using the YL-105 adapter |
| GND | GND |
| CE | GPIO 4 |
| CSN | GPIO 10 |
| SCK | GPIO 12 |
| MOSI / MO | GPIO 11 |
| MISO / MI | GPIO 13 |
| IRQ | not connected |

Add a `10uF` to `47uF` capacitor directly across the nRF24L01+ `VCC` and `GND` pins. Many flaky nRF24 tests are power stability problems.

The sketch defaults to the Side-B production PCB mapping:

| nRF24L01+ | ESP32 |
| --- | --- |
| CE | GPIO 1 |
| CSN | GPIO 11 |
| SCK | GPIO 5 |
| MOSI / MO | GPIO 10 |
| MISO / MI | GPIO 12 |
| IRQ | GPIO 39, unused by the test sketch |

## PlatformIO

Open this folder in VS Code with the PlatformIO extension:

```text
lightbar-nrf24-test
```

The project uses:

- `nrf24/RF24`
- `robtillaart/CRC`

Default environment:

```ini
[env:esp32-s3]
board = esp32-s3-devkitc-1
framework = arduino
```

If your prototype ESP32-S3 board needs a more specific PlatformIO board ID, change `board` in `platformio.ini`.

## Test Flow

1. Open the `lightbar-nrf24-test` folder in VS Code.
2. Use PlatformIO: `Upload`.
3. Use PlatformIO: `Monitor`, speed `115200`.
4. Press and rotate the Xiaomi remote.
5. Look for a line like:

```text
[RX] serial=0xABCDEF command=0x04 option=0x01
```

6. Copy the printed serial into `LIGHTBAR_REMOTE_ID` in `src/main.cpp`.
7. Upload again.
8. In Serial Monitor, send:

```text
t = toggle
+ = brighter
- = dimmer
w = warmer
c = cooler
r = reset/default
p = pair/reset command
```

## Notes

- The bar does not send state back. Track state in the macropad firmware later.
- If you use the original remote ID, pairing should not be needed.
- If you use a new arbitrary ID, power-cycle the light bar and send `p` within the pairing window.
- The protocol details and ESP32 reference implementation come from:
  - https://github.com/lamperez/xiaomi-lightbar-nrf24
  - https://github.com/ebinf/lightbar2mqtt
