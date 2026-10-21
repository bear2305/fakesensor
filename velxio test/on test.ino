// Transmitter code (ESP32)

// Use UART2 on the ESP32: RX = GPIO18, TX = GPIO19
#define RXD2 18
#define TXD2 19

void setup() {
  // Hardware serial for debugging on your computer (USB)
  Serial.begin(9600);

  // UART2 for communication with the receiver
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println("Transmitter Initialized.");
}

void loop() {
  // Send the ping message
  Serial2.println("PING");
  Serial.println("Sent: PING");

  // Wait 1 second before the next ping
  delay(1000);
}









//slave

// Use UART2 on the ESP32: RX = GPIO16, TX = GPIO17
#define RXD2 18
#define TXD2 19

void setup() {
  // Hardware serial for debugging on your computer (USB)
  Serial.begin(9600);

  // UART2 for communication with the other board
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  Serial.println("Receiver Ready. Listening for signals...");
}

void loop() {
  if (Serial2.available() > 0) {
    String incoming = Serial2.readStringUntil('\n');
    incoming.trim();

    Serial.print("Received message: ");
    Serial.println(incoming);
  }
}


