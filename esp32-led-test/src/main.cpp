#include <Arduino.h>
#include <FastLED.h>

namespace {

constexpr uint8_t LED_PIN = 41;
constexpr uint8_t LED_COUNT = 10;
constexpr uint8_t BRIGHTNESS = 48;

constexpr uint8_t BATTERY_LED = 0;
constexpr uint8_t PER_KEY_START = 1;
constexpr uint8_t PER_KEY_COUNT = 5;
constexpr uint8_t INDICATOR_START = 6;
constexpr uint8_t INDICATOR_COUNT = 2;
constexpr uint8_t UNDERGLOW_START = 8;
constexpr uint8_t UNDERGLOW_COUNT = 2;

CRGB leds[LED_COUNT];

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

} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(BRIGHTNESS);
    render_test_pattern();

    Serial.println("ZMK-BLE ESP32 LED test");
    Serial.println("LED 0: battery");
    Serial.println("LEDs 1-5: temporary per-key RGB");
    Serial.println("LEDs 6-7: indicators");
    Serial.println("LEDs 8-9: underglow");
}

void loop() {
    static uint8_t hue = 0;

    leds[BATTERY_LED] = CHSV(96, 255, BRIGHTNESS);

    for (uint8_t i = 0; i < PER_KEY_COUNT; i++) {
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
