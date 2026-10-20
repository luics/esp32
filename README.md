# ESP32 blink

Minimal ESP-IDF GPIO blink example. The default target is classic ESP32 and the default pin is GPIO2; both are board-specific assumptions. Set `CONFIG_BLINK_GPIO` in `idf.py menuconfig` to match the LED wiring. Many ESP32 development boards only have a power LED, not a GPIO-controlled LED. For a bare LED, connect GPIO2 through a 220–1000 Ω resistor to the LED anode and connect its cathode to GND. GPIO2 is a boot strapping pin on classic ESP32, so check the board/chip documentation and use a suitable alternative GPIO if the wiring affects boot.

```sh
eim run "idf.py set-target esp32"
eim run "idf.py build"
```

To run on hardware, connect a compatible ESP32 development board over a USB data cable, identify its serial port, then use `idf.py -p <PORT> flash monitor` in the active ESP-IDF environment. A successful build alone does not verify the LED.
