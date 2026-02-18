# CAN Bus Live Dashboard

Plotly Dash application that connects to the ESP32 CAN logger over WiFi, decodes frames with your DBC file, and renders live signal values. Designed for non-technical users: just join the ESP WiFi network, upload the DBC file, and press **Start Live View**.

## Requirements

- ESP32 logger running the latest firmware in this repo (exposes `/live` endpoint).
- Laptop/tablet connected to the ESP32 WiFi access point (`CAN_Logger` by default).
- Python 3.8+ with project dependencies (`pip install -r requirements.txt`).
- DBC file for the CAN bus you want to monitor.

## Quick Start

1. **Connect to WiFi**
   - Power the ESP32 logger.
   - On the viewing device, connect to the `CAN_Logger` WiFi network (password `CANLogger123` unless changed).

2. **Launch the Dashboard**
   - Double-click `Launch_Live_Dashboard.bat` (Windows) or run manually:
     ```bash
     python dbc_realtime_dashboard_dash.py
     ```
   - Your browser will open automatically (http://127.0.0.1:8501 by default).

3. **Configure**
   - Upload your `.dbc` file in the sidebar.
   - Confirm the logger address (default `http://192.168.4.1`).
   - Choose a refresh interval (0.5‑5 seconds).

4. **Start Live View**
   - Click **Start Live View**.
   - The dashboard fetches frames from `/live`, decodes them with your DBC, and shows:
     - Connection status & latest sequence number.
     - Colored cards grouped by CAN message with fixed-position signal values.
     - Rolling table of the most recent decoded signals.

5. **Stop**
   - Click **Stop** or close the Streamlit window.

## `/live` Endpoint Details

The ESP32 web server now exposes a lightweight JSON endpoint for live data:

- **URL:** `http://192.168.4.1/live`
- **Query Parameters:**
  - `since` (optional): last sequence number received; the logger returns frames after this value.
  - `limit` (optional): max frames to return (1‑200, default 50).
- **Response:**
  ```json
  {
    "status": "ok",
    "latest": 1234,
    "frames": [
      {
        "seq": 1234,
        "time": "2025-06-20 14:30:45.123",
        "unix": 1718884245,
        "micros": 45000,
        "id": "123",
        "extended": false,
        "rtr": false,
        "dlc": 8,
        "data": ["01","02","03","04","05","06","07","08"]
      }
    ]
  }
  ```

Polling `/live?since=<last_seq>&limit=100` every ~1 s keeps the dashboard in sync while keeping ESP CPU usage low.

## Tips

- Leave the refresh interval at 1 s for smooth updates; go lower only if you need faster changes and the laptop is close to the ESP AP.
- If no data appears, check:
  - DBC file loaded successfully (sidebar turns green).
  - ESP32 actually sees CAN traffic (LED should be green/yellow).
  - Receiving device is connected to the ESP WiFi and not a different network.
- The dashboard only shows signals defined in the uploaded DBC. Frames without DBC entries are ignored.
- For layout changes or new widgets, edit `dbc_realtime_dashboard_dash.py`.

## Extending

- Integrate with existing plant/test infrastructure by consuming the `/live` endpoint directly (Python, Node, LabVIEW, etc.).
- Customize the Dash app (`dbc_realtime_dashboard_dash.py`) to add charts, alarm thresholds, or logging of decoded data.


