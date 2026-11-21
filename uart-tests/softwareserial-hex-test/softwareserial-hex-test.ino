#include <SoftwareSerial.h>

// SoftwareSerial(RX, TX)
// Connect TX to the receiver's RX pin (Pin 10)
SoftwareSerial mySerial(10, 11); // RX, TX

void setup() {
  // Hardware serial for debugging on your computer (USB)
  Serial.begin(9600);

  // Software serial for communication with the receiver
  mySerial.begin(9600);

  Serial.println("Transmitter Initialized.");
}

void loop() {
  // Send the ping message over SoftwareSerial
  mySerial.println("PING");
  Serial.println("Sent: PING");

  // Wait 1 second before the next ping
  delay(1000);
}














#include <SoftwareSerial.h>

// SoftwareSerial(RX, TX)
// Connect RX to the transmitter's TX pin (Pin 11)
SoftwareSerial mySerial(10, 11); // RX, TX

void setup() {
  // Hardware serial for debugging on your computer (USB)
  Serial.begin(9600);

  // Software serial for communication with the other board
  mySerial.begin(9600);

  Serial.println("Receiver Ready. Listening for signals...");
}

void loop() {
  while (mySerial.available() > 0) {
    int b = mySerial.read();

    Serial.print("Byte: ");
    Serial.print(b);

    Serial.print(" | HEX: 0x");
    Serial.println(b, HEX);
  }
}
