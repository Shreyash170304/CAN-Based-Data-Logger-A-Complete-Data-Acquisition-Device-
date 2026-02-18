**DBC Decoder V3 — Background & How It Works**

- **Location**: `dbc_decoder_gui_v3_excel.py` (GUI) — created utilities also include `nxt_to_csv.py` (standalone .nxt → .csv converter) and helper launchers (`Launch_DBCDecoder_GUI_v3_install.bat`).

Overview
--------
This document explains what happens behind the scenes when you run the DBC Decoder V3 (Excel multi-sheet export). The GUI is designed to accept encrypted `.nxt` logs or plaintext `.csv` logs, decode CAN messages with a `.dbc` file using `cantools`, and export results (Excel sheets and/or CSV files). It also detects and exports sensor-only rows separately.

High-level steps performed by the application
-------------------------------------------
1. Input selection
   - You choose a log file (`.nxt` encrypted file or `.csv`) and a `.dbc` file. You also choose an output filename and export format (Excel, CSV or Both). There is a checkbox to include sensor records as a separate sheet/CSV.

2. Decryption (for `.nxt` files)
   - If you select a `.nxt` file, the GUI calls the same decryption routine used elsewhere (`decrypt_nxt_file`).
   - The `.nxt` header is validated (magic bytes, version). A nonce is extracted and used to seed a small PRNG stream. The body bytes are XORed with the generated keystream to produce plaintext CSV bytes. The plaintext CSV is written to a temporary file and used for the rest of processing.

3. CSV reading with robust encoding handling
   - The program tries to read the CSV with `utf-8`. If that fails it tries `latin-1` and finally `utf-8` with `errors='ignore'` to avoid decode crashes. This handles logs produced on different devices/OS locales.

4. Row-level parsing and timestamp handling
   - Each row is examined. Typical columns expected: `ID`, `DLC`, `Data0`..`Data7`, plus optional `Timestamp`, `UnixTime`, `Microseconds` and sensor columns.
   - Timestamp milliseconds preservation: if `UnixTime` contains fractional seconds (float) the code preserves the fractional part and also adds `Microseconds` column when present. The combined microseconds are normalized (overflow handled) and the formatted timestamp includes milliseconds (e.g., `YYYY-MM-DD HH:MM:SS.mmm`).

5. Sensor-only row detection
   - Rows where `ID == 0xFFFF` and `DLC == 0` are treated as high-rate sensor samples (not CAN frames). The program collects sensor columns (like `AccelX`, `GPS_Lat`, etc.) into a separate table. These rows are skipped for CAN decoding.

6. CAN frame processing and decoding
   - For rows that are CAN frames (not sensor-only), the program builds the raw data bytes from `Data0..DataN`, converts payloads to `bytes` and attempts to decode them with `cantools` using the loaded `.dbc` database.
   - When `cantools` successfully decodes a frame, each signal and its value are collected. Decoding failures are counted as errors.

7. Grouping & aggregation
   - V3 primarily groups frames by message ID and writes each ID to its own Excel sheet (one sheet per CAN message ID). Other GUI variants offer additional grouping methods (time window, cycles, fixed intervals, sequential windows).
   - The GUI can optionally append a `Sensors` sheet containing all sensor rows.

8. Export
   - Excel mode: an `.xlsx` workbook is created with one sheet per CAN message ID (sheet name `0x123`), containing `Date`, `Time` and decoded signals. If selected, a `Sensors` sheet is appended.
   - CSV mode: a single grouped CSV is written; when `Include sensor messages` is checked, a separate `_sensors.csv` file is written.
   - Formatting: header row styling and adjusted column widths are applied for readability.

9. Statistics & success rate
   - The GUI reports statistics including total input rows, total CAN rows (input rows minus sensor-only rows), decoded frames, decode errors, unique message IDs, and signals.
   - Important: success/error rates are computed over the CAN frames only (i.e., sensor rows excluded). This prevents large numbers of sensor rows from skewing decode metrics.

Dependencies
------------
- Python 3.8+ recommended
- Required packages (see `requirements.txt`):
  - `pandas`
  - `cantools`
  - `openpyxl` (for Excel export)
  - (other UI packages bundled with Python standard library)

Install command (from workspace root):
```powershell
python -m pip install -r requirements.txt
```

How to run
----------
- To launch V3 GUI (installer script ensures dependencies):
```powershell
C:\Users\amitk\Desktop\New Try\Launch_DBCDecoder_GUI_v3_install.bat
```
- Or run directly:
```powershell
python "C:\Users\amitk\Desktop\New Try\dbc_decoder_gui_v3_excel.py"
```

Useful tools included
---------------------
- `nxt_to_csv.py` — decrypt `.nxt` files to plaintext CSV (standalone, no DBC decoding). Useful for quick offline inspection.
- `Convert_NXT_to_CSV.bat` — drag-and-drop launcher for the converter.

Debugging tips
--------------
- If the GUI reports a high error rate, check whether many rows are sensor-only (`ID==0xFFFF`). Our metrics now account for this and compute rates using CAN-only rows.
- Use `nxt_to_csv.py --debug` to print the `.nxt` header and the first decrypted bytes (hex) to validate decryption.
- If Excel export fails, make sure `openpyxl` is installed.

If you want enhancements
-----------------------
- Add decode-error breakdown by CAN ID
- Add an option to write raw hex payload per CAN frame into Excel
- Export raw hex + decoded signals to the same sheet for cross-reference

Contact
-------
If something looks wrong with the output data (missing ms, malformed rows), attach a small sample CSV (first ~200 rows) and I can adjust parsing/format handling quickly.