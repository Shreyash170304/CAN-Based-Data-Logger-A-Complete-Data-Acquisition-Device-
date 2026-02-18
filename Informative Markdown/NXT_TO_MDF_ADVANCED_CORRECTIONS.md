# nxt_to_mdf_advanced.py - All Corrections Applied

This document lists all corrections that were made to `nxt_to_mdf_advanced.py` during development.

## ✅ Correction 1: Type Conversion Fixes
**Issue:** `Error: can only concatenate str (not "float") to str` during CSV parsing

**Fix Applied:**
- Lines 208-211: Explicitly convert `UnixTime` and `Microseconds` to numeric using `pd.to_numeric()`
- Lines 118-138: Robust hex parsing in `parse_data_byte()` to handle both string (hex) and numeric values
- Lines 141-159: Robust CAN ID parsing in `parse_can_id()` to handle various formats

**Code:**
```python
if 'UnixTime' in df.columns:
    df['UnixTime'] = pd.to_numeric(df['UnixTime'], errors='coerce').fillna(0)
if 'Microseconds' in df.columns:
    df['Microseconds'] = pd.to_numeric(df['Microseconds'], errors='coerce').fillna(0)
```

## ✅ Correction 2: MDF.append() Parameter Fix
**Issue:** `MDF.append() got an unexpected keyword argument 'source'`

**Fix Applied:**
- Lines 318-319, 409-410, 437-438, 471-473: Removed `source` parameter from all `mdf.append()` calls
- Changed to use `common_timebase=True` instead

**Code:**
```python
mdf.append(sig, common_timebase=True)  # ✅ Correct
# NOT: mdf.append(sig, source=..., common_timebase=True)  # ❌ Old (incorrect)
```

## ✅ Correction 3: Signal Object Creation
**Issue:** `'dict' object has no attribute 'timestamps'`

**Fix Applied:**
- All `mdf.append()` calls now use `asammdf.Signal` objects instead of dictionaries
- Lines 296-302, 309-315, 399-405, 427-433, 462-468: All use `Signal()` constructor with proper parameters

**Code:**
```python
channels.append(Signal(
    samples=np.full(len(id_df), can_id_int),
    timestamps=id_timestamps,
    name="ID",
    unit="",
    comment=f"CAN Message ID {can_id_hex}"
))
```

## ✅ Correction 4: Monotonic Timestamps
**Issue:** "The following channels do not have monotonous increasing time stamps"

**Fix Applied:**
- Lines 162-199: Implemented `ensure_monotonic_timestamps()` function
- Sorts data by timestamp, filters invalid timestamps, ensures strictly increasing
- Applied to all timestamp arrays before creating Signal objects
- Lines 223, 252, 283, 351, 397, 421, 447: All timestamps are processed through this function

**Code:**
```python
def ensure_monotonic_timestamps(timestamps):
    """Ensure timestamps are strictly increasing"""
    # ... implementation ...
    # Adds 1 microsecond increments to duplicates
    for i in range(1, len(sorted_ts)):
        if sorted_ts[i] <= sorted_ts[i-1]:
            sorted_ts[i] = sorted_ts[i-1] + 1e-6
```

## ✅ Correction 5: Channel Grouping Structure
**Issue:** User requested organized channel groups based on DBC messages and sensor types

**Fix Applied:**
- Lines 265-319: Raw CAN data groups (`CAN_0x{ID}`)
- Lines 321-410: DBC decoded signal groups (`DBC_{MessageName}`)
- Lines 412-438: IMU sensor group
- Lines 440-473: GPS group

**Structure:**
- Each CAN ID gets its own group: `CAN_0x123`
- Each DBC message gets its own group: `DBC_MCU_Status`
- IMU sensors grouped together: `IMU`
- GPS data grouped together: `GPS`

## ✅ Correction 6: String Field Validation
**Issue:** `'NoneType' object has no attribute 'encode'`

**Fix Applied:**
- Lines 403-404: Ensure `unit` and `comment` are always strings (never `None`)
- Uses `str()` conversion and empty string fallback

**Code:**
```python
unit=str(sig_data['unit']) if sig_data['unit'] else "",
comment=str(sig_data['comment']) if sig_data['comment'] else ""
```

## ✅ Correction 7: Hex Parsing Logic (from dbc_decoder_gui_v3_excel.py)
**Issue:** "The dbc decode is taking some of the HEX characters from the decrypted csv"

**Fix Applied:**
- Lines 118-138: Robust hex parsing in `parse_data_byte()`
- Handles: `"80"`, `"0x80"`, `"A5"`, decimal fallback
- Lines 141-159: Similar robust parsing for CAN IDs

**Code:**
```python
def parse_data_byte(x):
    if isinstance(x, str):
        token = x.strip().upper()
        if token.startswith('0X'):
            return int(token, 16)
        elif re.fullmatch(r'[0-9A-F]{1,2}', token):
            return int(token, 16)
        elif any(c in token for c in 'ABCDEF'):
            return int(token, 16)
        else:
            return int(float(token)) & 0xFF
```

## ✅ Correction 8: Encryption Key Encoding
**Issue:** User requested manual encoding of encryption key for better protection

**Fix Applied:**
- Lines 37-43: XOR-encoded key split into 4 parts
- Lines 46-54: `_decode_encryption_key()` function
- Lines 57-66: Key caching to avoid repeated decoding
- Lines 69-84: Encryption functions use `_get_encryption_key()`

**Code:**
```python
_ENCODED_KEY_PART1 = [0x7A, 0x3C, 0xF5, 0x59]
_ENCODED_KEY_PART2 = [0xA4, 0x18, 0x81, 0x4D]
_ENCODED_KEY_PART3 = [0xD2, 0xEF, 0x23, 0x67]
_ENCODED_KEY_PART4 = [0xBE, 0x74, 0xC8, 0x0B]
_XOR_KEY = 0x40

def _decode_encryption_key():
    part1 = [b ^ _XOR_KEY for b in _ENCODED_KEY_PART1]
    # ... combine all parts ...
```

## ✅ Bug Fix: Missing Variable in DBC Section
**Issue:** `can_id_hex` used but not defined in DBC decoding section (line 387)

**Fix Applied:**
- Line 333: Added `can_id_hex = f"0x{can_id_int:X}"` in DBC decoding loop
- Now error messages can properly display the CAN ID

## File Status

✅ **All corrections applied**
✅ **Syntax verified** (no errors)
✅ **Imports working** (all functions accessible)
✅ **Ready for use**

## Public API Functions

The file exports these functions (used by GUI):
- `decrypt_nxt_to_csv(nxt_path)` - Decrypts NXT file to CSV string
- `parse_csv_to_dataframe(csv_content)` - Parses CSV to DataFrame
- `create_mdf_from_dataframe(df, output_path, ...)` - Creates MDF file

All functions are properly implemented and tested.

