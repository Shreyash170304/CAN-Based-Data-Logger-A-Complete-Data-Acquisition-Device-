# Serial Monitor Troubleshooting Guide

## Problem: No Output on Serial Monitor

If you're not seeing any output on the Serial Monitor, follow these steps:

## Step 1: Check Serial Monitor Settings

1. **Open Serial Monitor** in Arduino IDE
2. **Set Baud Rate** to `115200`
3. **Set Line Ending** to "Both NL & CR" or "Newline"
4. **Check Auto-scroll** is enabled

## Step 2: Check COM Port

1. **Verify COM Port** is correct:
   - Tools → Port → Select your ESP32-C6 port
   - If port doesn't appear, check USB cable connection
   - Try different USB port on computer

2. **Check if port is in use**:
   - Close other programs using the port
   - Close other Serial Monitor windows

## Step 3: Verify Code Upload

1. **Check for upload errors**:
   - Look for "Upload successful" message
   - Check for any compilation errors

2. **Try re-uploading**:
   - Click Upload button again
   - Wait for "Done uploading" message

## Step 4: Check for Library Issues

If you enabled NeoPixel (`#define USE_NEOPIXEL`):

1. **Install NeoPixel Library**:
   - Tools → Manage Libraries
   - Search "Adafruit NeoPixel"
   - Install "Adafruit NeoPixel by Adafruit"

2. **OR Disable NeoPixel**:
   - Comment out `#define USE_NEOPIXEL` in code
   - Use separate RGB pins instead

## Step 5: Hardware Reset

1. **Press Reset Button** on ESP32-C6 board
2. **Watch Serial Monitor** immediately after reset
3. **Look for startup messages**

## Step 6: Test Serial Communication

Add this simple test at the very beginning of `setup()`:

```cpp
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("TEST: Serial is working!");
    Serial.println("If you see this, Serial is OK");
    // ... rest of code
}
```

If you see "TEST: Serial is working!" then Serial is fine.

## Step 7: Check for Blocking Code

The code has been updated to:
- ✅ Remove infinite `while(!Serial)` blocking
- ✅ Add timeout to Serial wait (2 seconds max)
- ✅ Continue even if CAN bus fails
- ✅ Add debug messages throughout initialization

## Step 8: Common Issues

### Issue: Serial Monitor Opens But No Output

**Solution**: 
- Press Reset button on board
- Check baud rate matches (115200)
- Try closing and reopening Serial Monitor

### Issue: Upload Works But No Serial Output

**Solution**:
- Board might be stuck in bootloader
- Press Reset button
- Try uploading again

### Issue: "Port Already in Use"

**Solution**:
- Close all Serial Monitor windows
- Close other Arduino IDE windows
- Unplug and replug USB cable
- Restart Arduino IDE

### Issue: Code Compiles But Won't Upload

**Solution**:
- Check board selection: Tools → Board → ESP32-C6
- Check port selection
- Try holding BOOT button while uploading
- Check USB cable (data cable, not charge-only)

## Step 9: Verify Expected Output

After successful initialization, you should see:

```
========================================
=== CAN Bus Monitor with RTC Support ===
========================================
Starting initialization...
RGB LED (NeoPixel) initialized
Initializing RTC...
[or RTC warnings if not connected]
Initializing CAN bus...
CAN Bus initialized successfully!
Speed: 500 kbps
...
System ready! CAN messages will be logged to SD card.
```

## Step 10: Still Not Working?

1. **Try Minimal Test Code**:
   ```cpp
   void setup() {
       Serial.begin(115200);
       delay(2000);
       Serial.println("Hello World!");
   }
   
   void loop() {
       Serial.println("Loop running...");
       delay(1000);
   }
   ```

2. **If minimal code works**: Issue is in main code
3. **If minimal code doesn't work**: Hardware/USB issue

## Quick Checklist

- [ ] Serial Monitor baud rate = 115200
- [ ] Correct COM port selected
- [ ] Code uploaded successfully
- [ ] No compilation errors
- [ ] USB cable is data-capable
- [ ] Board reset after upload
- [ ] NeoPixel library installed (if using)
- [ ] Serial Monitor auto-scroll enabled

## Still Having Issues?

1. Try a different USB cable
2. Try a different USB port
3. Try a different computer
4. Check ESP32-C6 board documentation
5. Verify board is ESP32-C6 (not ESP32 or ESP32-S2)

