# Issue 02 - Logger Not Logging After Init

## Symptoms
- Serial showed CAN + SD initialized but no logs.
- LED not turning magenta on traffic.

## Root Cause
- Log file creation not tied correctly to CAN traffic.
- SD checks caused false removal, disabling logging.
- CAN frame processing rate too low under traffic.

## Fix
- Create log file at SD init and on first data flow.
- Simplified SD removal detection.
- Increased CAN RX queue and processing per loop.

## Files Referenced
- `CAN_Data_Logger_Only/CAN_Data_Logger_Only.ino`
