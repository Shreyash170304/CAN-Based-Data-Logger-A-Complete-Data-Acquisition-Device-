# Issue 05 - IMU Calibration Not Running

## Symptoms
- Calibration messages missing or incomplete.

## Root Cause
- Calibration routine not fully triggered or no feedback.

## Fix
- Added calibration loop with sample logging and final offsets.
- Serial prints added for visibility.

## Files Referenced
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
