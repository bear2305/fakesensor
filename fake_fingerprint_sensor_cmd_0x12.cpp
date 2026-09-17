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
  CMD_UPCHAR          = 0x08, //0x0A to 0x08 Master downloads 256-byte buffer from sensor
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
  CMD_GETECHO         = 0x40 //0x53 to 0x40 
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

// Internal 256-byte raw signature buffers
uint8_t rawBuffer1[256];
uint8_t rawBuffer2[256];

// DELIBERATE FAKE-SENSOR FEATURE -- DO NOT REMOVE:
// Internal 512-byte representation is a 256-byte fake template duplicated
// twice. This is intentionally different from a genuine fingerprint
// template and lets the simulator recognize its own deterministic format.
uint8_t rawTemplate512[512];

// DELIBERATE FAKE-SENSOR FEATURE:
// UPCHAR can return either:
//   - 256 bytes: [3-bit finger code] + [255 bytes of 0x00]
//   - 512 bytes: the exact same 256-byte template duplicated twice
// The mode is selected by the UPCHAR parameter. This is a simulator
// extension intended for masters that want to exercise either template size.
uint16_t upcharTemplateLength = 256;

// Most recent successful GETIMAGE result.
int16_t lastCapturedCode = -1;

// Sensor password. Adafruit_Fingerprint defaults to 0x00000000.
// Mutable sensor password. Defaults to the Adafruit_Fingerprint value.
uint32_t expectedPassword = 0x00000000;

// Forward declaration for inbound stream processor
uint8_t receive256BytePayload(uint8_t *destBuffer, uint16_t *outLen);

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

void stream256BytePayload(const uint8_t *payload256) {
  // Chunk 1: Send bytes 0..127 using PTYPE_DATA (0x02)
  sendDataChunk(PTYPE_DATA, payload256, CHUNK_SIZE);

  // Chunk 2: Send bytes 128..255 using PTYPE_ENDDATA (0x08)
  sendDataChunk(PTYPE_ENDDATA, payload256 + CHUNK_SIZE, CHUNK_SIZE);
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
void handleGetEcho() {
  if (DEBUG) Serial.println("GetEcho");
  sendSimpleStatus(OK);
}

void handleSetPassword(const uint8_t *data, uint16_t len) {
  // CMD_SETPASSWORD (0x12) expects exactly a 4-byte password.
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

  if (DEBUG) {
    Serial.printf("SetPassword: password updated to 0x%08lX\n",
                  (unsigned long)expectedPassword);
  }

  sendSimpleStatus(OK);
}

void handleVerifyPassword(const uint8_t *data, uint16_t len) {
  if (len < 4) {
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

  sendSimpleStatus(ok ? OK : ERR_PASSFAIL);
}

void handleGetImage() {
  if (!fingerPresent()) {
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
  if (len < 1) {
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

  if (DEBUG) Serial.printf("RegModel OK: Dual 256B buffers verified and merged (Code=%d)\n", buffer1);
  sendSimpleStatus(OK);
}

// ---------------------------------------------------------------------------
// HANDLER FOR CMD_MATCH (0x03): 1:1 Buffer Comparison
// ---------------------------------------------------------------------------
// In this simulator, buffer1/buffer2 contain the custom 3-bit finger codes
// rather than real fingerprint feature templates.
//
// AS608-style response on success:
//   ConfirmCode = 0x00
//   MatchScore  = 0x0064 (100)
//
// On mismatch:
//   ConfirmCode = 0x0A (ERR_ENROLLMISMATCH)
// ---------------------------------------------------------------------------

// ============================================================================
// FAKE TEMPLATE 3-BIT EXTRACTION / COMPARISON
// ============================================================================
//
// The fake template intentionally stores the finger identity in only the
// lowest 3 bits of byte 0. Valid values are therefore 0..7.
//
// For a 256-byte template:
//   [byte 0: xxxx xccc] [255 x 0x00]
//
// For a 512-byte template:
//   [256-byte template] [exact duplicate of the same 256 bytes]
//
// The helper below accepts either size, validates the deliberate format,
// and extracts the same 3-bit code from either representation.
// ============================================================================

bool extractFakeFingerCode(const uint8_t *data, uint16_t len, uint8_t *outCode) {
  if (data == nullptr || outCode == nullptr) {
    return false;
  }

  if (len != 256 && len != 512) {
    return false;
  }

  // First 256 bytes must always be our deterministic fake format.
  if (!isValidFakeTemplate256(data)) {
    return false;
  }

  if (len == 512) {
    // The second half must be an exact duplicate of the first half.
    if (!isValidFakeTemplate512(data)) {
      return false;
    }
  }

  *outCode = data[0] & 0x07;
  return true;
}

bool compareFakeTemplates(const uint8_t *a, uint16_t aLen,
                          const uint8_t *b, uint16_t bLen,
                          uint8_t *outCodeA, uint8_t *outCodeB) {
  uint8_t codeA = 0;
  uint8_t codeB = 0;

  if (!extractFakeFingerCode(a, aLen, &codeA)) {
    return false;
  }

  if (!extractFakeFingerCode(b, bLen, &codeB)) {
    return false;
  }

  if (outCodeA != nullptr) {
    *outCodeA = codeA;
  }

  if (outCodeB != nullptr) {
    *outCodeB = codeB;
  }

  return codeA == codeB;
}

void handleMatch() {
  // CMD_MATCH remains a deliberately simplified fake-sensor match operation.
  //
  // The identity is the 3-bit code stored in the deterministic fake template.
  // The internal logical buffers already hold that code, so matching them is
  // equivalent to extracting byte0 & 0x07 from a valid 256/512 template and
  // comparing the resulting values.
  //
  // This is NOT a biometric algorithm and must not be treated as one.

  if (buffer1 < 0 || buffer2 < 0) {
    sendAck({ERR_FEATUREFAIL});
    return;
  }

  uint8_t code1 = (uint8_t)(buffer1 & 0x07);
  uint8_t code2 = (uint8_t)(buffer2 & 0x07);

  if (code1 == code2) {
    // Keep the established fake match response: success + score 100.
    sendAck({OK, 0x00, 0x64});
  } else {
    sendAck({ERR_NOMATCH});
  }
}

void handleUpChar(uint8_t bufferId, const uint8_t *params, uint16_t paramLen) {
  // DELIBERATE FAKE-SENSOR FEATURE -- UPCHAR TEMPLATE SIZE:
  //
  // The normal/safest compatibility path is still 256 bytes.
  // A simulator master may deliberately request 512 bytes by sending
  // an optional size selector after the usual UPCHAR buffer number:
  //
  //   UPCHAR [bufferId]                  -> 256-byte template
  //   UPCHAR [bufferId, 0x02]            -> 512-byte duplicated template
  //
  // 0x01 is accepted as an explicit 256-byte selector.
  // This selector is a simulator extension; it does not claim that every
  // genuine AS608/R30x module implements a standard 512-byte UPCHAR mode.
  uint16_t templateLen = 256;

  if (paramLen >= 2) {
    if (params[1] == 0x02) {
      templateLen = 512;
    } else if (params[1] == 0x01) {
      templateLen = 256;
    } else {
      sendAck({ERR_BAD_PACKET});
      return;
    }
  }

  // The fake sensor's source code is the only meaningful template data.
  // Keep the first 256 bytes in the established deterministic format.
  uint8_t code = 0;
  if (bufferId == 1) {
    code = (buffer1 >= 0) ? (uint8_t)(buffer1 & 0x07) : 0;
  } else if (bufferId == 2) {
    code = (buffer2 >= 0) ? (uint8_t)(buffer2 & 0x07) : 0;
  } else {
    sendAck({ERR_BAD_LOCATION});
    return;
  }

  uint8_t template256[256];
  memset(template256, 0, sizeof(template256));
  template256[0] = code;

  // UPCHAR sends a DATA packet followed by ENDDATA packets.
  // The stream payload itself is either exactly 256 or exactly 512 bytes.
  uint16_t offset = 0;
  uint16_t chunkIndex = 0;

  while (offset < templateLen) {
    uint16_t remaining = templateLen - offset;
    uint16_t chunkLen = (remaining > 128) ? 128 : remaining;

    // The final chunk uses ENDDATA (0x08); all earlier chunks use DATA (0x02).
    uint8_t pid = (offset + chunkLen == templateLen) ? 0x08 : 0x02;

    uint8_t chunk[128];
    memset(chunk, 0, sizeof(chunk));

    for (uint16_t i = 0; i < chunkLen; ++i) {
      uint16_t absoluteIndex = offset + i;

      if (absoluteIndex < 256) {
        chunk[i] = template256[absoluteIndex];
      } else {
        // 512-byte form = exact duplicate of the first 256-byte template.
        chunk[i] = template256[absoluteIndex - 256];
      }
    }

    // Use the existing packet sender/checksum path used by this simulator.
    sendDataPacket(pid, chunk, chunkLen);

    offset += chunkLen;
    ++chunkIndex;
  }

  // Keep the internal 512-byte representation synchronized for debugging
  // and for any future code that needs to inspect the simulator signature.
  memcpy(rawTemplate512, template256, 256);
  memcpy(rawTemplate512 + 256, template256, 256);

  upcharTemplateLength = templateLen;
}

void handleDownChar(const uint8_t *data, uint16_t len) {
  if (len < 1) {
    sendSimpleStatus(ERR_PACKETRECEIVE); // 0x01
    return;
  }

  const uint8_t slot = data[0];
  if (slot != 1 && slot != 2) {
    sendSimpleStatus(ERR_PACKETRECEIVE); // 0x01
    return;
  }

  // Step 1: Acknowledge readiness to receive payload
  sendSimpleStatus(OK);

  // Step 2: Ingest dynamic payload until PID 0x08 arrives
  uint8_t incomingPayload[MAX_PACKET_LENGTH];
  uint16_t receivedLen = 0;
  uint8_t rxStatus = receive256BytePayload(incomingPayload, &receivedLen);

  if (rxStatus == OK && !isValidFakeTemplate256(incomingPayload)) {
    if (DEBUG) Serial.println("DownChar REJECTED: not a valid fake 256-byte template");
    rxStatus = ERR_PACKETRECEIVE;
  }

  if (rxStatus != OK) {
    if (DEBUG) Serial.printf("DownChar REJECTED: Status 0x%02X\n", rxStatus);
    // Transmit explicit protocol error ACK back to master (0x01 or 0x0E)
    sendSimpleStatus(rxStatus);
    return;
  }

  // Step 3: Check byte length (Must be exactly 256 bytes)
  if (receivedLen != 256) {
    if (DEBUG) Serial.printf("DownChar REJECTED: Invalid total length (%u bytes)\n", receivedLen);
    sendSimpleStatus(ERR_PACKETRECEIVE); // 0x01
    return;
  }

  // Step 4: Validate padding structure (Bytes 1..255 MUST be 0x00)
  for (uint16_t i = 1; i < 256; ++i) {
    if (incomingPayload[i] != 0x00) {
      if (DEBUG) Serial.println("DownChar REJECTED: Invalid padding structure");
      sendSimpleStatus(ERR_PACKETRECEIVE);
      return;
    }
  }

  // Step 5: Save extracted code into target buffer
  uint8_t extractedCode = incomingPayload[0];
  if (slot == 1) {
    buffer1 = extractedCode;
    memcpy(rawBuffer1, incomingPayload, 256);
  } else {
    buffer2 = extractedCode;
    memcpy(rawBuffer2, incomingPayload, 256);
  }

  if (DEBUG) {
    Serial.printf("DownChar(%u): Valid 256B signature, code=%d\n", slot, extractedCode);
  }
}

// Fixed stream receiver driven by PID 0x08 (End-Marker)
uint8_t receive256BytePayload(uint8_t *destBuffer, uint16_t *outLen) {
  *outLen = 0;
  bool endMarkerReceived = false;
  uint32_t startTimeout = millis();

  while (!endMarkerReceived) {
    // 2-second stream timeout guard
    if (millis() - startTimeout > 2000) {
      return ERR_PACKETRECEIVE; // 0x01
    }

    if (SensorSerial.available() < 9) continue;

    // Header check (0xEF01)
    if (SensorSerial.read() != 0xEF || SensorSerial.read() != 0x01) {
      return ERR_PACKETRECEIVE; // 0x01
    }

    // Address check
    uint32_t addr = 0;
    for (int i = 0; i < 4; i++) {
      while (!SensorSerial.available()) {
        if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
      }
      addr = (addr << 8) | SensorSerial.read();
    }
    if (addr != DEVICE_ADDRESS) return ERR_PACKETRECEIVE;

    // Read Packet Type (PID)
    while (!SensorSerial.available()) {
      if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
    }
    uint8_t pType = SensorSerial.read();
    if (pType != PTYPE_DATA && pType != PTYPE_ENDDATA) {
      return ERR_PACKETRECEIVE; // 0x01
    }

    // Read Length
    while (SensorSerial.available() < 2) {
      if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
    }
    uint16_t len = ((uint16_t)SensorSerial.read() << 8) | SensorSerial.read();
    if (len < 2) return ERR_PACKETRECEIVE;
    uint16_t payloadLen = len - 2;

    // Buffer Overflow Guard: Check if chunk exceeds destination limits
    if ((*outLen + payloadLen) > MAX_PACKET_LENGTH) {
      return 0x0E; // Module cannot accept subsequent data packets
    }

    // Read Payload
    uint16_t checksum = pType + (uint16_t)(len >> 8) + (uint16_t)(len & 0xFF);
    for (uint16_t i = 0; i < payloadLen; i++) {
      while (!SensorSerial.available()) {
        if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
      }
      uint8_t b = SensorSerial.read();
      destBuffer[(*outLen)++] = b;
      checksum += b;
    }

    // Read & Validate Checksum
    while (SensorSerial.available() < 2) {
      if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
    }
    uint16_t rxChecksum = ((uint16_t)SensorSerial.read() << 8) | SensorSerial.read();
    if (rxChecksum != checksum) {
      return ERR_PACKETRECEIVE; // 0x01
    }

    // Check if PID 0x08 arrived
    if (pType == PTYPE_ENDDATA) {
      endMarkerReceived = true;
    }

    startTimeout = millis(); // Refresh timeout timer
  }

  return OK; // 0x00
}

// ---------------------------------------------------------------------------
// DELIBERATE FAKE-TEMPLATE FORMAT -- DO NOT REMOVE
// ---------------------------------------------------------------------------
//
// The fake sensor intentionally defines its exported template as:
//
//   256 bytes = [3-bit finger code] + [255 bytes of 0x00 padding]
//
// It also supports an internal/simulator-specific 512-byte representation:
//
//   512 bytes = [the 256-byte fake template] +
//               [an exact duplicate of that same 256-byte template]
//
// This is a deliberate simulator feature. It is NOT intended to emulate a
// real biometric template. Arbitrary/random real fingerprint template bytes
// should therefore not be treated as a valid fake-sensor template.
//
// A master may still request/store the normal 256-byte template. The 512-byte
// form exists in addition to that compatibility path.
//
// ---------------------------------------------------------------------------
bool isValidFakeTemplate256(const uint8_t *data) {
  if (data == nullptr) return false;

  // Only the simulator's 3-bit finger identity is allowed in byte 0.
  if (data[0] > 0x07) return false;

  // Deliberate zero padding: bytes 1..255 must all be zero.
  for (uint16_t i = 1; i < 256; ++i) {
    if (data[i] != 0x00) return false;
  }

  return true;
}

bool isValidFakeTemplate512(const uint8_t *data) {
  if (data == nullptr) return false;

  // First 256-byte half must be a valid fake template.
  if (!isValidFakeTemplate256(data)) return false;

  // Second 256-byte half must be an exact duplicate.
  for (uint16_t i = 0; i < 256; ++i) {
    if (data[i] != data[i + 256]) return false;
  }

  return true;
}

// Receive exactly 256 or 512 bytes from the master's streamed DATA/ENDDATA
// packets. This is deliberately kept separate from the normal packet parser
// because DOWNCHAR data arrives as subsequent data packets.
uint8_t receiveTemplatePayload(uint8_t *destBuffer,
                               uint16_t expectedLen,
                               uint16_t *outLen) {
  if (expectedLen != 256 && expectedLen != 512) {
    return ERR_PACKETRECEIVE;
  }

  *outLen = 0;
  bool endMarkerReceived = false;
  uint32_t startTimeout = millis();

  while (!endMarkerReceived) {
    if (millis() - startTimeout > 2000) {
      return ERR_PACKETRECEIVE;
    }

    if (SensorSerial.available() < 9) continue;

    if (SensorSerial.read() != 0xEF || SensorSerial.read() != 0x01) {
      return ERR_PACKETRECEIVE;
    }

    uint32_t addr = 0;
    for (int i = 0; i < 4; ++i) {
      while (!SensorSerial.available()) {
        if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
      }
      addr = (addr << 8) | SensorSerial.read();
    }

    if (addr != DEVICE_ADDRESS) return ERR_PACKETRECEIVE;

    while (!SensorSerial.available()) {
      if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
    }

    uint8_t pType = SensorSerial.read();
    if (pType != PTYPE_DATA && pType != PTYPE_ENDDATA) {
      return ERR_PACKETRECEIVE;
    }

    while (SensorSerial.available() < 2) {
      if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
    }

    uint16_t len =
        ((uint16_t)SensorSerial.read() << 8) | SensorSerial.read();

    if (len < 2) return ERR_PACKETRECEIVE;

    uint16_t payloadLen = len - 2;

    if ((uint32_t)(*outLen) + payloadLen > expectedLen) {
      return 0x0E;
    }

    uint16_t checksum =
        pType +
        (uint16_t)(len >> 8) +
        (uint16_t)(len & 0xFF);

    for (uint16_t i = 0; i < payloadLen; ++i) {
      while (!SensorSerial.available()) {
        if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
      }

      uint8_t b = SensorSerial.read();
      destBuffer[(*outLen)++] = b;
      checksum += b;
    }

    while (SensorSerial.available() < 2) {
      if (millis() - startTimeout > 2000) return ERR_PACKETRECEIVE;
    }

    uint16_t rxChecksum =
        ((uint16_t)SensorSerial.read() << 8) | SensorSerial.read();

    if (rxChecksum != checksum) {
      return ERR_PACKETRECEIVE;
    }

    if (pType == PTYPE_ENDDATA) {
      endMarkerReceived = true;
    }

    startTimeout = millis();
  }

  return (*outLen == expectedLen) ? OK : ERR_PACKETRECEIVE;
}

// Keep the original 256-byte helper for master compatibility.
// Do NOT remove this: normal masters can request/store 256-byte templates.
uint8_t receive256BytePayload(uint8_t *destBuffer, uint16_t *outLen) {
  return receiveTemplatePayload(destBuffer, 256, outLen);
}

void handleStore(const uint8_t *data, uint16_t len) {
  if (len < 3) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t slot = data[0];
  const uint16_t id = ((uint16_t)data[1] << 8) | (uint16_t)data[2];

  if (id >= MAX_TEMPLATES) {
    sendSimpleStatus(ERR_BADLOCATION);
    return;
  }

  if (slot != 1 && slot != 2) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const int16_t sourceBuffer = (slot == 1) ? buffer1 : buffer2;

  if (sourceBuffer < 0) {
    sendSimpleStatus(ERR_FEATUREFAIL);
    return;
  }

  templateCode[id] = (uint8_t)sourceBuffer;

  if (DEBUG) {
    Serial.printf("Store: ID=%u <- buffer%d/code=%d\n", id, slot, sourceBuffer);
  }

  sendSimpleStatus(OK);
}

void handleLoadChar(const uint8_t *data, uint16_t len) {
  if (len < 3) {
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
  if (len < 4) {
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
  lastCapturedCode = -1;

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
  const uint8_t slot = (len >= 1) ? data[0] : 1;
  const int16_t targetCode = (slot == 2) ? buffer2 : buffer1;

  if (targetCode < 0) {
    const uint8_t payload[] = { ERR_IMAGEFAIL, 0x00, 0x00, 0x00, 0x00 };
    sendAckPacket(payload, sizeof(payload));
    return;
  }

  for (uint16_t id = 0; id < MAX_TEMPLATES; ++id) {
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
  const uint8_t slot = (len >= 1) ? data[0] : 1;
  const int16_t targetCode = (slot == 2) ? buffer2 : buffer1;

  if (targetCode < 0) {
    const uint8_t payload[] = { ERR_IMAGEFAIL, 0x00, 0x00, 0x00, 0x00 };
    sendAckPacket(payload, sizeof(payload));
    return;
  }

  for (uint16_t id = 0; id < MAX_TEMPLATES; ++id) {
    if (templateCode[id] == (uint8_t)targetCode) {
      const uint16_t confidence = 150;
      const uint8_t payload[] = {
        OK,
        (uint8_t)(id >> 8),
        (uint8_t)(id & 0xFF),
        (uint8_t)(confidence >> 8),
        (uint8_t)(confidence & 0xFF)
      };

      if (DEBUG) Serial.printf("HiSpeedSearch: MATCH ID=%u code=%d\n", id, targetCode);

      sendAckPacket(payload, sizeof(payload));
      return;
    }
  }

  if (DEBUG) Serial.printf("HiSpeedSearch: NO MATCH code=%d\n", targetCode);

  const uint8_t payload[] = { ERR_NOTFOUND, 0x00, 0x00, 0x00, 0x00 };
  sendAckPacket(payload, sizeof(payload));
}

void handleAuraControl(const uint8_t *data, uint16_t len) {
  if (DEBUG) Serial.printf("Aura/LED control: %u bytes -> ACK\n", len);
  (void)data;
  sendSimpleStatus(OK);
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
void processPacket(uint8_t packetType, const uint8_t *payload, uint16_t payloadLen) {
  if (packetType != PTYPE_COMMAND) {
    if (DEBUG) Serial.printf("Ignoring non-command packet type 0x%02X\n", packetType);
    return;
  }

  if (payloadLen == 0) {
    sendSimpleStatus(ERR_PACKETRECEIVE);
    return;
  }

  const uint8_t cmd = payload[0];
  const uint8_t *args = payload + 1;
  const uint16_t argsLen = payloadLen - 1;

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
    case CMD_DOWNCHAR: handleDownChar(args, argsLen); break;
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
      case 0: // Header byte 1
        if (b == 0xEF) state = 1;
        break;

      case 1: // Header byte 2
        if (b == 0x01) {
          packetAddress = 0;
          payloadIndex = 0;
          state = 2;
        } else if (b != 0xEF) {
          state = 0;
        }
        break;

      case 2: // Address (4 bytes)
        packetAddress = (packetAddress << 8) | b;
        if (++payloadIndex == 4) {
          payloadIndex = 0;
          state = 3;
        }
        break;

      case 3: // Packet type
        packetType = b;
        state = 4;
        break;

      case 4: // Length high byte
        packetLength = (uint16_t)b << 8;
        state = 5;
        break;

      case 5: // Length low byte
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

      case 6: // Payload
        payload[payloadIndex++] = b;
        checksum += b;
        if (payloadIndex >= payloadLength) state = 7;
        break;

      case 7: // Checksum high byte
        checksumHi = b;
        state = 8;
        break;

      case 8: { // Checksum low byte
        const uint16_t receivedChecksum = ((uint16_t)checksumHi << 8) | b;

        if (DEBUG) {
          Serial.printf("RX addr=%08lX type=0x%02X len=%u checksum=%04X/%04X\n",
                        (unsigned long)packetAddress, packetType, packetLength, checksum, receivedChecksum);
        }

        const bool addressOK  = (packetAddress == DEVICE_ADDRESS);
        const bool checksumOK = (receivedChecksum == checksum);

        if (!addressOK) {
          if (DEBUG) Serial.println("RX rejected: address mismatch");
        } else if (!checksumOK) {
          if (DEBUG) Serial.println("RX rejected: checksum mismatch");
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
}