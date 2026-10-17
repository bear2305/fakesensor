I'll port it to a Wokwi chip: `chip.json` stays the same, and `chip.c` holds the full sensor logic.

I ported the whole sensor to a Wokwi chip. All commands, the DOWNCHAR/UPCHAR handling and the packet parser carry over with the same protocol behavior, but I haven't compiled or run it in Wokwi. `chip.json` is unchanged, and `chip.c` replaces your sketch.

## What changed from the Arduino version

| Arduino | Wokwi chip |
|---|---|
| `setup()` | `chip_init()` |
| `loop()` → `readIncomingPackets()` | `on_uart_rx()` runs once per received byte, driving the same state machine |
| `loop()` → `checkDownCharTimeout()` | A repeating 100 ms timer calls `on_timeout_timer()` |
| `SensorSerial.write()` + `flush()` | Bytes go into a TX ring buffer and `uart_write()` sends them. `on_uart_write_done()` sends whatever queued up in the meantime |
| `millis()` | `get_sim_nanos() / 1000000` |
| `Serial.print(F(...))` | `printf`, wrapped in a `DBG()` macro you can turn off with `#define DEBUG 0` |
| `digitalRead()` | `pin_read()` |

## Things to know

- **TX queue:** Wokwi's `uart_write()` is asynchronous and can only handle one write at a time. A UPCHAR response sends several packets back to back, so I added a 2048-byte queue. Without it, the later packets would be dropped.
- **Switch polarity:** The switches are still active LOW with pull-ups, as in your sketch. If your diagram wires them to VCC instead, set `SWITCHES_ACTIVE_LOW` to `0`.
- **Wiring:** Connect the chip's `Tx` to your MCU's Rx pin and its `Rx` to your MCU's Tx pin. Use `Serial2` or a similar spare hardware UART on an ESP32. Set the baud to 57600, which `finger.begin(57600)` does in Adafruit_Fingerprint.
- **Debug timing:** Debug `printf` calls don't affect UART timing in the simulation, so the checksum-error problem from the Uno's SoftwareSerial won't occur.
- **Globals:** The state is kept in global variables instead of a struct, so two copies of this chip in one diagram would share state. Tell me if you need multiple instances, and I'll move it into a `chip_state_t`.

If the build complains about anything (the `uart_config_t` field names are the most likely candidate if your Wokwi version differs), paste the error and I'll fix it.




**The error:** `pin_mode_t` isn't a type in the Wokwi chips API. `pin_init()` just takes a plain integer mode. I changed those two lines to `uint32_t`, and the updated `chip.c` is attached. That should clear the error. If another one appears, paste it and I'll fix it.

**VCC and logic voltage:**

- **In the simulator:** My code never reads the `VCC` or `GND` pins, so they are only labels on the chip. I'm not certain whether Wokwi derives the chip's output logic level from the voltage on `VCC`. To be safe, wire `VCC` to the Uno's 5V (or the ESP32's 3V3) and `GND` to GND. With that wiring the levels match the board you're using, and no level shifting is needed in the simulation.
- **On real hardware:** The two common modules differ.
  - **R307:** Powered from roughly 3.6–6V, with 3.3V logic on its Tx pin.
  - **AS608:** Powered from 3.3V, with 3.3V logic.
  - **ESP32:** Its 3.3V logic matches both modules, so you can wire them directly.
  - **Uno:** Its 5V Tx can overdrive a 3.3V module's Rx, so put a voltage divider or level shifter on that line.

When you move to the ESP32, only your wiring and the MCU sketch change. Use a hardware UART such as `Serial2` instead of SoftwareSerial, and keep the baud at 57600. The chip code stays the same.
