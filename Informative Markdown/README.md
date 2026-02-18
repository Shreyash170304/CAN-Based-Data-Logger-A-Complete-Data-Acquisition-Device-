# CAN Data Logger for ESP32-C6 Pico

A CAN bus data logger for ESP32-C6 Pico that writes encrypted logs to SD and serves a web UI over WiFi.

## Key Features
- Logs all CAN frames (standard and extended) to SD
- Encrypted .NXT files (NXTLOG header + stream-encrypted CSV payload)
- DS3231 RTC with optional NTP sync (STA mode)
- WiFi AP + optional STA, built-in web UI
- Live data streaming and file management from browser
- Optional ADXL345 IMU linear acceleration logging
- Python decoders with DBC support and multiple export formats

## Hardware
- ESP32-C6 Pico (Waveshare)
- TJA1050 CAN transceiver
- SD card module (SPI)
- DS3231 RTC (I2C)
- ADXL345 IMU (optional, I2C)
- Neopixel LED (optional)

## Pin Map (current firmware)
- CAN TX: GPIO17
- CAN RX: GPIO16
- SD CS: GPIO15
- SD MOSI: GPIO20
- SD MISO: GPIO19
- SD SCK: GPIO18
- RTC/IMU SDA: GPIO22
- RTC/IMU SCL: GPIO23
- Neopixel: GPIO8

## Quick Start
1. Upload `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
2. Connect to WiFi AP `CAN_Data_Logger` (password `CANDataLogger123`)
3. Open `http://192.168.10.1`
4. Connect CAN bus and verify logging (LED turns MAGENTA on traffic)
5. Download logs from `/CAN_Logged_Data/`

See `QUICK_START.md` for full steps.

## Decoding
- GUI (recommended): `Launch_New_Decoder.bat` or `python CAN_Data_Decoder_New.py`
- CLI: `python dbc_decode_csv.py log.nxt your.dbc -o decoded.csv`
- Web (Streamlit): `streamlit run dbc_decoder_web.py` (expects a CSV input)
- Legacy: `OnlyCAN_Data_decoder.py` is for the old CAND/AES format and does not match current logs

## File Format (Summary)
- 16-byte NXTLOG header (signature, version, header size, nonce)
- Encrypted CSV payload (stream cipher)
See `File_Format_Specification.md` and `docs/NXTLOG_FILE_FORMAT.md`.

## Documentation
- `INSTALLATION_GUIDE.md`
- `CONFIGURATION_GUIDE.md`
- `CAN_BUS_CONNECTION_GUIDE.md`
- `TESTING_PROCEDURE.md`
- `TESTING_WITHOUT_CAN.md`
- `CAN_Data_Logger_Documentation.md`
- `docs/ARCHITECTURE_OVERVIEW.md`
- `docs/FIRMWARE_DEEP_DIVE.md`
- `docs/DECODER_DEEP_DIVE.md`
- `docs/WEB_SERVER_API.md`
- `docs/DATA_PIPELINE.md`
- `docs/NXTLOG_FILE_FORMAT.md`

## License
Provided as-is for educational and development use.
