# RTC (Real-Time Clock) Setup Guide

## Overview

The CAN logger now supports RTC modules for accurate, real-time timestamps. The system automatically detects and uses the RTC if available, with fallback to compile-time if not found.

## Supported RTC Modules

- **DS3231** (Recommended - Very accurate, temperature compensated)
- **DS1307** (Common, less accurate)
- **PCF8523** (Alternative option)
- **PCF8563** (Alternative option)

## Hardware Connections

### ESP32-C6 Pin Connections

Connect your RTC module to the ESP32-C6:

```
RTC Module    →    ESP32-C6
─────────────────────────────
VCC           →    3.3V (or 5V - check RTC module specs)
GND           →    GND
SDA           →    Pin 6 (default - can be changed in code)
SCL           →    Pin 7 (default - can be changed in code)
Battery       →    3V coin cell (for DS3231/DS1307 to maintain time)
```

### Pin Configuration

Default I2C pins in code:
- **SDA (Data)**: Pin 6
- **SCL (Clock)**: Pin 7

**To change pins**, edit these lines in `Lastry.ino`:
```cpp
#define RTC_SDA_PIN    6       // Change to your SDA pin
#define RTC_SCL_PIN    7       // Change to your SCL pin
```

**Important**: Make sure I2C pins don't conflict with:
- CAN bus pins (TX=5, RX=4)
- SD card SPI pins (CS=18, MOSI=21, MISO=20, SCK=19)

## Library Installation

1. Open Arduino IDE
2. Go to **Tools** → **Manage Libraries**
3. Search for **"RTClib"**
4. Install **"RTClib" by Adafruit** (version 2.1.1 or later)
5. The library supports DS1307, DS3231, PCF8523, and PCF8563

## RTC Module Selection

In `Lastry.ino`, change the RTC object declaration based on your module:

### For DS3231 (Recommended):
```cpp
RTC_DS3231 rtc;
```

### For DS1307:
```cpp
RTC_DS1307 rtc;
```

### For PCF8523:
```cpp
RTC_PCF8523 rtc;
```

### For PCF8563:
```cpp
RTC_PCF8563 rtc;
```

## How It Works

### Automatic Detection
1. On startup, the code tries to initialize the RTC
2. If RTC is found and has valid time → Uses RTC
3. If RTC is found but lost power → Sets to compile-time, warns user
4. If RTC not found → Falls back to compile-time automatically

### Time Sources (Priority Order)
1. **RTC Module** (if connected and working)
2. **Compile-time** (automatic fallback)
3. **Serial SET_TIME command** (manual override)

### Setting Time

#### Method 1: Serial Command (Recommended)
Send via Serial Monitor:
```
SET_TIME,2025,06,20,14,30,45
```
Format: `SET_TIME,YYYY,MM,DD,HH,MM,SS`

This updates both the RTC (if available) and the system time.

#### Method 2: First Power-On
- If RTC has battery: Time persists from last setting
- If RTC lost power: Automatically set to compile-time
- Then use SET_TIME command to set correct time

## Features

✅ **Automatic RTC Detection** - No configuration needed
✅ **Battery Backup** - Time persists through power cycles (with battery)
✅ **Fallback Support** - Works without RTC (uses compile-time)
✅ **Serial Time Setting** - Easy time updates via Serial Monitor
✅ **Accurate Timestamps** - Real-time clock for precise logging

## Troubleshooting

### RTC Not Detected

**Symptoms**: Serial shows "WARNING: RTC not found!"

**Solutions**:
1. Check I2C wiring (SDA/SCL connections)
2. Verify power connections (VCC/GND)
3. Check I2C pin definitions match your board
4. Try different I2C pins if default pins conflict
5. Test RTC with I2C scanner sketch

### RTC Lost Power Warning

**Symptoms**: Serial shows "WARNING: RTC lost power!"

**Solutions**:
1. Install/replace RTC battery (3V coin cell)
2. Use SET_TIME command to set correct time
3. Time will persist after battery is installed

### Incorrect Time

**Solutions**:
1. Use SET_TIME command to set correct time
2. Check RTC battery is installed and working
3. Verify timezone settings if needed

## Testing RTC

To test if RTC is working:

1. Upload code to ESP32
2. Open Serial Monitor (115200 baud)
3. Look for "RTC found and time is valid!" message
4. Send `STATUS` command to see current RTC time
5. Send `SET_TIME,YYYY,MM,DD,HH,MM,SS` to set time
6. Check that timestamps in log files are correct

## Example Serial Output

```
=== CAN Bus Monitor with RTC Support ===
Initializing...
Initializing RTC...
RTC found and time is valid!
RTC Time: 2025-06-20 14:30:45
Using RTC for time synchronization
Base time set to: 2025-06-20 14:30:45
```

## Benefits of RTC

- ✅ **Accurate timestamps** even after power cycles
- ✅ **No manual time updates** needed
- ✅ **Battery backup** maintains time when powered off
- ✅ **Real-time accuracy** (DS3231 is ±2ppm = ~1 minute/year)
- ✅ **Automatic operation** - set once, works forever

## Notes

- DS3231 is recommended for best accuracy
- RTC battery (CR2032) typically lasts 2-3 years
- If RTC not available, system still works with compile-time fallback
- Serial SET_TIME command works with or without RTC

