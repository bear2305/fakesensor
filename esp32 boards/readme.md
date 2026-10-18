I've converted the sketch to ESP32 in `ESP32_Fingerprint_Master.ino`.

**What changed**
- **SoftwareSerial replaced with hardware UART2.** SoftwareSerial isn't reliable on the ESP32, and it has spare hardware UARTs. The sensor now uses `HardwareSerial mySerial(2)`.
- **New pins.** The default pins are GPIO16 (RX) and GPIO17 (TX), set by `FP_RX_PIN` and `FP_TX_PIN` at the top of the sketch.
- **Pin setup.** The library's `begin()` only applies the default pins, so I call `finger.begin(57600)` and then `mySerial.begin(57600, SERIAL_8N1, RX, TX)` to apply yours.
- **Watchdog-friendly waits.** I added `delay()` calls inside the blocking loops (reading a number, waiting for a finger, and the main loop), so the ESP32 watchdog doesn't reset the board.
- **Prototypes added.** I declared the functions up front to avoid compile ordering issues.

**Wiring**

| ESP32 | Sensor |
|---|---|
| GPIO16 (RX2) | TX |
| GPIO17 (TX2) | RX |
| GND | GND |
| 3.3V | VCC |

The TX and RX lines cross, as in your original. Power the sensor from 3.3V, which also matches the ESP32's logic level. The menu and all test functions work the same as before over the Serial Monitor at 115200 baud.
