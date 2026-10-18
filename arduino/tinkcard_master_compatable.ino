#ifndef ADAFRUIT_FINGERPRINT_H
#define ADAFRUIT_FINGERPRINT_H

/*!
 * @file Adafruit_Fingerprint.h
 */

//#include "Arduino.h"
#if defined(__AVR__) || defined(ESP8266)
#include <SoftwareSerial.h>
#elif defined(FREEDOM_E300_HIFIVE1)
//#include <SoftwareSerial32.h>
#define SoftwareSerial SoftwareSerial32
#endif

#define FINGERPRINT_OK 0x00               //!< Command execution is complete
#define FINGERPRINT_PACKETRECIEVEERR 0x01 //!< Error when receiving data package
#define FINGERPRINT_NOFINGER 0x02         //!< No finger on the sensor
#define FINGERPRINT_IMAGEFAIL 0x03        //!< Failed to enroll the finger
#define FINGERPRINT_IMAGEMESS                                                  \
  0x06 //!< Failed to generate character file due to overly disorderly
       //!< fingerprint image
#define FINGERPRINT_FEATUREFAIL                                                \
  0x07 //!< Failed to generate character file due to the lack of character point
       //!< or small fingerprint image
#define FINGERPRINT_NOMATCH 0x08  //!< Finger doesn't match
#define FINGERPRINT_NOTFOUND 0x09 //!< Failed to find matching finger
#define FINGERPRINT_ENROLLMISMATCH                                             \
  0x0A //!< Failed to combine the character files
#define FINGERPRINT_BADLOCATION                                                \
  0x0B //!< Addressed PageID is beyond the finger library
#define FINGERPRINT_DBRANGEFAIL                                                \
  0x0C //!< Error when reading template from library or invalid template
#define FINGERPRINT_UPLOADFEATUREFAIL 0x0D //!< Error when uploading template
#define FINGERPRINT_PACKETRESPONSEFAIL                                         \
  0x0E //!< Module failed to receive the following data packages
#define FINGERPRINT_UPLOADFAIL 0x0F  //!< Error when uploading image
#define FINGERPRINT_DELETEFAIL 0x10  //!< Failed to delete the template
#define FINGERPRINT_DBCLEARFAIL 0x11 //!< Failed to clear finger library
#define FINGERPRINT_PASSFAIL                                                   \
  0x13 //!< Find whether the fingerprint passed or failed
#define FINGERPRINT_INVALIDIMAGE                                               \
  0x15 //!< Failed to generate image because of lac of valid primary image
#define FINGERPRINT_FLASHERR 0x18   //!< Error when writing flash
#define FINGERPRINT_INVALIDREG 0x1A //!< Invalid register number
#define FINGERPRINT_ADDRCODE 0x20   //!< Address code
#define FINGERPRINT_PASSVERIFY 0x21 //!< Verify the fingerprint passed
#define FINGERPRINT_STARTCODE                                                  \
  0xEF01 //!< Fixed falue of EF01H; High byte transferred first

#define FINGERPRINT_COMMANDPACKET 0x1 //!< Command packet
#define FINGERPRINT_DATAPACKET                                                 \
  0x2 //!< Data packet, must follow command packet or acknowledge packet
#define FINGERPRINT_ACKPACKET 0x7     //!< Acknowledge packet
#define FINGERPRINT_ENDDATAPACKET 0x8 //!< End of data packet

#define FINGERPRINT_TIMEOUT 0xFF   //!< Timeout was reached
#define FINGERPRINT_BADPACKET 0xFE //!< Bad packet was sent

#define FINGERPRINT_GETIMAGE 0x01 //!< Collect finger image
#define FINGERPRINT_IMAGE2TZ 0x02 //!< Generate character file from image
#define FINGERPRINT_SEARCH 0x04   //!< Search for fingerprint in slot
#define FINGERPRINT_REGMODEL                                                   \
  0x05 //!< Combine character files and generate template
#define FINGERPRINT_STORE 0x06          //!< Store template
#define FINGERPRINT_LOAD 0x07           //!< Read/load template
#define FINGERPRINT_UPLOAD 0x08         //!< Upload template
#define FINGERPRINT_DELETE 0x0C         //!< Delete templates
#define FINGERPRINT_EMPTY 0x0D          //!< Empty library
#define FINGERPRINT_READSYSPARAM 0x0F   //!< Read system parameters
#define FINGERPRINT_SETPASSWORD 0x12    //!< Sets passwords
#define FINGERPRINT_VERIFYPASSWORD 0x13 //!< Verifies the password
#define FINGERPRINT_HISPEEDSEARCH                                              \
  0x1B //!< Asks the sensor to search for a matching fingerprint template to the
       //!< last model generated
#define FINGERPRINT_TEMPLATECOUNT 0x1D //!< Read finger template numbers
#define FINGERPRINT_AURALEDCONFIG 0x35 //!< Aura LED control
#define FINGERPRINT_LEDON 0x50         //!< Turn on the onboard LED
#define FINGERPRINT_LEDOFF 0x51        //!< Turn off the onboard LED

#define FINGERPRINT_LED_BREATHING 0x01   //!< Breathing light
#define FINGERPRINT_LED_FLASHING 0x02    //!< Flashing light
#define FINGERPRINT_LED_ON 0x03          //!< Always on
#define FINGERPRINT_LED_OFF 0x04         //!< Always off
#define FINGERPRINT_LED_GRADUAL_ON 0x05  //!< Gradually on
#define FINGERPRINT_LED_GRADUAL_OFF 0x06 //!< Gradually off
#define FINGERPRINT_LED_RED 0x01         //!< Red LED
#define FINGERPRINT_LED_BLUE 0x02        //!< Blue LED
#define FINGERPRINT_LED_PURPLE 0x03      //!< Purple LEDpassword

#define FINGERPRINT_REG_ADDR_ERROR 0x1A //!< shows register address error
#define FINGERPRINT_WRITE_REG 0x0E      //!< Write system register instruction

#define FINGERPRINT_BAUD_REG_ADDR 0x4   //!< BAUDRATE register address
#define FINGERPRINT_BAUDRATE_9600 0x1   //!< UART baud 9600
#define FINGERPRINT_BAUDRATE_19200 0x2  //!< UART baud 19200
#define FINGERPRINT_BAUDRATE_28800 0x3  //!< UART baud 28800
#define FINGERPRINT_BAUDRATE_38400 0x4  //!< UART baud 38400
#define FINGERPRINT_BAUDRATE_48000 0x5  //!< UART baud 48000
#define FINGERPRINT_BAUDRATE_57600 0x6  //!< UART baud 57600
#define FINGERPRINT_BAUDRATE_67200 0x7  //!< UART baud 67200
#define FINGERPRINT_BAUDRATE_76800 0x8  //!< UART baud 76800
#define FINGERPRINT_BAUDRATE_86400 0x9  //!< UART baud 86400
#define FINGERPRINT_BAUDRATE_96000 0xA  //!< UART baud 96000
#define FINGERPRINT_BAUDRATE_105600 0xB //!< UART baud 105600
#define FINGERPRINT_BAUDRATE_115200 0xC //!< UART baud 115200

#define FINGERPRINT_SECURITY_REG_ADDR 0x5 //!< Security level register address
// The safety level is 1 The highest rate of false recognition , The rejection
// rate is the lowest . The safety level is 5 The lowest tate of false
// recognition, The rejection rate is the highest .
#define FINGERPRINT_SECURITY_LEVEL_1 0X1 //!< Security level 1
#define FINGERPRINT_SECURITY_LEVEL_2 0X2 //!< Security level 2
#define FINGERPRINT_SECURITY_LEVEL_3 0X3 //!< Security level 3
#define FINGERPRINT_SECURITY_LEVEL_4 0X4 //!< Security level 4
#define FINGERPRINT_SECURITY_LEVEL_5 0X5 //!< Security level 5

#define FINGERPRINT_PACKET_REG_ADDR 0x6 //!< Packet size register address
#define FINGERPRINT_PACKET_SIZE_32 0X0  //!< Packet size is 32 Byte
#define FINGERPRINT_PACKET_SIZE_64 0X1  //!< Packet size is 64 Byte
#define FINGERPRINT_PACKET_SIZE_128 0X2 //!< Packet size is 128 Byte
#define FINGERPRINT_PACKET_SIZE_256 0X3 //!< Packet size is 256 Byte

// #define FINGERPRINT_DEBUG

#define DEFAULTTIMEOUT 1000 //!< UART reading timeout in milliseconds

///! Helper class to craft UART packets
struct Adafruit_Fingerprint_Packet {

  /**************************************************************************/
  /*!
      @brief   Create a new UART-borne packet
      @param   type Command, data, ack type packet
      @param   length Size of payload
      @param   data Pointer to bytes of size length we will memcopy into the
     internal buffer
  */
  /**************************************************************************/

  Adafruit_Fingerprint_Packet(uint8_t type, uint16_t length, uint8_t *data) {
    this->start_code = FINGERPRINT_STARTCODE;
    this->type = type;
    this->length = length;
    address[0] = 0xFF;
    address[1] = 0xFF;
    address[2] = 0xFF;
    address[3] = 0xFF;
    if (length < 64)
      memcpy(this->data, data, length);
    else
      memcpy(this->data, data, 64);
  }
  uint16_t start_code; ///< "Wakeup" code for packet detection
  uint8_t address[4];  ///< 32-bit Fingerprint sensor address
  uint8_t type;        ///< Type of packet
  uint16_t length;     ///< Length of packet
  uint8_t data[64];    ///< The raw buffer for packet payload
};

///! Helper class to communicate with and keep state for fingerprint sensors
class Adafruit_Fingerprint {
public:
#if defined(__AVR__) || defined(ESP8266) || defined(FREEDOM_E300_HIFIVE1)
  Adafruit_Fingerprint(SoftwareSerial *ss, uint32_t password = 0x0);
#endif
  Adafruit_Fingerprint(HardwareSerial *hs, uint32_t password = 0x0);
  Adafruit_Fingerprint(Stream *serial, uint32_t password = 0x0);

  void begin(uint32_t baud);

  boolean verifyPassword(void);
  uint8_t getParameters(void);

  uint8_t getImage(void);
  uint8_t image2Tz(uint8_t slot = 1);
  uint8_t createModel(void);

  uint8_t emptyDatabase(void);
  uint8_t storeModel(uint16_t id);
  uint8_t loadModel(uint16_t id);
  uint8_t getModel(void);
  uint8_t deleteModel(uint16_t id);
  uint8_t fingerFastSearch(void);
  uint8_t fingerSearch(uint8_t slot = 1);
  uint8_t getTemplateCount(void);
  uint8_t setPassword(uint32_t password);
  uint8_t LEDcontrol(bool on);
  uint8_t LEDcontrol(uint8_t control, uint8_t speed, uint8_t coloridx,
                     uint8_t count = 0);

  uint8_t setBaudRate(uint8_t baudrate);
  uint8_t setSecurityLevel(uint8_t level);
  uint8_t setPacketSize(uint8_t size);

  void writeStructuredPacket(const Adafruit_Fingerprint_Packet &p);
  uint8_t getStructuredPacket(Adafruit_Fingerprint_Packet *p,
                              uint16_t timeout = DEFAULTTIMEOUT);

  /// The matching location that is set by fingerFastSearch()
  uint16_t fingerID;
  /// The confidence of the fingerFastSearch() match, higher numbers are more
  /// confidents
  uint16_t confidence;
  /// The number of stored templates in the sensor, set by getTemplateCount()
  uint16_t templateCount;

  uint16_t status_reg = 0x0; ///< The status register (set by getParameters)
  uint16_t system_id = 0x0;  ///< The system identifier (set by getParameters)
  uint16_t capacity = 64; ///< The fingerprint capacity (set by getParameters)
  uint16_t security_level = 0; ///< The security level (set by getParameters)
  uint32_t device_addr =
      0xFFFFFFFF;             ///< The device address (set by getParameters)
  uint16_t packet_len = 64;   ///< The max packet length (set by getParameters)
  uint16_t baud_rate = 57600; ///< The UART baud rate (set by getParameters)

private:
  uint8_t checkPassword(void);
  uint8_t writeRegister(uint8_t regAdd, uint8_t value);
  uint32_t thePassword;
  uint32_t theAddress;
  uint8_t recvPacket[20];

  Stream *mySerial;
#if defined(__AVR__) || defined(ESP8266) || defined(FREEDOM_E300_HIFIVE1)
  SoftwareSerial *swSerial;
#endif
  HardwareSerial *hwSerial;
};

#endif















/*!
 * @file Adafruit_Fingerprint.cpp
 *
 * @mainpage Adafruit Fingerprint Sensor Library
 *
 * @section intro_sec Introduction
 *
 * This is a library for our optical Fingerprint sensor
 *
 * Designed specifically to work with the Adafruit Fingerprint sensor
 * ----> http://www.adafruit.com/products/751
 *
 * These displays use TTL Serial to communicate, 2 pins are required to
 * interface
 * Adafruit invests time and resources providing this open source code,
 * please support Adafruit and open-source hardware by purchasing
 * products from Adafruit!
 *
 * @section author Author
 *
 * Written by Limor Fried/Ladyada for Adafruit Industries.
 *
 * @section license License
 *
 * BSD license, all text above must be included in any redistribution
 *
 */

//#include "Adafruit_Fingerprint.h"

// #define FINGERPRINT_DEBUG

/*!
 * @brief Gets the command packet
 */
#define GET_CMD_PACKET(...)                                                    \
  uint8_t data[] = {__VA_ARGS__};                                              \
  Adafruit_Fingerprint_Packet packet(FINGERPRINT_COMMANDPACKET, sizeof(data),  \
                                     data);                                    \
  writeStructuredPacket(packet);                                               \
  if (getStructuredPacket(&packet) != FINGERPRINT_OK)                          \
    return FINGERPRINT_PACKETRECIEVEERR;                                       \
  if (packet.type != FINGERPRINT_ACKPACKET)                                    \
    return FINGERPRINT_PACKETRECIEVEERR;

/*!
 * @brief Sends the command packet
 */
#define SEND_CMD_PACKET(...)                                                   \
  GET_CMD_PACKET(__VA_ARGS__);                                                 \
  return packet.data[0];

/***************************************************************************
 PUBLIC FUNCTIONS
 ***************************************************************************/

#if defined(__AVR__) || defined(ESP8266) || defined(FREEDOM_E300_HIFIVE1)
/**************************************************************************/
/*!
    @brief  Instantiates sensor with Software Serial
    @param  ss Pointer to SoftwareSerial object
    @param  password 32-bit integer password (default is 0)
*/
/**************************************************************************/
Adafruit_Fingerprint::Adafruit_Fingerprint(SoftwareSerial *ss,
                                           uint32_t password) {
  thePassword = password;
  theAddress = 0xFFFFFFFF;

  hwSerial = NULL;
  swSerial = ss;
  mySerial = swSerial;
}
#endif

/**************************************************************************/
/*!
    @brief  Instantiates sensor with Hardware Serial
    @param  hs Pointer to HardwareSerial object
    @param  password 32-bit integer password (default is 0)

*/
/**************************************************************************/
Adafruit_Fingerprint::Adafruit_Fingerprint(HardwareSerial *hs,
                                           uint32_t password) {
  thePassword = password;
  theAddress = 0xFFFFFFFF;

#if defined(__AVR__) || defined(ESP8266) || defined(FREEDOM_E300_HIFIVE1)
  swSerial = NULL;
#endif
  hwSerial = hs;
  mySerial = hwSerial;
}

/**************************************************************************/
/*!
    @brief  Instantiates sensor with a stream for Serial
    @param  serial Pointer to a Stream object
    @param  password 32-bit integer password (default is 0)

*/
/**************************************************************************/

Adafruit_Fingerprint::Adafruit_Fingerprint(Stream *serial, uint32_t password) {

  thePassword = password;
  theAddress = 0xFFFFFFFF;

  hwSerial = NULL;
#if defined(__AVR__) || defined(ESP8266) || defined(FREEDOM_E300_HIFIVE1)
  swSerial = NULL;
#endif
  mySerial = serial;
}

/**************************************************************************/
/*!
    @brief  Initializes serial interface and baud rate
    @param  baudrate Sensor's UART baud rate (usually 57600, 9600 or 115200)
*/
/**************************************************************************/
void Adafruit_Fingerprint::begin(uint32_t baudrate) {
  delay(1000); // one second delay to let the sensor 'boot up'

  if (hwSerial)
    hwSerial->begin(baudrate);
#if defined(__AVR__) || defined(ESP8266) || defined(FREEDOM_E300_HIFIVE1)
  if (swSerial)
    swSerial->begin(baudrate);
#endif
}

/**************************************************************************/
/*!
    @brief  Verifies the sensors' access password (default password is
   0x0000000). A good way to also check if the sensors is active and responding
    @returns True if password is correct
*/
/**************************************************************************/
boolean Adafruit_Fingerprint::verifyPassword(void) {
  return checkPassword() == FINGERPRINT_OK;
}

uint8_t Adafruit_Fingerprint::checkPassword(void) {
  GET_CMD_PACKET(FINGERPRINT_VERIFYPASSWORD, (uint8_t)(thePassword >> 24),
                 (uint8_t)(thePassword >> 16), (uint8_t)(thePassword >> 8),
                 (uint8_t)(thePassword & 0xFF));
  if (packet.data[0] == FINGERPRINT_OK)
    return FINGERPRINT_OK;
  else
    return FINGERPRINT_PACKETRECIEVEERR;
}

/**************************************************************************/
/*!
    @brief  Get the sensors parameters, fills in the member variables
    status_reg, system_id, capacity, security_level, device_addr, packet_len
    and baud_rate
    @returns True if password is correct
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::getParameters(void) {
  GET_CMD_PACKET(FINGERPRINT_READSYSPARAM);

  status_reg = ((uint16_t)packet.data[1] << 8) | packet.data[2];
  system_id = ((uint16_t)packet.data[3] << 8) | packet.data[4];
  capacity = ((uint16_t)packet.data[5] << 8) | packet.data[6];
  security_level = ((uint16_t)packet.data[7] << 8) | packet.data[8];
  device_addr = ((uint32_t)packet.data[9] << 24) |
                ((uint32_t)packet.data[10] << 16) |
                ((uint32_t)packet.data[11] << 8) | (uint32_t)packet.data[12];
  packet_len = ((uint16_t)packet.data[13] << 8) | packet.data[14];
  if (packet_len == 0) {
    packet_len = 32;
  } else if (packet_len == 1) {
    packet_len = 64;
  } else if (packet_len == 2) {
    packet_len = 128;
  } else if (packet_len == 3) {
    packet_len = 256;
  }
  baud_rate = (((uint16_t)packet.data[15] << 8) | packet.data[16]) * 9600;

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to take an image of the finger pressed on surface
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_NOFINGER</code> if no finger detected
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_IMAGEFAIL</code> on imaging error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::getImage(void) {
  SEND_CMD_PACKET(FINGERPRINT_GETIMAGE);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to convert image to feature template
    @param slot Location to place feature template (put one in 1 and another in
   2 for verification to create model)
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_IMAGEMESS</code> if image is too messy
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_FEATUREFAIL</code> on failure to identify
   fingerprint features
    @returns <code>FINGERPRINT_INVALIDIMAGE</code> on failure to identify
   fingerprint features
*/
uint8_t Adafruit_Fingerprint::image2Tz(uint8_t slot) {
  SEND_CMD_PACKET(FINGERPRINT_IMAGE2TZ, slot);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to take two print feature template and create a
   model
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_ENROLLMISMATCH</code> on mismatch of fingerprints
*/
uint8_t Adafruit_Fingerprint::createModel(void) {
  SEND_CMD_PACKET(FINGERPRINT_REGMODEL);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to store the calculated model for later matching
    @param   location The model location #
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_FLASHERR</code> if the model couldn't be written
   to flash memory
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Adafruit_Fingerprint::storeModel(uint16_t location) {
  SEND_CMD_PACKET(FINGERPRINT_STORE, 0x01, (uint8_t)(location >> 8),
                  (uint8_t)(location & 0xFF));
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to load a fingerprint model from flash into buffer 1
    @param   location The model location #
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Adafruit_Fingerprint::loadModel(uint16_t location) {
  SEND_CMD_PACKET(FINGERPRINT_LOAD, 0x01, (uint8_t)(location >> 8),
                  (uint8_t)(location & 0xFF));
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to transfer 256-byte fingerprint template from the
   buffer to the UART
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Adafruit_Fingerprint::getModel(void) {
  SEND_CMD_PACKET(FINGERPRINT_UPLOAD, 0x01);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to delete a model in memory
    @param   location The model location #
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_FLASHERR</code> if the model couldn't be written
   to flash memory
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Adafruit_Fingerprint::deleteModel(uint16_t location) {
  SEND_CMD_PACKET(FINGERPRINT_DELETE, (uint8_t)(location >> 8),
                  (uint8_t)(location & 0xFF), 0x00, 0x01);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to delete ALL models in memory
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_BADLOCATION</code> if the location is invalid
    @returns <code>FINGERPRINT_FLASHERR</code> if the model couldn't be written
   to flash memory
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
uint8_t Adafruit_Fingerprint::emptyDatabase(void) {
  SEND_CMD_PACKET(FINGERPRINT_EMPTY);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to search the current slot 1 fingerprint features to
   match saved templates. The matching location is stored in <b>fingerID</b> and
   the matching confidence in <b>confidence</b>
    @returns <code>FINGERPRINT_OK</code> on fingerprint match success
    @returns <code>FINGERPRINT_NOTFOUND</code> no match made
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::fingerFastSearch(void) {
  // high speed search of slot #1 starting at page 0x0000 and page #0x00A3
  GET_CMD_PACKET(FINGERPRINT_HISPEEDSEARCH, 0x01, 0x00, 0x00, 0x00, 0xA3);
  fingerID = 0xFFFF;
  confidence = 0xFFFF;

  fingerID = packet.data[1];
  fingerID <<= 8;
  fingerID |= packet.data[2];

  confidence = packet.data[3];
  confidence <<= 8;
  confidence |= packet.data[4];

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Control the built in LED
    @param on True if you want LED on, False to turn LED off
    @returns <code>FINGERPRINT_OK</code> on success
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::LEDcontrol(bool on) {
  if (on) {
    SEND_CMD_PACKET(FINGERPRINT_LEDON);
  } else {
    SEND_CMD_PACKET(FINGERPRINT_LEDOFF);
  }
}

/**************************************************************************/
/*!
    @brief   Control the built in Aura LED (if exists). Check datasheet/manual
    for different colors and control codes available
    @param control The control code (e.g. breathing, full on)
    @param speed How fast to go through the breathing/blinking cycles
    @param coloridx What color to light the indicator
    @param count How many repeats of blinks/breathing cycles
    @returns <code>FINGERPRINT_OK</code> on fingerprint match success
    @returns <code>FINGERPRINT_NOTFOUND</code> no match made
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::LEDcontrol(uint8_t control, uint8_t speed,
                                         uint8_t coloridx, uint8_t count) {
  SEND_CMD_PACKET(FINGERPRINT_AURALEDCONFIG, control, speed, coloridx, count);
}

/**************************************************************************/
/*!
    @brief   Ask the sensor to search the current slot fingerprint features to
   match saved templates. The matching location is stored in <b>fingerID</b> and
   the matching confidence in <b>confidence</b>
   @param slot The slot to use for the print search, defaults to 1
    @returns <code>FINGERPRINT_OK</code> on fingerprint match success
    @returns <code>FINGERPRINT_NOTFOUND</code> no match made
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::fingerSearch(uint8_t slot) {
  // search of slot starting thru the capacity
  GET_CMD_PACKET(FINGERPRINT_SEARCH, slot, 0x00, 0x00, (uint8_t)(capacity >> 8),
                 (uint8_t)(capacity & 0xFF));

  fingerID = 0xFFFF;
  confidence = 0xFFFF;

  fingerID = packet.data[1];
  fingerID <<= 8;
  fingerID |= packet.data[2];

  confidence = packet.data[3];
  confidence <<= 8;
  confidence |= packet.data[4];

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Ask the sensor for the number of templates stored in memory. The
   number is stored in <b>templateCount</b> on success.
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::getTemplateCount(void) {
  GET_CMD_PACKET(FINGERPRINT_TEMPLATECOUNT);

  templateCount = packet.data[1];
  templateCount <<= 8;
  templateCount |= packet.data[2];

  return packet.data[0];
}

/**************************************************************************/
/*!
    @brief   Set the password on the sensor (future communication will require
   password verification so don't forget it!!!)
    @param   password 32-bit password code
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::setPassword(uint32_t password) {
  SEND_CMD_PACKET(FINGERPRINT_SETPASSWORD, (uint8_t)(password >> 24),
                  (uint8_t)(password >> 16), (uint8_t)(password >> 8),
                  (uint8_t)(password & 0xFF));
}

/**************************************************************************/
/*!
    @brief   Writing module registers
    @param   regAdd 8-bit address of register
    @param   value 8-bit value will write to register
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
    @returns <code>FINGERPRINT_ADDRESS_ERROR</code> on register address error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::writeRegister(uint8_t regAdd, uint8_t value) {

  SEND_CMD_PACKET(FINGERPRINT_WRITE_REG, regAdd, value);
}

/**************************************************************************/
/*!
    @brief   Change UART baudrate
    @param   baudrate 8-bit Uart baudrate
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::setBaudRate(uint8_t baudrate) {

  return (writeRegister(FINGERPRINT_BAUD_REG_ADDR, baudrate));
}

/**************************************************************************/
/*!
    @brief   Change security level
    @param   level 8-bit security level
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::setSecurityLevel(uint8_t level) {

  return (writeRegister(FINGERPRINT_SECURITY_REG_ADDR, level));
}

/**************************************************************************/
/*!
    @brief   Change packet size
    @param   size 8-bit packet size
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_PACKETRECIEVEERR</code> on communication error
*/
/**************************************************************************/
uint8_t Adafruit_Fingerprint::setPacketSize(uint8_t size) {

  return (writeRegister(FINGERPRINT_PACKET_REG_ADDR, size));
}
/**************************************************************************/
/*!
    @brief   Helper function to process a packet and send it over UART to the
   sensor
    @param   packet A structure containing the bytes to transmit
*/
/**************************************************************************/

void Adafruit_Fingerprint::writeStructuredPacket(
    const Adafruit_Fingerprint_Packet &packet) {

  mySerial->write((uint8_t)(packet.start_code >> 8));
  mySerial->write((uint8_t)(packet.start_code & 0xFF));
  mySerial->write(packet.address[0]);
  mySerial->write(packet.address[1]);
  mySerial->write(packet.address[2]);
  mySerial->write(packet.address[3]);
  mySerial->write(packet.type);

  uint16_t wire_length = packet.length + 2;
  mySerial->write((uint8_t)(wire_length >> 8));
  mySerial->write((uint8_t)(wire_length & 0xFF));

#ifdef FINGERPRINT_DEBUG
  Serial.print("-> 0x");
  Serial.print((uint8_t)(packet.start_code >> 8), HEX);
  Serial.print(", 0x");
  Serial.print((uint8_t)(packet.start_code & 0xFF), HEX);
  Serial.print(", 0x");
  Serial.print(packet.address[0], HEX);
  Serial.print(", 0x");
  Serial.print(packet.address[1], HEX);
  Serial.print(", 0x");
  Serial.print(packet.address[2], HEX);
  Serial.print(", 0x");
  Serial.print(packet.address[3], HEX);
  Serial.print(", 0x");
  Serial.print(packet.type, HEX);
  Serial.print(", 0x");
  Serial.print((uint8_t)(wire_length >> 8), HEX);
  Serial.print(", 0x");
  Serial.print((uint8_t)(wire_length & 0xFF), HEX);
#endif

  uint16_t sum = ((wire_length) >> 8) + ((wire_length) & 0xFF) + packet.type;
  for (uint8_t i = 0; i < packet.length; i++) {
    mySerial->write(packet.data[i]);
    sum += packet.data[i];
#ifdef FINGERPRINT_DEBUG
    Serial.print(", 0x");
    Serial.print(packet.data[i], HEX);
#endif
  }

  mySerial->write((uint8_t)(sum >> 8));
  mySerial->write((uint8_t)(sum & 0xFF));

#ifdef FINGERPRINT_DEBUG
  Serial.print(", 0x");
  Serial.print((uint8_t)(sum >> 8), HEX);
  Serial.print(", 0x");
  Serial.println((uint8_t)(sum & 0xFF), HEX);
#endif

  return;
}

/**************************************************************************/
/*!
    @brief   Helper function to receive data over UART from the sensor and
   process it into a packet
    @param   packet A structure containing the bytes received
    @param   timeout how many milliseconds we're willing to wait
    @returns <code>FINGERPRINT_OK</code> on success
    @returns <code>FINGERPRINT_TIMEOUT</code> or
   <code>FINGERPRINT_BADPACKET</code> on failure
*/
/**************************************************************************/
uint8_t
Adafruit_Fingerprint::getStructuredPacket(Adafruit_Fingerprint_Packet *packet,
                                          uint16_t timeout) {
  uint8_t byte;
  uint16_t idx = 0, timer = 0;

#ifdef FINGERPRINT_DEBUG
  Serial.print("<- ");
#endif

  while (true) {
    while (!mySerial->available()) {
      delay(1);
      timer++;
      if (timer >= timeout) {
#ifdef FINGERPRINT_DEBUG
        Serial.println("Timed out");
#endif
        return FINGERPRINT_TIMEOUT;
      }
    }
    byte = mySerial->read();
#ifdef FINGERPRINT_DEBUG
    Serial.print("0x");
    Serial.print(byte, HEX);
    Serial.print(", ");
#endif
    switch (idx) {
    case 0:
      if (byte != (FINGERPRINT_STARTCODE >> 8))
        continue;
      packet->start_code = (uint16_t)byte << 8;
      break;
    case 1:
      packet->start_code |= byte;
      if (packet->start_code != FINGERPRINT_STARTCODE)
        return FINGERPRINT_BADPACKET;
      break;
    case 2:
    case 3:
    case 4:
    case 5:
      packet->address[idx - 2] = byte;
      break;
    case 6:
      packet->type = byte;
      break;
    case 7:
      packet->length = (uint16_t)byte << 8;
      break;
    case 8:
      packet->length |= byte;
      break;
    default:
      packet->data[idx - 9] = byte;
      if ((idx - 8) == packet->length) {
#ifdef FINGERPRINT_DEBUG
        Serial.println(" OK ");
#endif
        return FINGERPRINT_OK;
      }
      break;
    }
    idx++;
    if ((idx + 9) >= sizeof(packet->data)) {
      return FINGERPRINT_BADPACKET;
    }
  }
  // Shouldn't get here so...
  return FINGERPRINT_BADPACKET;
}









































/*...............................................................


BEGIN THE MAIN CODE

..............................................
*/

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
  NOTICE FOR  tinkercad.com set both board Uart (Rx tx) baud to lesser or equal to 2400

  I made it specifically for tinkcard compatability. So it had been setup already, ie finger.begin(2400)
*/

#include <SoftwareSerial.h>
//#include <Adafruit_Fingerprint.h>

SoftwareSerial mySerial(10, 11); // RX = 10, TX = 11 (matches your updated pins)
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println(F("\n\n=============================================="));
  Serial.println(F(" Adafruit Fingerprint Sensor - Master Test"));
  Serial.println(F("=============================================="));

  finger.begin(2400);
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
