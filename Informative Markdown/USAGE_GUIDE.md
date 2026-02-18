# How to Use the Python DBC Decoder Scripts

## Prerequisites

1. **Python 3.7 or higher** installed on your computer
   - Check if installed: `python --version` or `python3 --version`
   - Download from: https://www.python.org/downloads/

2. **Required Python packages**
   - Install using: `pip install -r requirements.txt`
   - Or manually: `pip install pandas cantools`

> **Note:** The ESP32 logger now produces encrypted `.nxt` files. All tools in this folder (CLI, GUI, Streamlit dashboard) accept either `.nxt` or legacy `.csv` logs and decrypt automatically.

## Step-by-Step Instructions

### Method 1: Using the Full-Featured Script (`dbc_decode_csv.py`)

This is the recommended method with all features.

#### Step 1: Open Command Prompt or Terminal

- **Windows**: Press `Win + R`, type `cmd`, press Enter
- **Mac/Linux**: Open Terminal

#### Step 2: Navigate to Your Project Folder

```bash
cd "C:\Users\amit\Downloads\CAN\New Try"
```

(Replace with your actual folder path)

#### Step 3: Basic Usage - Decode and Display on Screen

```bash
python dbc_decode_csv.py your_log_file.nxt your_database.dbc
```

**Example:**
```bash
python dbc_decode_csv.py CAN_LOG_20250620_143045.nxt vehicle_can.dbc
```

**What happens:**
- Reads your CSV log file
- Loads your DBC file
- Decodes all CAN messages
- Displays decoded results on screen
- Shows statistics at the end

#### Step 4: Save Output to a File

```bash
python dbc_decode_csv.py your_log_file.nxt your_database.dbc -o decoded_output.csv
```

**Example:**
```bash
python dbc_decode_csv.py CAN_LOG_20250620_143045.nxt vehicle_can.dbc -o decoded_results.csv
```

This saves all decoded messages to `decoded_output.txt` instead of printing to screen.

#### Step 5: List All Messages in Your DBC File

Before decoding, you can see what messages are defined in your DBC file:

```bash
python dbc_decode_csv.py --list your_database.dbc
```

**Example:**
```bash
python dbc_decode_csv.py --list vehicle_can.dbc
```

This shows:
- All CAN IDs in the DBC file
- Message names
- Signal definitions
- Message lengths

#### Step 6: Quiet Mode (No Progress Messages)

If you have many messages and want cleaner output:

```bash
python dbc_decode_csv.py your_log_file.nxt your_database.dbc --quiet
```

#### Step 7: Get Help

```bash
python dbc_decode_csv.py --help
```

---

### Method 2: Using the Simple Example Script (`simple_decode_example.py`)

This is easier for beginners but has fewer features.

#### Step 1: Edit the Script

Open `simple_decode_example.py` in a text editor and change these lines:

```python
CSV_FILE = "CAN_LOG_20250620_143045.csv"  # Change to your CSV file name
DBC_FILE = "your_database.dbc"  # Change to your DBC file name
```

**Example:**
```python
CSV_FILE = "CAN_LOG_20250620_143045.csv"
DBC_FILE = "vehicle_can.dbc"
```

#### Step 2: Run the Script

```bash
python simple_decode_example.py
```

The script will:
- Load your DBC file
- Read your CSV file
- Decode messages
- Print results to screen

---

## Complete Example Workflow

Here's a complete example from start to finish:

### 1. Install Dependencies (First Time Only)

```bash
cd "C:\Users\amit\Downloads\CAN\New Try"
pip install pandas cantools
```

### 2. Check Your DBC File

```bash
python dbc_decode_csv.py --list vehicle_can.dbc
```

### 3. Decode Your Log File

```bash
python dbc_decode_csv.py CAN_LOG_20250620_143045.csv vehicle_can.dbc
```

### 4. Save Results to File

```bash
python dbc_decode_csv.py CAN_LOG_20250620_143045.csv vehicle_can.dbc -o results.txt
```

---

## Understanding the Output

### Successful Decode:
```
[2025-06-20 14:30:45] ID: 0x123 (291) | Data: 01 02 03 04 05 06 07 08 | Decoded: {'EngineSpeed': 1500, 'ThrottlePosition': 45.5, 'CoolantTemp': 85}
```

This means:
- **Timestamp**: When the message was received
- **ID**: CAN message ID (0x123 = 291 decimal)
- **Data**: Raw hex bytes
- **Decoded**: Signal names and their decoded values from DBC file

### Unknown Message:
```
[2025-06-20 14:30:45] ID: 0x456 (1110) | Data: AA BB CC DD | [No DBC definition found]
```

This means the CAN ID is not defined in your DBC file.

### Decode Error:
```
[2025-06-20 14:30:45] ID: 0x789 (1929) | Data: 11 22 33 | [Decode error: ...]
```

This means the message format doesn't match the DBC definition (wrong length, etc.)

---

## Common Issues and Solutions

### Issue 1: "ModuleNotFoundError: No module named 'pandas'"

**Solution:** Install missing packages
```bash
pip install pandas cantools
```

### Issue 2: "CSV file not found"

**Solution:** 
- Check the CSV file name is correct
- Make sure you're in the right folder
- Use full path: `python dbc_decode_csv.py "C:\full\path\to\file.csv" database.dbc`

### Issue 3: "DBC file not found"

**Solution:**
- Check the DBC file name is correct
- Make sure the DBC file is in the same folder, or use full path

### Issue 4: "No messages decoded" or "Success rate: 0%"

**Solution:**
- Check that CAN IDs in your CSV match IDs in your DBC file
- Use `--list` to see what IDs are in your DBC file
- Verify your DBC file is correct for your vehicle/system

### Issue 5: "Python is not recognized"

**Solution:**
- Make sure Python is installed
- Try `python3` instead of `python`
- On Windows, you may need to add Python to PATH during installation

---

## File Path Tips

### Using Full Paths (Windows):
```bash
python dbc_decode_csv.py "C:\Users\amit\Downloads\CAN\New Try\CAN_LOG_20250620_143045.csv" "C:\Users\amit\Downloads\CAN\New Try\vehicle_can.dbc"
```

### Using Relative Paths:
If files are in the same folder:
```bash
python dbc_decode_csv.py CAN_LOG_20250620_143045.csv vehicle_can.dbc
```

### Using Paths with Spaces:
Always use quotes:
```bash
python dbc_decode_csv.py "My Log File.csv" "My Database.dbc"
```

---

## Real-Time Dashboard (Streamlit)

Need a non-technical, live view of signals while the ESP32 is running?

1. Connect your viewing device to the ESP32 WiFi network (`CAN_Logger` by default).
2. Install project requirements (`pip install -r requirements.txt`).
3. Launch the dashboard:
   - Windows: double-click `Launch_Live_Dashboard.bat`
   - Any OS: `streamlit run dbc_realtime_dashboard.py`
4. Upload your `.dbc` file in the sidebar and click **Start Live View**.
5. The dashboard pulls frames from `http://192.168.4.1/live`, decodes them locally, and shows live metrics/table updates.

See `REALTIME_DASHBOARD.md` for screenshots, endpoint details, and troubleshooting tips.

---

## Quick Reference

| Command | Description |
|---------|-------------|
| `python dbc_decode_csv.py log.nxt dbc.dbc` | Basic decode, print to screen |
| `python dbc_decode_csv.py log.nxt dbc.dbc -o output.csv` | Decode and save to file |
| `python dbc_decode_csv.py --list dbc.dbc` | List all messages in DBC file |
| `python dbc_decode_csv.py log.nxt dbc.dbc --quiet` | Quiet mode (no progress) |
| `python dbc_decode_csv.py --help` | Show help message |

---

## Next Steps

1. **Get a DBC file** for your vehicle/system if you don't have one
2. **Test with a small CSV file** first to verify everything works
3. **Check the decoded output** to ensure values make sense
4. **Use the statistics** to see how many messages were successfully decoded

---

## Need Help?

- Check that all file paths are correct
- Verify Python and packages are installed
- Make sure your DBC file matches your CAN bus
- Use `--list` to verify your DBC file loads correctly

