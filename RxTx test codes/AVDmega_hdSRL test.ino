#include <Arduino.h>

// Serial1: RX1 = pin 19, TX1 = pin 18
// Serial2: RX2 = pin 17, TX2 = pin 16
// Serial3: RX3 = pin 15, TX3 = pin 14

void setup() {
  Serial.begin(9600);    // USB Serial Monitor
  Serial2.begin(9600);   // Hardware UART on pins 16 (TX) / 17 (RX)
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    Serial2.write(c);
    Serial.print(c);
  }

  if (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}
