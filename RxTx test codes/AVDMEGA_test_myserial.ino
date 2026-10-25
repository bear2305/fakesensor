#include <Arduino.h>
#include <SoftwareSerial.h>

#define RX_PIN 10   // Mega receives here (connect to other device's TX)
#define TX_PIN 11   // Mega transmits here (connect to other device's RX)

SoftwareSerial mySerial(RX_PIN, TX_PIN);  // RX, TX

void setup() {
  Serial.begin(9600);
  mySerial.begin(9600);
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    mySerial.write(c);
    Serial.print(c);
  }

  if (mySerial.available()) {
    Serial.write(mySerial.read());
  }
}
