# Architecture Overview

## System Block Diagram
![System Block Diagram](images/system_block_diagram.svg)

```mermaid
flowchart LR
  CAN[CAN Bus] --> TJA[TJA1050]
  TJA --> ESP[ESP32-C6]
  RTC[DS3231] --> ESP
  IMU[ADXL345] --> ESP
  ESP --> SD[SD Card]
  ESP --> WIFI[WiFi AP/STA]
  WIFI --> WEB[Web UI + API]
  SD --> PC[Decoder Tools]
```

## Subsystems
- CAN interface: TWAI driver, high RX queue, no filtering
- Time: DS3231 RTC with optional NTP sync
- Storage: SD card with rollover and encrypted payload
- Web: file list, download, delete, live frames
- IMU: linear acceleration logged into CSV

## Data Flow (High Level)
```mermaid
flowchart TD
  A[CAN Frame] --> B[Timestamp + IMU]
  B --> C[CSV Row]
  C --> D[Encrypt Stream]
  D --> E[SD .NXT File]
  E --> F[Download]
  F --> G[Decrypt + DBC Decode]
  G --> H[Exports]
```
