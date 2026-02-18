# RGB LED Status Indicator Guide

## Overview

The CAN logger uses an RGB LED to provide visual status feedback. The LED color indicates the current system state at a glance.

## Status Colors

| Color | Status | Meaning |
|-------|--------|---------|
| 🟢 **Green** | All Systems Go | CAN messages receiving AND logging to SD card (perfect state) |
| 🟡 **Yellow** | Partial Operation | CAN messages receiving but NOT logging to SD card |
| 🟠 **Orange** | SD Card Issue | SD card module not working/failed |
| 🩷 **Pink** | RTC Issue | RTC module not working/failed |
| 🔴 **Red** | CAN Bus Failure | CAN bus module not working/failed |
| 🔵 **Blue** | Initializing | System starting up (during setup) |

## LED Configuration Options

### Option 1: NeoPixel/WS2812B (Recommended for Built-in LED)

If your ESP32-C6-Pico has a built-in RGB LED (NeoPixel/WS2812B):

1. **Install Library**: 
   - Arduino IDE → Tools → Manage Libraries
   - Search "Adafruit NeoPixel"
   - Install "Adafruit NeoPixel by Adafruit"

2. **Enable in Code**:
   - Uncomment this line in `Lastry.ino`:
   ```cpp
   #define USE_NEOPIXEL
   #define RGB_LED_PIN    8       // Adjust pin if needed
   ```

3. **Comment out** the separate RGB pins section

### Option 2: Separate RGB Pins (Common Cathode LED)

If using a separate RGB LED with individual pins:

1. **Connect LED**:
   ```
   RGB LED    →    ESP32-C6
   ─────────────────────────
   Red        →    Pin 8 (PWM capable)
   Green      →    Pin 9 (PWM capable)
   Blue       →    Pin 10 (PWM capable)
   Common     →    GND (for common cathode)
   ```

2. **Keep default configuration** (separate pins are default)

3. **Adjust pins** if needed:
   ```cpp
   #define RGB_RED_PIN     8
   #define RGB_GREEN_PIN   9
   #define RGB_BLUE_PIN    10
   ```

## Pin Configuration

### Default Pins (Separate RGB)
- **Red**: Pin 8
- **Green**: Pin 9
- **Blue**: Pin 10

### Default Pin (NeoPixel)
- **Data**: Pin 8

**Important**: Make sure LED pins don't conflict with:
- CAN bus pins (TX=5, RX=4)
- SD card SPI pins (CS=18, MOSI=21, MISO=20, SCK=19)
- RTC I2C pins (SDA=6, SCL=7)

## Status Logic

The LED updates automatically based on:

1. **CAN Bus Status**: Is CAN bus initialized and working?
2. **SD Card Status**: Is SD card ready and logging?
3. **Message Activity**: Are CAN messages being received?

### Status Determination (Priority Order):

```
1. IF CAN bus NOT working:
    → RED (CAN bus module failure)

2. IF CAN working AND receiving messages:
    IF SD card working:
        → GREEN (CAN receiving + logging to SD card)
    ELSE:
        → YELLOW (CAN receiving but NOT logging to SD card)

3. IF CAN working BUT NOT receiving messages:
    IF RTC module NOT working:
        → PINK (RTC module failure)
    ELSE IF SD card module NOT working:
        → ORANGE (SD card module failure)
    ELSE:
        → YELLOW (System ready but no CAN messages)
```

## LED Update Frequency

- Updates every **100 messages** when receiving data
- Updates every **2 seconds** when idle
- Updates immediately when status changes (START_LOG/STOP_LOG)

## Testing LED

1. **Upload code** to ESP32
2. **Observe LED colors**:
   - **Blue** during startup/initialization
   - **Green** when CAN receiving and logging to SD card
   - **Yellow** when CAN receiving but NOT logging to SD card
   - **Orange** when SD card module fails
   - **Pink** when RTC module fails
   - **Red** when CAN bus module fails
   - **Yellow** when system ready but no CAN messages

## Customization

### Change Brightness (NeoPixel only)

Edit in `initializeRGBLED()`:
```cpp
rgbLED.setBrightness(50);  // Change 0-255 (lower = dimmer)
```

### Change Update Frequency

Edit in `loop()`:
```cpp
if (messageCount % 100 == 0) {  // Change 100 to desired frequency
    updateLEDStatus();
}
```

### Add Custom Colors

Edit `updateLEDStatus()` function to add more status conditions:
```cpp
void updateLEDStatus() {
    // Add your custom logic here
    if (customCondition) {
        setLEDColor(255, 0, 255);  // Magenta for custom status
    }
    // ... existing code
}
```

## Troubleshooting

### LED Not Working

1. **Check pin connections** match code configuration
2. **Verify pin numbers** are correct for your board
3. **Check if pins support PWM** (for separate RGB pins)
4. **Install NeoPixel library** if using NeoPixel option
5. **Test with simple color**: Add `setLEDColor(255, 0, 0);` in setup to test red

### Wrong Colors

1. **Check RGB pin order** - may need to swap pins
2. **For NeoPixel**: Check if LED uses GRB instead of RGB order
3. **Adjust color values** if LED is too bright/dim

### LED Flickering

1. **Increase update frequency** (update less often)
2. **Check power supply** - LED may need more current
3. **Add capacitor** (100µF) near LED power if needed

## Status Examples

### Normal Operation (Perfect State)
- **Green LED**: CAN messages receiving AND logging to SD card
- All modules working correctly
- System operating perfectly

### CAN Receiving But Not Logging
- **Yellow LED**: CAN messages receiving but NOT logging to SD card
- SD card may be removed or failed
- Messages visible on Serial but not saved

### SD Card Module Failure
- **Orange LED**: SD card module not working
- May occur when SD card initialization fails
- Check SD card connection and format

### RTC Module Failure
- **Pink LED**: RTC module not working
- System continues with compile-time fallback
- CAN and SD may still work normally

### CAN Bus Failure
- **Red LED**: CAN bus module not working
- System cannot receive messages
- Check CAN wiring and connections

### No CAN Messages (Idle)
- **Yellow LED**: System ready but no CAN messages received
- CAN bus may be idle or no traffic
- Normal if no devices transmitting on bus

## Benefits

✅ **Visual Status** - See system state at a glance
✅ **No Serial Needed** - Know status without computer
✅ **Real-time Updates** - Status changes immediately
✅ **Easy Troubleshooting** - Color indicates what's wrong
✅ **Professional Look** - Clear status indication

