# CAN Data Logger – Technical Specification (derived from `CAN_Data_Logger_Only.ino`)

## 1) Platform & Purpose
- Target MCU: ESP32-C6 Pico (Waveshare), Arduino core using ESP-IDF TWAI driver.
- Role: high-throughput CAN logger to encrypted `.NXT` files; live monitor via Wi-Fi web UI; OTA-capable; IMU and GPS augmentation.

## 2) External Hardware
- CAN transceiver: TJA1050 (GPIO17 TX, GPIO16 RX).
- RTC: DS3231 on shared I2C (GPIO22 SDA, GPIO23 SCL, 100 kHz).
- IMU: ADXL345 on shared I2C; ±2 g full-resolution; 100-sample calibration at boot.
- GPS: NEO-M8N on UART1 (GPIO4 RX, GPIO5 TX) at 9600 baud; parses RMC/GGA; fix timeout 5 s.
- Storage: SD card module on SPI (CS15, MOSI20, MISO19, SCK18) at 4 MHz; FAT32 expected.
- Status LED: single WS2812/NeoPixel on GPIO8 (800 kHz one-wire protocol).

## 3) Pin Map (ESP32-C6)
- CAN_TX 17, CAN_RX 16.
- SD: CS15, MOSI20, MISO19, SCK18.
- I2C (RTC/IMU): SDA22, SCL23.
- GPS: RX4, TX5.
- NeoPixel: 8.

## 4) Core Configuration & Limits
- CAN default speed: 500 kbps (options: 125/250/500/800/1000 kbps via `reinitCANWithSpeed`).
- CAN RX queue 256, TX queue 10; receive batch per loop max 200 frames with 10 ms wait per read.
- Max log file size: 10 MB (~214k frames) then auto-roll to a new file.
- Live buffer 200 frames; default live fetch limit 50 (`limit` and `since` query params).
- Data timeout 2 s; CAN re-init retry every 5 s; SD detect every 2 s; NTP resync every 10 min when idle.

## 4A) Frequencies, Data Rates, and Timings
- CAN bus line rate: 500 kbps default; selectable 125/250/500/800/1000 kbps.
- SPI clock to SD: 4 MHz (`SD_CARD_SPEED`).
- I2C clock (RTC + IMU): 100 kHz.
- IMU data rate: ADXL345 register `BW_RATE=0x0F` → 3200 Hz ODR; firmware polls at 1 kHz (`IMU_SAMPLE_INTERVAL_US=1000`) with low-pass alpha 0.2.
- GPS UART: 9600 baud; fix timeout 5 s.
- NeoPixel signal: 800 kHz (NEO_KHZ800).
- Web status poll cadence (UI script): 1 Hz (`setInterval(refreshStatus, 1000)`).
- NTP resync: every 600,000 ms (10 min) when not actively logging.
- CAN receive loop cadence: up to 200 frames per loop with 10 ms per `twai_receive` call.
- SD flush cadence: every 20 frames; first 5 writes flushed immediately.

## 5) File System & Naming
- Log directory: `/CAN_Logged_Data` (auto-created).
- File name format: `CAN_LOG_YYYYMMDD_HHMMSS.NXT` (`LOG_FILE_PREFIX` = `CAN_LOG_`).

## 6) `.NXT` File Format (encrypted CSV payload)
- Header (16 bytes, unencrypted):
  - Bytes 0-5: ASCII `NXTLOG`.
  - Byte 6: `NXT_LOG_VERSION` = 1.
  - Byte 7: `NXT_HEADER_SIZE` = 16.
  - Bytes 8-11: 32-bit `fileNonce` (random ^ time seeded).
  - Bytes 12-15: reserved = 0.
- Stream cipher:
  - Key (16 bytes): `3A 7C B5 19 E4 58 C1 0D 92 AF 63 27 FE 34 88 4B`.
  - State seed: `fileNonce ^ 0xA5A5A5A5`, advanced by LCG `state = state * 1664525 + 1013904223 + key[i]`.
  - Per byte: `streamByte = (state >> 24) ^ key[state & 0x0F]`; ciphertext = plaintext ^ streamByte.
- Encrypted CSV header: `Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC,Data0..Data7,LinearAccelX,LinearAccelY,LinearAccelZ,Gravity,GPS_Lat,GPS_Lon,GPS_Alt,GPS_Speed,GPS_Course,GPS_Sats,GPS_HDOP,GPS_Time`
- Data line contents:
  - Timestamp `YYYY-MM-DD HH:MM:SS`; UnixTime (s); Microseconds.
  - ID hex (upper), Extended/RTR flags, DLC, Data0-Data7 (padded `00`).
  - IMU: linear accel axes and gravity magnitude (m/s^2) or zeros if no data.
  - GPS: lat/lon (6 dp), alt (m, 2 dp), speed (km/h, 2 dp), course (deg, 2 dp), satellites, HDOP, time text; zeros if no fix.

## 7) CAN Subsystem
- Driver: ESP TWAI normal mode, accept-all filter.
- No auto-baud; fixed speed per config; `reinitCANWithSpeed` stops/uninstalls/reinstalls driver.
- All CAN IDs logged (tracked list 0x100-0x109 exists but no filtering).
- Counters: `messageCount` increments per frame; prints every 10th after first 20; milestones at 100/500/1000.
- Live frame storage feeds `/live` endpoint with sequence numbers.

## 8) Timekeeping
- RTC probe: up to 3 attempts at 0x68; fallback to compile time if absent.
- NTP servers: `pool.ntp.org`, `time.nist.gov`, `time.google.com`; GMT offset +19800 s (IST), DST 0.
- Base time maintained with `bootMillis`; RTC adjusted when NTP succeeds.

## 9) IMU Processing (ADXL345)
- Registers: `DATA_FORMAT` 0x08 (full-res ±2 g), `BW_RATE` 0x0F (3200 Hz), `POWER_CTL` 0x08 (measure).
- Calibration: 100 samples; offsets stored; scale correction to 9.80665 m/s^2.
- Sampling: every ≥1,000 µs; low-pass filter alpha 0.2 on accel and gravity.
- Recovery: after 5 failed reads, re-init I2C and reapply registers.

## 10) GPS Processing (NEO-M8N)
- UART1 at 9600 baud; parses RMC/GGA; NMEA to decimal conversion.
- Fix validity timeout 5 s; LED pulse every 3 s for 120 ms (green on fix, yellow otherwise) when not logging.

## 11) SD Card Handling
- SPI at 4 MHz; presence via `SD.cardType() != CARD_NONE`.
- Auto-detect retries; non-destructive checks after init.
- Flush policy: every 20 frames; header flush immediate; size check every 50 writes.
- If SD absent, CAN still received; logging resumes once card is re-detected and file recreated.

## 12) Networking, Web & OTA
- Wi-Fi mode AP+STA. AP SSID `CAN_Data_Logger`, password `CANDataLogger123`, IP 192.168.10.1/24. STA credentials: SSID `AkashGanga`, password `Naxatra2025` (hardcoded).
- Web server (port 80):
  - `/` control panel; `/files` JSON listing (default folder `/CAN_Logged_Data`).
  - `/download?file=<path>` stream file; `/delete` POST bulk delete via JSON; `/folder` browse helper.
  - `/status` JSON system status; `/live` latest CAN frames (`since`, `limit`); NotFound → 404 text.
- OTA (when ENABLE_OTA=1): hostname `can-data-logger`, port 3232; progress callbacks; available over AP/STA.

## 13) LED Status Map (NeoPixel)
- RED: component missing/failure (CAN/SD/Wi-Fi/log file/RTC).
- BLUE: system or Wi-Fi initializing.
- YELLOW: CAN initializing; GPS pulse when no fix (non-logging).
- CYAN: SD ready; AP active/visible.
- GREEN: all systems ready (no active logging); GPS pulse when fixed.
- MAGENTA: active CAN logging within last 2 s.
- ORANGE: logging stopped after activity (all systems otherwise OK).
- WHITE: system ready but no Wi-Fi connection.
- OFF: not used in normal flow.

## 14) Boot & Recovery Sequence
- Order: Serial → I2C → RTC → IMU → Wi-Fi (starts web/OTA) → SD → CAN → GPS → final status.
- CAN retries every 5 s on failure; SD hot-plug checks every 2 s; NTP sync when idle; LED reflects aggregate state.

## 15) Security/Keys/Passwords
- Encryption key is fixed 16-byte value (see §6); no key rotation in code.
- Wi-Fi AP and STA credentials are hardcoded (see §12); no runtime input validation.
- OTA has no explicit password/auth in current code path.
