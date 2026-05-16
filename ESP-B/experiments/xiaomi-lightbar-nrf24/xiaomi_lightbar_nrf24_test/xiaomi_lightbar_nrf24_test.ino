#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <CRC16.h>

// Set this to 0 for the prototype wiring:
// CE=GPIO4, CSN=GPIO5, SCK=GPIO6, MOSI=GPIO7, MISO=GPIO15.
//
// Set this to 1 for the real PCB mapping:
// SCK=GPIO5, MOSI=GPIO45, CSN=GPIO44, CE=GPIO1, MISO=GPIO42, IRQ=GPIO41.
// IRQ is not used by this polling sketch.
#define USE_REAL_PCB_PINS 0

#if USE_REAL_PCB_PINS
static constexpr uint8_t PIN_CE = 1;
static constexpr uint8_t PIN_CSN = 44;
static constexpr uint8_t PIN_SCK = 5;
static constexpr uint8_t PIN_MOSI = 45;
static constexpr uint8_t PIN_MISO = 42;
#else
static constexpr uint8_t PIN_CE = 4;
static constexpr uint8_t PIN_CSN = 5;
static constexpr uint8_t PIN_SCK = 6;
static constexpr uint8_t PIN_MOSI = 7;
static constexpr uint8_t PIN_MISO = 15;
#endif

// Fill this after the first receive test prints the original remote ID.
// Example: static constexpr uint32_t LIGHTBAR_REMOTE_ID = 0xABCDEF;
static constexpr uint32_t LIGHTBAR_REMOTE_ID = 0x000000;

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

RF24 radio(PIN_CE, PIN_CSN);
CRC16 crc(0x1021, 0xfffe, 0x0000, false, false);

uint8_t txSequence = 0;
uint32_t lastRxSerial = 0;
uint8_t lastRxSequence = 0;

void printHex2(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void setupRadio() {
  while (!radio.begin()) {
    Serial.println("[Radio] nRF24 not responding. Check wiring and 3.3V power.");
    delay(1000);
  }

  radio.failureDetected = false;
  radio.openReadingPipe(0, RECEIVE_ADDRESS);
  radio.setChannel(68);
  radio.setDataRate(RF24_2MBPS);
  radio.disableCRC();
  radio.disableDynamicPayloads();
  radio.setPayloadSize(17);
  radio.setAutoAck(false);
  radio.setRetries(15, 15);
  radio.openWritingPipe(SEND_ADDRESS);
  radio.startListening();

  Serial.println("[Radio] Ready on channel 68, 2 Mbps, 17-byte payload.");
}

bool decodeIncoming(uint8_t data[17]) {
  uint8_t raw[18] = {0};
  radio.read(&raw, sizeof(raw));

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
  if (!radio.available()) {
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

  radio.stopListening();
  for (int i = 0; i < 20; i++) {
    radio.write(&data, sizeof(data), true);
    delay(10);
  }
  radio.startListening();
}

void handleSerialCommand() {
  if (!Serial.available()) {
    return;
  }

  const char ch = Serial.read();
  switch (ch) {
    case 't':
      sendCommand(CMD_ON_OFF);
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
    case 'r':
    case 'p':
      sendCommand(CMD_RESET);
      break;
    case '\n':
    case '\r':
      break;
    default:
      Serial.println("[Serial] Commands: t + - w c r p");
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Xiaomi MJGJD01YL ESP32 + nRF24L01+ bench test");
  Serial.println("First press/rotate the original remote and copy the printed serial.");

  SPI.end();
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);
  setupRadio();
}

void loop() {
  if (radio.failureDetected) {
    Serial.println("[Radio] Failure detected, reinitializing.");
    delay(1000);
    setupRadio();
  }

  pollRemote();
  handleSerialCommand();
}
