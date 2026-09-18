/*
  Fake Fingerprint Sensor — ESP32 (Wokwi)
  ---------------------------------------------------------------------------
  Emulates the binary UART protocol used by Adafruit_Fingerprint.h
  (AS608 / R30x-style protocol) without a real fingerprint sensor.

  Includes support for 256-byte signature transfer via UPCHAR/DOWNCHAR.

  Connect this ESP32's UART pins to a second board running a normal
  Adafruit_Fingerprint sketch.

  Four switches control the simulated finger:
    - BIT0, BIT1, BIT2 = 3-bit finger identity (0..7)
    - FINGER_PRESENT   = finger currently touching the sensor

  Default wiring:
    Fake sensor RX  = GPIO16
    Fake sensor TX  = GPIO17
    Finger BIT0     = GPIO32
    Finger BIT1     = GPIO33
    Finger BIT2     = GPIO25
    Finger present  = GPIO26

  Switches are active LOW and use INPUT_PULLUP.
  ---------------------------------------------------------------------------
*/

#include <Arduino.h>

// ============================= USER CONFIG ================================
static const int PIN_SENSOR_RX = 16;       // RX <- master's TX
static const int PIN_SENSOR_TX = 17;       // TX -> master's RX

static const int PIN_FINGER_BIT0 = 32;     // LSB
static const int PIN_FINGER_BIT1 = 33;
static const int PIN_FINGER_BIT2 = 25;     // MSB
static const int PIN_FINGER_PRESENT = 26;

static const bool SWITCHES_ACTIVE_LOW = true;
static const uint32_t SENSOR_BAUD = 57600;
static const bool DEBUG = true;

// When enabled, commands other than password verification are rejected until
// the master has successfully verified the configured password.
static const bool ENFORCE_PASSWORD = false;

// Fake database size. Typical Adafruit sketches use IDs in the 0..N-1 range.
static const uint16_t MAX_TEMPLATES = 200;

// Address used by most AS608/R30x modules.
static const uint32_t DEVICE_ADDRESS = 0xFFFFFFFF;

// Maximum accepted packet length field.
static const uint16_t MAX_PACKET_LENGTH = 256;

// Standard chunk size for packet streaming (configured as 128 bytes)
static const uint16_t CHUNK_SIZE = 128;
// ===========================================================================

HardwareSerial SensorSerial(2);

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
  CMD_MATCH           = 0x03, // 1:1 comparison of CharBuffer1 and CharBuffer2
  CMD_SEARCH          = 0x04,
  CMD_REGMODEL        = 0x05,
  CMD_STORE           = 0x06,
  CMD_LOADCHAR        = 0x07,
  CMD_DOWNCHAR        = 0x09, // Master uploads 256-byte buffer to sensor
  CMD_UPCHAR          = 0x08, // Master downloads 256-byte buffer from sensor
  CMD_DELETE          = 0x0C,
  CMD_EMPTY           = 0x0D,
  CMD_READSYSPARAM    = 0x0F,
  CMD_SETPASSWORD     = 0x12,
  CMD_VERIFYPASSWORD  = 0x13,
  CMD_HISPEEDSEARCH   = 0x1B,
  CMD_TEMPLATECOUNT   = 0x1D,

  // Adafruit_Fingerprint uses LED control command 0x35.
  CMD_AURACONTROL     = 0x35,

  // Echo test command
  CMD_GETECHO         = 0x40
};

enum Status : uint8_t {
  OK                 = 0x00,
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
// Fake fingerprint database
// Each occupied ID stores one of the 8 switch-selected finger identities.
// 0xFF means empty.
// ---------------------------------------------------------------------------
uint8_t templateCode[MAX_TEMPLATES];

// Character buffers. In this simulator they contain the finger identity
// rather than an actual fingerprint feature set.
int16_t buffer1 = -1;
int16_t buffer2 = -1;

// Model staging buffer (holds merged result of REGMODEL prior to STORE)
int16_t modelCode = -1;

// Internal 256-byte raw signature buffers
uint8_t rawBuffer1[256];
uint8_t rawBuffer2[256];

// DELIBERATE FAKE-SENSOR FEATURE -- DO NOT REMOVE:
// Internal 512-byte representation is a 256-byte fake template duplicated
// twice. This is intentionally different from a genuine fingerprint
// template and lets the simulator recognize its own deterministic format.
uint8_t rawTemplate512[512];

// Most recent successful GETIMAGE result.
int16_t lastCapturedCode = -1;

// Single-owner UART download transaction state. No command handler reads
// SensorSerial directly; readIncomingPackets() is the only UART consumer.
bool downloadActive = false;
uint16_t downloadLength = 0;
uint8_t downloadBuffer[256];
uint8_t pendingDownloadSlot = 0;
uint32_t downloadStartedAt = 0;
uint32_t downloadLastPacketAt = 0;
static const uint32_t DOWNLOAD_TOTAL_TIMEOUT_MS = 5000;
static const uint32_t DOWNLOAD_PACKET_TIMEOUT_MS = 2000;

// Sensor password. Adafruit_Fingerprint defaults to 0x00000000.
uint32_t expectedPassword = 0x00000000;
bool authenticated = false;

// Forward declarations
void handleIncomingDataPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen);
bool isValidFakeTemplate256(const uint8_t *data);

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
bool switchOn(int pin) {
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

void debugHex(const uint8_t *data, uint16_t len) {
  if (!DEBUG) return;

  for (uint16_t i = 0; i < len; ++i) {
    Serial.printf("%02X ", data[i]);
  }
  Serial.println();
}

// ---------------------------------------------------------------------------
// Outgoing packet creation & streaming
// ---------------------------------------------------------------------------
void sendPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen) {
  const uint16_t length = payloadLen + 2;

  uint16_t checksum =
      packetType +
      (uint16_t)(length >> 8) +
      (uint16_t)(length & 0xFF);

  for (uint16_t i = 0; i < payloadLen; ++i) {
    checksum += payload[i];
  }

  SensorSerial.write((uint8_t)(PACKET_HEADER >> 8));
  SensorSerial.write((uint8_t)(PACKET_HEADER & 0xFF));

  SensorSerial.write((uint8_t)(DEVICE_ADDRESS >> 24));
  SensorSerial.write((uint8_t)(DEVICE_ADDRESS >> 16));
  SensorSerial.write((uint8_t)(DEVICE_ADDRESS >> 8));
  SensorSerial.write((uint8_t)(DEVICE_ADDRESS));

  SensorSerial.write(packetType);

  SensorSerial.write((uint8_t)(length >> 8));
  SensorSerial.write((uint8_t)(length & 0xFF));

  for (uint16_t i = 0; i < payloadLen; ++i) {
    SensorSerial.write(payload[i]);
  }

  SensorSerial.write((uint8_t)(checksum >> 8));
  SensorSerial.write((uint8_t)(checksum & 0xFF));

  SensorSerial.flush();

  if (DEBUG) {
    Serial.printf("TX type=0x%02X payloadLen=%u: ", packetType, payloadLen);
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

void sendDataChunk(uint8_t packetType, const uint8_t *data, uint16_t len) {
  sendPacket(packetType, data, len);
}

// Streams `len` bytes out of `data` in CHUNK_SIZE pieces, marking the final
// chunk as PTYPE_ENDDATA. Used by UPCHAR for both the 256-byte (RAM) and
// 512-byte (Flash) export paths so the chunking logic lives in one place.
void streamBytes(const uint8_t *data, uint16_t len) {
  uint16_t offset = 0;
  while (offset < len) {
    const uint16_t remaining = len - offset;
    const uint16_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    const uint8_t pid = (offset + chunkLen == len) ? PTYPE_ENDDATA : PTYPE_DATA;

    sendDataChunk(pid, data + offset, chunkLen);
    offset += chunkLen;
  }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
void handleGetEcho() {
  if (DEBUG) Serial.println("GetEcho");
  sendSimpleStatus(OK);
}

void handleSetPassword(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 4) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint32_t newPassword =
      ((uint32_t)data[0] << 24) |
      ((uint32_t)data[1] << 16) |
      ((uint32_t)data[2] << 8)  |
      (uint32_t)data[3];

  expectedPassword = newPassword;
  authenticated = false;

  if (DEBUG) {
    Serial.printf("SetPassword: password updated to 0x%08lX\n",
                  (unsigned long)expectedPassword);
  }

  sendSimpleStatus(OK);
}

void handleVerifyPassword(const uint8_t *data, uint16_t len) {
  if (data == nullptr || len != 4) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint32_t pw =
      ((uint32_t)data[0] << 24) |
      ((uint32_t)data[1] << 16) |
      ((uint32_t)data[2] << 8)  |
      (uint32_t)data[3];

  const bool ok = (pw == expectedPassword);

  if (DEBUG) {
    Serial.printf("VerifyPassword: %s\n", ok ? "OK" : "FAIL");
  }

  authenticated = ok;
  sendSimpleStatus(ok ? OK : ERR_PASSFAIL);
}

void handleGetImage() {
  if (!fingerPresent()) {
    // A failed capture must invalidate any previously captured image, so a
    // later Image2Tz() can't succeed on a stale code from an earlier,
    // already-consumed-or-not GetImage() call.
    lastCapturedCode = -1;
    if (DEBUG) Serial.println("GetImage: NO FINGER");
    sendSimpleStatus(ERR_NOFINGER);
    return;
  }

  lastCapturedCode = readFingerCode();

  if (DEBUG) {
    Serial.printf("GetImage: finger present, code=%d\n", lastCapturedCode);
  }

  sendSimpleStatus(OK);
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

  if (slot == 1) {
    buffer1 = lastCapturedCode;
  } else {
    buffer2 = lastCapturedCode;
  }

  if (DEBUG) {
    Serial.printf("Image2Tz(%u): code=%d\n", slot, lastCapturedCode);
  }

  lastCapturedCode = -1;
  sendSimpleStatus(OK);
}

void handleRegModelSymmetric() {
  if (buffer1 < 0 || buffer2 < 0) {
    sendSimpleStatus(ERR_FEATUREFAIL);
    return;
  }

  if (buffer1 != buffer2) {
    if (DEBUG) Serial.printf("RegModel REJECTED: Mismatch between Buffer1 (%d) and Buffer2 (%d)\n", buffer1, buffer2);
    sendSimpleStatus(ERR_ENROLLMISMATCH);
    return;
  }

  modelCode = buffer1;
  buffer1 = -1;
  buffer2 = -1;

  if (DEBUG) Serial.printf("RegModel OK: Model generated (Code=%d), character buffers cleared\n", modelCode);
  sendSimpleStatus(OK);
}

void handleMatch() {
  if (buffer1 < 0 || buffer2 < 0) {
    sendSimpleStatus(ERR_FEATUREFAIL);
    return;
  }

  uint8_t code1 = (uint8_t)(buffer1 & 0x07);
  uint8_t code2 = (uint8_t)(buffer2 & 0x07);

  if (code1 == code2) {
    const uint8_t payload[] = { OK, 0x00, 0x64 };
    sendAckPacket(payload, sizeof(payload));
  } else {
    sendSimpleStatus(ERR_NOMATCH);
  }
}

// ---------------------------------------------------------------------------
// UPCHAR — template export
//
// Two independent sources, deliberately kept separate:
//
//   256-byte request  (RAM / single scan)
//     params = [bufferId]              (1 byte:  bufferId = 1 or 2)
//     params = [bufferId, 0x01]        (2 bytes: explicit "give me 256" form)
//     Reads whichever CharBuffer (1 or 2) is currently populated in RAM.
//     This is exactly what a real sensor's UPCHAR does with the working
//     buffer, and it is unrelated to anything stored in Flash.
//
//   512-byte request  (enrolled Flash template ONLY)
//     params = [0x02, idHi, idLo]      (3 bytes: length code + 16-bit Flash ID)
//     This is NEVER generated on the fly from buffer1/buffer2. It is only
//     ever a byte-for-byte replay of a template that was already verified
//     (two matching scans via REGMODEL) and committed to templateCode[] via
//     STORE. If the ID is out of range or the slot is empty, the request
//     fails — there is no fallback to RAM.
//
// Any other parameter shape is a malformed request.
// ---------------------------------------------------------------------------
void handleUpChar(const uint8_t *params, uint16_t paramLen) {
  if (params == nullptr || (paramLen != 1 && paramLen != 2 && paramLen != 3)) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  // --- 512-byte path: strictly Flash-only, never synthesized from RAM. ---
  if (paramLen == 3) {
    if (params[0] != 0x02) {
      // Only the 512-byte length code carries a Flash ID in this form.
      sendSimpleStatus(ERR_PACKETRECEIVE);
      return;
    }

    const uint16_t flashId = ((uint16_t)params[1] << 8) | (uint16_t)params[2];

    if (flashId >= MAX_TEMPLATES) {
      if (DEBUG) Serial.printf("UpChar(512): Flash ID %u out of range\n", flashId);
      sendSimpleStatus(ERR_BADLOCATION);
      return;
    }

    if (templateCode[flashId] == 0xFF) {
      if (DEBUG) Serial.printf("UpChar(512): Flash ID %u is empty/unpopulated\n", flashId);
      sendSimpleStatus(ERR_FEATUREFAIL);
      return;
    }

    const uint8_t code = templateCode[flashId];

    uint8_t template256[256];
    memset(template256, 0, sizeof(template256));
    template256[0] = code;

    memcpy(rawTemplate512, template256, 256);
    memcpy(rawTemplate512 + 256, template256, 256);

    if (DEBUG) Serial.printf("UpChar(512): serving enrolled Flash ID=%u code=%u\n", flashId, code);

    sendSimpleStatus(OK);
    streamBytes(rawTemplate512, 512);
    return;
  }

  // --- 256-byte path: RAM working buffer (CharBuffer1 / CharBuffer2). ---
  const uint8_t bufferId = params[0];

  if (paramLen == 2 && params[1] != 0x01) {
    // The 2-byte form only exists to explicitly request 256 bytes.
    // A 512 request must use the 3-byte Flash-ID form above.
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  uint8_t code = 0;
  if (bufferId == 1) {
    if (buffer1 < 0) {
      sendSimpleStatus(ERR_FEATUREFAIL);
      return;
    }
    code = (uint8_t)(buffer1 & 0x07);
  } else if (bufferId == 2) {
    if (buffer2 < 0) {
      sendSimpleStatus(ERR_FEATUREFAIL);
      return;
    }
    code = (uint8_t)(buffer2 & 0x07);
  } else {
    sendSimpleStatus(ERR_BADLOCATION);
    return;
  }

  uint8_t template256[256];
  memset(template256, 0, sizeof(template256));
  template256[0] = code;

  if (DEBUG) Serial.printf("UpChar(256): serving RAM CharBuffer%u code=%u\n", bufferId, code);

  sendSimpleStatus(OK);
  streamBytes(template256, 256);
}

// ---------------------------------------------------------------------------
// DOWNCHAR transfer
// ---------------------------------------------------------------------------
// The command handler only arms the transfer.  It NEVER reads SensorSerial.
// Subsequent DATA/ENDDATA packets are consumed by readIncomingPackets(),
// which keeps UART ownership in exactly one place.
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
  downloadStartedAt = millis();
  downloadLastPacketAt = downloadStartedAt;

  if (DEBUG) Serial.printf("DownChar(%u): waiting for DATA/ENDDATA packets\n", slot);

  // Keep the destination slot in a stable command-state variable.
  // The actual bytes are stored in the shared transfer buffer below.
  pendingDownloadSlot = slot;
  memset(downloadBuffer, 0, sizeof(downloadBuffer));

  sendSimpleStatus(OK);
}

void finishDownChar(uint8_t status) {
  const uint8_t slot = pendingDownloadSlot;

  if (status == OK) {
    if (downloadLength != 256 || !isValidFakeTemplate256(downloadBuffer)) {
      status = ERR_PACKETRECEIVE;
    }
  }

  if (status == OK) {
    const uint8_t extractedCode = downloadBuffer[0];
    if (slot == 1) {
      buffer1 = extractedCode;
      memcpy(rawBuffer1, downloadBuffer, sizeof(rawBuffer1));
    } else if (slot == 2) {
      buffer2 = extractedCode;
      memcpy(rawBuffer2, downloadBuffer, sizeof(rawBuffer2));
    } else {
      status = ERR_BADLOCATION;
    }

    if (status == OK && DEBUG) {
      Serial.printf("DownChar(%u): Valid 256B signature, code=%u\n", slot, extractedCode);
    }
  } else if (DEBUG) {
    Serial.printf("DownChar(%u): rejected, status=0x%02X, received=%u\n",
                  slot, status, downloadLength);
  }

  downloadActive = false;
  downloadLength = 0;
  pendingDownloadSlot = 0;
  downloadStartedAt = 0;
  downloadLastPacketAt = 0;
  memset(downloadBuffer, 0, sizeof(downloadBuffer));

  sendSimpleStatus(status);
}

void handleIncomingDataPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen) {
  if (!downloadActive) {
    if (DEBUG) Serial.printf("Ignoring unexpected data packet type=0x%02X len=%u\n",
                             packetType, payloadLen);
    return;
  }

  if (payload == nullptr || payloadLen == 0 || payloadLen > CHUNK_SIZE) {
    finishDownChar(ERR_PACKETRECEIVE);
    return;
  }

  if ((uint32_t)downloadLength + payloadLen > sizeof(downloadBuffer)) {
    finishDownChar(ERR_PACKETRECEIVE);
    return;
  }

  memcpy(downloadBuffer + downloadLength, payload, payloadLen);
  downloadLength += payloadLen;
  downloadLastPacketAt = millis();

  if (DEBUG) {
    Serial.printf("DownChar RX: type=0x%02X payload=%u total=%u\n",
                  packetType, payloadLen, downloadLength);
  }

  if (packetType == PTYPE_ENDDATA) {
    finishDownChar(OK);
  }
}

bool isValidFakeTemplate256(const uint8_t *data) {
  if (data == nullptr) return false;
  if (data[0] > 0x07) return false;

  for (uint16_t i = 1; i < 256; ++i) {
    if (data[i] != 0x00) return false;
  }

  return true;
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
    Serial.printf("Store: ID=%u <- modelCode=%d\n", id, templateCode[id]);
  }

  sendSimpleStatus(OK);
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

  if (slot == 1) {
    buffer1 = templateCode[id];
  } else {
    buffer2 = templateCode[id];
  }

  if (DEBUG) {
    Serial.printf("LoadChar: buffer%d <- ID=%u/code=%d\n", slot, id, templateCode[id]);
  }

  sendSimpleStatus(OK);
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

  if (DEBUG) Serial.printf("Delete: start=%u count=%u\n", startId, count);

  sendSimpleStatus(OK);
}

void handleEmpty() {
  for (uint16_t i = 0; i < MAX_TEMPLATES; ++i) {
    templateCode[i] = 0xFF;
  }

  buffer1 = -1;
  buffer2 = -1;
  modelCode = -1;
  lastCapturedCode = -1;
  memset(rawBuffer1, 0, sizeof(rawBuffer1));
  memset(rawBuffer2, 0, sizeof(rawBuffer2));
  memset(rawTemplate512, 0, sizeof(rawTemplate512));
  downloadActive = false;
  downloadLength = 0;
  pendingDownloadSlot = 0;
  memset(downloadBuffer, 0, sizeof(downloadBuffer));

  if (DEBUG) Serial.println("Empty: database cleared");

  sendSimpleStatus(OK);
}

void handleTemplateCount() {
  uint16_t count = 0;

  for (uint16_t i = 0; i < MAX_TEMPLATES; ++i) {
    if (templateCode[i] != 0xFF) ++count;
  }

  const uint8_t payload[] = {
    OK,
    (uint8_t)(count >> 8),
    (uint8_t)(count & 0xFF)
  };

  if (DEBUG) Serial.printf("TemplateCount: %u\n", count);

  sendAckPacket(payload, sizeof(payload));
}

void handleReadSysParam() {
  const uint16_t capacity = MAX_TEMPLATES;

  const uint8_t payload[17] = {
    OK,
    0x00, 0x00,             // status register
    0x00, 0x00,             // system ID
    (uint8_t)(capacity >> 8),
    (uint8_t)(capacity & 0xFF),
    0x00, 0x03,             // security level 3
    0xFF, 0xFF, 0xFF, 0xFF, // device address
    0x00, 0x02,             // packet size code 2 = 128 bytes
    0x00, 0x06              // baud-rate code = 57600
  };

  if (DEBUG) Serial.println("ReadSysParam");

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

  const uint16_t endPage = (uint16_t)min((uint32_t)startPage + pageCount, (uint32_t)MAX_TEMPLATES);

  for (uint16_t id = startPage; id < endPage; ++id) {
    if (templateCode[id] == (uint8_t)targetCode) {
      const uint16_t confidence = 150;
      const uint8_t payload[] = {
        OK,
        (uint8_t)(id >> 8),
        (uint8_t)(id & 0xFF),
        (uint8_t)(confidence >> 8),
        (uint8_t)(confidence & 0xFF)
      };

      if (DEBUG) Serial.printf("Search: MATCH ID=%u code=%d\n", id, targetCode);

      sendAckPacket(payload, sizeof(payload));
      return;
    }
  }

  if (DEBUG) Serial.printf("Search: NO MATCH code=%d\n", targetCode);

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
  if (DEBUG) Serial.printf("Aura/LED control: %u bytes -> ACK\n", len);
  sendSimpleStatus(OK);
}

void checkDownCharTimeout() {
  if (!downloadActive) return;

  const uint32_t now = millis();
  if ((uint32_t)(now - downloadStartedAt) > DOWNLOAD_TOTAL_TIMEOUT_MS ||
      (uint32_t)(now - downloadLastPacketAt) > DOWNLOAD_PACKET_TIMEOUT_MS) {
    if (DEBUG) Serial.println("DownChar: transfer timeout");
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
    if (DEBUG) Serial.printf("Ignoring non-command packet type 0x%02X\n", packetType);
    return;
  }

  if (downloadActive) {
    if (DEBUG) Serial.println("RX command while DOWNCHAR transfer is active; rejecting");
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
      if (DEBUG) Serial.printf("Unhandled command 0x%02X\n", cmd);
      sendSimpleStatus(ERR_PACKETRECEIVE);
      break;
  }
}

// ---------------------------------------------------------------------------
// Incoming packet parser
// ---------------------------------------------------------------------------
void readIncomingPackets() {
  static uint8_t state = 0;
  static uint8_t packetType = 0;
  static uint16_t packetLength = 0;
  static uint16_t payloadLength = 0;
  static uint32_t packetAddress = 0;
  static uint16_t checksum = 0;
  static uint8_t payload[MAX_PACKET_LENGTH];
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
        if (packetLength < 2 || packetLength > MAX_PACKET_LENGTH + 2) {
          if (DEBUG) Serial.printf("RX invalid length=%u\n", packetLength);
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
          Serial.printf("RX addr=%08lX type=0x%02X len=%u checksum=%04X/%04X\n",
                        (unsigned long)packetAddress, packetType, packetLength, checksum, receivedChecksum);
        }

        const bool addressOK  = (packetAddress == DEVICE_ADDRESS);
        const bool checksumOK = (receivedChecksum == checksum);

        if (!addressOK) {
          if (DEBUG) Serial.println("RX rejected: address mismatch");
          if (downloadActive && (packetType == PTYPE_DATA || packetType == PTYPE_ENDDATA)) {
            finishDownChar(ERR_PACKETRECEIVE);
          }
        } else if (!checksumOK) {
          if (DEBUG) Serial.println("RX rejected: checksum mismatch");
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

  const int switchMode = SWITCHES_ACTIVE_LOW ? INPUT_PULLUP : INPUT;

  pinMode(PIN_FINGER_BIT0, switchMode);
  pinMode(PIN_FINGER_BIT1, switchMode);
  pinMode(PIN_FINGER_BIT2, switchMode);
  pinMode(PIN_FINGER_PRESENT, switchMode);

  for (uint16_t i = 0; i < MAX_TEMPLATES; ++i) {
    templateCode[i] = 0xFF;
  }

  authenticated = false;
  downloadActive = false;
  downloadLength = 0;
  pendingDownloadSlot = 0;
  memset(downloadBuffer, 0, sizeof(downloadBuffer));

  SensorSerial.begin(SENSOR_BAUD, SERIAL_8N1, PIN_SENSOR_RX, PIN_SENSOR_TX);

  if (DEBUG) {
    Serial.println("\n==============================================");
    Serial.println(" Fake Fingerprint Sensor - ESP32 / Wokwi");
    Serial.println("==============================================");
    Serial.printf("Sensor RX: GPIO%d | TX: GPIO%d\n", PIN_SENSOR_RX, PIN_SENSOR_TX);
    Serial.printf("Baud: %lu | Templates: %u\nReady.\n", (unsigned long)SENSOR_BAUD, MAX_TEMPLATES);
  }
}

void loop() {
  readIncomingPackets();
  checkDownCharTimeout();
}
