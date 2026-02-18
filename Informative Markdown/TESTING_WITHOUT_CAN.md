# Testing Without CAN Bus - CAN Data Logger

You can validate most subsystems without connecting to a CAN bus.

## What You Can Test
- Serial startup logs
- WiFi AP and web UI
- SD card detection and log file creation
- RTC/IMU initialization
- Web endpoints (/files, /live)

## Expected Behavior
- CAN may initialize even without CANH/CANL connected, but no frames are received.
- A log file is created on SD if the card is ready.
- LED is GREEN when all required components are ready.
- MAGENTA only appears when CAN frames are received.
- ORANGE appears only after traffic stops.

## Steps
1. Power the board and open Serial Monitor.
2. Connect to WiFi AP and open the web UI.
3. Verify SD status and that a file is created in `/CAN_Logged_Data/`.
4. Check that live data remains empty (no CAN frames).

## Common Issues
- RED LED: check RTC, SD, or CAN initialization errors in Serial.
- No WiFi: ensure `ENABLE_WIFI` is 1 and AP credentials are correct.
