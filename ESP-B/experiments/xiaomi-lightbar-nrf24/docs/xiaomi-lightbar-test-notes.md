# Xiaomi MJGJD01YL Lightbar Test Notes

## Confirmed Hardware

- Light bar model: `MJGJD01YL`
- Control protocol: proprietary 2.4 GHz protocol compatible with nRF24L01+
- Tested controller: ESP32-S3
- Radio: nRF24L01+ PA/LNA module
- Working radio power: `3.3V` directly to nRF24 module

Do not power a bare nRF24 module from 5V.

## Captured Remote

Original Xiaomi remote ID:

```text
0xF6F611
```

## Captured Commands

```text
brightness up:    command=0x04 option=0x01 / 0x02
brightness down:  command=0x05 option=0xFF / 0xFE
cooler:           command=0x02 option=0x01 / 0x02
warmer:           command=0x03 option=0xFF / 0xFE
on/off press:     command=0x01 option=0x03
reset/default:    command=0x06
```

## Confirmed Prototype Wiring

This wiring was tested and the radio was detected:

```text
nRF24 VCC  -> ESP32 3V3
nRF24 GND  -> ESP32 GND
nRF24 CE   -> GPIO4
nRF24 CSN  -> GPIO10
nRF24 SCK  -> GPIO12
nRF24 MOSI -> GPIO11
nRF24 MISO -> GPIO13
nRF24 IRQ  -> not connected
```

## Side-B Production Xiaomi Mapping

The firmware currently defaults to this Side-B production mapping:

```text
nRF24 CE   -> GPIO1
nRF24 CSN  -> GPIO11
nRF24 SCK  -> GPIO5
nRF24 MOSI -> GPIO10
nRF24 MISO -> GPIO12
nRF24 IRQ  -> GPIO39
```

## GUI State Notes

The light bar does not report state back. GUI values should be treated as commanded or estimated state.

For absolute brightness presets, use the known-level method:

```text
1. Send dimmer with a large negative step to force minimum.
2. Send brighter with 0..15 steps to reach the target percentage.
```

Approximate mapping:

```text
0%   -> step 0
50%  -> step 8
100% -> step 15
```

The same approach can be used for color temperature by forcing coolest or warmest first, then stepping toward target.
