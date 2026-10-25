#include <SoftwareSerial.h>


#define RX_PIN 10   // Mega receives here (connect to other device's TX)
#define TX_PIN 11   // Mega transmits here (connect to other device's RX)

SoftwareSerial mySerial(RX_PIN, TX_PIN);  // RX, TX

void setup() {
  Serial.begin(4800);
  mySerial.begin(2400);
}

void loop() {
  if (Serial.available()) {
  
    String monitor_typed  = Serial.readStringUntil('\n');
    monitor_typed.trim(); // Clean up any extra newline characters
    Serial.print("TYPED: ");
    Serial.println(monitor_typed);
    mySerial.println(monitor_typed);
    Serial.print("Tx Sent : ");
    Serial.println(monitor_typed);
   
     

  }

  if (mySerial.available()) {
    String received = mySerial.readStringUntil('\n');
    received.trim();
    Serial.println("");
    Serial.print("(Rx)_Received: ");
    Serial.println(received);
  }
}
