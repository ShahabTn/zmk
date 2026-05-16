# Side-B Review Notes

## Completed Decisions

### Step 1: Xiaomi nRF24 Pins

Initial mapping was changed to avoid UART0 pins.

Approved current mapping:

```text
nRF24 CE   -> GPIO1
nRF24 CSN  -> GPIO11
nRF24 SCK  -> GPIO5
nRF24 MOSI -> GPIO10
nRF24 MISO -> GPIO12
nRF24 IRQ  -> GPIO39
```

### Step 2: LCD Control Pins

`LCD CS` was moved away from `GPIO46`.

`LCD DC` was moved away from `GPIO3`.

Approved current LCD control mapping:

```text
LCD SCL -> GPIO17
LCD SDA -> GPIO18
LCD RES -> GPIO8
LCD DC  -> GPIO13
LCD CS  -> GPIO9
LCD BL  -> GPIO14 via MOSFET gate
```

Avoid these ESP32-S3 strapping pins for normal peripheral control where possible:

```text
GPIO0
GPIO3
GPIO45
GPIO46
```

## Remaining Review Items

Continue from Step 3:

1. Battery sense divider
2. MicroSD CS and pullups
3. TP4056 battery/load path
4. 3.3V regulator and backfeed behavior
5. I2C pullups and SHT40 decoupling
6. LCD backlight MOSFET orientation
7. RGB/indicator power rail and DIN level assumptions
8. ESP32-S3/nRF52840 reset and programming accessibility
