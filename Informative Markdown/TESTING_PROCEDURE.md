# Testing Procedure - CAN Data Logger

## Pre-Test Checklist
- Firmware uploaded successfully
- Serial Monitor set to 115200 baud
- SD card inserted and FAT32 formatted
- RTC and CAN transceiver wired

## Test 1: Boot and Serial Output
Expected startup sequence:
```
Step 1: Initializing Neopixel LED...
Step 2: Initializing RTC...
Step 2.5: Initializing IMU (ADXL345)...
Step 3: Initializing WiFi...
Step 4: Initializing SD Card...
Step 5: Initializing CAN Bus...
```

Pass criteria: no fatal errors and "SYSTEM INITIALIZATION COMPLETE" appears.

## Test 2: WiFi AP and Web UI
- Connect to SSID `CAN_Data_Logger`
- Open `http://192.168.10.1`
- Verify status cards and file list

## Test 3: SD Card and File Creation
- SD card should report type/size
- Log file should be created in `/CAN_Logged_Data/`

## Test 4: CAN Initialization
- Serial should show CAN initialized at 500 kbps
- If CAN fails, it will retry every 5 seconds

## Test 5: CAN Logging
- Connect to active CAN bus
- LED should turn MAGENTA when frames arrive
- Serial should show frames and [LOG]

## Test 6: Live Data Endpoint
- Web UI live section should update
- API: `GET /live?limit=30&since=<seq>` returns JSON frames

## Test 7: File Download
- Use web UI to download a `.NXT` file
- Confirm file size is non-zero

## Test 8: Decode File
- GUI: `Launch_New_Decoder.bat`
- CLI: `python dbc_decode_csv.py log.nxt your.dbc -o decoded.csv`

## Test 9: IMU Data (optional)
- If ADXL345 is connected, check `LinearAccelX/Y/Z` columns after decoding

## Test 10: Stability
- Leave system running for 30+ minutes
- No watchdog resets or SD errors
