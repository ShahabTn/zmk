#include <Arduino.h>
#include <FastLED.h>
#include <SPI.h>
#include <Wire.h>

namespace {

constexpr uint32_t UART_BAUD = 115200;

// ESP32-S3 <-> nRF52840 link.
constexpr uint8_t NRF_UART_RX_PIN = 47; // ESP RX, from nRF P0.22 TX
constexpr uint8_t NRF_UART_TX_PIN = 21; // ESP TX, to nRF P0.24 RX
constexpr uint8_t NRF_RESET_PIN = 6;

// ESP32-S3 <-> nRF52840 I2C link.
constexpr uint8_t NRF_I2C_SDA_PIN = 20; // to nRF P1.00
constexpr uint8_t NRF_I2C_SCL_PIN = 19; // to nRF P0.11

// TFT LCD SPI.
constexpr uint8_t LCD_CS_PIN = 46;
constexpr uint8_t LCD_DC_PIN = 3;
constexpr uint8_t LCD_RST_PIN = 8;
constexpr uint8_t LCD_MOSI_PIN = 18;
constexpr uint8_t LCD_SCK_PIN = 17;
constexpr uint8_t LCD_BACKLIGHT_PIN = 14;

// nRF24L01+ / Xiaomi radio SPI.
constexpr uint8_t XIAOMI_SCK_PIN = 5;
constexpr uint8_t XIAOMI_IRQ_PIN = 41;
constexpr uint8_t XIAOMI_MISO_PIN = 42;
constexpr uint8_t XIAOMI_CE_PIN = 1;
constexpr uint8_t XIAOMI_CSN_PIN = 44;
constexpr uint8_t XIAOMI_MOSI_PIN = 43;

// Side-B LEDs.
constexpr uint8_t LED_DIN_PIN = 2;
constexpr uint8_t LED_COUNT = 16;
constexpr uint8_t LED_BRIGHTNESS = 32;

// Sensor I2C bus for INA219 and SHT40.
constexpr uint8_t SENSOR_I2C_SCL_PIN = 38;
constexpr uint8_t SENSOR_I2C_SDA_PIN = 40;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint8_t INA219_ADDR = 0x40;
constexpr uint8_t SHT40_ADDR = 0x44;

HardwareSerial NrfSerial(1);
TwoWire NrfWire = TwoWire(0);
TwoWire SensorWire = TwoWire(1);
SPIClass LcdSpi(FSPI);
SPIClass XiaomiSpi(HSPI);
CRGB leds[LED_COUNT];

bool i2c_device_present(TwoWire &bus, uint8_t address) {
    bus.beginTransmission(address);
    return bus.endTransmission() == 0;
}

void print_i2c_device_status(TwoWire &bus, const char *name, uint8_t address) {
    Serial.printf("%s 0x%02X: %s\n", name, address,
                  i2c_device_present(bus, address) ? "found" : "not found");
}

void reset_nrf() {
    pinMode(NRF_RESET_PIN, OUTPUT);
    digitalWrite(NRF_RESET_PIN, LOW);
    delay(20);
    pinMode(NRF_RESET_PIN, INPUT);
}

void setup_lcd_pins() {
    pinMode(LCD_CS_PIN, OUTPUT);
    pinMode(LCD_DC_PIN, OUTPUT);
    pinMode(LCD_RST_PIN, OUTPUT);
    pinMode(LCD_BACKLIGHT_PIN, OUTPUT);

    digitalWrite(LCD_CS_PIN, HIGH);
    digitalWrite(LCD_DC_PIN, LOW);
    digitalWrite(LCD_RST_PIN, HIGH);
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH);

    LcdSpi.begin(LCD_SCK_PIN, -1, LCD_MOSI_PIN, LCD_CS_PIN);
}

void setup_xiaomi_radio_pins() {
    pinMode(XIAOMI_CE_PIN, OUTPUT);
    pinMode(XIAOMI_CSN_PIN, OUTPUT);
    pinMode(XIAOMI_IRQ_PIN, INPUT);

    digitalWrite(XIAOMI_CE_PIN, LOW);
    digitalWrite(XIAOMI_CSN_PIN, HIGH);

    XiaomiSpi.begin(XIAOMI_SCK_PIN, XIAOMI_MISO_PIN, XIAOMI_MOSI_PIN, XIAOMI_CSN_PIN);
}

void print_pin_summary() {
    Serial.println("Side-B ESP32-S3 bring-up");
    Serial.printf("nRF UART: RX GPIO%u, TX GPIO%u, %lu baud\n", NRF_UART_RX_PIN,
                  NRF_UART_TX_PIN, static_cast<unsigned long>(UART_BAUD));
    Serial.printf("nRF reset control: GPIO%u\n", NRF_RESET_PIN);
    Serial.printf("nRF I2C: SDA GPIO%u, SCL GPIO%u\n", NRF_I2C_SDA_PIN, NRF_I2C_SCL_PIN);
    Serial.printf("LCD SPI: CS GPIO%u, DC GPIO%u, RST GPIO%u, MOSI GPIO%u, SCK GPIO%u\n",
                  LCD_CS_PIN, LCD_DC_PIN, LCD_RST_PIN, LCD_MOSI_PIN, LCD_SCK_PIN);
    Serial.printf("LCD backlight: GPIO%u\n", LCD_BACKLIGHT_PIN);
    Serial.printf("Xiaomi/nRF24 SPI: SCK GPIO%u, MISO GPIO%u, MOSI GPIO%u, CE GPIO%u, CSN GPIO%u, IRQ GPIO%u\n",
                  XIAOMI_SCK_PIN, XIAOMI_MISO_PIN, XIAOMI_MOSI_PIN, XIAOMI_CE_PIN,
                  XIAOMI_CSN_PIN, XIAOMI_IRQ_PIN);
    Serial.printf("LED DIN: GPIO%u\n", LED_DIN_PIN);
    Serial.printf("Side-B LEDs: %u WS2812-2121, brightness %u\n", LED_COUNT, LED_BRIGHTNESS);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(NRF_RESET_PIN, INPUT);
    FastLED.addLeds<WS2812B, LED_DIN_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(LED_BRIGHTNESS);
    fill_rainbow(leds, LED_COUNT, 0, 12);
    FastLED.show();

    NrfSerial.begin(UART_BAUD, SERIAL_8N1, NRF_UART_RX_PIN, NRF_UART_TX_PIN);
    NrfWire.begin(NRF_I2C_SDA_PIN, NRF_I2C_SCL_PIN);
    NrfWire.setClock(I2C_CLOCK_HZ);
    SensorWire.begin(SENSOR_I2C_SDA_PIN, SENSOR_I2C_SCL_PIN);
    SensorWire.setClock(I2C_CLOCK_HZ);

    setup_lcd_pins();
    setup_xiaomi_radio_pins();
    print_pin_summary();

    print_i2c_device_status(SensorWire, "INA219", INA219_ADDR);
    print_i2c_device_status(SensorWire, "SHT40", SHT40_ADDR);

    NrfSerial.println("BOOT ESP32 SIDE-B");
}

void loop() {
    while (NrfSerial.available()) {
        Serial.write(NrfSerial.read());
    }

    static uint32_t last_heartbeat = 0;
    static uint8_t hue = 0;

    fill_rainbow(leds, LED_COUNT, hue++, 12);
    FastLED.show();
    delay(20);

    if (millis() - last_heartbeat > 2000) {
        last_heartbeat = millis();
        NrfSerial.println("PING ESP32 SIDE-B");
        Serial.println("Side-B heartbeat");
    }
}
