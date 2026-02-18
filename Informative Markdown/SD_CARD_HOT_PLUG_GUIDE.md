# SD Card Hot-Plug Support Guide

## Overview

The CAN logger now supports **hot-plugging** of SD cards. You can insert or remove the SD card at any time, and the system will automatically detect and use it.

## Features

✅ **Automatic Detection** - SD card detected automatically when inserted
✅ **Hot-Plug Support** - Insert SD card anytime, even after startup
✅ **Automatic Logging** - Logging starts immediately when SD card is detected
✅ **Removal Detection** - System detects when SD card is removed
✅ **Safe Shutdown** - Log file is properly closed when card is removed
✅ **Status Updates** - LED and Serial output show SD card status

## How It Works

### At Startup

1. **SD Card Present**: System initializes SD card and starts logging immediately
2. **SD Card Absent**: System continues without SD card, displays messages on Serial
3. **Auto-Detection Enabled**: System checks for SD card every 2 seconds

### During Operation

1. **SD Card Inserted**: 
   - System detects card within 2 seconds
   - Initializes SD card automatically
   - Creates new log file with current timestamp
   - Starts logging CAN messages immediately
   - LED changes to Green (if CAN is working)

2. **SD Card Removed**:
   - System detects removal within 2 seconds
   - Closes current log file safely
   - Stops logging (messages still displayed on Serial)
   - LED changes to Orange (if CAN messages still coming)

## Usage Scenarios

### Scenario 1: Start Without SD Card, Insert Later

1. Power on ESP32 without SD card
2. System starts, shows: "SD Card not available at startup"
3. CAN messages displayed on Serial Monitor
4. Insert SD card
5. Within 2 seconds: "SD Card detected! Initializing..."
6. Logging starts automatically
7. All future messages saved to SD card

### Scenario 2: SD Card Removed During Operation

1. System is logging to SD card (Green LED)
2. Remove SD card
3. Within 2 seconds: "WARNING: SD Card removed!"
4. Log file closed safely
5. System continues displaying messages on Serial
6. LED changes to Orange (if CAN active)

### Scenario 3: SD Card Re-inserted

1. After removal, system shows Orange LED
2. Re-insert SD card
3. Within 2 seconds: "SD Card detected! Initializing..."
4. New log file created with current timestamp
5. Logging resumes
6. LED changes to Green

## Serial Monitor Messages

### When SD Card is Detected:
```
SD Card detected! Initializing...
SD Card Type: SDHC
SD Card Size: 16384MB
Using RTC time for log file
Creating log file: /CAN_LOG_20250620_143045.csv
SD Card logging enabled!
Logging to: /CAN_LOG_20250620_143045.csv
CAN messages will now be saved to SD card
```

### When SD Card is Removed:
```
WARNING: SD Card removed!
Closed log file: /CAN_LOG_20250620_143045.csv (Size: 12345 bytes)
```

## LED Status Indicators

| LED Color | Meaning |
|-----------|---------|
| 🟢 **Green** | SD card ready, logging active |
| 🟠 **Orange** | CAN messages receiving, but SD card not logging |
| 🔴 **Red** | CAN bus or SD card failed |
| 🟡 **Yellow** | System ready, no CAN messages |

## Technical Details

### Detection Frequency
- SD card checked every **2 seconds**
- Low overhead, doesn't affect CAN message processing

### Timestamp Handling
When SD card is inserted:
- **With RTC**: Uses current RTC time for log filename
- **Without RTC**: Uses compile-time (fallback)
- **Time Updates**: Base time is updated when card is inserted

### File Naming
Log files are named with timestamp when SD card is inserted:
- Format: `CAN_LOG_YYYYMMDD_HHMMSS.csv`
- Example: `CAN_LOG_20250620_143045.csv`

### Safety Features
- ✅ Log file properly closed when card removed
- ✅ No data loss - file is flushed before closing
- ✅ Automatic recovery if file closes unexpectedly
- ✅ Safe to remove card at any time

## Best Practices

1. **Wait for Detection**: After inserting SD card, wait 2-3 seconds for detection
2. **Check Serial Monitor**: Watch for "SD Card detected!" message
3. **Safe Removal**: If possible, wait for current message to finish logging
4. **Format Card**: Use FAT32 format for best compatibility
5. **Card Quality**: Use quality SD cards for reliable operation

## Troubleshooting

### SD Card Not Detected After Insertion

**Symptoms**: Card inserted but not detected

**Solutions**:
1. Wait 2-3 seconds (detection happens every 2 seconds)
2. Check card format (must be FAT32)
3. Try re-inserting card
4. Check Serial Monitor for error messages
5. Verify card is properly seated

### Logging Doesn't Start After Detection

**Symptoms**: Card detected but messages not saved

**Solutions**:
1. Check Serial Monitor for "Failed to create log file" message
2. Verify card has free space
3. Check card is not write-protected
4. Try formatting card (FAT32)
5. Check card is not corrupted

### SD Card Removed But System Still Shows Ready

**Symptoms**: Card removed but `sdCardReady` still true

**Solutions**:
1. Wait 2-3 seconds (removal detection happens every 2 seconds)
2. System should detect removal automatically
3. Check Serial Monitor for "SD Card removed!" message
4. If issue persists, restart system

### File Not Closed Properly

**Symptoms**: Data missing after card removal

**Solutions**:
1. Wait a moment before removing card
2. Check Serial Monitor for "Closed log file" message
3. System automatically closes file on detection
4. If issue persists, use `STOP_LOG` command before removal

## Commands

### Manual Control

You can still use manual commands:

- **`START_LOG`**: Manually start logging (if SD card available)
- **`STOP_LOG`**: Manually stop logging
- **`STATUS`**: Check SD card status

### Example STATUS Output:
```
RTC: Available
RTC Time: 2025-06-20 14:30:45
CAN Bus: Ready
SD Card: Ready
Current File: /CAN_LOG_20250620_143045.csv
File Size: 12345 bytes
Messages Logged: 1234
Receiving Messages: Yes
```

## Benefits

✅ **Flexibility** - No need to power cycle when inserting SD card
✅ **Convenience** - Insert card anytime during operation
✅ **Safety** - Automatic file closing prevents data loss
✅ **User-Friendly** - No manual intervention required
✅ **Reliable** - Automatic detection and recovery

## Notes

- Detection happens every 2 seconds (configurable via `SD_CARD_CHECK_INTERVAL`)
- System continues operating even without SD card
- CAN messages are always displayed on Serial Monitor
- Logging only happens when SD card is present and ready
- LED status updates automatically when SD card status changes

