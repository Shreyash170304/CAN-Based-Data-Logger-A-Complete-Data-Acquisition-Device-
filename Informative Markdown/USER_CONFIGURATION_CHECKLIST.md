# User Configuration Checklist

Use this checklist to track configuration changes and verify setup.

## Hardware
- [ ] ESP32-C6 Pico wired
- [ ] TJA1050 CAN transceiver wired (TX=GPIO17, RX=GPIO16)
- [ ] CANH/CANL connected to bus with termination
- [ ] SD card inserted (FAT32)
- [ ] RTC connected (SDA=GPIO22, SCL=GPIO23)
- [ ] IMU connected (optional, same I2C bus)
- [ ] Neopixel connected (optional)

## WiFi
- [ ] AP SSID updated (if needed)
- [ ] AP password updated (if needed)
- [ ] STA credentials set or disabled
- [ ] Web UI reachable at http://192.168.10.1

## CAN
- [ ] CAN speed set (`CAN_SPEED_KBPS`)
- [ ] CAN transceiver powered
- [ ] Bus termination verified
- [ ] Frames received in Serial Monitor

## SD / Logging
- [ ] Log folder `/CAN_Logged_Data/` exists
- [ ] Log file created on boot
- [ ] File rollover configured (MAX_FILE_SIZE)

## File Format / Encryption
- [ ] NXTLOG header present
- [ ] Encryption key matches decoders
- [ ] Decoder successfully opens `.NXT`

## Decoder
- [ ] `CAN_Data_Decoder_New.py` runs
- [ ] DBC file loads
- [ ] Export works (CSV/XLSX/etc)

## Notes
- `trackedCANIDs[]` is present but filtering is disabled in firmware.
- If you change the encryption key, update all decoder scripts.
