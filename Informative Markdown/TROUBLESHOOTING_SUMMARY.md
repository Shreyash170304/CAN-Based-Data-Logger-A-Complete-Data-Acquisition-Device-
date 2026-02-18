# Troubleshooting Summary (Jan 27-29, 2026)

This document summarizes key issues encountered and their fixes.

1) Invalid encrypted log signature
- Cause: logger/decoder format mismatch
- Fix: aligned to NXTLOG header + stream cipher in firmware and decoder

2) Logger not logging after init
- Cause: log file creation timing and SD handling
- Fix: create log file at SD init and on first data flow

3) Compilation errors
- Cause: malformed string literals and stray code blocks
- Fix: repaired strings and brace balance

4) LED logic inconsistent
- Cause: LED set in multiple places
- Fix: centralized in `updateSystemLED()`

5) IMU calibration and sampling
- Fix: calibration at boot and 1 kHz sampling

6) Decoder UTF-8 errors
- Fix: tolerant decode paths for CSV input

7) Bus_current decoding issues
- Fix: multi-candidate correction in decoder when values are out of range

8) Signal synchronization
- Fix: forward-fill and drop empty rows for smoother plots

Files involved:
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
- `CAN_Data_Decoder_New.py`
- `dbc_decoder_gui.py`, `dbc_decode_csv.py`, `dbc_decoder_web.py`
