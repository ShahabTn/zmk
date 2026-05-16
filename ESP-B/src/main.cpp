#include <Arduino.h>
#include <FastLED.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
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
constexpr uint8_t LCD_CS_PIN = 9;
constexpr uint8_t LCD_DC_PIN = 13;
constexpr uint8_t LCD_RST_PIN = 8;
constexpr uint8_t LCD_MOSI_PIN = 18;
constexpr uint8_t LCD_SCK_PIN = 17;
constexpr uint8_t LCD_BACKLIGHT_PIN = 14;

// nRF24L01+ / Xiaomi radio SPI.
constexpr uint8_t XIAOMI_SCK_PIN = 5;
constexpr uint8_t XIAOMI_IRQ_PIN = 39;
constexpr uint8_t XIAOMI_MISO_PIN = 12;
constexpr uint8_t XIAOMI_CE_PIN = 1;
constexpr uint8_t XIAOMI_CSN_PIN = 11;
constexpr uint8_t XIAOMI_MOSI_PIN = 10;

// MicroSD local NAS SPI.
constexpr uint8_t SD_CS_PIN = 35;
constexpr uint8_t SD_MOSI_PIN = 15;
constexpr uint8_t SD_SCK_PIN = 7;
constexpr uint8_t SD_MISO_PIN = 4;

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
constexpr char NAS_AP_SSID[] = "ZMK-SideB-NAS";
constexpr char NAS_AP_PASSWORD[] = "12345678";

HardwareSerial NrfSerial(1);
TwoWire NrfWire = TwoWire(0);
TwoWire SensorWire = TwoWire(1);
SPIClass LcdSpi(FSPI);
SPIClass XiaomiSpi(HSPI);
SPIClass SdSpi(FSPI);
WebServer server(80);
CRGB leds[LED_COUNT];
File uploadFile;
bool sdReady = false;

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

    // LCD rendering is not initialized in NAS bring-up mode. Keep pins in safe idle states.
}

void setup_xiaomi_radio_pins() {
    pinMode(XIAOMI_CE_PIN, OUTPUT);
    pinMode(XIAOMI_CSN_PIN, OUTPUT);
    pinMode(XIAOMI_IRQ_PIN, INPUT);

    digitalWrite(XIAOMI_CE_PIN, LOW);
    digitalWrite(XIAOMI_CSN_PIN, HIGH);

    // Xiaomi radio is not initialized in NAS bring-up mode. The pins are reserved for final integration.
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
    Serial.printf("MicroSD SPI: CS GPIO%u, SCK GPIO%u, MISO GPIO%u, MOSI GPIO%u\n",
                  SD_CS_PIN, SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    Serial.printf("LED DIN: GPIO%u\n", LED_DIN_PIN);
    Serial.printf("Side-B LEDs: %u WS2812-2121, brightness %u\n", LED_COUNT, LED_BRIGHTNESS);
}

String html_escape(const String &value) {
    String out;
    out.reserve(value.length() + 8);

    for (char c : value) {
        switch (c) {
        case '&':
            out += F("&amp;");
            break;
        case '<':
            out += F("&lt;");
            break;
        case '>':
            out += F("&gt;");
            break;
        case '"':
            out += F("&quot;");
            break;
        default:
            out += c;
            break;
        }
    }

    return out;
}

String url_decode(const String &value) {
    String out;
    out.reserve(value.length());

    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (c == '+' ) {
            out += ' ';
        } else if (c == '%' && i + 2 < value.length()) {
            char hex[3] = {value[i + 1], value[i + 2], 0};
            out += static_cast<char>(strtol(hex, nullptr, 16));
            i += 2;
        } else {
            out += c;
        }
    }

    return out;
}

String clean_path(String path) {
    path = url_decode(path);
    path.trim();
    path.replace("\\", "/");

    while (path.indexOf("//") >= 0) {
        path.replace("//", "/");
    }

    if (!path.startsWith("/")) {
        path = "/" + path;
    }

    if (path.indexOf("..") >= 0) {
        return "/";
    }

    return path;
}

String human_size(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return String(bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return String(bytes / (1024.0 * 1024.0), 1) + " MB";
    }
    if (bytes >= 1024ULL) {
        return String(bytes / 1024.0, 1) + " KB";
    }
    return String(bytes) + " B";
}

void send_header(String &html, const String &title) {
    html += F("<!doctype html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += "<title>" + html_escape(title) + "</title>";
    html += F("<style>"
              "body{margin:0;background:#101316;color:#e9edf0;font:14px -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif}"
              "main{max-width:920px;margin:0 auto;padding:22px}"
              "h1{font-size:22px;margin:0 0 16px}"
              ".card{background:#181e24;border:1px solid #2e3842;border-radius:8px;padding:14px;margin:12px 0}"
              "a{color:#8bc7ff;text-decoration:none}a:hover{text-decoration:underline}"
              "table{width:100%;border-collapse:collapse}td,th{border-bottom:1px solid #2e3842;padding:9px;text-align:left}"
              "th{color:#9ba8b3;font-size:12px;text-transform:uppercase;letter-spacing:.08em}"
              ".muted{color:#9ba8b3}.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
              "button,input[type=file],input[type=text]{font:inherit}"
              "button{background:#2e88dc;color:white;border:0;border-radius:7px;padding:8px 11px;cursor:pointer}"
              "button.danger{background:#aa3434}"
              "input[type=text]{background:#11161b;color:#e9edf0;border:1px solid #34404a;border-radius:7px;padding:8px}"
              "</style></head><body><main>");
    html += "<h1>" + html_escape(title) + "</h1>";
}

void send_footer(String &html) {
    html += F("</main></body></html>");
}

bool require_sd() {
    if (sdReady) {
        return true;
    }

    server.send(503, "text/plain", "MicroSD not mounted");
    return false;
}

void handle_root() {
    if (!require_sd()) {
        return;
    }

    String dirPath = clean_path(server.arg("dir"));
    File dir = SD.open(dirPath);

    if (!dir || !dir.isDirectory()) {
        dirPath = "/";
        dir = SD.open("/");
    }

    String html;
    html.reserve(9000);
    send_header(html, "Side-B Local NAS");
    html += "<div class='card'><div class='muted'>AP: ";
    html += NAS_AP_SSID;
    html += " · IP: " + WiFi.softAPIP().toString();
    html += " · SD: " + human_size(SD.usedBytes()) + " used / " + human_size(SD.totalBytes()) + "</div></div>";
    html += "<div class='card'><form method='POST' action='/upload?dir=" + html_escape(dirPath) +
            "' enctype='multipart/form-data'><div class='row'><input type='file' name='file' multiple><button>upload</button></div></form></div>";
    html += "<div class='card'><form method='POST' action='/mkdir'><div class='row'><input type='hidden' name='dir' value='" +
            html_escape(dirPath) + "'><input type='text' name='name' placeholder='new folder'><button>create folder</button></div></form></div>";
    html += "<div class='card'><table><thead><tr><th>Name</th><th>Size</th><th>Actions</th></tr></thead><tbody>";

    if (dirPath != "/") {
        String parent = dirPath.substring(0, dirPath.lastIndexOf('/'));
        if (parent.length() == 0) {
            parent = "/";
        }
        html += "<tr><td><a href='/?dir=" + html_escape(parent) + "'>..</a></td><td class='muted'>folder</td><td></td></tr>";
    }

    File entry = dir.openNextFile();
    while (entry) {
        String name = String(entry.name());
        String fullPath = name.startsWith("/") ? name : (dirPath == "/" ? "/" + name : dirPath + "/" + name);

        html += "<tr><td>";
        if (entry.isDirectory()) {
            html += "<a href='/?dir=" + html_escape(fullPath) + "'>" + html_escape(name) + "/</a>";
        } else {
            html += "<a href='/download?path=" + html_escape(fullPath) + "'>" + html_escape(name) + "</a>";
        }
        html += "</td><td class='muted'>";
        html += entry.isDirectory() ? "folder" : human_size(entry.size());
        html += "</td><td><form method='POST' action='/delete' style='display:inline' onsubmit='return confirm(\"Delete this item?\")'>"
                "<input type='hidden' name='path' value='" +
                html_escape(fullPath) + "'><button class='danger'>delete</button></form></td></tr>";
        entry = dir.openNextFile();
    }

    html += F("</tbody></table></div>");
    send_footer(html);
    server.send(200, "text/html", html);
}

void handle_download() {
    if (!require_sd()) {
        return;
    }

    String path = clean_path(server.arg("path"));
    File file = SD.open(path, FILE_READ);

    if (!file || file.isDirectory()) {
        server.send(404, "text/plain", "file not found");
        return;
    }

    String name = path.substring(path.lastIndexOf('/') + 1);
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server.streamFile(file, "application/octet-stream");
    file.close();
}

void handle_delete() {
    if (!require_sd()) {
        return;
    }

    String path = clean_path(server.arg("path"));

    if (path == "/") {
        server.send(400, "text/plain", "refusing to delete root");
        return;
    }

    File target = SD.open(path);
    bool ok = false;

    if (target) {
        ok = target.isDirectory() ? SD.rmdir(path) : SD.remove(path);
        target.close();
    }

    server.sendHeader("Location", "/");
    server.send(ok ? 303 : 500, "text/plain", ok ? "deleted" : "delete failed");
}

void handle_mkdir() {
    if (!require_sd()) {
        return;
    }

    String dir = clean_path(server.arg("dir"));
    String name = clean_path(server.arg("name"));
    name.remove(0, 1);

    if (name.length() == 0 || name.indexOf('/') >= 0) {
        server.send(400, "text/plain", "invalid folder name");
        return;
    }

    String path = dir == "/" ? "/" + name : dir + "/" + name;
    bool ok = SD.mkdir(path);
    server.sendHeader("Location", "/?dir=" + dir);
    server.send(ok ? 303 : 500, "text/plain", ok ? "created" : "mkdir failed");
}

void handle_upload_complete() {
    server.sendHeader("Location", "/?dir=" + clean_path(server.arg("dir")));
    server.send(303, "text/plain", "uploaded");
}

void handle_upload_stream() {
    HTTPUpload &upload = server.upload();
    String dir = clean_path(server.arg("dir"));

    if (upload.status == UPLOAD_FILE_START) {
        String filename = clean_path(String(upload.filename));
        filename.remove(0, 1);

        if (filename.length() == 0 || filename.indexOf('/') >= 0) {
            filename = "upload.bin";
        }

        String path = dir == "/" ? "/" + filename : dir + "/" + filename;
        uploadFile = SD.open(path, FILE_WRITE);
        Serial.printf("NAS upload start: %s\n", path.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
            uploadFile.write(upload.buf, upload.currentSize);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
            Serial.printf("NAS upload complete: %u bytes\n", upload.totalSize);
            uploadFile.close();
        }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) {
            uploadFile.close();
        }
        Serial.println("NAS upload aborted");
    }
}

void setup_sd_nas() {
    SdSpi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    sdReady = SD.begin(SD_CS_PIN, SdSpi, 24000000);

    Serial.printf("MicroSD mount: %s\n", sdReady ? "ok" : "failed");
    if (sdReady) {
        Serial.printf("MicroSD size: %s, used: %s\n", human_size(SD.totalBytes()).c_str(),
                      human_size(SD.usedBytes()).c_str());
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(NAS_AP_SSID, NAS_AP_PASSWORD);
    Serial.printf("NAS AP: %s / %s\n", NAS_AP_SSID, NAS_AP_PASSWORD);
    Serial.printf("NAS URL: http://%s/\n", WiFi.softAPIP().toString().c_str());

    server.on("/", HTTP_GET, handle_root);
    server.on("/download", HTTP_GET, handle_download);
    server.on("/delete", HTTP_POST, handle_delete);
    server.on("/mkdir", HTTP_POST, handle_mkdir);
    server.on("/upload", HTTP_POST, handle_upload_complete, handle_upload_stream);
    server.onNotFound([]() {
        server.send(404, "text/plain", "not found");
    });
    server.begin();
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
    setup_sd_nas();
    print_pin_summary();

    print_i2c_device_status(SensorWire, "INA219", INA219_ADDR);
    print_i2c_device_status(SensorWire, "SHT40", SHT40_ADDR);

    NrfSerial.println("BOOT ESP32 SIDE-B");
}

void loop() {
    server.handleClient();

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
        Serial.printf("Side-B heartbeat, NAS %s\n", sdReady ? "ready" : "sd missing");
    }
}
