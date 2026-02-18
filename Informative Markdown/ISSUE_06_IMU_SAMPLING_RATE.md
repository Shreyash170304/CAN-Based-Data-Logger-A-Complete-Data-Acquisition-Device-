# Issue 06 - IMU Sampling Rate Too Low

## Symptoms
- IMU sampling ~100 Hz instead of 1 kHz+.

## Fix
- ADXL345 BW_RATE set to 3200 Hz.
- Sampling gate set to 1000 µs.
- I2C clock increased to 400 kHz.

## Files Referenced
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
