# Firmware Deep Dive

This document describes the major firmware modules and flows in `CAN_Data_Logger_Only.ino`.

## Boot Sequence
1. Serial + LED init
2. RTC init (DS3231)
3. IMU init (ADXL345)
4. WiFi AP/STA + web server
5. SD card init + create log file
6. CAN driver init (TWAI)

## Main Loop Flow
```mermaid
flowchart TD
  A[updateIMU()] --> B[handle web client]
  B --> C[read CAN frames]
  C --> D[store live buffer]
  D --> E[write encrypted CSV row]
  E --> F[LED + timeout logic]
  F --> G[periodic status + SD checks]
  G --> H[loop]
```

## Logging Details
- Every CAN frame is formatted as a CSV line
- CSV header is written once per file
- Stream cipher encrypts all CSV bytes after the NXTLOG header
- Rollover occurs at `MAX_FILE_SIZE`

## SD Card Behavior
- Uses `SD.begin()` with SPI at 4 MHz
- Folder `/CAN_Logged_Data/` is created if missing
- Periodic checks detect removal and allow reinsert

## CAN Receiver
- `CAN_RX_QUEUE_LEN = 256` to avoid frame loss
- Processes up to 200 frames per loop pass
- No filtering in firmware (logs all IDs)

## LED Logic
- GREEN only if RTC + SD + WiFi + CAN + log file are ready
- MAGENTA when recent CAN traffic is present
- ORANGE after traffic stops
- RED if a required component is missing
