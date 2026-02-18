# Issue 04 - LED Indication Wrong

## Symptoms
- GREEN LED before all subsystems ready.
- MAGENTA not showing on traffic.

## Root Cause
- LED set directly in WiFi init, bypassing readiness logic.

## Fix
- Centralized LED logic: GREEN only when RTC + SD + STA+AP + CAN + log file are ready.
- MAGENTA only when actively logging.

## Files Referenced
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
