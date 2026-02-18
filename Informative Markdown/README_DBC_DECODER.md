# CAN Bus Encrypted Log Decoder

Python scripts to decode encrypted CAN bus `.nxt` log files (or legacy CSV logs) using DBC (Database Container) files.

## Installation

1. Install Python 3.7 or higher
2. Install required packages:

```bash
pip install -r requirements.txt
```

Or install manually:
```bash
pip install pandas cantools
```

## Files

- **`dbc_decode_csv.py`** - Full-featured decoder with command-line interface
- **`simple_decode_example.py`** - Simple example script for basic usage
- **`requirements.txt`** - Python package dependencies

## Usage

### Full-Featured Decoder (`dbc_decode_csv.py`)

#### Basic Usage
```bash
python dbc_decode_csv.py your_log.nxt your_database.dbc
```

#### Save Output to File
```bash
python dbc_decode_csv.py your_log.nxt your_database.dbc -o decoded_output.csv
```

#### List All Messages in DBC File
```bash
python dbc_decode_csv.py --list your_database.dbc
```

#### Quiet Mode (No Progress Messages)
```bash
python dbc_decode_csv.py your_log.nxt your_database.dbc --quiet
```

#### Help
```bash
python dbc_decode_csv.py --help
```

### Simple Example (`simple_decode_example.py`)

1. Edit the script and set your CSV and DBC file paths (export a CSV by decrypting the `.nxt` log with `dbc_decode_csv.py` first):
```python
CSV_FILE = "CAN_LOG_20250620_143045.csv"
DBC_FILE = "your_database.dbc"
```

2. Run the script:
```bash
python simple_decode_example.py
```

## Log File Format

The ESP32 logger now writes encrypted `.nxt` files. The Python decoder automatically decrypts them back to CSV format with the following columns:
- `Timestamp` - Human-readable timestamp
- `UnixTime` - Unix timestamp
- `Microseconds` - Microsecond precision
- `ID` - CAN ID (hex format, e.g., "123" or "0x123")
- `Extended` - Extended frame flag (0 or 1)
- `RTR` - Remote transmission request flag (0 or 1)
- `DLC` - Data length code (0-8)
- `Data0` through `Data7` - Data bytes in hex format (e.g., "01", "FF")

Example CSV:
```csv
Timestamp,UnixTime,Microseconds,ID,Extended,RTR,DLC,Data0,Data1,Data2,Data3,Data4,Data5,Data6,Data7
2025-06-20 14:30:45,1718884245,45000,123,0,0,8,01,02,03,04,05,06,07,08
```

## DBC File Format

DBC (Database Container) files are standard CAN database files used in automotive applications. You can:
- Create them using tools like CANdb++, Vector CANoe, or other CAN tools
- Download them from vehicle manufacturers or CAN database repositories
- Convert from other formats using tools like `cantools`

## Output Format

The decoder outputs messages in the following format:
```
[2025-06-20 14:30:45] ID: 0x123 (291) | Data: 01 02 03 04 05 06 07 08 | Decoded: {'Signal1': 1.5, 'Signal2': 42, ...}
```

## Error Handling

- Messages with IDs not found in the DBC file are marked as `[No DBC definition found]`
- Messages that fail to decode are marked with the error message
- The script continues processing even if individual messages fail

## Statistics

At the end of decoding, the script displays:
- Total messages processed
- Successfully decoded count
- Error/Unknown count
- Success rate percentage

## Notes

- The script handles both hex string IDs (e.g., "123", "0x123") and integer IDs
- Data bytes can be in hex string format (e.g., "01", "FF") or integer format
- Extended CAN frames (29-bit IDs) are supported
- The decoder uses the `cantools` library, which is the standard Python library for DBC decoding

## Troubleshooting

**Error: "CSV file missing required columns"**
- Make sure your CSV file has all required columns (ID, DLC, Data0-Data7)

**Error: "Failed to load DBC file"**
- Verify the DBC file path is correct
- Check that the DBC file is valid (try opening it in a DBC editor)

**No messages decoded**
- Verify the CAN IDs in your CSV match the message IDs in your DBC file
- Check that the data length (DLC) matches the expected message length in DBC
- Use `--list` option to see all available message definitions in your DBC file

