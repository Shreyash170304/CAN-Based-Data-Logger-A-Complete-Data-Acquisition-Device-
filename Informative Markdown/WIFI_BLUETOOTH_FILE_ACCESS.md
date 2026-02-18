# WiFi File Access & Live Data Guide

## Overview

The CAN Logger now supports accessing encrypted log files over WiFi (and exposes a live JSON feed), allowing you to download `.nxt` logs or view decoded data without removing the SD card.

## Required Libraries

The code uses standard ESP32 libraries that should be included with ESP32 board support:
- `WiFi.h` - Built-in ESP32 WiFi library
- `WebServer.h` - Built-in ESP32 WebServer library  
- (Bluetooth support was removed to free flash space on ESP32-C6.)

**If you get compilation errors, ensure you have:**
1. ESP32 board support installed in Arduino IDE
2. Latest ESP32 board package (2.0.0 or later) - **Required for ESP32-C6 support**
3. All libraries are up to date

**See `LIBRARY_DOWNLOAD_GUIDE.md` for detailed library installation instructions.**

## Features

✅ **WiFi Access Point** - Create a WiFi network to browse and download files via web browser  
✅ **Encrypted Logs** - Files are stored as `.nxt` and can be decrypted with the Python tools  
✅ **Live Data Endpoint** - `/live` JSON feed for Streamlit dashboards or custom apps  
✅ **Non-Intrusive** - Doesn't interfere with CAN logging  
✅ **Secure** - Basic security checks to prevent unauthorized access

## WiFi Access (Web Browser)

### Setup

1. **Enable WiFi** (already enabled by default):
   ```cpp
   #define ENABLE_WIFI     1
   ```

2. **Configure WiFi Settings** (in `Lastry.ino`):
   ```cpp
   #define WIFI_AP_SSID    "CAN_Logger"      // Network name
   #define WIFI_AP_PASS   "CANLogger123"    // Password (min 8 chars)
   ```

3. **Upload code** to ESP32-C6

### How to Use

1. **Power on ESP32-C6** - WiFi Access Point will start automatically

2. **Connect to WiFi Network**:
   - Network Name (SSID): `CAN_Logger`
   - Password: `CANLogger123`
   - On Windows: Settings → Network & Internet → Wi-Fi → Connect
   - On Mobile: Settings → Wi-Fi → Select "CAN_Logger"

3. **Open Web Browser**:
   - Navigate to: `http://192.168.4.1`
   - Or: `http://192.168.4.1/files` for file list

4. **Browse and Download Files**:
   - See list of all `.nxt` log files
   - Click "Download" to grab any file
   - Files remain encrypted; use `dbc_decode_csv.py` or the GUI to decrypt

### Web Interface Features

- **File Browser**: See all CSV files on SD card
- **File Size**: View file sizes in bytes
- **One-Click Download**: Download files directly
- **System Status**: See CAN, SD, and message count status
- **Mobile Friendly**: Responsive design works on phones/tablets

### Troubleshooting WiFi

**Can't connect to WiFi:**
- Check password is correct (default: `CANLogger123`)
- Ensure ESP32-C6 is powered on
- Try restarting ESP32-C6
- Check Serial Monitor for WiFi status

**Can't access web page:**
- Verify you're connected to "CAN_Logger" network
- Try `http://192.168.4.1` in browser
- Check if SD card is inserted
- Check Serial Monitor for web server status

**Slow file downloads:**
- WiFi AP mode has limited bandwidth
- Large files may take time

## Live Data Endpoint (`/live`)

- URL: `http://192.168.4.1/live`
- Optional query parameters:
  - `since`: last sequence number you have processed (returns newer frames)
  - `limit`: number of frames to include (1‑200, default 50)
- JSON response example:
  ```json
  {
    "status": "ok",
    "latest": 1234,
    "frames": [
      {
        "seq": 1234,
        "time": "2025-06-20 14:30:45.123",
        "unix": 1718884245,
        "micros": 45000,
        "id": "123",
        "extended": false,
        "rtr": false,
        "dlc": 8,
        "data": ["01","02","03","04","05","06","07","08"]
      }
    ]
  }
  ```
- Poll every 0.5‑2 seconds to keep dashboards in sync.
- The Streamlit dashboard (`dbc_realtime_dashboard.py`) consumes this endpoint. See `REALTIME_DASHBOARD.md` for setup.

## Bluetooth File Transfer

> **Note:** Bluetooth support has been disabled in the latest firmware for ESP32-C6 to free flash space. The instructions below are retained for reference only.

### Setup

1. **Enable Bluetooth** (already enabled by default):
   ```cpp
   #define ENABLE_BLUETOOTH 1
   ```

2. **Configure Bluetooth Name**:
   ```cpp
   #define BT_DEVICE_NAME "CAN_Logger"
   ```

3. **Upload code** to ESP32-C6

### How to Use

1. **Pair Bluetooth Device**:
   - On Windows: Settings → Bluetooth → Add device → "CAN_Logger"
   - On Mobile: Settings → Bluetooth → Pair with "CAN_Logger"
   - On Linux: Use `bluetoothctl` or GUI tools

2. **Connect via Serial Terminal**:
   - Use a Bluetooth Serial terminal app
   - Windows: PuTTY, Tera Term, or Serial Bluetooth Terminal
   - Android: Serial Bluetooth Terminal, Bluetooth Terminal
   - iOS: LightBlue, BLE Scanner
   - Connect to "CAN_Logger" device

3. **Send Commands**:
   ```
   LIST          - List all CSV log files
   GET filename.csv - Download a file
   STATUS        - Show system status
   HELP          - Show available commands
   ```

### Bluetooth Commands

#### LIST
Lists all CSV log files on SD card:
```
LIST
=== Log Files ===
1. CAN_LOG_20250620_143045.csv (123456 bytes)
2. CAN_LOG_20250620_150230.csv (234567 bytes)
=== End ===
```

#### GET filename.csv
Downloads a file via Bluetooth:
```
GET CAN_LOG_20250620_143045.csv
FILE_START:CAN_LOG_20250620_143045.csv:123456
[file content here]
FILE_END
Download complete
```

**Note**: The file content is sent as raw data. Save it to a file on your device.

#### STATUS
Shows system status:
```
STATUS
CAN: Ready
SD: Ready
RTC: Available
Messages: 12345
```

#### HELP
Shows available commands:
```
HELP
=== Commands ===
LIST - List all log files
GET filename.csv - Download a file
STATUS - Show system status
HELP - Show this help
```

### Troubleshooting Bluetooth

**Can't pair device:**
- Ensure Bluetooth is enabled on ESP32-C6
- Check Serial Monitor for Bluetooth status
- Try restarting ESP32-C6
- Remove and re-pair device

**Can't send commands:**
- Verify you're connected to "CAN_Logger"
- Check Serial Monitor for Bluetooth messages
- Ensure commands end with newline (`\n`)
- Try sending "HELP" first

**File download incomplete:**
- Bluetooth has limited bandwidth
- Large files may timeout
- Try downloading smaller files
- Use WiFi for large files

## Configuration

### Enable/Disable Features

In `Lastry.ino`, you can enable or disable features:

```cpp
#define ENABLE_WIFI     1       // 1 = enabled, 0 = disabled
#define ENABLE_BLUETOOTH 1      // 1 = enabled, 0 = disabled
```

### Change WiFi Settings

```cpp
#define WIFI_AP_SSID    "CAN_Logger"      // Change network name
#define WIFI_AP_PASS   "CANLogger123"     // Change password (min 8 chars)
#define WIFI_AP_IP     IPAddress(192, 168, 4, 1)  // Change IP if needed
```

### Change Bluetooth Name

```cpp
#define BT_DEVICE_NAME "CAN_Logger"       // Change device name
```

## Security Notes

- **WiFi Password**: Change default password for security
- **File Access**: Only CSV files are accessible
- **Directory Traversal**: Protected against `../` attacks
- **No Authentication**: Currently no login required (for simplicity)

## Performance

- **WiFi AP Mode**: Limited to ~1-2 Mbps
- **Bluetooth**: Limited to ~1 Mbps
- **CAN Logging**: Not affected by WiFi/Bluetooth usage
- **SD Card**: Concurrent access is safe

## Use Cases

### WiFi Access
- ✅ Quick file browsing on phone/tablet
- ✅ Download files to computer
- ✅ Share files with team members
- ✅ View files without removing SD card

### Bluetooth Access
- ✅ Low power file transfer
- ✅ Works without WiFi network
- ✅ Command-line interface
- ✅ Automated file retrieval

## Examples

### Download File via WiFi
1. Connect to "CAN_Logger" WiFi
2. Open `http://192.168.4.1`
3. Click "Download" on desired file
4. File downloads to your device

### Download File via BLE (Android)
1. Install a BLE scanner/terminal app:
   - **Serial Bluetooth Terminal** (supports BLE)
   - **nRF Connect** (free, by Nordic Semiconductor)
   - **BLE Scanner** (various apps available)
2. Scan and connect to "CAN_Logger" BLE device
3. Find the service UUID: `12345678-1234-1234-1234-123456789abc`
4. Connect to RX characteristic (for sending commands)
5. Connect to TX characteristic (for receiving responses)
6. Send: `LIST` (via RX characteristic)
7. Send: `GET CAN_LOG_20250620_143045.csv` (via RX characteristic)
8. Receive file data via TX characteristic
9. Save received data to file

### Download File via BLE (Windows)
1. Use a BLE tool:
   - **nRF Connect Desktop** (free, by Nordic Semiconductor)
   - **BLE Scanner** (Windows Store)
   - **LightBlue** (if available)
2. Scan and connect to "CAN_Logger" BLE device
3. Find service UUID: `12345678-1234-1234-1234-123456789abc`
4. Write commands to RX characteristic
5. Read responses from TX characteristic
6. Send: `LIST` and read response
7. Send: `GET filename.csv` and read file data
8. Save output to file

**Note**: BLE works differently than Classic Bluetooth. You need a BLE-capable app, not a standard Serial Bluetooth terminal.

## Status

✅ **WiFi Access Point**: Fully functional
✅ **Web File Browser**: Working
✅ **Bluetooth File Transfer**: Working
✅ **CAN Logging**: Not affected
✅ **SD Card Access**: Concurrent access safe

## Next Steps

1. Upload updated code to ESP32-C6
2. Connect to WiFi network "CAN_Logger"
3. Open `http://192.168.4.1` in browser
4. Browse and download log files!

Or use Bluetooth:
1. Pair with "CAN_Logger" Bluetooth device
2. Connect via Serial terminal
3. Send `LIST` to see files
4. Send `GET filename.csv` to download

