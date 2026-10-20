#include <Arduino.h>
#include <SoftwareSerial.h>

#define RX_PIN 10   // Nano receives here  (connect to the other device's TX)
#define TX_PIN 11   // Nano transmits here (connect to the other device's RX)

SoftwareSerial Serial2(RX_PIN, TX_PIN);  // RX, TX (note: same order as ESP32 code)

void setup() {
  Serial.begin(9600);     // USB Serial Monitor
  Serial2.begin(9600);    // Software serial port
}

void loop() {
  // Read from Serial Monitor (USB) and forward to the software serial TX pin
  if (Serial.available()) {
    char c = Serial.read();
    Serial2.write(c);   // Pass it through
    Serial.print(c);    // Echo back to Serial Monitor
  }

  // Read incoming data from the software serial RX pin and print to Serial Monitor
  if (Serial2.available()) {
    Serial.write(Serial2.read());
  }
}
