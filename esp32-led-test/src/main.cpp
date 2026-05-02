#include <Arduino.h>
#include <FastLED.h>

namespace {

constexpr uint8_t LED_PIN = 41;
constexpr uint8_t LED_COUNT = 10;
constexpr uint8_t BRIGHTNESS = 48;
constexpr uint8_t UART_RX_PIN = 45;
constexpr uint8_t UART_TX_PIN = 0;
constexpr uint32_t UART_BAUD = 115200;

constexpr uint8_t BATTERY_LED = 0;
constexpr uint8_t PER_KEY_START = 1;
constexpr uint8_t PER_KEY_COUNT = 5;
constexpr uint8_t INDICATOR_START = 6;
constexpr uint8_t INDICATOR_COUNT = 2;
constexpr uint8_t UNDERGLOW_START = 8;
constexpr uint8_t UNDERGLOW_COUNT = 2;

CRGB leds[LED_COUNT];
HardwareSerial NrfSerial(1);
String rxLine;
uint32_t keyLedReleaseAt[PER_KEY_COUNT];

void fill_segment(uint8_t start, uint8_t count, const CRGB &color) {
    for (uint8_t i = 0; i < count; i++) {
        leds[start + i] = color;
    }
}

void render_test_pattern() {
    fill_solid(leds, LED_COUNT, CRGB::Black);

    leds[BATTERY_LED] = CRGB::Green;
    fill_segment(PER_KEY_START, PER_KEY_COUNT, CRGB::Blue);
    fill_segment(INDICATOR_START, INDICATOR_COUNT, CRGB::Orange);
    fill_segment(UNDERGLOW_START, UNDERGLOW_COUNT, CRGB::Purple);

    FastLED.show();
}

void handle_key_event(uint8_t position, bool pressed) {
    uint8_t led = PER_KEY_START + (position % PER_KEY_COUNT);

    if (pressed) {
        leds[led] = CRGB::White;
        keyLedReleaseAt[position % PER_KEY_COUNT] = millis() + 180;
    } else {
        leds[led] = CRGB::Blue;
        keyLedReleaseAt[position % PER_KEY_COUNT] = 0;
    }

    FastLED.show();
}

void handle_uart_line(const String &line) {
    unsigned int position = 0;
    unsigned int pressed = 0;

    if (sscanf(line.c_str(), "K %u %u", &position, &pressed) == 2) {
        handle_key_event(position, pressed != 0);
        Serial.printf("nRF key position %u %s\n", position, pressed ? "pressed" : "released");
    }
}

void poll_nrf_uart() {
    while (NrfSerial.available()) {
        char c = static_cast<char>(NrfSerial.read());

        if (c == '\n') {
            handle_uart_line(rxLine);
            rxLine = "";
        } else if (c != '\r' && rxLine.length() < 32) {
            rxLine += c;
        }
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    NrfSerial.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    delay(500);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(BRIGHTNESS);
    render_test_pattern();

    Serial.println("ZMK-BLE ESP32 LED test");
    Serial.println("LED 0: battery");
    Serial.println("LEDs 1-5: temporary per-key RGB");
    Serial.println("LEDs 6-7: indicators");
    Serial.println("LEDs 8-9: underglow");
    Serial.println("UART from nRF: RX GPIO45, TX GPIO0, 115200 baud");
}

void loop() {
    static uint8_t hue = 0;

    poll_nrf_uart();

    leds[BATTERY_LED] = CHSV(96, 255, BRIGHTNESS);

    for (uint8_t i = 0; i < PER_KEY_COUNT; i++) {
        if (keyLedReleaseAt[i] != 0 && millis() < keyLedReleaseAt[i]) {
            continue;
        }

        keyLedReleaseAt[i] = 0;
        leds[PER_KEY_START + i] = CHSV(hue + (i * 24), 255, BRIGHTNESS);
    }

    leds[INDICATOR_START] = CRGB::Red;
    leds[INDICATOR_START + 1] = CRGB::Cyan;

    for (uint8_t i = 0; i < UNDERGLOW_COUNT; i++) {
        leds[UNDERGLOW_START + i] = CHSV(hue + 128 + (i * 32), 200, BRIGHTNESS);
    }

    FastLED.show();
    hue++;
    delay(30);
}
