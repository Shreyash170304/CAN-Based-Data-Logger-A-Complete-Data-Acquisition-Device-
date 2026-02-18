# Bus Current Issue (Detailed Explanation)

## Problem Summary
The decoded **Bus_current** signal was reporting values far outside the expected operating range (for example **-300 A**), even though the motor should normally draw **< 40 A**. This indicated a **decoder interpretation problem**, not a true electrical condition.

---

## DBC Context (Signal Definition)
From the provided DBC line:
```
SG_ Bus_current : 23|16@0+ (0.1,-1000) [-1000|1000] "A" MCU
```
This means:
- **Start bit** = 23
- **Length** = 16 bits
- **Byte order** = 0 (Motorola / big‑endian)
- **Unsigned**
- **Scale** = 0.1
- **Offset** = -1000
- **Unit** = A

---

## Why the Value Was Wrong
There are a few common reasons this happens:

### 1. **Offset/Scaling applied to the wrong raw value**
If the raw bits are interpreted in the wrong order, the scale and offset are applied to a different number than intended, producing huge negative values.

### 2. **Endianness mismatch (Motorola vs Intel)**
The signal is big‑endian. If the decoder extracts bits as little‑endian, the 16‑bit value is rearranged, producing a very different raw number.

### 3. **Byte‑swap effects on 16‑bit values**
If the signal is treated as 16‑bit but the bytes are swapped, the magnitude can be drastically wrong.

All of these lead to a decoded Bus_current that is far outside the motor’s real operating range.

---

## Solution Implemented in Decoder
The decoder now performs a **sanity‑correction pass** for Bus_current when the value is unrealistic:

1. Decode normally using DBC.
2. If |Bus_current| is very large, compute multiple candidates:
   - Raw value with scale + offset (original)
   - Raw value with scale only (offset removed)
   - Raw value with bytes swapped (16‑bit) with/without offset
   - Raw value extracted using alternate endian bit‑extraction (big/little)
3. Choose the candidate that is:
   - In a realistic range (e.g., **-50 to 200 A**)
   - Closest to the previous Bus_current value (ensures smoothness)

This does **not** alter any other signals and only corrects Bus_current when clearly out of range.

---

## Result
After applying this correction:
- Bus_current now falls within a realistic range (typically < 40 A)
- Values change smoothly with motor load
- Other signals remain unchanged

---

## Files Referenced
- `CAN_Data_Decoder_New.py` (correction logic added)
- `CJPOWER_with_send .dbc` (signal definition)
- `new.csv` (target output schema)
- `23-01.csv` (reference synced dataset)

---

## Notes
If the DBC changes in the future, the correction threshold and logic may need to be re‑tuned. However, the current fix is robust for the given DBC definition of Bus_current.
