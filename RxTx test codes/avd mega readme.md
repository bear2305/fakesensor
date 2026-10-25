Correct. On the Mega 2560 (ATmega2560 chip), the hardware UART pins are fixed in silicon and can't be remapped. The ESP32 can reroute its UARTs to almost any GPIO through its GPIO matrix, but AVR chips have no equivalent.

| Port | RX | TX |
|---|---|---|
| Serial (USB) | 0 | 1 |
| Serial1 | 19 | 18 |
| Serial2 | 17 | 16 |
| Serial3 | 15 | 14 |

Since the Mega has four UARTs, you rarely need to remap anything. You just pick the port whose pins are most convenient.

## If you need different pins

- **SoftwareSerial** lets you choose pins, with limits. TX can be any pin, but RX must be one of 10, 11, 12, 13, 50, 51, 52, 53, A8 to A15. It's also less reliable at high baud rates and can drop characters if the Mega is busy with interrupts.
- **AltSoftSerial** is a more reliable software option, but its pins are fixed (RX = 48, TX = 46 on the Mega).
- **Pins 0 and 1** are wired to the USB-serial chip, so avoid them for another device unless you disconnect USB.

For a Mega talking to an ESP32, using `Serial1`, `Serial2` or `Serial3` is the best choice. Just remember the 5V to 3.3V level issue on the Mega's TX line.
