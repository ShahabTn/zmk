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
constexpr uint8_t WIFI_CONNECT_TRIES = 24;
constexpr uint8_t WIFI_TEST_TRIES = 20;
constexpr char DEFAULT_CONFIG[] =
    "{\"version\":4,\"layer\":\"trading\",\"keys\":[{\"id\":0,\"label\":\"BUY\","
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
String lastEspRemapCommand = "-";
String lastNrfStoredRemap = "-";
String lastEspAction = "-";
uint32_t restartAt;
String wifiSsid;
String wifiPassword;
uint8_t lightbarBrightness = 80;
uint8_t lightbarWarmth = 40;
bool lightbarOn = true;

String get_preference_string(const char *key, const char *fallback = "") {
    return preferences.isKey(key) ? preferences.getString(key, fallback) : String(fallback);
}

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

String json_escape(const String &value);
void load_lightbar_state_preferences();

bool is_keycode_type(const String &type) {
    String normalized = type;
    normalized.trim();
    normalized.toLowerCase();
    return normalized == "keycode" || normalized == "hid";
}

String normalize_hid_keycode(String value) {
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

String nrf_remap_name_for_key(JsonVariant key) {
    String type = key["type"] | "";

    if (!is_keycode_type(type)) {
        return "NONE";
    }

    return normalize_hid_keycode(key["tap1"] | "");
}

String action_for_key(uint8_t position, String *typeOut = nullptr, String *labelOut = nullptr) {
    JsonDocument doc;

    if (deserializeJson(doc, configJson)) {
        return "";
    }

    JsonArray keys = doc["keys"].as<JsonArray>();

    for (JsonVariant key : keys) {
        if ((key["id"] | 255) == position) {
            if (typeOut != nullptr) {
                *typeOut = key["type"] | "";
            }
            if (labelOut != nullptr) {
                *labelOut = key["label"] | "";
            }
            return String(key["tap1"] | "");
        }
    }

    return "";
}

void publish_esp_action(uint8_t position, const String &type, const String &label,
                        const String &action) {
    JsonDocument doc;
    doc["type"] = "action";
    doc["position"] = position;
    doc["actionType"] = type;
    doc["label"] = label;
    doc["action"] = action;

    String out;
    serializeJson(doc, out);
    webSocket.broadcastTXT(out);

    lastEspAction = String(position) + " " + type + " " + action;
    webSocket.broadcastTXT("{\"type\":\"diag\",\"espAction\":\"" + json_escape(lastEspAction) +
                           "\"}");
}

void execute_key_action(uint8_t position) {
    String actionType;
    String label;
    String action = action_for_key(position, &actionType, &label);

    actionType.trim();
    actionType.toLowerCase();
    action.trim();

    if (actionType.length() == 0 || actionType == "empty" || action.length() == 0 ||
        action == "NONE") {
        publish_esp_action(position, "empty", label, "NONE");
        Serial.printf("ESP action position %u: empty\n", position);
        return;
    }

    if (is_keycode_type(actionType)) {
        publish_esp_action(position, "keycode", label, action);
        Serial.printf("ESP action position %u: HID handled by nRF (%s)\n", position,
                      action.c_str());
        return;
    }

    publish_esp_action(position, actionType, label, action);
    Serial.printf("ESP action position %u: %s -> %s\n", position, actionType.c_str(),
                  action.c_str());
}

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
    lastEspRemapCommand = "M " + String(position) + " " + remapName;
    webSocket.broadcastTXT("{\"type\":\"diag\",\"espSent\":\"" + lastEspRemapCommand + "\"}");
    send_nrf_line(lastEspRemapCommand + "\n");
    delay(5);
    send_nrf_line("Q " + String(position) + "\n");
}

uint8_t sync_single_remap_to_nrf(uint8_t position, const String &type, const String &value) {
    if (position >= 28) {
        return 0;
    }

    String remapName = is_keycode_type(type) ? normalize_hid_keycode(value) : "NONE";
    mark_remap_sync_pending();
    send_remap_line_to_nrf(position, remapName);
    lastRemapSyncCount = 1;
    Serial.printf("Synced selected nRF HID remap %u -> %s\n", position, remapName.c_str());
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

        String remapName = nrf_remap_name_for_key(key);

        send_remap_line_to_nrf(position, remapName);
        delay(20);
        sent++;
    }

    lastRemapSyncCount = sent;
    Serial.printf("Synced %u nRF HID remap slots\n", sent);
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

String json_escape(const String &value) {
    String escaped;
    escaped.reserve(value.length() + 8);

    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '"' || c == '\\') {
            escaped += '\\';
        }
        escaped += c;
    }

    return escaped;
}

void migrate_config_if_needed() {
    JsonDocument doc;

    if (deserializeJson(doc, configJson)) {
        configJson = DEFAULT_CONFIG;
        preferences.putString("config", configJson);
        return;
    }

    uint8_t version = doc["version"] | 0;

    if (version >= 4) {
        return;
    }

    doc["version"] = 4;
    JsonArray keys = doc["keys"].as<JsonArray>();

    for (JsonVariant key : keys) {
        String type = key["type"] | "";
        String tap1 = key["tap1"] | "";
        String tap2 = key["tap2"] | "";
        String hold = key["hold"] | "";

        if (is_keycode_type(type) && normalize_hid_keycode(tap1) == "KC_A" &&
            normalize_hid_keycode(tap2) == "KC_B" &&
            normalize_hid_keycode(hold) == "KC_C") {
            key["type"] = "empty";
            key["tap1"] = "NONE";
            key["tap2"] = "";
            key["hold"] = "";
        }
    }

    String migrated;
    serializeJson(doc, migrated);
    configJson = migrated;
    preferences.putString("config", configJson);
    Serial.println("Migrated ESP-A config to action model v4");
}

void load_config() {
    preferences.begin("macropad", false);
    configJson = preferences.getString("config", DEFAULT_CONFIG);
    migrate_config_if_needed();
    load_lightbar_state_preferences();
}

void save_config(const String &json) {
    configJson = json;
    preferences.putString("config", configJson);
}

void broadcast_lightbar_state() {
    JsonDocument doc;
    doc["type"] = "lightbar_state";
    doc["brightness"] = lightbarBrightness;
    doc["warmth"] = lightbarWarmth;
    doc["kelvin"] = 6500 - static_cast<uint16_t>(lightbarWarmth) * 3800 / 100;
    doc["on"] = lightbarOn;

    String out;
    serializeJson(doc, out);
    webSocket.broadcastTXT(out);
}

void send_lightbar_state_http() {
    JsonDocument doc;
    doc["type"] = "lightbar_state";
    doc["brightness"] = lightbarBrightness;
    doc["warmth"] = lightbarWarmth;
    doc["kelvin"] = 6500 - static_cast<uint16_t>(lightbarWarmth) * 3800 / 100;
    doc["on"] = lightbarOn;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

bool parse_request_json(JsonDocument &doc) {
    String body = server.arg("plain");

    if (body.length() == 0) {
        server.send(400, "application/json", "{\"type\":\"error\",\"message\":\"empty body\"}");
        return false;
    }

    DeserializationError error = deserializeJson(doc, body);

    if (error) {
        server.send(400, "application/json", "{\"type\":\"error\",\"message\":\"invalid json\"}");
        return false;
    }

    return true;
}

void save_lightbar_state_preferences() {
    preferences.putUChar("lb_bright", lightbarBrightness);
    preferences.putUChar("lb_warmth", lightbarWarmth);
    preferences.putBool("lb_on", lightbarOn);
}

void load_lightbar_state_preferences() {
    lightbarBrightness = preferences.getUChar("lb_bright", lightbarBrightness);
    lightbarWarmth = preferences.getUChar("lb_warmth", lightbarWarmth);
    lightbarOn = preferences.getBool("lb_on", lightbarOn);
}

void handle_lightbar_brightness() {
    JsonDocument doc;

    if (!parse_request_json(doc)) {
        return;
    }

    int value = doc["value"] | lightbarBrightness;
    lightbarBrightness = constrain(value, 0, 100);
    lightbarOn = lightbarBrightness > 0;
    save_lightbar_state_preferences();
    broadcast_lightbar_state();
    send_lightbar_state_http();
}

void handle_lightbar_temperature() {
    JsonDocument doc;

    if (!parse_request_json(doc)) {
        return;
    }

    if (doc["warmth"].is<int>()) {
        int warmth = doc["warmth"] | lightbarWarmth;
        lightbarWarmth = constrain(warmth, 0, 100);
    } else {
        int kelvin = doc["kelvin"] | (6500 - static_cast<uint16_t>(lightbarWarmth) * 3800 / 100);
        kelvin = constrain(kelvin, 2700, 6500);
        lightbarWarmth = constrain((6500 - kelvin) * 100 / 3800, 0, 100);
    }

    save_lightbar_state_preferences();
    broadcast_lightbar_state();
    send_lightbar_state_http();
}

void send_config(uint8_t client) {
    webSocket.sendTXT(client, "{\"type\":\"config\",\"config\":" + configJson + "}");
}

void send_http_config() {
    server.send(200, "application/json", "{\"type\":\"config\",\"config\":" + configJson + "}");
}

void send_network_info() {
    bool connected = WiFi.status() == WL_CONNECTED;
    String mode = connected ? "ap_sta" : "ap";
    String savedSsid = get_preference_string("wifi_ssid");

    server.send(200, "application/json",
                "{\"type\":\"network\",\"mode\":\"" + mode + "\",\"apSsid\":\"" +
                    String(AP_SSID) + "\",\"apIp\":\"" + WiFi.softAPIP().toString() +
                    "\",\"homeSsid\":\"" + json_escape(connected ? WiFi.SSID() : savedSsid) +
                    "\",\"savedSsid\":\"" + json_escape(savedSsid) + "\",\"homeIp\":\"" +
                    (connected ? WiFi.localIP().toString() : "") + "\",\"hostname\":\"" +
                    String(MDNS_NAME) + ".local\"}");
}

void save_wifi_settings() {
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

    String ssid = doc["ssid"] | "";
    String password = doc["password"] | "";
    ssid.trim();

    if (ssid.length() == 0) {
        preferences.remove("wifi_ssid");
        preferences.remove("wifi_password");
        server.send(200, "application/json",
                    "{\"type\":\"ok\",\"message\":\"Wi-Fi cleared; restarting in AP mode\"}");
    } else {
        preferences.putString("wifi_ssid", ssid);
        preferences.putString("wifi_password", password);
        server.send(200, "application/json",
                    "{\"type\":\"ok\",\"message\":\"Wi-Fi saved; restarting\"}");
    }

    restartAt = millis() + 900;
}

void test_wifi_settings() {
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

    String ssid = doc["ssid"] | "";
    String password = doc["password"] | "";
    ssid.trim();

    if (ssid.length() == 0) {
        server.send(400, "application/json", "{\"type\":\"error\",\"message\":\"SSID required\"}");
        return;
    }

    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == ssid) {
        server.send(200, "application/json",
                    "{\"type\":\"wifi_test\",\"ok\":true,\"message\":\"Already connected\","
                    "\"ssid\":\"" +
                        json_escape(WiFi.SSID()) + "\",\"ip\":\"" +
                        WiFi.localIP().toString() + "\"}");
        Serial.printf("Wi-Fi test already connected: %s\n", WiFi.localIP().toString().c_str());
        return;
    }

    Serial.printf("Testing home Wi-Fi SSID %s", ssid.c_str());
    WiFi.disconnect(false, false);
    delay(100);
    WiFi.begin(ssid.c_str(), password.c_str());

    for (uint8_t i = 0; i < WIFI_TEST_TRIES && WiFi.status() != WL_CONNECTED; i++) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        server.send(200, "application/json",
                    "{\"type\":\"wifi_test\",\"ok\":true,\"message\":\"Wi-Fi test connected\","
                    "\"ssid\":\"" +
                        json_escape(WiFi.SSID()) + "\",\"ip\":\"" +
                        WiFi.localIP().toString() + "\"}");
        Serial.printf("Wi-Fi test connected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        server.send(200, "application/json",
                    "{\"type\":\"wifi_test\",\"ok\":false,\"message\":\"Wi-Fi test failed\"}");
        Serial.println("Wi-Fi test failed");
    }

    if (wifiSsid.length() > 0 && WiFi.SSID() != wifiSsid) {
        WiFi.disconnect(false, false);
        delay(100);
        WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
    }
}

void reboot_esp() {
    server.send(200, "application/json", "{\"type\":\"ok\",\"message\":\"ESP restarting\"}");
    restartAt = millis() + 500;
}

void request_nrf_bootloader() {
    send_nrf_line("BOOTLOADER\n");
    fill_segment(INDICATOR_START, INDICATOR_COUNT, CRGB::Yellow);
    FastLED.show();
    server.send(200, "application/json",
                "{\"type\":\"ok\",\"message\":\"nRF bootloader command sent\"}");
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
    String selectedType;
    String selectedRemap = action_for_key(selectedPosition, &selectedType, nullptr);
    uint8_t sent = selectedPosition < 28
                       ? sync_single_remap_to_nrf(selectedPosition, selectedType, selectedRemap)
                       : sync_remaps_to_nrf();

    server.send(200, "application/json",
                "{\"type\":\"ok\",\"message\":\"config saved; nRF HID slots synced: " + String(sent) +
                    "\"}");
    Serial.println("GUI config saved over HTTP");
}

void send_http_remap() {
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

    uint8_t position = doc["position"] | 255;
    const char *remap = doc["remap"] | "";
    const char *remapType = doc["type"] | "keycode";
    uint8_t sent = sync_single_remap_to_nrf(position, remapType, remap);

    String normalized = is_keycode_type(remapType) ? normalize_hid_keycode(remap) : "NONE";
    server.send(200, "application/json",
                "{\"type\":\"ok\",\"message\":\"direct nRF HID remap sent\",\"position\":" +
                    String(position) + ",\"remap\":\"" + normalized + "\",\"sent\":" +
                    String(sent) + "}");
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
        String selectedType;
        String selectedRemap = action_for_key(selectedPosition, &selectedType, nullptr);
        uint8_t sent = selectedPosition < 28
                           ? sync_single_remap_to_nrf(selectedPosition, selectedType, selectedRemap)
                           : sync_remaps_to_nrf();

        String message = "config saved; nRF HID slots synced: " + String(sent);
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
    wifiSsid = get_preference_string("wifi_ssid", WIFI_SSID);
    wifiPassword = get_preference_string("wifi_password", WIFI_PASSWORD);

    WiFi.persistent(false);
    WiFi.setHostname(MDNS_NAME);
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("Wi-Fi setup AP: %s / %s\n", AP_SSID, AP_PASSWORD);
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

    if (wifiSsid.length() > 0) {
        WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());

        Serial.printf("Connecting to home Wi-Fi SSID %s", wifiSsid.c_str());
        for (uint8_t i = 0; i < WIFI_CONNECT_TRIES && WiFi.status() != WL_CONNECTED; i++) {
            delay(250);
            Serial.print(".");
        }
        Serial.println();

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Home Wi-Fi connected: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("Home Wi-Fi not connected; setup AP remains available");
        }
    } else {
        Serial.println("No saved home Wi-Fi; setup AP remains available");
    }

    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addService("ws", "tcp", 81);
        Serial.printf("mDNS: http://%s.local\n", MDNS_NAME);
    }
}

void serve_gui_file(const char *path) {
    File file = LittleFS.open(path, "r");

    if (!file) {
        server.send(404, "text/plain", "not found");
        return;
    }

    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.streamFile(file, "text/html");
    file.close();
}

void start_web_gui() {
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed");
        return;
    }

    server.on("/", HTTP_GET, []() {
        serve_gui_file("/index.html");
    });
    server.on("/index.html", HTTP_GET, []() {
        serve_gui_file("/index.html");
    });
    server.on("/b", HTTP_GET, []() {
        serve_gui_file("/index-b.html");
    });
    server.on("/index-b.html", HTTP_GET, []() {
        serve_gui_file("/index-b.html");
    });
    server.on("/favicon.ico", HTTP_GET, []() {
        server.send(204);
    });
    server.on("/api/network", HTTP_GET, send_network_info);
    server.on("/api/wifi", HTTP_POST, save_wifi_settings);
    server.on("/api/wifi/test", HTTP_POST, test_wifi_settings);
    server.on("/api/reboot", HTTP_POST, reboot_esp);
    server.on("/api/nrf/bootloader", HTTP_POST, request_nrf_bootloader);
    server.on("/api/lightbar/state", HTTP_GET, send_lightbar_state_http);
    server.on("/api/lightbar/brightness", HTTP_POST, handle_lightbar_brightness);
    server.on("/api/lightbar/temperature", HTTP_POST, handle_lightbar_temperature);
    server.on("/api/config", HTTP_GET, send_http_config);
    server.on("/api/config", HTTP_POST, save_http_config);
    server.on("/api/remap", HTTP_POST, send_http_remap);
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
        execute_key_action(position);
    } else {
        leds[led] = CRGB::Blue;
        keyLedReleaseAt[position % PER_KEY_COUNT] = 0;
    }

    FastLED.show();
}

void handle_uart_line(const String &line) {
    unsigned int position = 0;
    unsigned int pressed = 0;
    char remapName[12] = {0};

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
    } else if (sscanf(line.c_str(), "STORED M %u %11s", &position, remapName) == 2) {
        lastNrfStoredRemap = "M " + String(position) + " " + String(remapName);
        webSocket.broadcastTXT("{\"type\":\"diag\",\"nrfStored\":\"" + lastNrfStoredRemap + "\"}");
        Serial.printf("nRF stored remap: %s\n", lastNrfStoredRemap.c_str());
    } else if (sscanf(line.c_str(), "ACTIVE M %u %11s", &position, remapName) == 2) {
        webSocket.broadcastTXT("{\"type\":\"diag\",\"nrfActive\":\"M " + String(position) +
                               " " + String(remapName) + "\"}");
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

    Serial.println("ESP-A firmware");
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

    if (restartAt != 0 && millis() > restartAt) {
        ESP.restart();
    }

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
