# CAN Decoder Checkpoint - November 15, 2025

## Backup Information
- **Backup File**: `CAN_Decoder_Checkpoint_20251115_175222.zip`
- **Date**: November 15, 2025
- **Purpose**: Checkpoint before fixing timestamp grouping precision issue

## Files Included in Backup
- `dbc_decode_csv.py` - Command-line decoder script
- `dbc_decoder_gui.py` - Desktop GUI application
- `dbc_decoder_web.py` - Web interface (Streamlit)
- `simple_decode_example.py` - Simple example script
- `requirements.txt` - Python dependencies
- `Launch_GUI.bat` - Windows launcher for GUI
- `Launch_Web.bat` - Windows launcher for web interface
- `README_DBC_DECODER.md` - Main documentation
- `USAGE_GUIDE.md` - Usage instructions
- `GUI_USAGE_GUIDE.md` - GUI usage guide

## Recent Changes (Before This Checkpoint)

### Features Implemented:
1. ✅ CSV output format with headers and signal columns
2. ✅ Timestamp grouping to merge signals from different CAN IDs
3. ✅ Milliseconds included in timestamps
4. ✅ Signal merging at same timestamps to reduce empty cells

### Issue Identified:
- **Problem**: Messages were being incorrectly grouped, causing loss of data
  - Input: 47 messages per ID
  - Output: Only 12 rows (should be ~47 rows)
  - **Root Cause**: Timestamp key was not precise enough, causing different time points to be merged

## Fix Applied in This Checkpoint

### Timestamp Grouping Precision Fix:
- **Changed**: Timestamp key from string-based to tuple-based `(UnixTime, Microseconds)`
- **Benefit**: Each unique time point (including microsecond precision) gets its own row
- **Result**: All 47 messages per ID should now be preserved in output

### Technical Details:
- Uses `(unix_time, microseconds)` tuple as dictionary key instead of formatted string
- Ensures microsecond-level precision for grouping
- Falls back to string-based key if UnixTime not available
- Maintains millisecond display in output timestamps

## How to Restore from Backup

1. Extract the zip file: `CAN_Decoder_Checkpoint_20251115_175222.zip`
2. All files will be restored to their state at this checkpoint
3. Run `pip install -r requirements.txt` to ensure dependencies are installed

## Testing Recommendations

After applying the fix, test with:
- CSV file containing multiple CAN IDs
- Verify output has correct number of rows (should match unique timestamps)
- Check that signals from different IDs are properly merged at same timestamps
- Verify timestamps include milliseconds

## Next Steps

1. Test the fix with your 47-message-per-ID dataset
2. Verify output contains all expected rows
3. Check that signals are properly merged at identical timestamps
4. Report any remaining issues

