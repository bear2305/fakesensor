/*
  Adafruit Fingerprint Sensor - Master Test Sketch (Corrected)
  ---------------------------------------------------------------------------
  Designed to test fingerprint sensor with custom ID enrollment and deletion.
  
  Dependencies:
    - Adafruit Fingerprint Sensor Library
  
  Wiring:
    - Master D10 (RX) -> Sensor TX (or Fake Sensor D11)
    - Master D11 (TX) -> Sensor RX (or Fake Sensor D10)
    - GND -> GND
    - VCC -> 5V / 3.3V
  ---------------------------------------------------------------------------
*/

#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h>

SoftwareSerial mySerial(10, 11); // RX = 10, TX = 11 (matches your updated pins)
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n\n=============================================="));
  Serial.println(F(" Adafruit Fingerprint Sensor - Master Test"));
  Serial.println(F("=============================================="));

  finger.begin(57600);
  delay(500);

  if (finger.verifyPassword()) {
    Serial.println(F("Found fingerprint sensor!"));
  } else {
    Serial.println(F("Did not find fingerprint sensor :("));
    while (1) { delay(1); }
  }

  finger.getParameters();
  Serial.print(F("Capacity: ")); Serial.println(finger.capacity);
  Serial.print(F("Security level: ")); Serial.println(finger.security_level);
  Serial.print(F("Device address: 0x")); Serial.println(finger.device_addr, HEX);
  
  finger.getTemplateCount();
  Serial.print(F("Sensor contains ")); 
  Serial.print(finger.templateCount); 
  Serial.println(F(" templates.\n"));

  printMenu();
}

void printMenu() {
  Serial.println(F("\n--- COMMAND MENU ---"));
  Serial.println(F("1. Check Template Count"));
  Serial.println(F("2. Search Fingerprint"));
  Serial.println(F("3. Enroll Fingerprint (Custom ID)"));
  Serial.println(F("4. Delete Template (Custom ID)"));
  Serial.println(F("5. Empty Database"));
  Serial.println(F("--------------------"));
  Serial.println(F("Enter command number (1-5):"));
}

void loop() {
  if (Serial.available()) {
    char choice = Serial.read();
    
    if (choice == '\n' || choice == '\r') return;

    switch (choice) {
      case '1':
        testTemplateCount();
        break;
      case '2':
        testSearch();
        break;
      case '3': {
        Serial.println(F("\nEnter target ID to enroll (0 to 199):"));
        uint16_t id = readNumber();
        testEnroll(id);
        break;
      }
      case '4': {
        Serial.println(F("\nEnter target ID to delete (0 to 199):"));
        uint16_t id = readNumber();
        testDelete(id);
        break;
      }
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

// Fixed helper to safely read numbers and ignore leftover buffer carriage returns
uint16_t readNumber() {
  // Clear any leftover newline or carriage return characters in the buffer first
  while (Serial.available() > 0) {
    char t = Serial.peek();
    if (t == '\n' || t == '\r') {
      Serial.read();
    } else {
      break;
    }
  }

  while (true) {
    while (Serial.available() == 0);
    String input = Serial.readStringUntil('\n');
    input.trim();
    
    // Ignore empty lines caused by leftover line endings
    if (input.length() == 0) {
      continue;
    }
    
    long val = input.toInt();
    if (val >= 0 && val <= 65535) {
      return (uint16_t)val;
    }
    Serial.println(F("Please enter a valid number (0 - 65535):"));
  }
}

void testTemplateCount() {
  Serial.println(F("\n[Test] Getting template count..."));
  uint8_t p = finger.getTemplateCount();
  if (p == FINGERPRINT_OK) {
    Serial.print(F("Success! Template count: "));
    Serial.println(finger.templateCount);
  } else {
    Serial.print(F("Error communicating with sensor. Code: "));
    Serial.println(p);
  }
}

void testSearch() {
  Serial.println(F("\n[Test] Place finger on sensor..."));
  
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    if (p == FINGERPRINT_NOFINGER) Serial.println(F("No finger detected."));
    else Serial.println(F("Imaging error."));
    return;
  }
  Serial.println(F("Image captured!"));

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println(F("Feature conversion failed."));
    return;
  }
  Serial.println(F("Converted image to feature buffer 1."));

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

  if (finger.createModel() != FINGERPRINT_OK) {
    Serial.println(F("Prints didn't match! Enrollment failed."));
    return;
  }
  Serial.println(F("Prints matched successfully!"));

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
