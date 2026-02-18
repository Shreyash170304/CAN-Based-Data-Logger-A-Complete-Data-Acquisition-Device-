# ASAM MDF Integration Guide

This guide explains how to integrate the CAN data logger with ASAM MDF GUI and other MDF-compatible tools.

## Overview

The CAN logger creates encrypted `.nxt` files that can be converted to ASAM MDF format (`.mf4`) for use with professional automotive analysis tools.

## What is ASAM MDF?

**ASAM MDF (Measurement Data Format)** is the industry standard for storing measurement data in automotive applications. MDF files are used by:

- **ASAM MDF GUI** - Free viewer/editor for MDF files
- **CANoe/CANalyzer** (Vector) - Professional CAN analysis tools
- **INCA** (ETAS) - ECU calibration and measurement tools
- **MATLAB/Simulink** - Engineering analysis
- **Many other automotive tools**

## Installation

### 1. Install Required Python Library

```bash
pip install asammdf
```

Or update your requirements:

```bash
pip install -r requirements.txt
```

### 2. Verify Installation

```bash
python -c "import asammdf; print('ASAM MDF library installed successfully')"
```

## Usage

### Basic Conversion (Raw CAN Data)

Convert encrypted `.nxt` file to MDF format:

```bash
python nxt_to_mdf.py CAN_LOG_20250620_143045.nxt output.mf4
```

This creates an MDF file with:
- Raw CAN message IDs
- CAN data bytes (Data0-Data7)
- Sensor data (accelerometer, magnetometer)
- GPS data (latitude, longitude, altitude, speed, etc.)

### Advanced Conversion (with DBC Decoding)

Convert with DBC file to decode CAN signals:

```bash
python nxt_to_mdf_advanced.py input.nxt output.mf4 --dbc vehicle.dbc
```

This creates an MDF file with:
- **Raw CAN data** (as above)
- **DBC decoded signals** (human-readable signal names and values)
- Sensor data
- GPS data

### Options

```bash
# Exclude sensor data
python nxt_to_mdf.py input.nxt output.mf4 --no-sensors

# Exclude GPS data
python nxt_to_mdf.py input.nxt output.mf4 --no-gps

# Verbose output
python nxt_to_mdf.py input.nxt output.mf4 --verbose
```

## MDF File Structure

The converted MDF file contains the following channel groups:

### 1. CAN Raw Data
- `CAN_ID` - CAN message identifier
- `CAN_Data0` through `CAN_Data7` - Raw data bytes

### 2. DBC Decoded Signals (if DBC file provided)
- `DBC_SignalName` - Decoded signal values with proper units
- Example: `DBC_Motor_speed`, `DBC_Bus_voltage`, etc.

### 3. Sensor Data
- `Sensor_AccelX`, `Sensor_AccelY`, `Sensor_AccelZ` (m/s²)
- `Sensor_MagX`, `Sensor_MagY`, `Sensor_MagZ` (µT)
- `Sensor_Heading` (degrees)

### 4. GPS Data
- `GPS_Lat`, `GPS_Lon` (degrees)
- `GPS_Alt` (meters)
- `GPS_Speed` (km/h)
- `GPS_Course` (degrees)
- `GPS_Sats`, `GPS_HDOP`

## Opening MDF Files

### ASAM MDF GUI (Free)

1. **Download**: https://www.asam.net/standards/detail/mdf/
2. **Install** ASAM MDF GUI
3. **Open** your `.mf4` file
4. **View** channels, plot signals, export data

### CANoe/CANalyzer (Vector)

1. Open CANoe/CANalyzer
2. File → Open → Select `.mf4` file
3. Use Trace window to view CAN messages
4. Use Graphics window to plot signals

### INCA (ETAS)

1. Open INCA
2. File → Import → Measurement Data
3. Select `.mf4` file
4. View and analyze measurement data

## Workflow Example

### Complete Workflow

```bash
# 1. Record data on ESP32 logger
#    (Data is saved as encrypted .nxt file)

# 2. Download .nxt file from SD card or via WiFi

# 3. Convert to MDF with DBC decoding
python nxt_to_mdf_advanced.py \
    CAN_LOG_20250620_143045.nxt \
    measurement.mf4 \
    --dbc vehicle_can.dbc

# 4. Open in ASAM MDF GUI
#    - View all channels
#    - Plot signals
#    - Export to Excel/CSV if needed
```

## Channel Naming Convention

Channels are organized by source:

- `CAN_*` - Raw CAN bus data
- `DBC_*` - DBC decoded signals
- `Sensor_*` - IMU sensor data
- `GPS_*` - GPS data

## Time Synchronization

All channels use a common timebase:
- Time starts at 0 (relative to first sample)
- Timestamps are in seconds (float)
- Microsecond precision preserved

## Troubleshooting

### Error: "asammdf library not installed"

```bash
pip install asammdf
```

### Error: "Invalid encrypted log signature"

- Ensure you're using a valid `.nxt` file from the ESP32 logger
- Check that the file wasn't corrupted during transfer

### Error: "DBC decoding failed"

- Verify DBC file is valid: `cantools dump vehicle.dbc`
- Check that CAN IDs in log match DBC file
- Use `--verbose` flag to see detailed error messages

### MDF file is too large

- Use `--no-sensors` or `--no-gps` to exclude data you don't need
- MDF files can be large for long recordings (this is normal)

## Advanced Usage

### Batch Conversion

Convert multiple files:

```bash
for file in *.nxt; do
    python nxt_to_mdf.py "$file" "${file%.nxt}.mf4"
done
```

### Integration with Python Scripts

```python
from nxt_to_mdf import decrypt_nxt_to_csv, parse_csv_to_dataframe, create_mdf_from_dataframe

# Decrypt and parse
csv_content = decrypt_nxt_to_csv('input.nxt')
df = parse_csv_to_dataframe(csv_content)

# Create MDF
mdf = create_mdf_from_dataframe(df, 'output.mf4', include_sensors=True, include_gps=True)
```

## Comparison: NXT vs MDF

| Feature | NXT Format | MDF Format |
|---------|-----------|------------|
| **Encryption** | ✓ Encrypted | ✗ Plaintext |
| **File Size** | Smaller | Larger |
| **Compatibility** | Custom tools only | Industry standard |
| **Tool Support** | Limited | Extensive |
| **Signal Decoding** | Requires DBC | Can include decoded signals |
| **Time Precision** | Microseconds | Microseconds |

## Best Practices

1. **Keep original `.nxt` files** - They're encrypted and smaller
2. **Convert to MDF only when needed** - For analysis in professional tools
3. **Use DBC decoding** - Makes signals human-readable
4. **Include metadata** - MDF files preserve timestamps and units

## Resources

- **ASAM MDF Specification**: https://www.asam.net/standards/detail/mdf/
- **ASAM MDF GUI**: https://www.asam.net/standards/detail/mdf/
- **Python asammdf Library**: https://github.com/danielhrisca/asammdf

## Support

For issues or questions:
1. Check this guide
2. Review error messages with `--verbose` flag
3. Verify file formats and DBC compatibility

