#include <Arduino.h>
#include <FastLED.h>
#include <Wire.h>

namespace {

constexpr uint8_t LED_PIN = 39;
constexpr uint8_t LED_COUNT = 10;
// Auto-sync test
constexpr uint8_t BRIGHTNESS = 10;
constexpr uint8_t UART_RX_PIN = 10;
constexpr uint8_t UART_TX_PIN = 11;
constexpr uint32_t UART_BAUD = 115200;
constexpr uint8_t BUZZER_PIN = 14;
constexpr uint16_t BUZZER_FREQUENCY_HZ = 2400;
constexpr uint16_t BUZZER_DURATION_MS = 25;
constexpr uint8_t I2C_SDA_PIN = 42;
constexpr uint8_t I2C_SCL_PIN = 2;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint8_t DRV2605L_TRIGGER_PIN = 41;

constexpr uint8_t INA219_ADDR = 0x40;
constexpr uint8_t DRV2605L_ADDR = 0x5A;
constexpr uint8_t ATECC608A_ADDR = 0x60;
constexpr uint8_t BH1750_ADDR = 0x23;

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

bool i2c_device_present(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
}

void print_i2c_device_status(const char *name, uint8_t address) {
    Serial.printf("%s 0x%02X: %s\n", name, address,
                  i2c_device_present(address) ? "found" : "not found");
}

void print_i2c_status() {
    Serial.printf("I2C: SDA GPIO%u, SCL GPIO%u, %lu Hz\n", I2C_SDA_PIN, I2C_SCL_PIN,
                  static_cast<unsigned long>(I2C_CLOCK_HZ));
    print_i2c_device_status("INA219", INA219_ADDR);
    print_i2c_device_status("DRV2605L", DRV2605L_ADDR);
    print_i2c_device_status("ATECC608A", ATECC608A_ADDR);
    print_i2c_device_status("BH1750", BH1750_ADDR);
}

void handle_key_event(uint8_t position, bool pressed) {
    uint8_t led = PER_KEY_START + (position % PER_KEY_COUNT);

    if (pressed) {
        leds[led] = CRGB::White;
        keyLedReleaseAt[position % PER_KEY_COUNT] = millis() + 180;
        tone(BUZZER_PIN, BUZZER_FREQUENCY_HZ, BUZZER_DURATION_MS);
    } else {
        leds[led] = CRGB::Blue;
        keyLedReleaseAt[position % PER_KEY_COUNT] = 0;
    }

    FastLED.show();
}

void handle_uart_line(const String &line) {
    unsigned int position = 0;
    unsigned int pressed = 0;

    Serial.print("nRF UART: ");
    Serial.println(line);

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
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(DRV2605L_TRIGGER_PIN, OUTPUT);
    digitalWrite(DRV2605L_TRIGGER_PIN, LOW);
    delay(500);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_CLOCK_HZ);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(BRIGHTNESS);
    render_test_pattern();

    Serial.println("ZMK-BLE ESP32 LED test");
    Serial.println("LED 0: battery");
    Serial.println("LEDs 1-5: temporary per-key RGB");
    Serial.println("LEDs 6-7: indicators");
    Serial.println("LEDs 8-9: underglow");
    Serial.println("UART from nRF: RX GPIO10, TX GPIO11, 115200 baud");
    Serial.println("Buzzer: GPIO14");
    Serial.println("DRV2605L IN/TRIG: GPIO41");
    print_i2c_status();
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
