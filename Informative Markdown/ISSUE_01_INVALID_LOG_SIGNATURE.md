# Issue 01 - Invalid Encrypted Log Signature

## Symptoms
- Decoder error: `Invalid encrypted log signature`.

## Root Cause
- Logger encryption/header did not match decoder expectations for NXTLOG format.

## Fix
- Aligned logger header + stream cipher to NXTLOG format used by decoder.
- Ensured same key and header fields across logger/decoder.

## Files Referenced
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
- `CAN_Data_Decoder_New.py`
- `dbc_decoder_gui.py`
- `dbc_decode_csv.py`
- `dbc_decoder_web.py`
