//tranmitter code

#include <SoftwareSerial.h>

// Set up SoftwareSerial: RX pin = 10, TX pin = 11
SoftwareSerial mySerial(10, 11); 

void setup() {
  // Start the hardware serial for debugging on your computer
  Serial.begin(9600);
  
  // Start the software serial for communication with the other Arduino
  mySerial.begin(9600);
  
  Serial.println("Transmitter Initialized.");
}

void loop() {
  // Send the ping message
  mySerial.println("PING");
  Serial.println("Sent: PING");
  
  // Wait 1 second before the next ping
  delay(1000); 
}










//reciever code






#include <SoftwareSerial.h>

// Set up SoftwareSerial: RX pin = 10, TX pin = 11
SoftwareSerial mySerial(10, 11); 

void setup() {
  // Start the hardware serial for debugging on your computer
  Serial.begin(9600);
  
  // Start the software serial for communication with the other Arduino
  mySerial.begin(9600);
  
  Serial.println("Receiver Ready. Listening for signals...");
}

void loop() {
  // Check if data is available to read from the other Arduino
  if (mySerial.available() > 0) {
    String incoming = mySerial.readStringUntil('\n');
    incoming.trim(); // Clean up any extra newline characters
    
    Serial.print("Received message: ");
    Serial.println(incoming);
  }
}









