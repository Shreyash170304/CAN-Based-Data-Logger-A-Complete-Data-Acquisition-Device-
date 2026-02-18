# Source Files Backup

This folder contains backup copies of the original source files for the NXT to MDF Converter.

## Files Included

- **`nxt_to_mdf_advanced.py`** - Advanced converter with DBC decoding support
  - Includes XOR-encoded encryption key (split into 4 parts)
  - DBC decoding functionality
  - Channel grouping (CAN_0x{ID}, DBC_{MessageName}, IMU, GPS)
  - All helper functions with proper names

- **`nxt_to_mdf_gui.py`** - GUI interface for the converter
  - Tkinter-based user interface
  - File selection dialogs
  - Conversion progress logging
  - Error handling

## Important Notes

### Key Encoding
The encryption key in `nxt_to_mdf_advanced.py` is XOR-encoded and split into 4 parts for better protection:
- `_ENCODED_KEY_PART1` through `_ENCODED_KEY_PART4`
- XOR key: `0x40`
- Decoded at runtime using `_decode_encryption_key()`

### Protection Features
- Encryption key is encoded (not plain text)
- Key is split into multiple parts
- Runtime decoding prevents easy extraction

### Build Scripts
- **`build_pyobfus.bat`** - Builds with PyObfus obfuscation + key encoding
- **`build_basic_only.bat`** - Basic build (PyInstaller only)
- **`build_advanced.bat`** - Advanced build (PyArmor - requires license)

## Usage

### Restore from Backup
If source files are missing, copy them back:
```bash
copy source_backup_*\nxt_to_mdf_advanced.py .
copy source_backup_*\nxt_to_mdf_gui.py .
```

### Build Executable
```bash
build_pyobfus.bat
```

## Date Created
Backup created: 2025-12-08 20:29:47

