# Quick Start Guide - CAN Data Logger

## Prerequisites
- ESP32-C6 Pico flashed with `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
- SD card inserted (FAT32)
- CAN transceiver wired (TJA1050)
- Serial Monitor at 115200 baud

## Step 1: Power On and Check Serial Output
Open Serial Monitor and reset the board. You should see:

```
========================================
=== CAN DATA LOGGER - ESP32-C6 PICO ===
========================================
>>> CODE STARTED - SERIAL IS WORKING <<<
Step 1: Initializing Neopixel LED...
Step 2: Initializing RTC...
Step 2.5: Initializing IMU (ADXL345)...
Step 3: Initializing WiFi...
Step 4: Initializing SD Card...
Step 5: Initializing CAN Bus...
```

## Step 2: Connect to WiFi AP
- SSID: CAN_Data_Logger
- Password: CANDataLogger123
- Open: http://192.168.10.1

## Step 3: Verify Web UI
- Check status cards for RTC, SD, CAN, WiFi
- File list should show `/CAN_Logged_Data/` and any existing logs
- Live data section should be empty until CAN traffic arrives

## Step 4: Connect CAN Bus and Start Logging
- Connect CANH/CANL to the active bus
- When frames arrive, LED turns MAGENTA
- Serial shows "DATA LOGGING STARTED"

## Step 5: Download Logs
- Use web UI to download .NXT files
- Files live under `/CAN_Logged_Data/`

## Step 6: Decode Logs
- GUI (recommended): `Launch_New_Decoder.bat`
- CLI: `python dbc_decode_csv.py log.nxt your.dbc -o decoded.csv`
- Streamlit: `streamlit run dbc_decoder_web.py` (CSV input)

## LED Quick Reference
| Color | Meaning |
|------|---------|
| BLUE | Boot/initialization |
| YELLOW | CAN driver initializing |
| CYAN | WiFi AP ready |
| GREEN | All systems ready (RTC + SD + WiFi + CAN + log file) |
| MAGENTA | Active logging (CAN frames received recently) |
| ORANGE | Logging stopped after traffic (timeout) |
| RED | Missing/failed component |

Note: ORANGE appears after traffic stops. Before any CAN traffic, the system is typically GREEN if all components are ready.

## Troubleshooting Fast Checks
- No WiFi: wait 20 seconds, reboot, verify SSID/password
- No SD: reformat FAT32 and check wiring
- No CAN data: verify transceiver power, CANH/CANL, termination, and bus speed
