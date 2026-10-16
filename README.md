# ESP Home Hub

Demonstration of a home automation hub built on the ESP32-S31 Function Core Board.

> **Warning:** This repository contains demonstration code and is not ready for production use.

## Hardware required

- An ESP32-S31 Function Core Board
## Build and run

Use an ESP-IDF tree that supports ESP32-S31. The firmware is C++ and follows the C++ standard of that tree.

```bash
idf.py build
idf.py -p PORT flash monitor
```

Replace `PORT` with the serial port. Leave the monitor with `Ctrl-]`.

## Thread demonstration client

[`examples/thread_client`](examples/thread_client) is an independent ESP32-H2 ESP-IDF project. It joins the hub's
Thread network and sends confirmable CoAP reachability updates to the ESP32-S31. Build it from its own directory so
its ESP32-H2 configuration remains separate from the hub firmware.
