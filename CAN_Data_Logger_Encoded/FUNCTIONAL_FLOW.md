# CAN Data Logger - Functional Flow (ESP32-C6 Pico)

If your viewer does not render Mermaid, use the plain-text (ASCII) flows below each diagram.

## 1) Boot / Initialization Sequence

```mermaid
flowchart TD
    PWR[Power-on / Reset] --> SERIAL[Serial.begin(115200)]
    SERIAL --> I2C[Init shared I2C (pins 22/23)]
    I2C --> RTC[RTC init (DS3231 @0x68)\n3 attempts; fallback compile-time]
    RTC --> IMU[IMU init (ADXL345)\nprobe DEVID; config regs; 100-sample calib]
    IMU --> WIFI[Wi-Fi init (AP+STA)\nAP SSID CAN_Data_Logger\nstart web routes + server]
    WIFI --> OTA[OTA init (if ENABLE_OTA)]
    OTA --> SD[SD card init (SPI CS15/MOSI20/MISO19/SCK18 @4MHz)\ncreate /CAN_Logged_Data\nopen new .NXT file]
    SD --> CAN[CAN init (TWAI normal mode)\n500 kbps default\naccept-all filter]
    CAN --> GPS[GPS init (UART1 9600 baud)\nRMC/GGA parsing]
    GPS --> READY[System Ready\nLED reflects aggregate state]
```

**ASCII flow (boot/init)**
Power-on/reset -> Serial.begin(115200) -> Init shared I2C (22/23) -> RTC init (3 tries, DS3231 @0x68, fallback compile time) -> IMU init (ADXL345, regs, 100-sample calib) -> Wi-Fi init (AP+STA, start web routes/server) -> OTA init (if enabled) -> SD init (SPI 4MHz, ensure /CAN_Logged_Data, open new .NXT) -> CAN init (TWAI normal mode, 500 kbps, accept-all) -> GPS init (UART1 9600, RMC/GGA) -> System ready (LED shows state)

## 2) Main Loop Execution

```mermaid
flowchart LR
    START[[loop()]] --> IMU_UPD[updateIMU()\n1 kHz poll, LPF alpha=0.2]
    IMU_UPD --> GPS_UPD[updateGPS()\nNMEA parse, 5s fix timeout]
    GPS_UPD --> WEB[Web server handleClient()\n+ ArduinoOTA.handle()]
    WEB --> CAN_RX{{CAN initialized?}}
    CAN_RX -- no --> LED_RED[updateSystemLED()\n(red)] --> PERIODIC
    CAN_RX -- yes --> RECV[twai_receive loop\nmax 200 frames, 10 ms wait]
    RECV -->|per frame| LOG[writeCANMessage()\nNXT encrypt+CSV\nflush every 20 frames]
    LOG --> LIVE[storeLiveFrame()\n200-frame ring for /live]
    LIVE --> LED_MAG[setLED(MAGENTA)\nif active <2s]
    LED_MAG --> DTA[Update dataTransferActive\n(lastDataReceivedTime)]
    RECV --> YIELD[yield() every 5/10 frames]
    RECV --> STATUS_PRINT[Serial prints milestone/10th frame]
    DTA --> PERIODIC[Periodic checks]
    LED_RED --> PERIODIC
    PERIODIC --> STATUS10S[10 s status print + LED refresh]
    PERIODIC --> SDCHK[SD card check every 2 s\nre-detect/remount]
    PERIODIC --> CANRETRY[CAN re-init every 5 s if down]
    PERIODIC --> NTPSYNC[NTP sync every 10 min when idle]
    PERIODIC --> GPS_LED[GPS LED pulse every 3 s when idle]
    GPS_LED --> DELAY[delayMicroseconds(100)]
    NTPSYNC --> DELAY
    CANRETRY --> DELAY
    SDCHK --> DELAY
    STATUS10S --> DELAY
    DELAY --> START
```

**ASCII flow (main loop)**
loop():
- updateIMU (1 kHz poll, LPF alpha 0.2)
- updateGPS (NMEA parse, 5 s fix timeout)
- web server handleClient (+ ArduinoOTA.handle)
- if CAN initialized:
  - twai_receive loop (<=200 frames, 10 ms wait each)
  - per frame: writeCANMessage (encrypt+CSV, flush every 20), storeLiveFrame (ring 200), set LED magenta if active <2 s, update dataTransferActive; yields every 5/10 frames; milestone serial prints
- else: updateSystemLED red
- periodic tasks: LED refresh + status every 10 s; SD check every 2 s; CAN re-init every 5 s if down; NTP sync every 10 min when idle; GPS LED pulse every 3 s when idle
- delayMicroseconds(100) then repeat

## 3) LED Logic (summary)
- MAGENTA: active CAN logging (<2 s since last frame)
- ORANGE: recently stopped logging, all subsystems OK
- GREEN: all required subsystems ready, idle
- CYAN: SD ready / AP active; also Wi-Fi STA+AP success during init
- YELLOW: CAN initializing; GPS pulse without fix when idle
- BLUE: system or Wi-Fi initializing
- RED: missing/failed subsystem (CAN/SD/Wi-Fi/log file/RTC)
- OFF: not used in normal flow

## 4) Data Path Snapshot (frame reception)
CAN transceiver -> TWAI driver -> `twai_receive()` -> `CanFrame` struct -> `storeLiveFrame()` (for `/live`) + `writeCANMessage()` -> encrypt stream -> `.NXT` on SD -> optional download via web `/download`.
