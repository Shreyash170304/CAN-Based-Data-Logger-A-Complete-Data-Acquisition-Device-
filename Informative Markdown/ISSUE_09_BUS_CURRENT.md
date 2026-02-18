# Issue 09 - Bus_Current Incorrect

## Symptoms
- Bus_current displayed around -300 A (expected < 40 A).

## Root Cause
- DBC offset/endianness mismatch for Bus_current.

## Fix
- Multi‑candidate correction (raw, no offset, byte‑swap, alt endian) with range selection.

## Files Referenced
- `CJPOWER_with_send .dbc`
- `CAN_Data_Decoder_New.py`
