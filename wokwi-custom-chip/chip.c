// Fake Fingerprint Sensor - Wokwi custom chip
// Emulates the AS608/R30x binary UART protocol used by Adafruit_Fingerprint.
// Ported from the Arduino version.
//
// Pin mapping (chip.json):
//   switch1 = finger code BIT0 (LSB)
//   switch2 = finger code BIT1
//   switch3 = finger code BIT2 (MSB)
//   present_finger_switch = finger present
//   Rx = receives from the master's TX
//   Tx = sends to the master's RX
//
// SPDX-License-Identifier: MIT

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// ============================= USER CONFIG ================================
#define SWITCHES_ACTIVE_LOW  1        // 1: switch closed to GND = ON (pull-up)
#define SENSOR_BAUD          57600
#define DEBUG                1
#define ENFORCE_PASSWORD     0
#define MAX_TEMPLATES        200
#define DEVICE_ADDRESS       0xFFFFFFFFUL
#define CHUNK_SIZE           128
#define MAX_PAYLOAD          128
#define TX_QUEUE_SIZE        2048
#define DOWNLOAD_TOTAL_TIMEOUT_MS   5000
#define DOWNLOAD_PACKET_TIMEOUT_MS  2000
// ===========================================================================

#if DEBUG
  #define DBG(...) printf(__VA_ARGS__)
#else
  #define DBG(...) ((void)0)
#endif

#define PACKET_HEADER 0xEF01

enum { PTYPE_COMMAND = 0x01, PTYPE_DATA = 0x02, PTYPE_ACK = 0x07, PTYPE_ENDDATA = 0x08 };

enum {
  CMD_GETIMAGE = 0x01, CMD_IMAGE2TZ = 0x02, CMD_MATCH = 0x03, CMD_SEARCH = 0x04,
  CMD_REGMODEL = 0x05, CMD_STORE = 0x06, CMD_LOADCHAR = 0x07, CMD_UPCHAR = 0x08,
  CMD_DOWNCHAR = 0x09, CMD_DELETE = 0x0C, CMD_EMPTY = 0x0D, CMD_READSYSPARAM = 0x0F,
  CMD_SETPASSWORD = 0x12, CMD_VERIFYPASSWORD = 0x13, CMD_HISPEEDSEARCH = 0x1B,
  CMD_TEMPLATECOUNT = 0x1D, CMD_AURACONTROL = 0x35, CMD_GETECHO = 0x40
};

enum {
  FP_OK = 0x00, ERR_PACKETRECEIVE = 0x01, ERR_NOFINGER = 0x02, ERR_IMAGEFAIL = 0x03,
  ERR_FEATUREFAIL = 0x07, ERR_NOMATCH = 0x08, ERR_NOTFOUND = 0x09,
  ERR_ENROLLMISMATCH = 0x0A, ERR_BADLOCATION = 0x0B, ERR_PASSFAIL = 0x13
};

// ---------------------------------------------------------------------------
// State (one chip instance)
// ---------------------------------------------------------------------------
static pin_t pin_sw1, pin_sw2, pin_sw3, pin_present;
static uart_dev_t uart;

// TX ring buffer (uart_write is async, so we queue bytes)
static uint8_t  txq[TX_QUEUE_SIZE];
static uint32_t txq_head = 0;      // next write position
static uint32_t txq_tail = 0;      // next byte to send
static uint32_t txq_count = 0;
static uint32_t txq_inflight = 0;  // bytes currently handed to uart_write
static bool     tx_busy = false;

// Fake fingerprint database (0xFF = empty)
static uint8_t templateCode[MAX_TEMPLATES];
static int16_t buffer1 = -1, buffer2 = -1, modelCode = -1, lastCapturedCode = -1;

// DOWNCHAR state
static bool     downloadActive = false;
static uint16_t downloadLength = 0;
static uint8_t  downloadCode = 0;
static bool     downloadBad = false;
static uint8_t  pendingDownloadSlot = 0;
static uint32_t downloadStartedAt = 0;
static uint32_t downloadLastPacketAt = 0;

static uint32_t expectedPassword = 0;
static bool     authenticated = false;

// Parser state
static uint8_t  p_state = 0;
static uint8_t  p_type = 0;
static uint16_t p_length = 0;
static uint16_t p_payloadLen = 0;
static uint32_t p_address = 0;
static uint16_t p_checksum = 0;
static uint8_t  p_payload[MAX_PAYLOAD];
static uint16_t p_index = 0;
static uint8_t  p_checksumHi = 0;

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
static uint32_t millis_now(void) {
  return (uint32_t)(get_sim_nanos() / 1000000ULL);
}

// ---------------------------------------------------------------------------
// TX queue
// ---------------------------------------------------------------------------
static void tx_kick(void) {
  if (tx_busy || txq_count == 0) return;
  uint32_t contiguous = TX_QUEUE_SIZE - txq_tail;
  uint32_t n = (txq_count < contiguous) ? txq_count : contiguous;
  txq_inflight = n;
  tx_busy = true;
  uart_write(uart, &txq[txq_tail], n);
}

static void on_uart_write_done(void *user_data) {
  (void)user_data;
  txq_tail = (txq_tail + txq_inflight) % TX_QUEUE_SIZE;
  txq_count -= txq_inflight;
  txq_inflight = 0;
  tx_busy = false;
  tx_kick();
}

static void tx_push(uint8_t b) {
  if (txq_count >= TX_QUEUE_SIZE) {
    DBG("TX queue overflow!\n");
    return;
  }
  txq[txq_head] = b;
  txq_head = (txq_head + 1) % TX_QUEUE_SIZE;
  txq_count++;
}

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------
static bool switch_on(pin_t pin) {
  uint32_t level = pin_read(pin);
#if SWITCHES_ACTIVE_LOW
  return level == LOW;
#else
  return level == HIGH;
#endif
}

static uint8_t read_finger_code(void) {
  uint8_t code = 0;
  if (switch_on(pin_sw1)) code |= 0x01;
  if (switch_on(pin_sw2)) code |= 0x02;
  if (switch_on(pin_sw3)) code |= 0x04;
  return code;
}

static bool finger_present(void) { return switch_on(pin_present); }

#if DEBUG
static void debug_hex(const uint8_t *data, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) printf("%02X ", data[i]);
  printf("\n");
}
#endif

// ---------------------------------------------------------------------------
// Outgoing packets
// ---------------------------------------------------------------------------
static void send_packet(uint8_t type, const uint8_t *payload, uint16_t len) {
  uint16_t length = len + 2;
  uint16_t checksum = type + (length >> 8) + (length & 0xFF);
  for (uint16_t i = 0; i < len; i++) checksum += payload[i];

  tx_push(PACKET_HEADER >> 8);
  tx_push(PACKET_HEADER & 0xFF);
  tx_push((DEVICE_ADDRESS >> 24) & 0xFF);
  tx_push((DEVICE_ADDRESS >> 16) & 0xFF);
  tx_push((DEVICE_ADDRESS >> 8) & 0xFF);
  tx_push(DEVICE_ADDRESS & 0xFF);
  tx_push(type);
  tx_push(length >> 8);
  tx_push(length & 0xFF);
  for (uint16_t i = 0; i < len; i++) tx_push(payload[i]);
  tx_push(checksum >> 8);
  tx_push(checksum & 0xFF);

  tx_kick();

#if DEBUG
  if (len <= 16) {
    printf("TX type=0x%02X payloadLen=%u: ", type, len);
    debug_hex(payload, len);
  }
#endif
}

static void send_ack(const uint8_t *payload, uint16_t len) {
  send_packet(PTYPE_ACK, payload, len);
}

static void send_status(uint8_t status) {
  uint8_t p[1] = { status };
  send_ack(p, 1);
}

// Streams `copies` x 256-byte fake templates ([code, 0 x 255]) in chunks.
static void stream_template(uint8_t code, uint8_t copies) {
  uint16_t total = 256 * copies;
  uint8_t chunk[CHUNK_SIZE];
  uint16_t offset = 0;

  while (offset < total) {
    uint16_t remaining = total - offset;
    uint16_t chunkLen = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    uint8_t pid = (offset + chunkLen == total) ? PTYPE_ENDDATA : PTYPE_DATA;
    for (uint16_t i = 0; i < chunkLen; i++) {
      chunk[i] = (((offset + i) & 0xFF) == 0) ? code : 0x00;
    }
    send_packet(pid, chunk, chunkLen);
    offset += chunkLen;
  }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------
static void handle_get_echo(void) {
  DBG("GetEcho\n");
  send_status(FP_OK);
}

static void handle_set_password(const uint8_t *d, uint16_t len) {
  if (len != 4) { send_status(ERR_PACKETRECEIVE); return; }
  expectedPassword = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                     ((uint32_t)d[2] << 8) | d[3];
  authenticated = false;
  DBG("SetPassword: updated to 0x%08X\n", (unsigned)expectedPassword);
  send_status(FP_OK);
}

static void handle_verify_password(const uint8_t *d, uint16_t len) {
  if (len != 4) { send_status(ERR_PACKETRECEIVE); return; }
  uint32_t pw = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                ((uint32_t)d[2] << 8) | d[3];
  bool ok = (pw == expectedPassword);
  DBG("VerifyPassword: %s\n", ok ? "OK" : "FAIL");
  authenticated = ok;
  send_status(ok ? FP_OK : ERR_PASSFAIL);
}

static void handle_get_image(void) {
  if (!finger_present()) {
    lastCapturedCode = -1;
    DBG("GetImage: NO FINGER\n");
    send_status(ERR_NOFINGER);
    return;
  }
  lastCapturedCode = read_finger_code();
  DBG("GetImage: finger present, code=%d\n", lastCapturedCode);
  send_status(FP_OK);
}

static void handle_image2tz(const uint8_t *d, uint16_t len) {
  if (len != 1) { send_status(ERR_PACKETRECEIVE); return; }
  uint8_t slot = d[0];
  if (slot != 1 && slot != 2) { send_status(ERR_PACKETRECEIVE); return; }
  if (lastCapturedCode < 0) { send_status(ERR_IMAGEFAIL); return; }

  if (slot == 1) buffer1 = lastCapturedCode;
  else           buffer2 = lastCapturedCode;

  DBG("Image2Tz(%u): code=%d\n", slot, lastCapturedCode);
  lastCapturedCode = -1;
  send_status(FP_OK);
}

static void handle_reg_model(void) {
  if (buffer1 < 0 || buffer2 < 0) { send_status(ERR_FEATUREFAIL); return; }
  if (buffer1 != buffer2) {
    DBG("RegModel REJECTED: Buffer1 (%d) != Buffer2 (%d)\n", buffer1, buffer2);
    send_status(ERR_ENROLLMISMATCH);
    return;
  }
  modelCode = buffer1;
  buffer1 = -1;
  buffer2 = -1;
  DBG("RegModel OK: Code=%d\n", modelCode);
  send_status(FP_OK);
}

static void handle_match(void) {
  if (buffer1 < 0 || buffer2 < 0) { send_status(ERR_FEATUREFAIL); return; }
  uint8_t c1 = buffer1 & 0x07;
  uint8_t c2 = buffer2 & 0x07;
  if (c1 == c2) {
    uint8_t p[3] = { FP_OK, 0x00, 0x64 };
    send_ack(p, 3);
  } else {
    send_status(ERR_NOMATCH);
  }
}

// UPCHAR: 256-byte (RAM) params = [bufferId] or [bufferId, 0x01]
//         512-byte (Flash) params = [0x02, idHi, idLo]
static void handle_upchar(const uint8_t *params, uint16_t len) {
  if (len != 1 && len != 2 && len != 3) { send_status(ERR_PACKETRECEIVE); return; }

  if (len == 3) {
    if (params[0] != 0x02) { send_status(ERR_PACKETRECEIVE); return; }
    uint16_t flashId = ((uint16_t)params[1] << 8) | params[2];
    if (flashId >= MAX_TEMPLATES) {
      DBG("UpChar(512): Flash ID out of range: %u\n", flashId);
      send_status(ERR_BADLOCATION);
      return;
    }
    if (templateCode[flashId] == 0xFF) {
      DBG("UpChar(512): Flash ID empty: %u\n", flashId);
      send_status(ERR_FEATUREFAIL);
      return;
    }
    uint8_t code = templateCode[flashId];
    DBG("UpChar(512): Flash ID=%u code=%u\n", flashId, code);
    send_status(FP_OK);
    stream_template(code, 2);
    return;
  }

  uint8_t bufferId = params[0];
  if (len == 2 && params[1] != 0x01) { send_status(ERR_PACKETRECEIVE); return; }

  uint8_t code = 0;
  if (bufferId == 1) {
    if (buffer1 < 0) { send_status(ERR_FEATUREFAIL); return; }
    code = buffer1 & 0x07;
  } else if (bufferId == 2) {
    if (buffer2 < 0) { send_status(ERR_FEATUREFAIL); return; }
    code = buffer2 & 0x07;
  } else {
    send_status(ERR_BADLOCATION);
    return;
  }

  DBG("UpChar(256): CharBuffer%u code=%u\n", bufferId, code);
  send_status(FP_OK);
  stream_template(code, 1);
}

// DOWNCHAR
static void begin_downchar(const uint8_t *d, uint16_t len) {
  if (len != 1) { send_status(ERR_PACKETRECEIVE); return; }
  uint8_t slot = d[0];
  if (slot != 1 && slot != 2) { send_status(ERR_BADLOCATION); return; }
  if (downloadActive) { send_status(ERR_PACKETRECEIVE); return; }

  downloadActive = true;
  downloadLength = 0;
  downloadCode = 0;
  downloadBad = false;
  downloadStartedAt = millis_now();
  downloadLastPacketAt = downloadStartedAt;
  pendingDownloadSlot = slot;

  DBG("DownChar(%u): waiting for DATA/ENDDATA\n", slot);
  send_status(FP_OK);
}

static void finish_downchar(uint8_t status) {
  uint8_t slot = pendingDownloadSlot;

  if (status == FP_OK) {
    if (downloadLength != 256 || downloadBad || downloadCode > 0x07) {
      status = ERR_PACKETRECEIVE;
    }
  }

  if (status == FP_OK) {
    if (slot == 1)      buffer1 = downloadCode;
    else if (slot == 2) buffer2 = downloadCode;
    else                status = ERR_BADLOCATION;

    if (status == FP_OK) {
      DBG("DownChar(%u): valid 256B signature, code=%u\n", slot, downloadCode);
    }
  } else {
    DBG("DownChar(%u): rejected, status=0x%02X, received=%u\n", slot, status, downloadLength);
  }

  downloadActive = false;
  downloadLength = 0;
  downloadCode = 0;
  downloadBad = false;
  pendingDownloadSlot = 0;
  downloadStartedAt = 0;
  downloadLastPacketAt = 0;

  send_status(status);
}

static void handle_incoming_data_packet(uint8_t type, const uint8_t *payload, uint16_t len) {
  if (!downloadActive) {
    DBG("Ignoring unexpected data packet type=0x%02X\n", type);
    return;
  }
  if (len == 0 || len > CHUNK_SIZE) { finish_downchar(ERR_PACKETRECEIVE); return; }
  if ((uint32_t)downloadLength + len > 256) { finish_downchar(ERR_PACKETRECEIVE); return; }

  for (uint16_t i = 0; i < len; i++) {
    uint16_t idx = downloadLength + i;
    if (idx == 0) downloadCode = payload[i];
    else if (payload[i] != 0x00) downloadBad = true;
  }

  downloadLength += len;
  downloadLastPacketAt = millis_now();
  DBG("DownChar RX: payload=%u total=%u\n", len, downloadLength);

  if (type == PTYPE_ENDDATA) finish_downchar(FP_OK);
}

static void handle_store(const uint8_t *d, uint16_t len) {
  if (len != 3) { send_status(ERR_PACKETRECEIVE); return; }
  uint8_t slot = d[0];
  uint16_t id = ((uint16_t)d[1] << 8) | d[2];

  if (id >= MAX_TEMPLATES) { send_status(ERR_BADLOCATION); return; }
  if (slot != 1) { send_status(ERR_PACKETRECEIVE); return; }
  if (modelCode < 0) { send_status(ERR_FEATUREFAIL); return; }

  templateCode[id] = (uint8_t)modelCode;
  modelCode = -1;
  DBG("Store: ID=%u <- code=%u\n", id, templateCode[id]);
  send_status(FP_OK);
}

static void handle_load_char(const uint8_t *d, uint16_t len) {
  if (len != 3) { send_status(ERR_PACKETRECEIVE); return; }
  uint8_t slot = d[0];
  uint16_t id = ((uint16_t)d[1] << 8) | d[2];

  if (slot != 1 && slot != 2) { send_status(ERR_PACKETRECEIVE); return; }
  if (id >= MAX_TEMPLATES || templateCode[id] == 0xFF) { send_status(ERR_NOTFOUND); return; }

  if (slot == 1) buffer1 = templateCode[id];
  else           buffer2 = templateCode[id];

  DBG("LoadChar: buffer%u <- ID=%u code=%u\n", slot, id, templateCode[id]);
  send_status(FP_OK);
}

static void handle_delete(const uint8_t *d, uint16_t len) {
  if (len != 4) { send_status(ERR_PACKETRECEIVE); return; }
  uint16_t startId = ((uint16_t)d[0] << 8) | d[1];
  uint16_t count   = ((uint16_t)d[2] << 8) | d[3];

  if (count == 0 || startId >= MAX_TEMPLATES || (uint32_t)startId + count > MAX_TEMPLATES) {
    send_status(ERR_BADLOCATION);
    return;
  }
  for (uint16_t i = 0; i < count; i++) templateCode[startId + i] = 0xFF;

  DBG("Delete: start=%u count=%u\n", startId, count);
  send_status(FP_OK);
}

static void handle_empty(void) {
  for (uint16_t i = 0; i < MAX_TEMPLATES; i++) templateCode[i] = 0xFF;
  buffer1 = buffer2 = modelCode = lastCapturedCode = -1;
  downloadActive = false;
  downloadLength = 0;
  downloadCode = 0;
  downloadBad = false;
  pendingDownloadSlot = 0;
  DBG("Empty: database cleared\n");
  send_status(FP_OK);
}

static void handle_template_count(void) {
  uint16_t count = 0;
  for (uint16_t i = 0; i < MAX_TEMPLATES; i++) if (templateCode[i] != 0xFF) count++;
  uint8_t p[3] = { FP_OK, count >> 8, count & 0xFF };
  DBG("TemplateCount: %u\n", count);
  send_ack(p, 3);
}

static void handle_read_sys_param(void) {
  uint16_t capacity = MAX_TEMPLATES;
  uint8_t p[17] = {
    FP_OK,
    0x00, 0x00,                     // status register
    0x00, 0x00,                     // system ID
    capacity >> 8, capacity & 0xFF,
    0x00, 0x03,                     // security level 3
    0xFF, 0xFF, 0xFF, 0xFF,         // device address
    0x00, 0x02,                     // packet size code 2 = 128 bytes
    0x00, 0x06                      // baud-rate code = 57600
  };
  DBG("ReadSysParam\n");
  send_ack(p, sizeof(p));
}

static void handle_search(const uint8_t *d, uint16_t len) {
  if (len != 5) { send_status(ERR_PACKETRECEIVE); return; }
  uint8_t slot = d[0];
  uint16_t startPage = ((uint16_t)d[1] << 8) | d[2];
  uint16_t pageCount = ((uint16_t)d[3] << 8) | d[4];

  if (slot != 1 && slot != 2) { send_status(ERR_PACKETRECEIVE); return; }

  int16_t target = (slot == 2) ? buffer2 : buffer1;
  if (target < 0) {
    uint8_t p[5] = { ERR_IMAGEFAIL, 0, 0, 0, 0 };
    send_ack(p, 5);
    return;
  }

  uint32_t end32 = (uint32_t)startPage + pageCount;
  if (end32 > MAX_TEMPLATES) end32 = MAX_TEMPLATES;

  for (uint16_t id = startPage; id < (uint16_t)end32; id++) {
    if (templateCode[id] == (uint8_t)target) {
      uint16_t confidence = 150;
      uint8_t p[5] = { FP_OK, id >> 8, id & 0xFF, confidence >> 8, confidence & 0xFF };
      DBG("Search: MATCH ID=%u code=%d\n", id, target);
      send_ack(p, 5);
      return;
    }
  }

  DBG("Search: NO MATCH code=%d\n", target);
  uint8_t p[5] = { ERR_NOTFOUND, 0, 0, 0, 0 };
  send_ack(p, 5);
}

static void handle_aura_control(uint8_t len) {
  if (len != 4) { send_status(ERR_PACKETRECEIVE); return; }
  DBG("Aura/LED control -> ACK\n");
  send_status(FP_OK);
}

// ---------------------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------------------
static void process_packet(uint8_t type, const uint8_t *payload, uint16_t len) {
  if (type == PTYPE_DATA || type == PTYPE_ENDDATA) {
    handle_incoming_data_packet(type, payload, len);
    return;
  }
  if (type != PTYPE_COMMAND) { DBG("Ignoring non-command packet\n"); return; }

  if (downloadActive) {
    DBG("Command during DOWNCHAR; rejecting\n");
    send_status(ERR_PACKETRECEIVE);
    return;
  }
  if (len == 0) { send_status(ERR_PACKETRECEIVE); return; }

  uint8_t cmd = payload[0];
  const uint8_t *args = payload + 1;
  uint16_t argsLen = len - 1;

#if ENFORCE_PASSWORD
  if (cmd != CMD_VERIFYPASSWORD && cmd != CMD_SETPASSWORD && cmd != CMD_GETECHO &&
      !authenticated) {
    send_status(ERR_PASSFAIL);
    return;
  }
#endif

  switch (cmd) {
    case CMD_GETECHO:        handle_get_echo(); break;
    case CMD_SETPASSWORD:    handle_set_password(args, argsLen); break;
    case CMD_VERIFYPASSWORD: handle_verify_password(args, argsLen); break;
    case CMD_GETIMAGE:
      if (argsLen != 0) send_status(ERR_PACKETRECEIVE); else handle_get_image();
      break;
    case CMD_IMAGE2TZ:       handle_image2tz(args, argsLen); break;
    case CMD_MATCH:
      if (argsLen != 0) send_status(ERR_PACKETRECEIVE); else handle_match();
      break;
    case CMD_REGMODEL:
      if (argsLen != 0) send_status(ERR_PACKETRECEIVE); else handle_reg_model();
      break;
    case CMD_UPCHAR:         handle_upchar(args, argsLen); break;
    case CMD_DOWNCHAR:       begin_downchar(args, argsLen); break;
    case CMD_STORE:          handle_store(args, argsLen); break;
    case CMD_LOADCHAR:       handle_load_char(args, argsLen); break;
    case CMD_DELETE:         handle_delete(args, argsLen); break;
    case CMD_EMPTY:
      if (argsLen != 0) send_status(ERR_PACKETRECEIVE); else handle_empty();
      break;
    case CMD_READSYSPARAM:
      if (argsLen != 0) send_status(ERR_PACKETRECEIVE); else handle_read_sys_param();
      break;
    case CMD_TEMPLATECOUNT:
      if (argsLen != 0) send_status(ERR_PACKETRECEIVE); else handle_template_count();
      break;
    case CMD_SEARCH:
    case CMD_HISPEEDSEARCH:  handle_search(args, argsLen); break;
    case CMD_AURACONTROL:    handle_aura_control(argsLen); break;
    default:
      DBG("Unhandled command 0x%02X\n", cmd);
      send_status(ERR_PACKETRECEIVE);
      break;
  }
}

// ---------------------------------------------------------------------------
// Incoming packet parser: called once per received byte
// ---------------------------------------------------------------------------
static void on_uart_rx(void *user_data, uint8_t b) {
  (void)user_data;

  switch (p_state) {
    case 0:
      if (b == 0xEF) p_state = 1;
      break;

    case 1:
      if (b == 0x01) {
        p_address = 0;
        p_index = 0;
        p_state = 2;
      } else if (b != 0xEF) {
        p_state = 0;
      }
      break;

    case 2:
      p_address = (p_address << 8) | b;
      if (++p_index == 4) { p_index = 0; p_state = 3; }
      break;

    case 3:
      p_type = b;
      p_state = 4;
      break;

    case 4:
      p_length = (uint16_t)b << 8;
      p_state = 5;
      break;

    case 5:
      p_length |= b;
      if (p_length < 2 || p_length > MAX_PAYLOAD + 2) {
        DBG("RX invalid length=%u\n", p_length);
        p_state = 0;
        p_index = 0;
        break;
      }
      p_payloadLen = p_length - 2;
      p_index = 0;
      p_checksum = p_type + (p_length >> 8) + (p_length & 0xFF);
      p_state = (p_payloadLen == 0) ? 7 : 6;
      break;

    case 6:
      p_payload[p_index++] = b;
      p_checksum += b;
      if (p_index >= p_payloadLen) p_state = 7;
      break;

    case 7:
      p_checksumHi = b;
      p_state = 8;
      break;

    case 8: {
      uint16_t received = ((uint16_t)p_checksumHi << 8) | b;

      DBG("RX addr=%08X type=0x%02X len=%u chk=%04X/%04X\n",
          (unsigned)p_address, p_type, p_length, p_checksum, received);

      bool addressOK  = (p_address == DEVICE_ADDRESS);
      bool checksumOK = (received == p_checksum);

      if (!addressOK) {
        DBG("RX rejected: address mismatch\n");
        if (downloadActive && (p_type == PTYPE_DATA || p_type == PTYPE_ENDDATA))
          finish_downchar(ERR_PACKETRECEIVE);
      } else if (!checksumOK) {
        DBG("RX rejected: checksum mismatch\n");
        if (downloadActive && (p_type == PTYPE_DATA || p_type == PTYPE_ENDDATA))
          finish_downchar(ERR_PACKETRECEIVE);
      } else {
        process_packet(p_type, p_payload, p_payloadLen);
      }

      p_state = 0;
      p_index = 0;
      p_length = 0;
      p_payloadLen = 0;
      p_address = 0;
      p_checksum = 0;
      break;
    }

    default:
      p_state = 0;
      p_index = 0;
      break;
  }
}

// ---------------------------------------------------------------------------
// DOWNCHAR timeout (replaces checkDownCharTimeout() in loop())
// ---------------------------------------------------------------------------
static void on_timeout_timer(void *user_data) {
  (void)user_data;
  if (!downloadActive) return;

  uint32_t now = millis_now();
  if ((uint32_t)(now - downloadStartedAt) > DOWNLOAD_TOTAL_TIMEOUT_MS ||
      (uint32_t)(now - downloadLastPacketAt) > DOWNLOAD_PACKET_TIMEOUT_MS) {
    DBG("DownChar: transfer timeout\n");
    finish_downchar(ERR_PACKETRECEIVE);
  }
}

// ---------------------------------------------------------------------------
// Init (replaces setup())
// ---------------------------------------------------------------------------
void chip_init(void) {
#if SWITCHES_ACTIVE_LOW
  const uint32_t sw_mode = INPUT_PULLUP;
#else
  const uint32_t sw_mode = INPUT_PULLDOWN;
#endif

  pin_sw1     = pin_init("switch1", sw_mode);
  pin_sw2     = pin_init("switch2", sw_mode);
  pin_sw3     = pin_init("switch3", sw_mode);
  pin_present = pin_init("present_finger_switch", sw_mode);

  for (uint16_t i = 0; i < MAX_TEMPLATES; i++) templateCode[i] = 0xFF;

  uart_config_t cfg = {
    .tx = pin_init("Tx", INPUT_PULLUP),
    .rx = pin_init("Rx", INPUT),
    .baud_rate = SENSOR_BAUD,
    .rx_data = on_uart_rx,
    .write_done = on_uart_write_done,
    .user_data = NULL,
  };
  uart = uart_init(&cfg);

  timer_config_t tcfg = { .callback = on_timeout_timer, .user_data = NULL };
  timer_t t = timer_init(&tcfg);
  timer_start(t, 100000, true);   // every 100 ms

  DBG("\n==============================================\n");
  DBG(" Fake Fingerprint Sensor - Wokwi chip\n");
  DBG("==============================================\n");
  DBG("Baud: %d | Templates: %d\n", SENSOR_BAUD, MAX_TEMPLATES);
  DBG("Ready.\n");
}
