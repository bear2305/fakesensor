
/*
  Fake Fingerprint Sensor — classic Arduino (Uno / Nano / Mega)
  ---------------------------------------------------------------------------
  Port of the ESP32/Wokwi version. Emulates the AS608/R30x binary UART
  protocol used by Adafruit_Fingerprint.h, including 256-byte UPCHAR/DOWNCHAR.

  Changes vs. the ESP32 version:
    - HardwareSerial(2)  -> SoftwareSerial (Uno/Nano) or Serial1 (Mega)
    - Serial.printf()    -> Serial.print() with F() strings (AVR has no printf)
    - RAM trimmed for 2 KB boards:
        * rawBuffer1/2 and rawTemplate512 removed (never read back)
        * 512-byte UPCHAR is streamed on the fly (256-byte template sent
          twice) instead of being built in a 512-byte array
        * DOWNCHAR validates bytes as they arrive instead of storing 256 bytes
        * parser payload buffer reduced from 256 to 128 bytes
    - Behaviour/protocol is otherwise identical.

  Default wiring (Uno/Nano):
    Fake sensor RX  = D10  (<- master TX)
    Fake sensor TX  = D11  (-> master RX)
    Finger BIT0     = D2
    Finger BIT1     = D3
    Finger BIT2     = D4
    Finger present  = D5
    Debug output    = USB serial @ 115200

  Switches are active LOW (INPUT_PULLUP). Connect GND between both boards.

  NOTE: SoftwareSerial at 57600 baud is marginal on a 16 MHz AVR, and debug
  prints on the hardware UART can disturb its timing. If you see checksum
  errors, set DEBUG to false, or use a Mega with USE_SOFTWARE_SERIAL=false
  (uses Serial1: RX1=19, TX1=18).
  ---------------------------------------------------------------------------
*/

#include <Arduino.h>

// ============================= USER CONFIG ================================
#define USE_SOFTWARE_SERIAL true   // false -> use Serial1 (Mega / Leonardo)

static const uint8_t PIN_SENSOR_RX = 10;   // RX <- master's TX (SoftwareSerial)
static const uint8_t PIN_SENSOR_TX = 11;   // TX -> master's RX (SoftwareSerial)

static const uint8_t PIN_FINGER_BIT0 = 2;  // LSB
static const uint8_t PIN_FINGER_BIT1 = 3;
static const uint8_t PIN_FINGER_BIT2 = 4;  // MSB
static const uint8_t PIN_FINGER_PRESENT = 5;

static const bool SWITCHES_ACTIVE_LOW = true;
static const uint32_t SENSOR_BAUD = 57600;
static const bool DEBUG = true;

static const bool ENFORCE_PASSWORD = false;

// Kept modest for 2 KB RAM boards (uses 1 byte per slot).
static const uint16_t MAX_TEMPLATES = 200;

static const uint32_t DEVICE_ADDRESS = 0xFFFFFFFFUL;

// Packet payload chunk size (128 bytes) = max accepted data payload.
static const uint16_t CHUNK_SIZE = 128;
static const uint16_t MAX_PAYLOAD = 128;
// ===========================================================================

#if USE_SOFTWARE_SERIAL
  #include <SoftwareSerial.h>
  SoftwareSerial SensorSerial(PIN_SENSOR_RX, PIN_SENSOR_TX);
#else
  #define SensorSerial Serial1
#endif

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------
static const uint16_t PACKET_HEADER = 0xEF01;

enum PacketType : uint8_t {
  PTYPE_COMMAND = 0x01,
  PTYPE_DATA    = 0x02,
  PTYPE_ACK     = 0x07,
  PTYPE_ENDDATA = 0x08
};

enum Command : uint8_t {
  CMD_GETIMAGE        = 0x01,
  CMD_IMAGE2TZ        = 0x02,
  CMD_MATCH           = 0x03,
  CMD_SEARCH          = 0x04,
  CMD_REGMODEL        = 0x05,
  CMD_STORE           = 0x06,
  CMD_LOADCHAR        = 0x07,
  CMD_UPCHAR          = 0x08,
  CMD_DOWNCHAR        = 0x09,
  CMD_DELETE          = 0x0C,
  CMD_EMPTY           = 0x0D,
  CMD_READSYSPARAM    = 0x0F,
  CMD_SETPASSWORD     = 0x12,
  CMD_VERIFYPASSWORD  = 0x13,
  CMD_HISPEEDSEARCH   = 0x1B,
  CMD_TEMPLATECOUNT   = 0x1D,
  CMD_AURACONTROL     = 0x35,
  CMD_GETECHO         = 0x40
};

enum Status : uint8_t {
  FP_OK              = 0x00,
  ERR_PACKETRECEIVE  = 0x01,
  ERR_NOFINGER       = 0x02,
  ERR_IMAGEFAIL      = 0x03,
  ERR_FEATUREFAIL    = 0x07,
  ERR_NOMATCH        = 0x08,
  ERR_NOTFOUND       = 0x09,
  ERR_ENROLLMISMATCH = 0x0A,
  ERR_BADLOCATION    = 0x0B,
  ERR_PASSFAIL       = 0x13
};

// ---------------------------------------------------------------------------
// Fake fingerprint database (0xFF = empty)
// ---------------------------------------------------------------------------
uint8_t templateCode[MAX_TEMPLATES];

int16_t buffer1 = -1;
int16_t buffer2 = -1;
int16_t modelCode = -1;
int16_t lastCapturedCode = -1;

// DOWNCHAR transaction state. The 256 bytes are validated on arrival, so no
// 256-byte storage is needed: the template format is [code, 0 x 255].
bool downloadActive = false;
uint16_t downloadLength = 0;
uint8_t downloadCode = 0;
bool downloadBad = false;
uint8_t pendingDownloadSlot = 0;
uint32_t downloadStartedAt = 0;
uint32_t downloadLastPacketAt = 0;
static const uint32_t DOWNLOAD_TOTAL_TIMEOUT_MS = 5000;
static const uint32_t DOWNLOAD_PACKET_TIMEOUT_MS = 2000;

uint32_t expectedPassword = 0x00000000UL;
bool authenticated = false;

// ---------------------------------------------------------------------------
// Debug helpers (replacement for Serial.printf)
// ---------------------------------------------------------------------------
void printHex(uint32_t v, uint8_t digits) {
  for (int8_t i = digits - 1; i >= 0; --i) {
    Serial.print((v >> (i * 4)) & 0xF, HEX);
  }
}

void debugHex(const uint8_t *data, uint16_t len) {
  if (!DEBUG) return;
  for (uint16_t i = 0; i < len; ++i) {
    printHex(data[i], 2);
    Serial.print(' ');
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
bool switchOn(uint8_t pin) {
  const int level = digitalRead(pin);
  return SWITCHES_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

uint8_t readFingerCode() {
  uint8_t code = 0;
  if (switchOn(PIN_FINGER_BIT0)) code |= 0x01;
  if (switchOn(PIN_FINGER_BIT1)) code |= 0x02;
  if (switchOn(PIN_FINGER_BIT2)) code |= 0x04;
  return code;
}

bool fingerPresent() {
  return switchOn(PIN_FINGER_PRESENT);
}

// ---------------------------------------------------------------------------
// Outgoing packets
// ---------------------------------------------------------------------------
void sendPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen) {
  const uint16_t length = payloadLen + 2;

  uint16_t checksum = packetType + (uint16_t)(length >> 8) + (uint16_t)(length & 0xFF);
  for (uint16_t i = 0; i < payloadLen; ++i) checksum += payload[i];

  SensorSerial.write((uint8_t)(PACKET_HEADER >> 8));
  SensorSerial.write((uint8_t)(PACKET_HEADER & 0xFF));

  SensorSerial.write((uint8_t)(DEVICE_ADDRESS >> 24));
  SensorSerial.write((uint8_t)(DEVICE_ADDRESS >> 16));
  SensorSerial.write((uint8_t)(DEVICE_ADDRESS >> 8));
  SensorSerial.write((uint8_t)(DEVICE_ADDRESS));

  SensorSerial.write(packetType);
  SensorSerial.write((uint8_t)(length >> 8));
  SensorSerial.write((uint8_t)(length & 0xFF));

  for (uint16_t i = 0; i < payloadLen; ++i) SensorSerial.write(payload[i]);

  SensorSerial.write((uint8_t)(checksum >> 8));
  SensorSerial.write((uint8_t)(checksum & 0xFF));

  SensorSerial.flush();

  // Only dump small packets to keep debug output from stalling data streams.
  if (DEBUG && payloadLen <= 16) {
    Serial.print(F("TX type=0x"));
    printHex(packetType, 2);
    Serial.print(F(" payloadLen="));
    Serial.print(payloadLen);
    Serial.print(F(": "));
    debugHex(payload, payloadLen);
  }
}

void sendAckPacket(const uint8_t *payload, uint16_t payloadLen) {
  sendPacket(PTYPE_ACK, payload, payloadLen);
}

void sendSimpleStatus(uint8_t status) {
  const uint8_t payload[] = { status };
  sendAckPacket(payload, sizeof(payload));
}

// Streams `copies` x 256-byte fake templates ([code, 0 x 255]) in CHUNK_SIZE
// pieces, generated on the fly. Final chunk is PTYPE_ENDDATA.
void streamTemplate(uint8_t code, uint8_t copies) {
  const uint16_t total = (uint16_t)256 * copies;
  uint8_t chunk[CHUNK_SIZE];
  uint16_t offset = 0;

  while (offset < total) {
    const uint16_t remaining = total - offset;
    const uint16_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    const uint8_t pid = (offset + chunkLen == total) ? PTYPE_ENDDATA : PTYPE_DATA;

    for (uint16_t i = 0; i < chunkLen; ++i) {
      chunk[i] = (((offset + i) & 0xFF) == 0) ? code : 0x00;
    }

    sendPacket(pid, chunk, chunkLen);
    offset += chunkLen;
  }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
void handleGetEcho() {
  if (DEBUG) Serial.println(F("GetEcho"));
  sendSimpleStatus(FP_OK);
}

void handleSetPassword(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 4) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  expectedPassword =
      ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
      ((uint32_t)data[2] << 8)  |  (uint32_t)data[3];
  authenticated = false;

  if (DEBUG) {
    Serial.print(F("SetPassword: updated to 0x"));
    printHex(expectedPassword, 8);
    Serial.println();
  }

  sendSimpleStatus(FP_OK);
}

void handleVerifyPassword(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 4) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint32_t pw =
      ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
      ((uint32_t)data[2] << 8)  |  (uint32_t)data[3];

  const bool ok = (pw == expectedPassword);

  if (DEBUG) {
    Serial.print(F("VerifyPassword: "));
    Serial.println(ok ? F("OK") : F("FAIL"));
  }

  authenticated = ok;
  sendSimpleStatus(ok ? FP_OK : ERR_PASSFAIL);
}

void handleGetImage() {
  if (!fingerPresent()) {
    lastCapturedCode = -1;
    if (DEBUG) Serial.println(F("GetImage: NO FINGER"));
    sendSimpleStatus(ERR_NOFINGER);
    return;
  }

  lastCapturedCode = readFingerCode();

  if (DEBUG) {
    Serial.print(F("GetImage: finger present, code="));
    Serial.println(lastCapturedCode);
  }

  sendSimpleStatus(FP_OK);
}

void handleImage2Tz(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 1) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t slot = data[0];
  if (slot != 1 && slot != 2) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  if (lastCapturedCode < 0) {
    sendSimpleStatus(ERR_IMAGEFAIL);
    return;
  }

  if (slot == 1) buffer1 = lastCapturedCode;
  else           buffer2 = lastCapturedCode;

  if (DEBUG) {
    Serial.print(F("Image2Tz("));
    Serial.print(slot);
    Serial.print(F("): code="));
    Serial.println(lastCapturedCode);
  }

  lastCapturedCode = -1;
  sendSimpleStatus(FP_OK);
}

void handleRegModelSymmetric() {
  if (buffer1 < 0 || buffer2 < 0) {
    sendSimpleStatus(ERR_FEATUREFAIL);
    return;
  }

  if (buffer1 != buffer2) {
    if (DEBUG) {
      Serial.print(F("RegModel REJECTED: Buffer1 ("));
      Serial.print(buffer1);
      Serial.print(F(") != Buffer2 ("));
      Serial.print(buffer2);
      Serial.println(F(")"));
    }
    sendSimpleStatus(ERR_ENROLLMISMATCH);
    return;
  }

  modelCode = buffer1;
  buffer1 = -1;
  buffer2 = -1;

  if (DEBUG) {
    Serial.print(F("RegModel OK: Code="));
    Serial.println(modelCode);
  }
  sendSimpleStatus(FP_OK);
}

void handleMatch() {
  if (buffer1 < 0 || buffer2 < 0) {
    sendSimpleStatus(ERR_FEATUREFAIL);
    return;
  }

  const uint8_t code1 = (uint8_t)(buffer1 & 0x07);
  const uint8_t code2 = (uint8_t)(buffer2 & 0x07);

  if (code1 == code2) {
    const uint8_t payload[] = { FP_OK, 0x00, 0x64 };
    sendAckPacket(payload, sizeof(payload));
  } else {
    sendSimpleStatus(ERR_NOMATCH);
  }
}

// ---------------------------------------------------------------------------
// UPCHAR — template export
//   256-byte (RAM):   params = [bufferId] or [bufferId, 0x01]
//   512-byte (Flash): params = [0x02, idHi, idLo]  (enrolled templates only)
// ---------------------------------------------------------------------------
void handleUpChar(const uint8_t *params, uint16_t paramLen) {
  if (params == nullptr || (paramLen != 1 && paramLen != 2 && paramLen != 3)) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  // --- 512-byte path: Flash only ---
  if (paramLen == 3) {
    if (params[0] != 0x02) {
      sendSimpleStatus(ERR_PACKETRECEIVE);
      return;
    }

    const uint16_t flashId = ((uint16_t)params[1] << 8) | (uint16_t)params[2];

    if (flashId >= MAX_TEMPLATES) {
      if (DEBUG) {
        Serial.print(F("UpChar(512): Flash ID out of range: "));
        Serial.println(flashId);
      }
      sendSimpleStatus(ERR_BADLOCATION);
      return;
    }

    if (templateCode[flashId] == 0xFF) {
      if (DEBUG) {
        Serial.print(F("UpChar(512): Flash ID empty: "));
        Serial.println(flashId);
      }
      sendSimpleStatus(ERR_FEATUREFAIL);
      return;
    }

    const uint8_t code = templateCode[flashId];

    if (DEBUG) {
      Serial.print(F("UpChar(512): Flash ID="));
      Serial.print(flashId);
      Serial.print(F(" code="));
      Serial.println(code);
    }

    sendSimpleStatus(FP_OK);
    streamTemplate(code, 2);
    return;
  }

  // --- 256-byte path: RAM working buffer ---
  const uint8_t bufferId = params[0];

  if (paramLen == 2 && params[1] != 0x01) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  uint8_t code = 0;
  if (bufferId == 1) {
    if (buffer1 < 0) { sendSimpleStatus(ERR_FEATUREFAIL); return; }
    code = (uint8_t)(buffer1 & 0x07);
  } else if (bufferId == 2) {
    if (buffer2 < 0) { sendSimpleStatus(ERR_FEATUREFAIL); return; }
    code = (uint8_t)(buffer2 & 0x07);
  } else {
    sendSimpleStatus(ERR_BADLOCATION);
    return;
  }

  if (DEBUG) {
    Serial.print(F("UpChar(256): CharBuffer"));
    Serial.print(bufferId);
    Serial.print(F(" code="));
    Serial.println(code);
  }

  sendSimpleStatus(FP_OK);
  streamTemplate(code, 1);
}

// ---------------------------------------------------------------------------
// DOWNCHAR transfer (UART is only ever read by readIncomingPackets())
// ---------------------------------------------------------------------------
void beginDownChar(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 1) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t slot = data[0];
  if (slot != 1 && slot != 2) {
    sendSimpleStatus(ERR_BADLOCATION);
    return;
  }

  if (downloadActive) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  downloadActive = true;
  downloadLength = 0;
  downloadCode = 0;
  downloadBad = false;
  downloadStartedAt = millis();
  downloadLastPacketAt = downloadStartedAt;
  pendingDownloadSlot = slot;

  if (DEBUG) {
    Serial.print(F("DownChar("));
    Serial.print(slot);
    Serial.println(F("): waiting for DATA/ENDDATA"));
  }

  sendSimpleStatus(FP_OK);
}

void finishDownChar(uint8_t status) {
  const uint8_t slot = pendingDownloadSlot;

  if (status == FP_OK) {
    // Valid fake template: exactly 256 bytes, first byte 0..7, rest zero.
    if (downloadLength != 256 || downloadBad || downloadCode > 0x07) {
      status = ERR_PACKETRECEIVE;
    }
  }

  if (status == FP_OK) {
    if (slot == 1)      buffer1 = downloadCode;
    else if (slot == 2) buffer2 = downloadCode;
    else                status = ERR_BADLOCATION;

    if (status == FP_OK && DEBUG) {
      Serial.print(F("DownChar("));
      Serial.print(slot);
      Serial.print(F("): valid 256B signature, code="));
      Serial.println(downloadCode);
    }
  } else if (DEBUG) {
    Serial.print(F("DownChar("));
    Serial.print(slot);
    Serial.print(F("): rejected, status=0x"));
    printHex(status, 2);
    Serial.print(F(", received="));
    Serial.println(downloadLength);
  }

  downloadActive = false;
  downloadLength = 0;
  downloadCode = 0;
  downloadBad = false;
  pendingDownloadSlot = 0;
  downloadStartedAt = 0;
  downloadLastPacketAt = 0;

  sendSimpleStatus(status);
}

void handleIncomingDataPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen) {
  if (!downloadActive) {
    if (DEBUG) {
      Serial.print(F("Ignoring unexpected data packet type=0x"));
      printHex(packetType, 2);
      Serial.println();
    }
    return;
  }

  if (payload == nullptr || payloadLen == 0 || payloadLen > CHUNK_SIZE) {
    finishDownChar(ERR_PACKETRECEIVE);
    return;
  }

  if ((uint32_t)downloadLength + payloadLen > 256) {
    finishDownChar(ERR_PACKETRECEIVE);
    return;
  }

  // Validate on the fly instead of buffering.
  for (uint16_t i = 0; i < payloadLen; ++i) {
    const uint16_t idx = downloadLength + i;
    if (idx == 0) downloadCode = payload[i];
    else if (payload[i] != 0x00) downloadBad = true;
  }

  downloadLength += payloadLen;
  downloadLastPacketAt = millis();

  if (DEBUG) {
    Serial.print(F("DownChar RX: payload="));
    Serial.print(payloadLen);
    Serial.print(F(" total="));
    Serial.println(downloadLength);
  }

  if (packetType == PTYPE_ENDDATA) {
    finishDownChar(FP_OK);
  }
}

void handleStore(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 3) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t slot = data[0];
  const uint16_t id = ((uint16_t)data[1] << 8) | (uint16_t)data[2];

  if (id >= MAX_TEMPLATES) {
    sendSimpleStatus(ERR_BADLOCATION);
    return;
  }

  if (slot != 1) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  if (modelCode < 0) {
    sendSimpleStatus(ERR_FEATUREFAIL);
    return;
  }

  templateCode[id] = (uint8_t)modelCode;
  modelCode = -1;

  if (DEBUG) {
    Serial.print(F("Store: ID="));
    Serial.print(id);
    Serial.print(F(" <- code="));
    Serial.println(templateCode[id]);
  }

  sendSimpleStatus(FP_OK);
}

void handleLoadChar(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 3) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t slot = data[0];
  const uint16_t id = ((uint16_t)data[1] << 8) | (uint16_t)data[2];

  if (slot != 1 && slot != 2) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  if (id >= MAX_TEMPLATES || templateCode[id] == 0xFF) {
    sendSimpleStatus(ERR_NOTFOUND);
    return;
  }

  if (slot == 1) buffer1 = templateCode[id];
  else           buffer2 = templateCode[id];

  if (DEBUG) {
    Serial.print(F("LoadChar: buffer"));
    Serial.print(slot);
    Serial.print(F(" <- ID="));
    Serial.print(id);
    Serial.print(F(" code="));
    Serial.println(templateCode[id]);
  }

  sendSimpleStatus(FP_OK);
}

void handleDelete(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 4) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint16_t startId = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
  const uint16_t count   = ((uint16_t)data[2] << 8) | (uint16_t)data[3];

  if (count == 0 || startId >= MAX_TEMPLATES || (uint32_t)startId + count > MAX_TEMPLATES) {
    sendSimpleStatus(ERR_BADLOCATION);
    return;
  }

  for (uint16_t i = 0; i < count; ++i) {
    templateCode[startId + i] = 0xFF;
  }

  if (DEBUG) {
    Serial.print(F("Delete: start="));
    Serial.print(startId);
    Serial.print(F(" count="));
    Serial.println(count);
  }

  sendSimpleStatus(FP_OK);
}

void handleEmpty() {
  for (uint16_t i = 0; i < MAX_TEMPLATES; ++i) templateCode[i] = 0xFF;

  buffer1 = -1;
  buffer2 = -1;
  modelCode = -1;
  lastCapturedCode = -1;
  downloadActive = false;
  downloadLength = 0;
  downloadCode = 0;
  downloadBad = false;
  pendingDownloadSlot = 0;

  if (DEBUG) Serial.println(F("Empty: database cleared"));

  sendSimpleStatus(FP_OK);
}

void handleTemplateCount() {
  uint16_t count = 0;
  for (uint16_t i = 0; i < MAX_TEMPLATES; ++i) {
    if (templateCode[i] != 0xFF) ++count;
  }

  const uint8_t payload[] = { FP_OK, (uint8_t)(count >> 8), (uint8_t)(count & 0xFF) };

  if (DEBUG) {
    Serial.print(F("TemplateCount: "));
    Serial.println(count);
  }

  sendAckPacket(payload, sizeof(payload));
}

void handleReadSysParam() {
  const uint16_t capacity = MAX_TEMPLATES;

  const uint8_t payload[17] = {
    FP_OK,
    0x00, 0x00,             // status register
    0x00, 0x00,             // system ID
    (uint8_t)(capacity >> 8),
    (uint8_t)(capacity & 0xFF),
    0x00, 0x03,             // security level 3
    0xFF, 0xFF, 0xFF, 0xFF, // device address
    0x00, 0x02,             // packet size code 2 = 128 bytes
    0x00, 0x06              // baud-rate code = 57600
  };

  if (DEBUG) Serial.println(F("ReadSysParam"));

  sendAckPacket(payload, sizeof(payload));
}

void handleSearch(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 5) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t slot = data[0];
  const uint16_t startPage = ((uint16_t)data[1] << 8) | (uint16_t)data[2];
  const uint16_t pageCount = ((uint16_t)data[3] << 8) | (uint16_t)data[4];

  if (slot != 1 && slot != 2) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const int16_t targetCode = (slot == 2) ? buffer2 : buffer1;

  if (targetCode < 0) {
    const uint8_t payload[] = { ERR_IMAGEFAIL, 0x00, 0x00, 0x00, 0x00 };
    sendAckPacket(payload, sizeof(payload));
    return;
  }

  uint32_t end32 = (uint32_t)startPage + pageCount;
  if (end32 > MAX_TEMPLATES) end32 = MAX_TEMPLATES;
  const uint16_t endPage = (uint16_t)end32;

  for (uint16_t id = startPage; id < endPage; ++id) {
    if (templateCode[id] == (uint8_t)targetCode) {
      const uint16_t confidence = 150;
      const uint8_t payload[] = {
        FP_OK,
        (uint8_t)(id >> 8),
        (uint8_t)(id & 0xFF),
        (uint8_t)(confidence >> 8),
        (uint8_t)(confidence & 0xFF)
      };

      if (DEBUG) {
        Serial.print(F("Search: MATCH ID="));
        Serial.print(id);
        Serial.print(F(" code="));
        Serial.println(targetCode);
      }

      sendAckPacket(payload, sizeof(payload));
      return;
    }
  }

  if (DEBUG) {
    Serial.print(F("Search: NO MATCH code="));
    Serial.println(targetCode);
  }

  const uint8_t payload[] = { ERR_NOTFOUND, 0x00, 0x00, 0x00, 0x00 };
  sendAckPacket(payload, sizeof(payload));
}

void handleHiSpeedSearch(const uint8_t *data, uint16_t len) {
  handleSearch(data, len);
}

void handleAuraControl(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 4) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }
  if (DEBUG) Serial.println(F("Aura/LED control -> ACK"));
  sendSimpleStatus(FP_OK);
}

void checkDownCharTimeout() {
  if (!downloadActive) return;

  const uint32_t now = millis();
  if ((uint32_t)(now - downloadStartedAt) > DOWNLOAD_TOTAL_TIMEOUT_MS ||
      (uint32_t)(now - downloadLastPacketAt) > DOWNLOAD_PACKET_TIMEOUT_MS) {
    if (DEBUG) Serial.println(F("DownChar: transfer timeout"));
    finishDownChar(ERR_PACKETRECEIVE);
  }
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
void processPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen) {
  if (packetType == PTYPE_DATA || packetType == PTYPE_ENDDATA) {
    handleIncomingDataPacket(packetType, payload, payloadLen);
    return;
  }

  if (packetType != PTYPE_COMMAND) {
    if (DEBUG) Serial.println(F("Ignoring non-command packet"));
    return;
  }

  if (downloadActive) {
    if (DEBUG) Serial.println(F("Command during DOWNCHAR; rejecting"));
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  if (payloadLen == 0) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t cmd = payload[0];
  const uint8_t *args = payload + 1;
  const uint16_t argsLen = payloadLen - 1;

  if (ENFORCE_PASSWORD &&
      cmd != CMD_VERIFYPASSWORD && cmd != CMD_SETPASSWORD && cmd != CMD_GETECHO &&
      !authenticated) {
    sendSimpleStatus(ERR_PASSFAIL);
    return;
  }

  switch (cmd) {
    case CMD_GETECHO: handleGetEcho(); break;
    case CMD_SETPASSWORD: handleSetPassword(args, argsLen); break;
    case CMD_VERIFYPASSWORD: handleVerifyPassword(args, argsLen); break;
    case CMD_GETIMAGE:
      if (argsLen != 0) sendSimpleStatus(ERR_PACKETRECEIVE);
      else handleGetImage();
      break;
    case CMD_IMAGE2TZ: handleImage2Tz(args, argsLen); break;
    case CMD_MATCH:
      if (argsLen != 0) sendSimpleStatus(ERR_PACKETRECEIVE);
      else handleMatch();
      break;
    case CMD_REGMODEL:
      if (argsLen != 0) sendSimpleStatus(ERR_PACKETRECEIVE);
      else handleRegModelSymmetric();
      break;
    case CMD_UPCHAR: handleUpChar(args, argsLen); break;
    case CMD_DOWNCHAR: beginDownChar(args, argsLen); break;
    case CMD_STORE: handleStore(args, argsLen); break;
    case CMD_LOADCHAR: handleLoadChar(args, argsLen); break;
    case CMD_DELETE: handleDelete(args, argsLen); break;
    case CMD_EMPTY:
      if (argsLen != 0) sendSimpleStatus(ERR_PACKETRECEIVE);
      else handleEmpty();
      break;
    case CMD_READSYSPARAM:
      if (argsLen != 0) sendSimpleStatus(ERR_PACKETRECEIVE);
      else handleReadSysParam();
      break;
    case CMD_TEMPLATECOUNT:
      if (argsLen != 0) sendSimpleStatus(ERR_PACKETRECEIVE);
      else handleTemplateCount();
      break;
    case CMD_SEARCH: handleSearch(args, argsLen); break;
    case CMD_HISPEEDSEARCH: handleHiSpeedSearch(args, argsLen); break;
    case CMD_AURACONTROL: handleAuraControl(args, argsLen); break;
    default:
      if (DEBUG) {
        Serial.print(F("Unhandled command 0x"));
        printHex(cmd, 2);
        Serial.println();
      }
      sendSimpleStatus(ERR_PACKETRECEIVE);
      break;
  }
}

// ---------------------------------------------------------------------------
// Incoming packet parser (sole consumer of SensorSerial)
// ---------------------------------------------------------------------------
void readIncomingPackets() {
  static uint8_t state = 0;
  static uint8_t packetType = 0;
  static uint16_t packetLength = 0;
  static uint16_t payloadLength = 0;
  static uint32_t packetAddress = 0;
  static uint16_t checksum = 0;
  static uint8_t payload[MAX_PAYLOAD];
  static uint16_t payloadIndex = 0;
  static uint8_t checksumHi = 0;

  while (SensorSerial.available()) {
    const uint8_t b = (uint8_t)SensorSerial.read();

    switch (state) {
      case 0:
        if (b == 0xEF) state = 1;
        break;

      case 1:
        if (b == 0x01) {
          packetAddress = 0;
          payloadIndex = 0;
          state = 2;
        } else if (b != 0xEF) {
          state = 0;
        }
        break;

      case 2:
        packetAddress = (packetAddress << 8) | b;
        if (++payloadIndex == 4) {
          payloadIndex = 0;
          state = 3;
        }
        break;

      case 3:
        packetType = b;
        state = 4;
        break;

      case 4:
        packetLength = (uint16_t)b << 8;
        state = 5;
        break;

      case 5:
        packetLength |= b;
        if (packetLength < 2 || packetLength > MAX_PAYLOAD + 2) {
          if (DEBUG) {
            Serial.print(F("RX invalid length="));
            Serial.println(packetLength);
          }
          state = 0;
          payloadIndex = 0;
          break;
        }

        payloadLength = packetLength - 2;
        payloadIndex = 0;
        checksum = packetType + (uint16_t)(packetLength >> 8) + (uint16_t)(packetLength & 0xFF);
        state = (payloadLength == 0) ? 7 : 6;
        break;

      case 6:
        payload[payloadIndex++] = b;
        checksum += b;
        if (payloadIndex >= payloadLength) state = 7;
        break;

      case 7:
        checksumHi = b;
        state = 8;
        break;

      case 8: {
        const uint16_t receivedChecksum = ((uint16_t)checksumHi << 8) | b;

        if (DEBUG) {
          Serial.print(F("RX addr="));
          printHex(packetAddress, 8);
          Serial.print(F(" type=0x"));
          printHex(packetType, 2);
          Serial.print(F(" len="));
          Serial.print(packetLength);
          Serial.print(F(" chk="));
          printHex(checksum, 4);
          Serial.print('/');
          printHex(receivedChecksum, 4);
          Serial.println();
        }

        const bool addressOK  = (packetAddress == DEVICE_ADDRESS);
        const bool checksumOK = (receivedChecksum == checksum);

        if (!addressOK) {
          if (DEBUG) Serial.println(F("RX rejected: address mismatch"));
          if (downloadActive && (packetType == PTYPE_DATA || packetType == PTYPE_ENDDATA)) {
            finishDownChar(ERR_PACKETRECEIVE);
          }
        } else if (!checksumOK) {
          if (DEBUG) Serial.println(F("RX rejected: checksum mismatch"));
          if (downloadActive && (packetType == PTYPE_DATA || packetType == PTYPE_ENDDATA)) {
            finishDownChar(ERR_PACKETRECEIVE);
          }
        } else {
          processPacket(packetType, payload, payloadLength);
        }

        state = 0;
        payloadIndex = 0;
        packetLength = 0;
        payloadLength = 0;
        packetAddress = 0;
        checksum = 0;
        break;
      }

      default:
        state = 0;
        payloadIndex = 0;
        break;
    }
  }
}

// ---------------------------------------------------------------------------
// Setup & Loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  const uint8_t switchMode = SWITCHES_ACTIVE_LOW ? INPUT_PULLUP : INPUT;

  pinMode(PIN_FINGER_BIT0, switchMode);
  pinMode(PIN_FINGER_BIT1, switchMode);
  pinMode(PIN_FINGER_BIT2, switchMode);
  pinMode(PIN_FINGER_PRESENT, switchMode);

  for (uint16_t i = 0; i < MAX_TEMPLATES; ++i) templateCode[i] = 0xFF;

  authenticated = false;
  downloadActive = false;
  downloadLength = 0;
  pendingDownloadSlot = 0;

  SensorSerial.begin(SENSOR_BAUD);

  if (DEBUG) {
    Serial.println(F("\n=============================================="));
    Serial.println(F(" Fake Fingerprint Sensor - Arduino"));
    Serial.println(F("=============================================="));
    Serial.print(F("Baud: "));
    Serial.print(SENSOR_BAUD);
    Serial.print(F(" | Templates: "));
    Serial.println(MAX_TEMPLATES);
    Serial.println(F("Ready."));
  }
}

void loop() {
  readIncomingPackets();
  checkDownCharTimeout();
}
