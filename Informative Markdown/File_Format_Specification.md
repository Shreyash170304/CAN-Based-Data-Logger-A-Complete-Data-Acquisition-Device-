# NXTLOG File Format Specification

This document specifies the current `.NXT` file format produced by the logger.

## Overview
- File extension: `.NXT`
- Header: 16 bytes, unencrypted
- Payload: encrypted CSV stream
- Encryption: custom stream cipher (LCG-based)

## File Layout
```
[NXTLOG Header (16 bytes)] [Encrypted CSV bytes...]
```

### Header (16 bytes)
| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 6 | Signature | ASCII "NXTLOG" |
| 0x06 | 1 | Version | 0x01 |
| 0x07 | 1 | Header Size | 0x10 (16) |
| 0x08 | 4 | Nonce | uint32 LE |
| 0x0C | 4 | Reserved | uint32 LE (0) |

### Payload
- The payload is a CSV stream encrypted byte-by-byte.
- First encrypted line is the CSV header line.
- Each subsequent line is one CAN frame with optional IMU/GPS fields.

## CSV Columns (after decryption)
```
Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC,
Data0,Data1,Data2,Data3,Data4,Data5,Data6,Data7,
LinearAccelX,LinearAccelY,LinearAccelZ,Gravity,
GPS_Lat,GPS_Lon,GPS_Alt,GPS_Speed,GPS_Course,GPS_Sats,GPS_HDOP,GPS_Time
```

- `Timestamp` is `YYYY-MM-DD HH:MM:SS`
- `UnixTime` is seconds since epoch
- `Microseconds` is 0-999999 (or higher precision if present)
- `ID` is hex without `0x` (uppercase)
- `Extended` and `RTR` are 0/1
- Data bytes are hex `00`..`FF`
- IMU values are linear acceleration in m/s^2
- GPS fields are currently empty in firmware (reserved)

## Example (decrypted)
```
Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC,Data0,Data1,Data2,Data3,Data4,Data5,Data6,Data7,LinearAccelX,LinearAccelY,LinearAccelZ,GPS_Lat,GPS_Lon,GPS_Alt,GPS_Speed,GPS_Course,GPS_Sats,GPS_HDOP,GPS_Time
2026-01-29 12:00:01,1769678401,123456,100,0,0,8,01,02,03,04,05,06,07,08,0.0123,-0.0456,9.8123,,,,,,,,
```

## Encryption Summary
- Stream cipher initialized from the header nonce
- One keystream byte per payload byte
- Same algorithm is used for encryption and decryption
- See `docs/NXTLOG_FILE_FORMAT.md` for the full algorithm

## File Naming
- Stored under `/CAN_Logged_Data/`
- Name format: `CAN_LOG_YYYYMMDD_HHMMSS.NXT`

## Notes
- Older CAND/AES logs are not compatible with current firmware.
- Use `CAN_Data_Decoder_New.py` or `dbc_decode_csv.py` for this format.
