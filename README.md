# ESP Weather Display

Minimal ESP32 weather screen using LVGL on a 2.4" SPI TFT with touch.

Setup
- Set WiFi and location in `main/config.h`.

Wiring (ILI9341 + XPT2046)
- VCC -> 3.3V
- GND -> GND
- SCK -> GPIO18
- MOSI -> GPIO23
- MISO -> GPIO19
- LCD CS -> GPIO5
- LCD DC -> GPIO21
- LCD RST -> GPIO22
- LCD BL -> GPIO4
- TP CS -> GPIO15
- TP IRQ -> GPIO34

Build / Flash
```
idf.py set-target esp32
idf.py build
idf.py flash monitor
```