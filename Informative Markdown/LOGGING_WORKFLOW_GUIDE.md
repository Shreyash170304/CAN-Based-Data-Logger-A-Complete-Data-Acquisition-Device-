# CAN Bus Logger - Complete Logging Workflow Guide

## Overview
This guide explains the proper workflow to record CAN messages on the SD card using the ESP32 CAN Logger device and GUI application.

---

## **Step-by-Step Workflow**

### **Step 1: Connect Device to Laptop**
- Power on the ESP32 device
- Connect it to laptop via USB cable
- LED should display **WHITE** (idle, ready)

### **Step 2: Launch CAN Logger GUI Executable**
- Double-click `CAN_Logger_GUI.exe`
- GUI window should open showing:
  - Connection Setup panel
  - Configuration panel
  - Logging Control panel
  - System Status & Logs panel

### **Step 3: Detect and Connect to Device**
1. Click "🔄 Refresh Ports" button
   - Status should show: `ESP32 device detected: COMx`

2. Select the correct serial port from dropdown (ESP32 should be selected by default)

3. Click "🔌 Connect to Device" button
   - Status should show: `Connected to COMx`
   - Status should show: `Sent: STATUS (checking device state)`

### **Step 4: Set Filename**
1. Enter a filename prefix in the "Log Filename Prefix:" field
   - Example: `CAN_LOG_`
   - Max 20 characters
   - Use only alphanumeric characters and underscores

2. Click "📝 Set Filename" button
   - Status should show: `Filename set: CAN_LOG_`
   - Status should show: `Next step: Use CREATE_FILE to create the log file on SD card.`

### **Step 5: Create File on SD Card**
1. Click "📁 Create File on SD" button
   - A dialog box will appear: "Creating file 'CAN_LOG_' on SD card. Please wait..."
   - Status should show: `Sent: CREATE_FILE - waiting for FILE_CREATED response...`

2. **IMPORTANT**: Wait for the response (wait ~1-2 seconds)
   - Status should show: `✓✓✓ FILE CREATED: CAN_LOG_YYYYMMDD_HHMMSS.CSV`
   - Status should show: `File is ready. Click 'Start Logging' to begin recording data.`
   - **Start Logging button should NOW be ENABLED (green)**

### **Step 6: Start Logging**
1. Review that the "Start Logging" button is **ENABLED** (should be clickable)

2. Click "▶️ Start Logging" button
   - Status should show: `Sent: START_LOG - device should start logging now...`
   - Logging Status should change to: `Logging Status: ✓ ACTIVE` (green)
   - "▶️ Start Logging" button becomes DISABLED
   - "⏹️ Stop Logging" button becomes ENABLED (red)
   - **Live Logged Data section** should start showing CAN messages

3. **CAN data should now be recording to the SD card**

### **Step 7: Monitor Logging**
- Watch the "Live Logged Data" table on the right
- You should see CAN messages appearing with:
  - **Time**: Timestamp
  - **Count**: Message count
  - **ID**: CAN message ID (hex)
  - **Data**: CAN message payload
  - Messages scroll down as new data arrives

### **Step 8: Stop Logging**
1. When ready to stop recording, click "⏹️ Stop Logging" button
   - Status should show: `Sent: STOP_LOG - Waiting for device confirmation`
   - Status should show: `✓ Logging stopped - file saved on SD card`
   - Logging Status should change to: `Logging Status: STOPPED` (red)
   - "▶️ Start Logging" button becomes ENABLED again
   - "⏹️ Stop Logging" button becomes DISABLED
   - **Live Logged Data section** will clear

2. **File is now complete and saved on the SD card**

### **Step 9: Retrieve Data**
- Remove SD card from device and insert into laptop
- Navigate to logged file:
  - Files are named: `CAN_LOG_YYYYMMDD_HHMMSS.CSV`
  - Each row contains one CAN message with timestamp and data
- Open with Excel, Python, or any text editor

---

## **Troubleshooting**

### **Issue: Start Logging button is DISABLED (gray) after creating file**

**Possible Causes:**
1. **FILE_CREATED response not received**
   - Check Status log for: `ERROR: Failed to create log file`
   - Ensure SD card is properly inserted
   - Try clicking "Create File on SD" again

2. **Communication delay**
   - Wait 2-3 seconds after clicking Create File before clicking Start
   - GUI will show `✓✓✓ FILE CREATED` when ready

3. **Device not responding**
   - Disconnect and reconnect the device
   - Click "🔌 Disconnect" then "🔌 Connect to Device"

### **Issue: Logging shows ACTIVE but no data in Live Logged Data section**

**Possible Causes:**
1. **No CAN messages on the bus**
   - Ensure your CAN bus devices are powered and sending messages
   - Click "📊 Get Status" to verify CAN is working

2. **Data format issue**
   - Device may be sending CAN data in a different format
   - Check Status log for incoming messages (lines starting with `RX:`)

### **Issue: Device disconnects frequently**

**Solutions:**
1. Check USB cable quality (use short, high-quality cable)
2. Ensure laptop USB port is supplying power (test other USB devices)
3. Update device firmware if available
4. Try different USB port on laptop

### **Issue: SD Card not detected**

**Solutions:**
1. **Power off device, remove and re-insert SD card**
2. Ensure SD card is properly formatted (FAT32 recommended)
3. Try a different SD card if available
4. Format SD card on device itself if supported

---

## **Technical Details**

### **Serial Commands (if using direct serial terminal)**

```
SET_FILENAME <name>     - Set the log file name prefix (max 20 chars)
CREATE_FILE            - Create a new log file on SD card
START_LOG              - Start recording CAN messages
STOP_LOG               - Stop recording, save file
STATUS                 - Get current device and logging status
SYNC_TIME              - Sync device RTC with NTP
```

### **Expected Device Responses**

| Command | Response |
|---------|----------|
| `SET_FILENAME CAN_LOG_` | `Filename set: CAN_LOG_` |
| `CREATE_FILE` | `FILE_CREATED:CAN_LOG_YYYYMMDD_HHMMSS.CSV` |
| `START_LOG` | `Logging started!` + `Logging to: CAN_LOG_...` |
| `STOP_LOG` | `Logging stopped` |

### **LED Status Indicators**

| Color | Meaning |
|-------|---------|
| 🟢 **GREEN** | Logging is ACTIVE, recording to SD card |
| ⚪ **WHITE** | Device idle, SD card present, ready to log |
| 🟠 **ORANGE** | User stopped logging (file saved) |
| 🔴 **RED** | Error: CAN bus failed OR SD card not detected |

---

## **Advanced Tips**

1. **Multiple logging sessions**
   - Each time you click Create File, a new file is created
   - Old files remain on SD card

2. **File size management**
   - Each file has a 10MB limit
   - Device automatically creates new file when limit reached

3. **Real-time data verification**
   - Live Logged Data shows messages as they arrive
   - Use this to verify CAN messages are being captured

4. **Filename conventions**
   - Use descriptive names: `Engine_Test_`, `Sensor_Cal_`, etc.
   - Avoid special characters: `!@#$%^&*()`
   - Underscores and numbers are safe

---

## **File Format (CSV)**

Logged files are standard CSV format with columns:

```
Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC,Data0,Data1,Data2,Data3,Data4,Data5,Data6,Data7
2025-02-14 10:30:45,1739520645,123456,123,0,0,8,DE,AD,BE,EF,00,00,00,00
2025-02-14 10:30:46,1739520646,234567,456,0,0,8,CA,FE,BA,BE,DE,AD,BE,EF
```

---

## **Quick Reference**

```
✓ Connect → ✓ Set Filename → ✓ Create File → ✓ Start Logging → (recording) → Stop Logging
```

---

## **Still Having Issues?**

Check the **Status Log** panel for error messages. Key indicators:
- `✓✓✓` = Success
- `ERROR:` = Problem, read the message
- `RX:` = Device response (verify expected response appears)

Each step should show clear confirmation in the Status Log before proceeding.
