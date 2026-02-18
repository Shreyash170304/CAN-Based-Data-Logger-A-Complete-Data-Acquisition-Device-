# Installation Guide - CAN Data Logger

This guide installs firmware and decoder for the ESP32-C6 logger.

## Hardware Required
- ESP32-C6 Pico (Waveshare)
- TJA1050 CAN transceiver
- SD card module (SPI) + microSD (FAT32)
- DS3231 RTC module (recommended)
- ADXL345 IMU (optional)
- Neopixel LED (optional)
- Wires and a stable 3.3V/5V supply as required by the modules

## Wiring (current firmware pin map)
| Signal | ESP32-C6 GPIO | Module Pin |
|--------|----------------|------------|
| CAN TX | 17 | TJA1050 TXD |
| CAN RX | 16 | TJA1050 RXD |
| SD CS | 15 | SD CS |
| SD MOSI | 20 | SD MOSI |
| SD MISO | 19 | SD MISO |
| SD SCK | 18 | SD SCK |
| RTC/IMU SDA | 22 | DS3231 SDA / ADXL345 SDA |
| RTC/IMU SCL | 23 | DS3231 SCL / ADXL345 SCL |
| Neopixel | 8 | DIN |

Notes:
- RTC and IMU share the same I2C bus (SDA/SCL).
- CANH/CANL must be connected to the active bus for data reception.
- Use 120 ohm termination at each end of the CAN bus.

## Software Required
- Arduino IDE 2.x (or 1.8.19+)
- ESP32 board package (Espressif)
- Arduino libraries: RTClib, Adafruit NeoPixel (others are built-in)
- Python 3.8+ for decoder tools

## Firmware Upload
1. Open `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino` in Arduino IDE.
2. Select board: `ESP32C6 Dev Module`.
3. Enable `CDC On Boot` in Tools.
4. Set serial baud to 115200.
5. Update WiFi credentials if needed (see `CONFIGURATION_GUIDE.md`).
6. Upload the sketch.

## Decoder Setup
1. Install Python packages:
   ```
   python -m pip install -r requirements.txt
   ```
2. Launch the GUI decoder:
   ```
   Launch_New_Decoder.bat
   ```

## First Boot Checklist
- Serial output shows all initialization steps
- WiFi AP is visible (`CAN_Data_Logger`)
- Web UI loads at `http://192.168.10.1`
- SD card is READY and a log file is created in `/CAN_Logged_Data/`
