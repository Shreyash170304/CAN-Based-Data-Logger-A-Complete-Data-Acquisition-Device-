# Library Download Guide for ESP32-C6 CAN Logger

## Required Libraries

All libraries listed here are available through the Arduino Library Manager. This is the easiest way to install them.

## Installation Method 1: Arduino Library Manager (Recommended)

1. Open **Arduino IDE**
2. Go to **Tools → Manage Libraries...** (or press `Ctrl+Shift+I`)
3. Search for each library name below
4. Click **Install** for each library

## Required Libraries

### 1. **RTClib by Adafruit**
   - **Purpose**: Real-Time Clock (RTC) support for DS1307, DS3231, PCF8523, PCF8563
   - **Search**: `RTClib`
   - **Author**: Adafruit
   - **Version**: Latest
   - **Library Manager Link**: 
     - Arduino IDE: Tools → Manage Libraries → Search "RTClib"
   - **GitHub**: https://github.com/adafruit/RTClib
   - **Direct Download**: https://github.com/adafruit/RTClib/archive/refs/heads/master.zip

### 2. **Adafruit NeoPixel** (Optional - only if using NeoPixel LED)
   - **Purpose**: Control NeoPixel/WS2812B RGB LEDs
   - **Search**: `Adafruit NeoPixel`
   - **Author**: Adafruit
   - **Version**: Latest
   - **Library Manager Link**:
     - Arduino IDE: Tools → Manage Libraries → Search "Adafruit NeoPixel"
   - **GitHub**: https://github.com/adafruit/Adafruit_NeoPixel
   - **Direct Download**: https://github.com/adafruit/Adafruit_NeoPixel/archive/refs/heads/master.zip

### 3. **ESP32-TWAI-CAN** (Built-in with ESP32 Board Package)
   - **Purpose**: CAN bus communication for ESP32
   - **Note**: This is included with ESP32 board support package
   - **Installation**: Install ESP32 board package (see below)
   - **GitHub**: https://github.com/nhatuan84/esp32-arduino-can

## Built-in ESP32 Libraries (No Installation Needed)

These libraries are included with the ESP32 board support package:

### 1. **WiFi.h**
   - **Purpose**: WiFi connectivity
   - **Status**: Built-in with ESP32 board package
   - **No installation needed**

### 2. **WebServer.h**
   - **Purpose**: HTTP web server
   - **Status**: Built-in with ESP32 board package
   - **No installation needed**

### 3. **BLEDevice.h, BLEServer.h, BLEUtils.h, BLE2902.h**
   - **Purpose**: Bluetooth Low Energy (BLE) support
   - **Status**: Built-in with ESP32 board package
   - **No installation needed**

### 4. **SD.h**
   - **Purpose**: SD card file system access
   - **Status**: Built-in with ESP32 board package
   - **No installation needed**

### 5. **SPI.h**
   - **Purpose**: SPI communication (for SD card)
   - **Status**: Built-in with Arduino/ESP32
   - **No installation needed**

### 6. **Wire.h**
   - **Purpose**: I2C communication (for RTC)
   - **Status**: Built-in with Arduino/ESP32
   - **No installation needed**

## ESP32 Board Package Installation

If you haven't installed ESP32 board support:

1. Open **Arduino IDE**
2. Go to **File → Preferences**
3. In **Additional Board Manager URLs**, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Go to **Tools → Board → Boards Manager...**
5. Search for **"esp32"**
6. Install **"esp32 by Espressif Systems"** (version 2.0.0 or later)
7. Select your board: **Tools → Board → ESP32 Arduino → ESP32C6 Dev Module**

## Installation Method 2: Manual Installation

If Library Manager doesn't work, you can install libraries manually:

### Steps:
1. Download the library ZIP file from GitHub (links provided above)
2. In Arduino IDE: **Sketch → Include Library → Add .ZIP Library...**
3. Select the downloaded ZIP file
4. Restart Arduino IDE

## Library Versions Tested

- **RTClib**: v2.1.1 or later
- **Adafruit NeoPixel**: v1.11.0 or later
- **ESP32 Board Package**: v2.0.0 or later (for ESP32-C6 support)

## Troubleshooting

### Library Not Found
- Ensure ESP32 board package is installed
- Check Arduino IDE version (1.8.19 or later recommended)
- Try restarting Arduino IDE

### Compilation Errors
- Update all libraries to latest versions
- Ensure ESP32 board package is up to date
- Check that you selected the correct board (ESP32C6 Dev Module)

### BLE Not Working
- ESP32-C6 uses BLE (not Classic Bluetooth)
- Ensure ESP32 board package v2.0.0 or later
- BLE libraries are built-in, no separate installation needed

### WiFi Not Working
- WiFi libraries are built-in with ESP32
- No separate installation needed
- Check ESP32 board package version

## Quick Installation Checklist

- [ ] Install ESP32 board package (v2.0.0+)
- [ ] Install RTClib library
- [ ] Install Adafruit NeoPixel (if using NeoPixel LED)
- [ ] Select board: ESP32C6 Dev Module
- [ ] Select correct COM port
- [ ] Upload code

## Direct Download Links

### RTClib
- **GitHub**: https://github.com/adafruit/RTClib
- **ZIP Download**: https://github.com/adafruit/RTClib/archive/refs/heads/master.zip

### Adafruit NeoPixel
- **GitHub**: https://github.com/adafruit/Adafruit_NeoPixel
- **ZIP Download**: https://github.com/adafruit/Adafruit_NeoPixel/archive/refs/heads/master.zip

### ESP32 Board Package
- **Board Manager URL**: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- **Install via**: Tools → Board → Boards Manager → Search "esp32"

## Summary

**Required Libraries (via Library Manager):**
1. RTClib by Adafruit
2. Adafruit NeoPixel (optional, only if using NeoPixel LED)

**Built-in Libraries (no installation needed):**
- WiFi.h
- WebServer.h
- BLEDevice.h, BLEServer.h, BLEUtils.h, BLE2902.h
- SD.h
- SPI.h
- Wire.h

**ESP32 Board Package:**
- Install via Boards Manager
- Includes all ESP32-specific libraries

All other libraries are built-in with ESP32 board support package!

