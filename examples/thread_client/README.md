# ESP Thread Client

ESP32-H2 proof client for the ESP Home Hub Thread segment.

The client joins the preconfigured `ESPHomeHub` Thread network, resolves the current Thread leader, and sends an
alternating confirmable CoAP request every five seconds:

```text
PUT /device/living-sensor/r

true
```

The hub's CoAP adapter expands this URI to `hub/device/living-sensor/reachability` and forwards the body unchanged.
The compact topic keeps this demonstration request within one IEEE 802.15.4 frame. The configured dataset is public
demonstration data and must not be reused in a deployed network.

## Build and run

From this directory:

```bash
source /path/to/esp-idf/export.sh
idf.py build
idf.py -p /dev/cu.usbmodem1101 flash monitor
```

The OpenThread CLI is available from the USB Serial/JTAG console:

```text
ot state
ot ipaddr
ot dataset active
```
