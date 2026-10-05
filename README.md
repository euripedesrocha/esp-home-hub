# ESP Home Hub

Demonstration of a home automation hub built on the ESP32-S31 Function Core Board.
## Hardware required

- An ESP32-S31 Function Core Board
## Build and run

Use an ESP-IDF tree that supports ESP32-S31. The firmware is C++ and follows the C++ standard of that tree.

```bash
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the serial port. Leave the monitor with `Ctrl-]`.
