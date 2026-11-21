void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 16, 17);
}

void loop() {
  static uint8_t n = 0;
  Serial1.write(n++);
  Serial1.flush();
  delay(1000);
}


void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 16, 17);
}

void loop() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    Serial.printf("RX: %u\n", b);
  }
}
