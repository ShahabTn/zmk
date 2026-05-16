#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <CRC16.h>

// Set this to 0 for the prototype wiring:
// CE=GPIO4, CSN=GPIO10, SCK=GPIO12, MOSI=GPIO11, MISO=GPIO13.
//
// Set this to 1 for the Side-B production PCB mapping:
// CE=GPIO1, CSN=GPIO11, SCK=GPIO5, MOSI=GPIO10, MISO=GPIO12, IRQ=GPIO39.
// IRQ is not used by this polling sketch.
#define USE_REAL_PCB_PINS 1

#if USE_REAL_PCB_PINS
static constexpr uint8_t PIN_CE = 1;
static constexpr uint8_t PIN_CSN = 11;
static constexpr uint8_t PIN_SCK = 5;
static constexpr uint8_t PIN_MOSI = 10;
static constexpr uint8_t PIN_MISO = 12;
static constexpr uint8_t PIN_IRQ = 39;
#else
static constexpr uint8_t PIN_CE = 4;
static constexpr uint8_t PIN_CSN = 10;
static constexpr uint8_t PIN_SCK = 12;
static constexpr uint8_t PIN_MOSI = 11;
static constexpr uint8_t PIN_MISO = 13;
static constexpr uint8_t PIN_IRQ = 255;
#endif

// Fill this after the first receive test prints the original remote ID.
// Example: static constexpr uint32_t LIGHTBAR_REMOTE_ID = 0xABCDEF;
static constexpr uint32_t LIGHTBAR_REMOTE_ID = 0xF6F611;
static constexpr bool AUTO_TEST_TX = false;
static constexpr uint32_t AUTO_TEST_INTERVAL_MS = 3000;
static constexpr uint8_t MAX_LEVEL_STEPS = 15;

static constexpr uint64_t SEND_ADDRESS = 0x5555555555ULL;
static constexpr uint64_t RECEIVE_ADDRESS = 0xAAAAAAAAAAULL;
static constexpr uint8_t PREAMBLE[8] = {
  0x53, 0x39, 0x14, 0xDD, 0x1C, 0x49, 0x34, 0x12
};

enum Command : uint8_t {
  CMD_ON_OFF = 0x01,
  CMD_COOLER = 0x02,
  CMD_WARMER = 0x03,
  CMD_BRIGHTER = 0x04,
  CMD_DIMMER = 0x05,
  CMD_RESET = 0x06
};

RF24 radioConfigured(PIN_CE, PIN_CSN);
RF24 radioCeCsnSwapped(PIN_CSN, PIN_CE);
RF24 *radio = &radioConfigured;
CRC16 crc(0x1021, 0xfffe, 0x0000, false, false);

uint8_t txSequence = 0;
uint32_t lastRxSerial = 0;
uint8_t lastRxSequence = 0;
bool radioReady = false;
uint32_t lastAutoTxMs = 0;
uint8_t autoTestStep = 0;

void printHex2(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

bool tryRadioPins(RF24 &candidate, uint8_t sck, uint8_t miso, uint8_t mosi, const char *label) {
  Serial.print("[Radio] Trying ");
  Serial.print(label);
  Serial.print(": SCK=");
  Serial.print(sck);
  Serial.print(" MISO=");
  Serial.print(miso);
  Serial.print(" MOSI=");
  Serial.println(mosi);

  SPI.end();
  delay(50);
  SPI.begin(sck, miso, mosi);
  delay(100);

  radio = &candidate;
  if (!radio->begin()) {
    return false;
  }

  radio->failureDetected = false;
  radio->openReadingPipe(0, RECEIVE_ADDRESS);
  radio->setChannel(68);
  radio->setDataRate(RF24_2MBPS);
  radio->disableCRC();
  radio->disableDynamicPayloads();
  radio->setPayloadSize(17);
  radio->setAutoAck(false);
  radio->setRetries(15, 15);
  radio->openWritingPipe(SEND_ADDRESS);
  radio->startListening();

  Serial.print("[Radio] Ready on ");
  Serial.print(label);
  Serial.println(", channel 68, 2 Mbps, 17-byte payload.");
  return true;
}

void setupRadio() {
  while (true) {
    if (tryRadioPins(radioConfigured, PIN_SCK, PIN_MISO, PIN_MOSI, "configured CE/CSN + configured MO/MI")) {
      radioReady = true;
      return;
    }

    if (tryRadioPins(radioConfigured, PIN_SCK, PIN_MOSI, PIN_MISO, "configured CE/CSN + MO/MI swapped")) {
      radioReady = true;
      Serial.println("[Radio] Your MO and MI wires are swapped relative to the sketch.");
      return;
    }

    if (tryRadioPins(radioCeCsnSwapped, PIN_SCK, PIN_MISO, PIN_MOSI, "CE/CSN swapped + configured MO/MI")) {
      radioReady = true;
      Serial.println("[Radio] Your CE and CSN wires are swapped relative to the sketch.");
      return;
    }

    if (tryRadioPins(radioCeCsnSwapped, PIN_SCK, PIN_MOSI, PIN_MISO, "CE/CSN swapped + MO/MI swapped")) {
      radioReady = true;
      Serial.println("[Radio] Your CE/CSN and MO/MI wires are swapped relative to the sketch.");
      return;
    }

    radioReady = false;
    Serial.println("[Radio] nRF24 not responding on any tested SPI/CE/CSN mapping.");
    Serial.println("[Radio] Check VCC/GND, module orientation, CE/CSN, and ESP32 pin labels.");
    delay(1500);
  }
}

bool decodeIncoming(uint8_t data[17]) {
  uint8_t raw[18] = {0};
  radio->read(&raw, sizeof(raw));

  for (int i = 0; i < 17; i++) {
    if (i == 0) {
      data[i] = 0x50 | raw[i] >> 5;
    } else {
      data[i] = ((raw[i - 1] >> 1) & 0x0F) << 4 |
                ((raw[i - 1] & 0x01) << 3) |
                raw[i] >> 5;
    }
  }

  if (memcmp(data, PREAMBLE, sizeof(PREAMBLE)) != 0) {
    return false;
  }

  crc.restart();
  crc.add(data, 15);
  const uint16_t calculated = crc.calc();
  const uint16_t received = (static_cast<uint16_t>(data[15]) << 8) | data[16];
  return calculated == received;
}

void pollRemote() {
  if (!radio->available()) {
    return;
  }

  uint8_t data[17] = {0};
  if (!decodeIncoming(data)) {
    return;
  }

  const uint32_t serial =
    (static_cast<uint32_t>(data[8]) << 16) |
    (static_cast<uint32_t>(data[9]) << 8) |
    data[10];
  const uint8_t sequence = data[12];

  if (serial == lastRxSerial && sequence == lastRxSequence) {
    return;
  }
  lastRxSerial = serial;
  lastRxSequence = sequence;

  Serial.print("[RX] serial=0x");
  Serial.print(serial, HEX);
  Serial.print(" sequence=");
  Serial.print(sequence);
  Serial.print(" command=0x");
  printHex2(data[13]);
  Serial.print(" option=0x");
  printHex2(data[14]);
  Serial.println();
}

void sendCommand(uint8_t command, uint8_t option = 0x00) {
  if (LIGHTBAR_REMOTE_ID == 0) {
    Serial.println("[TX] Set LIGHTBAR_REMOTE_ID first, then flash again.");
    return;
  }

  uint8_t data[17] = {0};
  memcpy(data, PREAMBLE, sizeof(PREAMBLE));
  data[8] = (LIGHTBAR_REMOTE_ID >> 16) & 0xFF;
  data[9] = (LIGHTBAR_REMOTE_ID >> 8) & 0xFF;
  data[10] = LIGHTBAR_REMOTE_ID & 0xFF;
  data[11] = 0xFF;
  data[12] = ++txSequence;
  data[13] = command;
  data[14] = option;

  crc.restart();
  crc.add(data, 15);
  const uint16_t checksum = crc.calc();
  data[15] = (checksum >> 8) & 0xFF;
  data[16] = checksum & 0xFF;

  Serial.print("[TX] ");
  for (uint8_t b : data) {
    printHex2(b);
  }
  Serial.println();

  radio->stopListening();
  for (int i = 0; i < 20; i++) {
    radio->write(&data, sizeof(data), true);
    delay(10);
  }
  radio->startListening();
}

uint8_t percentToStep(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  return (percent * MAX_LEVEL_STEPS + 50) / 100;
}

void setBrightnessPercent(uint8_t percent) {
  const uint8_t step = percentToStep(percent);

  Serial.print("[Preset] Brightness ");
  Serial.print(percent);
  Serial.print("% -> step ");
  Serial.println(step);

  sendCommand(CMD_DIMMER, 0xF0);
  delay(80);
  if (step > 0) {
    sendCommand(CMD_BRIGHTER, step);
  }
}

void setTemperaturePercent(uint8_t percentWarm) {
  const uint8_t step = percentToStep(percentWarm);

  Serial.print("[Preset] Warmth ");
  Serial.print(percentWarm);
  Serial.print("% -> step ");
  Serial.println(step);

  sendCommand(CMD_COOLER, 0xF0);
  delay(80);
  if (step > 0) {
    sendCommand(CMD_WARMER, step);
  }
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char ch = Serial.read();
  switch (ch) {
    case 't':
      sendCommand(CMD_ON_OFF, 0x03);
      break;
    case '0':
      setBrightnessPercent(0);
      break;
    case '5':
      setBrightnessPercent(50);
      break;
    case '9':
      setBrightnessPercent(100);
      break;
    case '+':
      sendCommand(CMD_BRIGHTER, 0x01);
      break;
    case '-':
      sendCommand(CMD_DIMMER, 0xFF);
      break;
    case 'w':
      sendCommand(CMD_WARMER, 0xFF);
      break;
    case 'c':
      sendCommand(CMD_COOLER, 0x01);
      break;
    case 'k':
      setTemperaturePercent(0);
      break;
    case 'm':
      setTemperaturePercent(50);
      break;
    case 'h':
      setTemperaturePercent(100);
      break;
    case 'r':
    case 'p':
      sendCommand(CMD_RESET);
      break;
    case '\n':
    case '\r':
      break;
    default:
      Serial.println("[Serial] Commands: t + - w c r p 0 5 9 k m h");
      break;
  }
}

void handleAutoTestTx() {
  if (!AUTO_TEST_TX || !radioReady) {
    return;
  }

  const uint32_t now = millis();
  if (now - lastAutoTxMs < AUTO_TEST_INTERVAL_MS) {
    return;
  }
  lastAutoTxMs = now;

  switch (autoTestStep) {
    case 0:
      Serial.println("[Auto] Brighter.");
      sendCommand(CMD_BRIGHTER, 0x01);
      break;
    case 1:
      Serial.println("[Auto] Brighter.");
      sendCommand(CMD_BRIGHTER, 0x01);
      break;
    case 2:
      Serial.println("[Auto] Brighter.");
      sendCommand(CMD_BRIGHTER, 0x02);
      break;
    case 3:
      Serial.println("[Auto] Brighter.");
      sendCommand(CMD_BRIGHTER, 0x01);
      break;
    case 4:
      Serial.println("[Auto] Dimmer.");
      sendCommand(CMD_DIMMER, 0xFF);
      break;
    case 5:
      Serial.println("[Auto] Dimmer.");
      sendCommand(CMD_DIMMER, 0xFF);
      break;
    case 6:
      Serial.println("[Auto] Dimmer.");
      sendCommand(CMD_DIMMER, 0xFF);
      break;
    case 7:
      Serial.println("[Auto] Dimmer.");
      sendCommand(CMD_DIMMER, 0xFE);
      break;
    case 8:
      Serial.println("[Auto] Cooler color temperature.");
      sendCommand(CMD_COOLER, 0x01);
      break;
    case 9:
      Serial.println("[Auto] Cooler color temperature.");
      sendCommand(CMD_COOLER, 0x02);
      break;
    case 10:
      Serial.println("[Auto] Warmer color temperature.");
      sendCommand(CMD_WARMER, 0xFF);
      break;
    case 11:
      Serial.println("[Auto] Warmer color temperature.");
      sendCommand(CMD_WARMER, 0xFE);
      break;
    case 12:
      Serial.println("[Auto] Toggle on/off.");
      sendCommand(CMD_ON_OFF, 0x03);
      break;
  }

  autoTestStep = (autoTestStep + 1) % 13;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Xiaomi MJGJD01YL ESP32 + nRF24L01+ bench test");
  Serial.println("First press/rotate the original remote and copy the printed serial.");
  setupRadio();
}

void loop() {
  if (radio->failureDetected) {
    Serial.println("[Radio] Failure detected, reinitializing.");
    delay(1000);
    setupRadio();
  }

  pollRemote();
  handleSerialCommand();
  handleAutoTestTx();
}
