#include <Arduino.h>

// Create a HardwareSerial instance on UART1
HardwareSerial mySerial(1);

void setup() {
  // Hardware serial for debugging on your computer (USB)
  Serial.begin(9600);

  // Initialize Hardware Serial 1: (Baud, Config, RX Pin, TX Pin)
  mySerial.begin(9600, SERIAL_8N1, 10, 11);

  Serial.println("Transmitter Initialized.");
}

void loop() {
  // Send the ping message over Hardware Serial
  mySerial.println("PING");
  Serial.println("Sent: PING");

  // Wait 1 second before the next ping
  delay(1000);
}




#include <Arduino.h>

// Create a HardwareSerial instance on UART1
HardwareSerial mySerial(1);

void setup() {
  // Hardware serial for debugging on your computer (USB)
  Serial.begin(9600);

  // Initialize Hardware Serial 1: (Baud, Config, RX Pin, TX Pin)
  mySerial.begin(9600, SERIAL_8N1, 10, 11);

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
