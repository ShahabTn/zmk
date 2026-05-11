#include <Arduino.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Wire.h>

namespace {

constexpr uint8_t LED_PIN = 21;
constexpr uint8_t LED_COUNT = 10;
// Auto-sync test
constexpr uint8_t BRIGHTNESS = 10;
constexpr uint8_t UART_RX_PIN = 13;
constexpr uint8_t UART_TX_PIN = 12;
constexpr uint32_t UART_BAUD = 115200;
constexpr uint8_t BUZZER_PIN = 4;
constexpr uint16_t BUZZER_FREQUENCY_HZ = 2400;
constexpr uint16_t BUZZER_DURATION_MS = 25;
constexpr uint8_t INA219_I2C_SDA_PIN = 10;
constexpr uint8_t INA219_I2C_SCL_PIN = 9;
constexpr uint8_t SHARED_I2C_SDA_PIN = 38;
constexpr uint8_t SHARED_I2C_SCL_PIN = 39;
constexpr uint32_t I2C_CLOCK_HZ = 400000;
constexpr uint8_t DRV2605L_TRIGGER_PIN = 47;

constexpr uint8_t INA219_ADDR = 0x40;
constexpr uint8_t DRV2605L_ADDR = 0x5A;
constexpr uint8_t ATECC608A_ADDR = 0x60;
constexpr uint8_t BH1750_ADDR = 0x23;
constexpr char MDNS_NAME[] = "macropad";
constexpr char WIFI_SSID[] = "";
constexpr char WIFI_PASSWORD[] = "";
constexpr char AP_SSID[] = "ZMK-BLE-Macropad";
constexpr char AP_PASSWORD[] = "12345678";
constexpr char DEFAULT_CONFIG[] =
    "{\"version\":1,\"layer\":\"trading\",\"keys\":[{\"id\":0,\"label\":\"BUY\","
    "\"type\":\"broker_api\",\"color\":\"#ef9f27\",\"tap1\":\"BUY 0.01 XAUUSD\","
    "\"tap2\":\"BUY 0.05 XAUUSD\",\"tap3\":\"\",\"hold\":\"\",\"params\":\"{\\\"symbol\\\":\\\"XAUUSD\\\"}\"}]}";

constexpr uint8_t BATTERY_LED = 0;
constexpr uint8_t PER_KEY_START = 1;
constexpr uint8_t PER_KEY_COUNT = 5;
constexpr uint8_t INDICATOR_START = 6;
constexpr uint8_t INDICATOR_COUNT = 2;
constexpr uint8_t UNDERGLOW_START = 8;
constexpr uint8_t UNDERGLOW_COUNT = 2;

CRGB leds[LED_COUNT];
HardwareSerial NrfSerial(1);
TwoWire Ina219Wire = TwoWire(0);
TwoWire SharedWire = TwoWire(1);
WebServer server(80);
WebSocketsServer webSocket(81);
Preferences preferences;
String rxLine;
String configJson;
uint32_t keyLedReleaseAt[PER_KEY_COUNT];
uint8_t lastRemapSyncCount;
uint8_t remapAckCount;
uint32_t remapAckDeadline;

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

CRGB color_from_hex(const char *hex, const CRGB &fallback) {
    if (hex == nullptr || hex[0] != '#' || strlen(hex) != 7) {
        return fallback;
    }

    char channel[3] = {0};
    channel[0] = hex[1];
    channel[1] = hex[2];
    uint8_t red = strtoul(channel, nullptr, 16);
    channel[0] = hex[3];
    channel[1] = hex[4];
    uint8_t green = strtoul(channel, nullptr, 16);
    channel[0] = hex[5];
    channel[1] = hex[6];
    uint8_t blue = strtoul(channel, nullptr, 16);

    return CRGB(red, green, blue);
}

CRGB color_for_key(uint8_t position) {
    JsonDocument doc;

    if (deserializeJson(doc, configJson)) {
        return CRGB::White;
    }

    JsonArray keys = doc["keys"].as<JsonArray>();

    for (JsonVariant key : keys) {
        if ((key["id"] | 255) == position) {
            return color_from_hex(key["color"] | "#ffffff", CRGB::White);
        }
    }

    return CRGB::White;
}

bool buzzer_enabled() {
    JsonDocument doc;

    if (deserializeJson(doc, configJson)) {
        return false;
    }

    return doc["settings"]["buzzer"] | false;
}

String normalize_remap_name(String value) {
    value.trim();
    value.toUpperCase();
    value.replace("_", "");
    value.replace("-", "");
    value.replace(" ", "");

    if (value == "A" || value == "KCA") {
        return "KC_A";
    }
    if (value == "B" || value == "KCB") {
        return "KC_B";
    }
    if (value == "C" || value == "KCC") {
        return "KC_C";
    }
    if (value == "NONE") {
        return "NONE";
    }

    return "NONE";
}

String remap_name_for_key(JsonVariant key) { return normalize_remap_name(key["tap1"] | ""); }

void send_nrf_line(const String &line) {
    NrfSerial.print(line);
    NrfSerial.flush();
    Serial.print("ESP -> nRF: ");
    Serial.print(line);
}

void mark_remap_sync_pending() {
    remapAckCount = 0;
    remapAckDeadline = millis() + 1500;
    fill_segment(INDICATOR_START, INDICATOR_COUNT, CRGB::Yellow);
    FastLED.show();
}

void send_remap_line_to_nrf(uint8_t position, const String &remapName) {
    send_nrf_line("M " + String(position) + " " + remapName + "\n");
}

uint8_t sync_single_remap_to_nrf(uint8_t position, const String &value) {
    if (position >= 28) {
        return 0;
    }

    String remapName = normalize_remap_name(value);
    mark_remap_sync_pending();
    send_remap_line_to_nrf(position, remapName);
    lastRemapSyncCount = 1;
    Serial.printf("Synced selected remap %u -> %s\n", position, remapName.c_str());
    return 1;
}

uint8_t sync_remaps_to_nrf() {
    JsonDocument doc;
    uint8_t sent = 0;

    if (deserializeJson(doc, configJson)) {
        Serial.println("Cannot sync remaps: invalid config JSON");
        lastRemapSyncCount = 0;
        return 0;
    }

    JsonArray keys = doc["keys"].as<JsonArray>();
    mark_remap_sync_pending();

    for (JsonVariant key : keys) {
        uint8_t position = key["id"] | 255;

        if (position >= 28) {
            continue;
        }

        String remapName = remap_name_for_key(key);

        if (remapName == "NONE") {
            continue;
        }

        send_remap_line_to_nrf(position, remapName);
        delay(20);
        sent++;
    }

    lastRemapSyncCount = sent;
    Serial.printf("Synced %u simple KC_A/KC_B/KC_C remaps to nRF\n", sent);
    return sent;
}

bool i2c_device_present(TwoWire &bus, uint8_t address) {
    bus.beginTransmission(address);
    return bus.endTransmission() == 0;
}

void print_i2c_device_status(TwoWire &bus, const char *name, uint8_t address) {
    Serial.printf("%s 0x%02X: %s\n", name, address,
                  i2c_device_present(bus, address) ? "found" : "not found");
}

void print_i2c_status() {
    Serial.printf("INA219 I2C: SDA GPIO%u, SCL GPIO%u, %lu Hz\n", INA219_I2C_SDA_PIN,
                  INA219_I2C_SCL_PIN, static_cast<unsigned long>(I2C_CLOCK_HZ));
    print_i2c_device_status(Ina219Wire, "INA219", INA219_ADDR);

    Serial.printf("Shared I2C: SDA GPIO%u, SCL GPIO%u, %lu Hz\n", SHARED_I2C_SDA_PIN,
                  SHARED_I2C_SCL_PIN,
                  static_cast<unsigned long>(I2C_CLOCK_HZ));
    print_i2c_device_status(SharedWire, "DRV2605L", DRV2605L_ADDR);
    print_i2c_device_status(SharedWire, "ATECC608A", ATECC608A_ADDR);
    print_i2c_device_status(SharedWire, "BH1750", BH1750_ADDR);
}

void load_config() {
    preferences.begin("macropad", false);
    configJson = preferences.getString("config", DEFAULT_CONFIG);
}

void save_config(const String &json) {
    configJson = json;
    preferences.putString("config", configJson);
}

void send_config(uint8_t client) {
    webSocket.sendTXT(client, "{\"type\":\"config\",\"config\":" + configJson + "}");
}

void send_http_config() {
    server.send(200, "application/json", "{\"type\":\"config\",\"config\":" + configJson + "}");
}

void save_http_config() {
    String body = server.arg("plain");

    if (body.length() == 0) {
        server.send(400, "application/json", "{\"type\":\"error\",\"message\":\"empty body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        server.send(400, "application/json", "{\"type\":\"error\",\"message\":\"invalid json\"}");
        return;
    }

    String nextConfig;

    if (doc["type"] == "set_config") {
        serializeJson(doc["config"], nextConfig);
    } else {
        serializeJson(doc, nextConfig);
    }

    save_config(nextConfig);

    uint8_t selectedPosition = doc["selected"] | 255;
    const char *selectedRemap = doc["remap"] | "";
    uint8_t sent = selectedPosition < 28 ? sync_single_remap_to_nrf(selectedPosition, selectedRemap)
                                         : sync_remaps_to_nrf();

    server.send(200, "application/json",
                "{\"type\":\"ok\",\"message\":\"config saved; remaps sent: " + String(sent) +
                    "\"}");
    Serial.println("GUI config saved over HTTP");
}

void send_ok(uint8_t client, const char *message) {
    JsonDocument doc;
    doc["type"] = "ok";
    doc["message"] = message;
    String out;
    serializeJson(doc, out);
    webSocket.sendTXT(client, out);
}

void handle_websocket_message(uint8_t client, const String &payload) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        webSocket.sendTXT(client, "{\"type\":\"error\",\"message\":\"invalid json\"}");
        return;
    }

    const char *type = doc["type"] | "";

    if (strcmp(type, "get_config") == 0) {
        send_config(client);
        return;
    }

    if (strcmp(type, "set_config") == 0) {
        String nextConfig;
        serializeJson(doc["config"], nextConfig);
        save_config(nextConfig);

        uint8_t selectedPosition = doc["selected"] | 255;
        const char *selectedRemap = doc["remap"] | "";
        uint8_t sent = selectedPosition < 28 ? sync_single_remap_to_nrf(selectedPosition, selectedRemap)
                                             : sync_remaps_to_nrf();

        String message = "config saved; remaps sent: " + String(sent);
        send_ok(client, message.c_str());
        Serial.println("GUI config saved");
        return;
    }

    webSocket.sendTXT(client, "{\"type\":\"error\",\"message\":\"unknown command\"}");
}

void on_websocket_event(uint8_t client, WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        send_config(client);
        break;
    case WStype_TEXT:
        handle_websocket_message(client, String(reinterpret_cast<char *>(payload), length));
        break;
    default:
        break;
    }
}

void start_network() {
    if (strlen(WIFI_SSID) > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        Serial.printf("Connecting to Wi-Fi SSID %s", WIFI_SSID);
        for (uint8_t i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();
    }

    if (WiFi.status() != WL_CONNECTED) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        Serial.printf("Wi-Fi AP started: %s / %s\n", AP_SSID, AP_PASSWORD);
        Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        Serial.printf("Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
    }

    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
        Serial.printf("mDNS: http://%s.local\n", MDNS_NAME);
    }
}

void start_web_gui() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return;
    }

    server.on("/", HTTP_GET, []() {
        File file = LittleFS.open("/index.html", "r");
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/index.html", HTTP_GET, []() {
        File file = LittleFS.open("/index.html", "r");
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/favicon.ico", HTTP_GET, []() {
        server.send(204);
    });
    server.on("/api/config", HTTP_GET, send_http_config);
    server.on("/api/config", HTTP_POST, save_http_config);
    server.onNotFound([]() {
        Serial.printf("HTTP route not found: %s\n", server.uri().c_str());
        server.send(404, "text/plain", "not found");
    });
    server.begin();

    webSocket.begin();
    webSocket.onEvent(on_websocket_event);
    Serial.println("HTTP server on port 80, WebSocket on port 81");
}

void handle_key_event(uint8_t position, bool pressed) {
    uint8_t led = PER_KEY_START + (position % PER_KEY_COUNT);
    String eventJson = "{\"type\":\"key\",\"position\":" + String(position) +
                       ",\"pressed\":" + String(pressed ? "true" : "false") + "}";

    webSocket.broadcastTXT(eventJson);

    if (pressed) {
        leds[led] = color_for_key(position);
        keyLedReleaseAt[position % PER_KEY_COUNT] = millis() + 800;
        if (buzzer_enabled()) {
            tone(BUZZER_PIN, BUZZER_FREQUENCY_HZ, BUZZER_DURATION_MS);
        }
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
    } else if (line.startsWith("BOOT ")) {
        Serial.println("nRF boot detected, syncing saved remaps");
        sync_remaps_to_nrf();
    } else if (line.startsWith("ACK M ")) {
        remapAckCount++;
        fill_segment(INDICATOR_START, INDICATOR_COUNT, CRGB::Green);
        FastLED.show();
        webSocket.broadcastTXT("{\"type\":\"remap_ack\",\"line\":\"" + line + "\"}");
        Serial.printf("nRF remap ACK count: %u\n", remapAckCount);
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
    load_config();
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(DRV2605L_TRIGGER_PIN, OUTPUT);
    digitalWrite(DRV2605L_TRIGGER_PIN, LOW);
    delay(500);

    Ina219Wire.begin(INA219_I2C_SDA_PIN, INA219_I2C_SCL_PIN);
    Ina219Wire.setClock(I2C_CLOCK_HZ);
    SharedWire.begin(SHARED_I2C_SDA_PIN, SHARED_I2C_SCL_PIN);
    SharedWire.setClock(I2C_CLOCK_HZ);

    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, LED_COUNT);
    FastLED.setBrightness(BRIGHTNESS);
    render_test_pattern();

    Serial.println("ZMK-BLE ESP32 LED test");
    Serial.println("LED 0: battery");
    Serial.println("LEDs 1-5: temporary per-key RGB");
    Serial.println("LEDs 6-7: indicators");
    Serial.println("LEDs 8-9: underglow");
    Serial.println("UART from nRF: RX GPIO13, TX GPIO12, 115200 baud");
    Serial.println("Buzzer: GPIO4");
    Serial.println("DRV2605L IN/TRIG: GPIO47");
    print_i2c_status();

    start_network();
    start_web_gui();
    sync_remaps_to_nrf();
}

void loop() {
    static uint8_t hue = 0;

    poll_nrf_uart();
    server.handleClient();
    webSocket.loop();

    if (remapAckDeadline != 0 && millis() > remapAckDeadline) {
        if (remapAckCount == 0) {
            fill_segment(INDICATOR_START, INDICATOR_COUNT, CRGB::Red);
            FastLED.show();
            Serial.println("No nRF remap ACK received");
        }
        remapAckDeadline = 0;
    }

    leds[BATTERY_LED] = CHSV(96, 255, BRIGHTNESS);

    for (uint8_t i = 0; i < PER_KEY_COUNT; i++) {
        if (keyLedReleaseAt[i] != 0 && millis() < keyLedReleaseAt[i]) {
            continue;
        }

        keyLedReleaseAt[i] = 0;
        leds[PER_KEY_START + i] = CHSV(hue + (i * 24), 255, BRIGHTNESS);
    }

    if (remapAckDeadline == 0) {
        leds[INDICATOR_START] = CRGB::Red;
        leds[INDICATOR_START + 1] = CRGB::Cyan;
    }

    for (uint8_t i = 0; i < UNDERGLOW_COUNT; i++) {
        leds[UNDERGLOW_START + i] = CHSV(hue + 128 + (i * 32), 200, BRIGHTNESS);
    }

    FastLED.show();
    hue++;
    delay(30);
}
