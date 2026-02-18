# GUI Usage Guide - CAN Bus Log Decoder

Two user-friendly interfaces are available for non-technical users:

## Option 1: Desktop GUI (Tkinter) - Recommended for Beginners

### Features
- ✅ Simple point-and-click interface
- ✅ No web browser needed
- ✅ Works offline
- ✅ Built into Python (no extra installation needed)

### How to Run

1. **Double-click** `dbc_decoder_gui.py` (if Python is set up to run .py files)
   
   **OR**

2. **Open Command Prompt/Terminal** and run:
   ```bash
   python dbc_decoder_gui.py
   ```

### How to Use

1. **Select CSV File**
   - Click "Browse..." next to "CSV Log File"
   - Navigate to your CSV log file
   - Click "Open"

2. **Select DBC File**
   - Click "Browse..." next to "DBC File"
   - Navigate to your DBC file
   - Click "Open"

3. **Optional: Save Output**
   - Click "Browse..." next to "Save Output To"
   - Choose where to save the decoded results
   - (Leave empty to just view on screen)

4. **Decode Messages**
   - Click the "Decode CAN Messages" button
   - Wait for the progress bar to complete
   - View decoded messages in the output area

5. **View DBC Messages** (Optional)
   - Click "View DBC Messages" to see what CAN IDs are in your DBC file
   - Useful to verify your DBC file is correct

6. **Clear Output**
   - Click "Clear Output" to start fresh

### Interface Overview

```
┌─────────────────────────────────────────┐
│     CAN Bus Log Decoder                 │
├─────────────────────────────────────────┤
│ CSV Log File: [Browse...]               │
│ DBC File:     [Browse...]               │
│ Save Output:  [Browse...]               │
│                                         │
│ [Decode] [View DBC] [Clear]            │
│                                         │
│ Progress: [████████░░]                 │
│ Status: Ready                          │
│                                         │
│ Decoded Messages:                      │
│ ┌─────────────────────────────────────┐ │
│ │ [2025-06-20 14:30:45] ID: 0x123... │ │
│ │ Decoded: {'EngineSpeed': 1500}     │ │
│ └─────────────────────────────────────┘ │
│                                         │
│ Statistics:                             │
│ Total: 1000 | Decoded: 850 | Errors: 150│
└─────────────────────────────────────────┘
```

---

## Option 2: Web Interface (Streamlit) - Modern & Beautiful

### Features
- ✅ Modern web-based interface
- ✅ Works in any web browser
- ✅ Beautiful, responsive design
- ✅ Interactive message browser
- ✅ Download decoded results

### Installation

First, install Streamlit:
```bash
pip install streamlit
```

Or install all requirements:
```bash
pip install -r requirements.txt
```

### How to Run

1. **Open Command Prompt/Terminal**

2. **Navigate to your project folder:**
   ```bash
   cd "C:\Users\amit\Downloads\CAN\New Try"
   ```

3. **Run the web interface:**
   ```bash
   streamlit run dbc_decoder_web.py
   ```

4. **Your web browser will automatically open** to the interface
   - Usually opens at: http://localhost:8501

### How to Use

1. **Upload Files**
   - In the left sidebar, click "Upload CSV Log File"
   - Select your CSV file
   - Click "Upload DBC File"
   - Select your DBC file

2. **Configure Options** (Optional)
   - ☑️ Show Unknown Messages - Display messages not in DBC
   - ☑️ Download Decoded Output - Enable download button

3. **Decode Messages**
   - Click the big "🔍 Decode Messages" button
   - Watch the progress bar
   - Results appear below

4. **View Results**
   - See statistics at the top
   - Browse decoded messages (click to expand)
   - Download results if enabled

5. **Browse DBC Definitions**
   - Click "📖 View DBC Message Definitions"
   - See all available CAN IDs and signals

### Interface Overview

```
┌─────────────────────────────────────────────────┐
│ 🚗 CAN Bus Log Decoder                          │
├──────────┬──────────────────────────────────────┤
│ 📁 Files │ Main Content Area                    │
│          │                                      │
│ [Upload] │ 📊 Statistics                         │
│ CSV      │ Total: 1000  Decoded: 850            │
│          │                                      │
│ [Upload] │ 📋 Decoded Messages                  │
│ DBC      │ ✅ [2025-06-20] ID: 0x123...        │
│          │    Decoded: {'EngineSpeed': 1500}   │
│ ⚙️ Options│                                      │
│ ☑ Show   │ ✅ [2025-06-20] ID: 0x456...        │
│   Unknown│    Decoded: {'Throttle': 45}        │
│          │                                      │
│ ☑ Download│                                     │
└──────────┴──────────────────────────────────────┘
```

---

## Comparison: Which Should I Use?

| Feature | Desktop GUI | Web Interface |
|---------|-------------|----------------|
| **Ease of Use** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **Installation** | None (built-in) | Requires Streamlit |
| **Internet Required** | No | No (runs locally) |
| **Modern Look** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **File Browser** | Native OS dialogs | Web upload |
| **Best For** | Quick, simple use | Modern, interactive experience |

**Recommendation:**
- **Beginners**: Start with Desktop GUI (simpler, no extra install)
- **Power Users**: Use Web Interface (more features, better UX)

---

## Troubleshooting

### Desktop GUI Issues

**"Python is not recognized"**
- Make sure Python is installed
- Try `python3` instead of `python`
- Add Python to your system PATH

**Window doesn't open**
- Check for error messages in terminal
- Make sure tkinter is installed (usually comes with Python)

**Files won't load**
- Check file paths are correct
- Make sure files exist
- Try using full file paths

### Web Interface Issues

**"streamlit is not recognized"**
- Install Streamlit: `pip install streamlit`
- Make sure you're using the correct Python environment

**Browser doesn't open**
- Manually go to: http://localhost:8501
- Check if port 8501 is already in use

**Upload fails**
- Check file size (very large files may take time)
- Make sure file format is correct (.csv and .dbc)

**Decoding is slow**
- Large CSV files take time to process
- Progress bar shows current status
- Be patient, it will complete

---

## Tips for Non-Technical Users

1. **Start Small**: Test with a small CSV file first (100-1000 messages)

2. **Check Your Files**: 
   - Make sure CSV file is from your ESP32 logger
   - Make sure DBC file matches your vehicle/system

3. **View DBC First**: Use "View DBC Messages" to verify your DBC file is correct

4. **Save Results**: Always save output to a file for later reference

5. **Read Statistics**: The success rate tells you how well your DBC file matches your log

6. **Unknown Messages**: If many messages are "unknown", your DBC file might not match your CAN bus

---

## Quick Start Checklist

- [ ] Python 3.7+ installed
- [ ] Required packages installed (`pip install -r requirements.txt`)
- [ ] CSV log file ready
- [ ] DBC file ready
- [ ] Choose GUI (Desktop or Web)
- [ ] Run the application
- [ ] Upload files
- [ ] Click decode
- [ ] View results!

---

## Need Help?

- Check the main `USAGE_GUIDE.md` for command-line usage
- Verify your CSV file format matches the expected format
- Make sure your DBC file is valid (try opening in a DBC editor)
- Check that CAN IDs in CSV match IDs in DBC file

