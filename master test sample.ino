/*
  Adafruit Fingerprint Sensor - Master Test Sketch
  ---------------------------------------------------------------------------
  Designed to test the Fake Fingerprint Sensor emulator sketch.
  
  Dependencies:
    - Adafruit Fingerprint Sensor Library
  
  Wiring:
    - Master D2 (RX) -> Fake Sensor D11 (TX)
    - Master D3 (TX) -> Fake Sensor D10 (RX)
    - GND -> GND
  ---------------------------------------------------------------------------
*/

#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h>

// Software serial pin configuration for the master
// RX = 2, TX = 3
SoftwareSerial mySerial(2, 3);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void setup() {
  // Initialize USB Serial for debugging/menu output
  Serial.begin(115200);
  while (!Serial);  // Wait for native USB serial (if applicable)
  delay(1000);

  Serial.println(F("\n\n=============================================="));
  Serial.println(F(" Adafruit Fingerprint Sensor - Master Test"));
  Serial.println(F("=============================================="));

  // Set the data rate for the sensor serial port
  finger.begin(57600);
  delay(500);

  // Test connection to the sensor
  if (finger.verifyPassword()) {
    Serial.println(F("Found fingerprint sensor!"));
  } else {
    Serial.println(F("Did not find fingerprint sensor :("));
    Serial.println(F("Please check your wiring and baud rate, then reset."));
    while (1) { delay(1); }
  }

  // Display parameters from the sensor
  finger.getParameters();
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: 0x")); Serial.println(finger.device_addr, HEX);
  Serial.print(F("Packet len: ")); Serial.println(finger.packet_len);
  Serial.print(F("Baud rate: ")); Serial.println(finger.baud_rate);

  // Get current template count
  finger.templateCount();
  Serial.print(F("Sensor contains ")); 
  Serial.print(finger.templateCount); 
  Serial.println(F(" templates.\n"));

  printMenu();
}

void printMenu() {
  Serial.println(F("\n--- COMMAND MENU ---"));
  Serial.println(F("1. Check Template Count"));
  Serial.println(F("2. Search Fingerprint (ID 0)"));
  Serial.println(F("3. Enroll Fingerprint to ID #1"));
  Serial.println(F("4. Delete Template ID #1"));
  Serial.println(F("5. Empty Database"));
  Serial.println(F("--------------------"));
  Serial.println(F("Enter command number (1-5):"));
}

void loop() {
  if (Serial.available()) {
    char choice = Serial.read();
    
    // Ignore newline / carriage return characters
    if (choice == '\n' || choice == '\r') return;

    switch (choice) {
      case '1':
        testTemplateCount();
        break;
      case '2':
        testSearch();
        break;
      case '3':
        testEnroll(1);
        break;
      case '4':
        testDelete(1);
        break;
      case '5':
        testEmpty();
        break;
      default:
        Serial.println(F("Invalid selection. Try 1-5."));
        break;
    }
    
    printMenu();
  }
}

void testTemplateCount() {
  Serial.println(F("\n[Test] Getting template count..."));
  uint8_t p = finger.templateCount();
  if (p == FINGERPRINT_OK) {
    Serial.print(F("Success! Template count: "));
    Serial.println(finger.templateCount);
  } else {
    Serial.print(F("Error communicating with sensor. Code: "));
    Serial.println(p);
  }
}

void testSearch() {
  Serial.println(F("\n[Test] Place finger on sensor (ensure fake sensor switches are set)..."));
  
  // 1. Capture image
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) Serial.println(F("No finger detected."));
    else Serial.println(F("Imaging error."));
    return;
  }
  Serial.println(F("Image captured!"));

  // 2. Convert to feature set 1
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println(F("Feature conversion failed."));
    return;
  }
  Serial.println(F("Converted image to feature buffer 1."));

  // 3. Search database
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println(F("Found a match!"));
    Serial.print(F("Found ID #")); Serial.print(finger.fingerID);
    Serial.print(F(" with confidence of ")); Serial.println(finger.confidence);
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println(F("Did not find a matching fingerprint in database."));
  } else {
    Serial.println(F("Search error."));
  }
}

void testEnroll(uint16_t id) {
  Serial.print(F("\n[Test] Enrolling new template to ID #"));
  Serial.println(id);
  Serial.println(F("Step 1: Place finger..."));

  // Get image for buffer 1
  while (finger.getImage() != FINGERPRINT_OK);
  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    Serial.println(F("Enroll failed at image 1 conversion."));
    return;
  }
  Serial.println(F("Image 1 captured. Remove finger."));
  delay(2000);

  Serial.println(F("Step 2: Place SAME finger again..."));
  while (finger.getImage() != FINGERPRINT_OK);
  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    Serial.println(F("Enroll failed at image 2 conversion."));
    return;
  }
  Serial.println(F("Image 2 captured."));

  // Create model
  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println(F("Prints didn't match! Enrollment failed."));
    return;
  }
  Serial.println(F("Prints matched successfully!"));

  // Store model
  if (finger.storeModel(id) == FINGERPRINT_OK) {
    Serial.print(F("Successfully stored template at ID #"));
    Serial.println(id);
  } else {
    Serial.println(F("Failed to store template in database."));
  }
}

void testDelete(uint16_t id) {
  Serial.print(F("\n[Test] Deleting template at ID #"));
  Serial.println(id);
  
  if (finger.deleteModel(id) == FINGERPRINT_OK) {
    Serial.println(F("Template deleted successfully!"));
  } else {
    Serial.println(F("Failed to delete template (location might already be empty)."));
  }
}

void testEmpty() {
  Serial.println(F("\n[Test] Emptying entire fingerprint database..."));
  if (finger.emptyDatabase() == FINGERPRINT_OK) {
    Serial.println(F("Database cleared successfully!"));
  } else {
    Serial.println(F("Failed to clear database."));
  }
}
