#undef Serial  
#include <Arduino.h>

constexpr uint8_t  UART_TX_PIN       = 16;  // GP16 (physical pin 21)
constexpr uint8_t  UART_RX_PIN       = 17;  // GP17 (physical pin 22)
constexpr uint32_t UART_BAUD         = 115200;
constexpr uint32_t PING_INTERVAL_MS  = 1000;

uint32_t lastPing = 0;
uint32_t counter  = 0;

void setup() {
  // USB serial for the monitor on your computer
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);  // wait up to 3 s for the monitor, then continue anyway
  }

  // Hardware UART0 on GP16/GP17 (must be set before begin())
  bool ok = Serial1.setTX(UART_TX_PIN) && Serial1.setRX(UART_RX_PIN);
  if (!ok) {
    Serial.println("ERROR: invalid UART pins!");
    while (true) delay(1000);
  }
  Serial1.begin(UART_BAUD);

  Serial.println("Transmitter initialized.");
}

void loop() {
  uint32_t now = millis();
  if (now - lastPing >= PING_INTERVAL_MS) {
    lastPing = now;

    Serial1.print("PING ");
    Serial1.println(counter);

    Serial.print("Sent: PING ");
    Serial.println(counter);
    counter++;
  }
}






























#undef Serial  
#include <Arduino.h>

constexpr uint8_t  UART_TX_PIN = 16;  // GP16 (physical pin 21)
constexpr uint8_t  UART_RX_PIN = 17;  // GP17 (physical pin 22)
constexpr uint32_t UART_BAUD   = 115200;

constexpr size_t   LINE_MAX    = 64;
char   lineBuf[LINE_MAX];
size_t lineLen = 0;

void setup() {
  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) {
    delay(10);
  }

  bool ok = Serial1.setTX(UART_TX_PIN) && Serial1.setRX(UART_RX_PIN);
  if (!ok) {
    Serial.println("ERROR: invalid UART pins!");
    while (true) delay(1000);
  }
  Serial1.begin(UART_BAUD);

  Serial.println("Receiver ready. Listening for signals...");
}

void loop() {
  while (Serial1.available() > 0) {
    char c = Serial1.read();

    if (c == '\r') continue;          // ignore carriage return

    if (c == '\n') {                  // end of message
      lineBuf[lineLen] = '\0';
      if (lineLen > 0) {
        Serial.print("Received: ");
        Serial.println(lineBuf);
      }
      lineLen = 0;
    } else if (lineLen < LINE_MAX - 1) {
      lineBuf[lineLen++] = c;
    } else {
      lineLen = 0;                    // overflow: discard and resync
    }
  }
}




