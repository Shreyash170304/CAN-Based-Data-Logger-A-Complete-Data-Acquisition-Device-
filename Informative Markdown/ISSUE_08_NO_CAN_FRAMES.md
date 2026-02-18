# Issue 08 - Decoder: No CAN Frames Processed

## Symptoms
- `No CAN frames were processed into decoded rows`.

## Root Cause
- CSV headers mismatched (BOM/alias), missing Data0–Data7 columns.

## Fix
- Normalize column names and support ID/DLC aliases.
- Pass‑through mode for already‑decoded files.

## Files Referenced
- `CAN_Data_Decoder_New.py`
