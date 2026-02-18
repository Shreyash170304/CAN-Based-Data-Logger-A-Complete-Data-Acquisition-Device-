# Issue 07 - Decoder UTF-8 Decode Error

## Symptoms
- `UnicodeDecodeError: utf-8 codec can't decode byte...` during `pd.read_csv`.

## Root Cause
- Non-UTF8 bytes in decrypted CSV (partial or corrupt files).

## Fix
- Added encoding fallbacks: UTF‑8 → Latin‑1 → errors="replace".

## Files Referenced
- `CAN_Data_Decoder_New.py`
