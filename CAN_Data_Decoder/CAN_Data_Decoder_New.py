#!/usr/bin/env python3
"""
CAN Bus Encrypted Log Decoder - GUI Application
Professional Edition with Modern UI
User-friendly interface for non-technical users
"""

import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk
import threading
import queue
try:
    import pandas as pd  # type: ignore
except Exception:
    pd = None
try:
    from pandas.errors import ParserError  # type: ignore
except Exception:
    ParserError = Exception

try:
    import cantools  # type: ignore
except Exception:
    cantools = None
from pathlib import Path

try:
    from PIL import Image, ImageTk  # type: ignore
    PIL_AVAILABLE = True
except Exception:
    PIL_AVAILABLE = False
import os
import tempfile
import struct
import csv
import re
import math
from collections import Counter
try:
    import numpy as np  # type: ignore
    NUMPY_AVAILABLE = True
except Exception:
    NUMPY_AVAILABLE = False

NXT_MAGIC = b"NXTLOG"
NXT_VERSION = 1
NXT_HEADER_SIZE = 16
ENCRYPTION_KEY = [
    0x3A, 0x7C, 0xB5, 0x19,
    0xE4, 0x58, 0xC1, 0x0D,
    0x92, 0xAF, 0x63, 0x27,
    0xFE, 0x34, 0x88, 0x4B
]


def init_cipher_state(nonce):
    state = (nonce ^ 0xA5A5A5A5) & 0xFFFFFFFF
    for key_byte in ENCRYPTION_KEY:
        state = (state * 1664525 + 1013904223 + key_byte) & 0xFFFFFFFF
    return state


def cipher_step(state):
    state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
    key_byte = ENCRYPTION_KEY[state & 0x0F]
    stream_byte = ((state >> 24) & 0xFF) ^ key_byte
    return state, stream_byte


def decrypt_nxt_file(nxt_path):
    with open(nxt_path, 'rb') as f:
        header = f.read(NXT_HEADER_SIZE)
        if len(header) < NXT_HEADER_SIZE:
            raise ValueError("Encrypted file header too short")
        if header[:len(NXT_MAGIC)] != NXT_MAGIC:
            raise ValueError("Invalid encrypted log signature")
        version = header[len(NXT_MAGIC)]
        if version != NXT_VERSION:
            raise ValueError(f"Unsupported encrypted log version: {version}")
        nonce = struct.unpack('<I', header[8:12])[0]
        state = init_cipher_state(nonce)

        temp_file = tempfile.NamedTemporaryFile(delete=False, suffix=".csv")
        try:
            while True:
                chunk = f.read(4096)
                if not chunk:
                    break
                decrypted = bytearray(len(chunk))
                for idx, byte in enumerate(chunk):
                    state, stream = cipher_step(state)
                    decrypted[idx] = byte ^ stream
                temp_file.write(decrypted)
        finally:
            temp_file.close()

    return temp_file.name


def format_timestamp_with_ms(timestamp, microseconds=None):
    """
    Format timestamp string to include milliseconds with improved accuracy.
    If timestamp is in format 'YYYY-MM-DD HH:MM:SS', adds milliseconds.
    Handles multiple timestamp formats and edge cases.
    """
    if not timestamp:
        return ''

    # Pandas is optional for launching; without it we skip normalization.
    if pd is None:
        return str(timestamp).strip() if isinstance(timestamp, str) else str(timestamp)
    
    try:
        # Try to parse timestamp
        if isinstance(timestamp, str):
            timestamp = timestamp.strip()
            
            # Check if already has milliseconds/microseconds
            if '.' in timestamp:
                # Already has fractional seconds, return as-is or normalize format
                try:
                    dt = pd.to_datetime(timestamp, errors='coerce')
                    if not pd.isna(dt):
                        # Normalize to consistent format with 3 decimal places
                        return dt.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
                    else:
                        return timestamp  # Return original if parsing fails
                except:
                    return timestamp
            
            # Try to parse common formats
            try:
                dt = pd.to_datetime(timestamp, errors='coerce')
                if pd.isna(dt):
                    return timestamp  # Return original if parsing fails
            except:
                return timestamp
            
            # Add milliseconds if microseconds provided
            if microseconds is not None:
                try:
                    micros_int = int(microseconds)
                    # Convert microseconds to milliseconds and add to datetime
                    ms = micros_int // 1000
                    # Handle microseconds properly (0-999999 range)
                    micros_remainder = micros_int % 1000
                    dt = dt.replace(microsecond=ms * 1000 + micros_remainder)
                except (ValueError, TypeError):
                    # If microseconds conversion fails, keep original timestamp
                    pass
            
            # Format with milliseconds (3 decimal places)
            return dt.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]  # Keep only 3 digits for ms
        else:
            # Try to convert non-string to datetime
            try:
                dt = pd.to_datetime(timestamp, errors='coerce')
                if not pd.isna(dt):
                    if microseconds is not None:
                        try:
                            micros_int = int(microseconds)
                            ms = micros_int // 1000
                            micros_remainder = micros_int % 1000
                            dt = dt.replace(microsecond=ms * 1000 + micros_remainder)
                        except:
                            pass
                    return dt.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
            except:
                pass
            return str(timestamp)
    except Exception as e:
        # Return original timestamp if all parsing fails
        return str(timestamp) if timestamp else ''


def _format_timestamp_high_res(timestamp_str, micros_value):
    """
    Build a `23-01.csv` style timestamp:
      `YYYY-MM-DD HH:MM:SS.fffffffff` (up to 9 fractional digits)

    The logger provides:
      - Timestamp: second resolution `YYYY-MM-DD HH:MM:SS`
      - Microseconds: sometimes 0-999999, but in some datasets it's already 9 digits
        (used as nanoseconds field in `23-01.csv`).
    """
    ts = (str(timestamp_str).strip() if timestamp_str is not None else "")
    if not ts:
        return ""

    # If input already contains a timezone or fractional seconds, preserve it.
    if "." in ts:
        return ts

    frac = ""
    try:
        if micros_value is None or (isinstance(micros_value, float) and math.isnan(micros_value)):
            frac = ""
        else:
            raw = str(int(micros_value)).strip()
            # Heuristic:
            # - <= 6 digits: treat as microseconds and pad to 6
            # - > 6 digits: treat as high-res fractional (often 9 digits)
            if len(raw) <= 6:
                frac = raw.zfill(6)
            else:
                frac = raw.zfill(9)[-9:]
    except Exception:
        frac = ""

    return f"{ts}.{frac}" if frac else ts


def _safe_int_hex_or_dec(value):
    """Parse CAN IDs that may be hex strings without 0x, with 0x, or decimal."""
    s = str(value).strip()
    if not s:
        return 0
    try:
        if s.lower().startswith("0x"):
            return int(s, 16)
        # Prefer hex for CAN IDs (logger writes HEX without 0x), fallback to decimal.
        try:
            return int(s, 16)
        except ValueError:
            return int(s, 10)
    except Exception:
        return 0


def _parse_data_bytes_from_row(row, dlc):
    out = []
    for i in range(8):
        key = f"Data{i}"
        v = row.get(key, 0)
        if i >= dlc:
            out.append(0)
            continue
        try:
            if isinstance(v, str):
                vv = v.strip()
                if not vv:
                    out.append(0)
                elif vv.lower().startswith("0x"):
                    out.append(int(vv, 16) & 0xFF)
                else:
                    # logger writes hex without 0x
                    try:
                        out.append(int(vv, 16) & 0xFF)
                    except ValueError:
                        out.append(int(vv) & 0xFF)
            else:
                out.append(int(v) & 0xFF)
        except Exception:
            out.append(0)
    return out


def _extract_raw_signal(data_bytes, start_bit, length, byte_order):
    """Extract raw signal bits from data bytes for both endian types."""
    if length <= 0:
        return 0
    raw = 0
    if byte_order == "little_endian":
        for i in range(length):
            bit_index = start_bit + i
            byte_index = bit_index // 8
            bit_in_byte = bit_index % 8
            if byte_index >= len(data_bytes):
                break
            bit = (data_bytes[byte_index] >> bit_in_byte) & 0x1
            raw |= (bit << i)
    else:
        # big_endian / Motorola
        for i in range(length):
            bit_index = start_bit - i
            byte_index = bit_index // 8
            bit_in_byte = bit_index % 8
            if byte_index < 0 or byte_index >= len(data_bytes):
                break
            bit = (data_bytes[byte_index] >> bit_in_byte) & 0x1
            raw = (raw << 1) | bit
    return raw


def _is_number(x):
    try:
        if x is None:
            return False
        if isinstance(x, (int, float)) and not (isinstance(x, float) and math.isnan(x)):
            return True
        float(x)
        return True
    except Exception:
        return False


def _load_dbc_with_fallback(dbc_path):
    """
    Load DBC with a compatibility fallback for unsupported attributes like VFrameFormat.
    Some DBCs include BO_ attributes that older cantools versions can't parse.
    """
    try:
        return cantools.database.load_file(dbc_path)
    except Exception as e:
        if "VFrameFormat" not in str(e):
            raise

    # Fallback: strip unsupported attribute definition/assignments.
    try:
        with open(dbc_path, "r", encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except Exception:
        with open(dbc_path, "r", errors="replace") as fh:
            lines = fh.readlines()

    filtered = []
    for line in lines:
        if "VFrameFormat" in line:
            continue
        filtered.append(line)

    cleaned = "".join(filtered)
    return cantools.database.load_string(cleaned, database_format="dbc")

class DBCDecoderGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("CAN Bus Log Decoder - Professional Edition")
        self.root.geometry("1100x750")
        self.root.resizable(True, True)
        
        # Modern attractive color scheme - Clean White Theme with Vibrant Accents
        
        
        self.colors = {
            'bg_main': '#F2F4F8',          # App background
            'bg_secondary': '#E8ECF3',     # Soft panel background
            'bg_card': '#FFFFFF',          # Card background
            'bg_input': '#F8FAFC',         # Input background
            'accent': '#06B6D4',           # Cyan accent
            'accent_hover': '#0891B2',     # Cyan hover
            'accent_alt': '#F97316',       # Orange accent
            'accent_alt_hover': '#EA580C', # Orange hover
            'success': '#10B981',          # Green
            'warning': '#F59E0B',          # Amber
            'error': '#EF4444',            # Red
            'text': '#0F172A',             # Primary text
            'text_secondary': '#334155',   # Secondary text
            'text_muted': '#64748B',       # Muted text
            'border': '#E2E8F0',           # Border color
            'header_bg': '#0B1220',        # Header background
            'header_text': '#F8FAFC',      # Header title
            'header_subtext': '#CBD5E1',   # Header subtitle
            'button': '#06B6D4',           # Primary button
            'button_hover': '#0891B2'      # Primary hover
        }
        
        # Configure root background
        self.root.configure(bg=self.colors['bg_main'])
        
        # Variables
        self.csv_file_path = tk.StringVar()
        self.dbc_file_path = tk.StringVar()
        self.decoding = False
        
        # Store decoded data for tabs
        self.decoded_df = None
        self.raw_df = None
        self.stats_data = {}
        self.all_signal_names = set()
        self.signal_units = {}  # {signal_name: unit_string} - extracted from DBC
        self.signal_offsets = {}  # {signal_name: offset_value} - for verification
        self.signal_scales = {}  # {signal_name: scale_value} - for verification
        self.db = None  # Store DBC database for signal info access
        self.logo_img = None  # Logo image cache
        
        # Thread-safe queue for GUI updates from background threads
        self.gui_queue = queue.Queue()
        
        self.create_widgets()
        
        # Start processing GUI update queue
        self.process_gui_queue()
        
    def process_gui_queue(self):
        """Process GUI update queue from background threads - called periodically from main thread"""
        try:
            while True:
                try:
                    callback = self.gui_queue.get_nowait()
                    callback()
                except queue.Empty:
                    break
        except Exception as e:
            print(f"Error processing GUI queue: {e}")
        # Schedule next check
        self.root.after(50, self.process_gui_queue)
    
    def safe_gui_update(self, callback):
        """Thread-safe method to update GUI from background threads"""
        self.gui_queue.put(callback)
        return None

    def _load_logo_image(self):
        search_dirs = [Path(__file__).parent / 'assets', Path(__file__).parent]
        candidates = (
            'naxatra_labs_logo.png', 'naxatra_labs_logo.jpg', 'naxatra_labs_logo.jpeg',
            'logo.png', 'logo.jpg', 'logo.jpeg'
        )
        for base_dir in search_dirs:
            if not base_dir.exists():
                continue
            for name in candidates:
                logo_path = base_dir / name
                if not logo_path.exists():
                    continue
                try:
                    if PIL_AVAILABLE:
                        img = Image.open(str(logo_path)).convert('RGBA')
                        img.thumbnail((140, 140), Image.LANCZOS)
                        return ImageTk.PhotoImage(img)
                    if logo_path.suffix.lower() == '.png':
                        photo = tk.PhotoImage(file=str(logo_path))
                        w = max(1, photo.width())
                        if w > 140:
                            factor = max(1, w // 140)
                            photo = photo.subsample(factor, factor)
                        return photo
                except Exception:
                    return None
            for ext in ('.png', '.jpg', '.jpeg'):
                for logo_path in base_dir.glob(f'*logo*{ext}'):
                    try:
                        if PIL_AVAILABLE:
                            img = Image.open(str(logo_path)).convert('RGBA')
                            img.thumbnail((140, 140), Image.LANCZOS)
                            return ImageTk.PhotoImage(img)
                        if logo_path.suffix.lower() == '.png':
                            photo = tk.PhotoImage(file=str(logo_path))
                            w = max(1, photo.width())
                            if w > 140:
                                factor = max(1, w // 140)
                                photo = photo.subsample(factor, factor)
                            return photo
                    except Exception:
                        return None
        return None

    def create_widgets(self):
        # Header Section with artistic banner + logo
        header_frame = tk.Frame(self.root, bg=self.colors['header_bg'], height=140)
        header_frame.pack(fill=tk.X)
        header_frame.pack_propagate(False)

        header_canvas = tk.Canvas(header_frame, bg=self.colors['header_bg'], highlightthickness=0)
        header_canvas.pack(fill=tk.BOTH, expand=True)

        # Base band
        header_canvas.create_rectangle(0, 0, 2000, 140, fill=self.colors['header_bg'], outline="")

        # Artistic geometry
        header_canvas.create_polygon(0, 0, 300, 0, 230, 140, 0, 140,
                                     fill=self.colors['accent'], outline="")
        header_canvas.create_polygon(240, 0, 520, 0, 620, 140, 360, 140,
                                     fill=self.colors['accent_alt'], outline="")
        header_canvas.create_polygon(980, 0, 1400, 0, 1600, 140, 1180, 140,
                                     fill="#111827", outline="")
        header_canvas.create_rectangle(0, 132, 2000, 140, fill=self.colors['accent_alt'], outline="")

        # Logo
        self.logo_img = self._load_logo_image()

        text_x = 28
        if self.logo_img is not None:
            header_canvas.create_image(28, 22, anchor="nw", image=self.logo_img)
            text_x = 28 + self.logo_img.width() + 16

        header_canvas.create_text(
            text_x,
            28,
            anchor="nw",
            text="Naxatra Labs CAN Decoder",
            font=("Segoe UI", 22, "bold"),
            fill=self.colors['header_text'],
        )
        header_canvas.create_text(
            text_x,
            64,
            anchor="nw",
            text="Decode logs, inspect signals, and export clean datasets.",
            font=("Segoe UI", 11),
            fill=self.colors['header_subtext'],
        )

        # Main container with padding



        main_frame = tk.Frame(self.root, bg=self.colors['bg_main'], padx=20, pady=20)
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # File Selection Card
        file_card = tk.Frame(main_frame, bg=self.colors['bg_card'], relief=tk.FLAT, bd=0)
        file_card.pack(fill=tk.X, pady=(0, 15))
        file_card.config(highlightbackground=self.colors['border'], highlightthickness=1)
        
        # Card title
        card_title = tk.Label(file_card,
                             text="📁 File Selection",
                             font=("Segoe UI", 14, "bold"),
                             bg=self.colors['bg_card'],
                             fg=self.colors['text'],
                             anchor='w')
        card_title.pack(fill=tk.X, padx=20, pady=(15, 10))
        
        # File selection frame
        file_inner = tk.Frame(file_card, bg=self.colors['bg_card'])
        file_inner.pack(fill=tk.X, padx=20, pady=(0, 15))
        
        # Log File Selection
        log_frame = tk.Frame(file_inner, bg=self.colors['bg_card'])
        log_frame.pack(fill=tk.X, pady=(0, 12))
        
        log_label = tk.Label(log_frame,
                            text="Log File (.nxt/.csv):",
                            font=("Segoe UI", 10, "bold"),
                            bg=self.colors['bg_card'],
                            fg=self.colors['text_secondary'],
                            anchor='w',
                            width=18)
        log_label.pack(side=tk.LEFT, padx=(0, 10))
        
        log_entry = tk.Entry(log_frame,
                            textvariable=self.csv_file_path,
                            font=("Segoe UI", 10),
                            bg=self.colors['bg_input'],
                            fg=self.colors['text'],
                            insertbackground=self.colors['text'],
                            relief=tk.FLAT,
                            bd=0,
                            highlightthickness=1,
                            highlightbackground=self.colors['border'],
                            highlightcolor=self.colors['accent'],
                            width=50)
        log_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 10), ipady=8)
        
        log_browse = tk.Button(log_frame,
                              text="Browse...",
                              font=("Segoe UI", 9, "bold"),
                              bg=self.colors['accent_alt'],
                              fg='white',
                              activebackground=self.colors['accent_alt_hover'],
                              activeforeground='white',
                              relief=tk.FLAT,
                              padx=20,
                              pady=6,
                              cursor='hand2',
                              bd=0,
                              command=self.browse_csv_file)
        log_browse.pack(side=tk.LEFT)
        
        # DBC File Selection
        dbc_frame = tk.Frame(file_inner, bg=self.colors['bg_card'])
        dbc_frame.pack(fill=tk.X, pady=(0, 12))
        
        dbc_label = tk.Label(dbc_frame,
                            text="DBC File:",
                            font=("Segoe UI", 10, "bold"),
                            bg=self.colors['bg_card'],
                            fg=self.colors['text_secondary'],
                            anchor='w',
                            width=18)
        dbc_label.pack(side=tk.LEFT, padx=(0, 10))
        
        dbc_entry = tk.Entry(dbc_frame,
                            textvariable=self.dbc_file_path,
                            font=("Segoe UI", 10),
                            bg=self.colors['bg_input'],
                            fg=self.colors['text'],
                            insertbackground=self.colors['text'],
                            relief=tk.FLAT,
                            bd=0,
                            highlightthickness=1,
                            highlightbackground=self.colors['border'],
                            highlightcolor=self.colors['accent'],
                            width=50)
        dbc_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 10), ipady=8)
        
        dbc_browse = tk.Button(dbc_frame,
                              text="Browse...",
                              font=("Segoe UI", 9, "bold"),
                              bg=self.colors['accent'],
                              fg='white',
                              activebackground=self.colors['accent_hover'],
                              activeforeground='white',
                              relief=tk.FLAT,
                              padx=20,
                              pady=6,
                              cursor='hand2',
                              bd=0,
                              command=self.browse_dbc_file)
        dbc_browse.pack(side=tk.LEFT)
        
        # Info label
        help_label = tk.Label(file_inner,
                             text="💡 Tip: Decoded data will be available in Statistics, Visualization, and Export tabs",
                             font=("Segoe UI", 8, "italic"),
                             bg=self.colors['bg_card'],
                             fg=self.colors['text_muted'])
        help_label.pack(anchor='w', padx=(0, 0), pady=(0, 10))
        
        # Buttons Frame
        button_frame = tk.Frame(file_card, bg=self.colors['bg_card'])
        button_frame.pack(fill=tk.X, padx=20, pady=(0, 15))
        
        self.decode_button = tk.Button(button_frame,
                                      text="🔍 Decode CAN Messages",
                                      font=("Segoe UI", 11, "bold"),
                                      bg=self.colors['button'],
                                      fg='white',
                                      activebackground=self.colors['button_hover'],
                                      activeforeground='white',
                                      relief=tk.FLAT,
                                      padx=25,
                                      pady=10,
                                      cursor='hand2',
                                      bd=0,
                                      command=self.start_decode)
        self.decode_button.pack(side=tk.LEFT, padx=(0, 10))
        
        view_dbc_btn = tk.Button(button_frame,
                                 text="📖 View DBC Messages",
                                 font=("Segoe UI", 10, "bold"),
                                 bg=self.colors['accent'],
                                 fg='white',
                                 activebackground=self.colors['accent_hover'],
                                 activeforeground='white',
                                 relief=tk.FLAT,
                                 padx=20,
                                 pady=10,
                                 cursor='hand2',
                                 bd=0,
                                 command=self.view_dbc_messages)
        view_dbc_btn.pack(side=tk.LEFT, padx=(0, 10))
        
        clear_btn = tk.Button(button_frame,
                             text="🗑️ Clear Output",
                             font=("Segoe UI", 10, "bold"),
                             bg=self.colors['bg_secondary'],
                             fg=self.colors['text'],
                             activebackground=self.colors['border'],
                             activeforeground=self.colors['text'],
                             relief=tk.FLAT,
                             padx=20,
                             pady=10,
                             cursor='hand2',
                             bd=0,
                             command=self.clear_output)
        clear_btn.pack(side=tk.LEFT)
        
        # Progress Bar
        progress_frame = tk.Frame(main_frame, bg=self.colors['bg_main'])
        progress_frame.pack(fill=tk.X, pady=(0, 10))
        
        # Configure progress bar style
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TProgressbar',
                       background=self.colors['accent'],
                       troughcolor=self.colors['bg_card'],
                       borderwidth=0,
                       lightcolor=self.colors['accent'],
                       darkcolor=self.colors['accent'])
        
        self.progress = ttk.Progressbar(progress_frame, mode='indeterminate', style='TProgressbar')
        self.progress.pack(fill=tk.X)
        
        # Status Label
        self.status_label = tk.Label(progress_frame,
                                    text="● Ready",
                                    font=("Segoe UI", 10),
                                    bg=self.colors['bg_main'],
                                    fg=self.colors['success'],
                                    anchor='w')
        self.status_label.pack(fill=tk.X, pady=(5, 0))
        
        # Create Notebook for Tabs
        self.notebook = ttk.Notebook(main_frame)
        self.notebook.pack(fill=tk.BOTH, expand=True, pady=(0, 10))
        
        # Configure notebook style
        style = ttk.Style()
        style.theme_use('clam')
        style.configure('TNotebook', background=self.colors['bg_main'], borderwidth=0)
        style.configure('TNotebook.Tab', background=self.colors['bg_secondary'], foreground=self.colors['text'],
                      padding=[20, 10], borderwidth=1)
        style.map('TNotebook.Tab', 
                 background=[('selected', self.colors['accent'])],
                 foreground=[('selected', 'white')])
        
        # Tab 1: Decoding Output
        self.setup_output_tab()
        
        # Tab 2: Statistics
        self.setup_statistics_tab()
        
        # Tab 3: Visualization
        self.setup_visualization_tab()
        
        # Tab 4: Export
        self.setup_export_tab()
    
    def setup_output_tab(self):
        """Setup output tab with decoding preview"""
        output_frame = tk.Frame(self.notebook, bg=self.colors['bg_main'])
        self.notebook.add(output_frame, text="📊 Decoding Output")
        
        # Output Text Area
        text_frame = tk.Frame(output_frame, bg=self.colors['bg_main'])
        text_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        
        self.output_text = scrolledtext.ScrolledText(text_frame,
                                                     height=30,
                                                     width=100,
                                                     wrap=tk.WORD,
                                                     font=("Segoe UI", 9),
                                                     bg=self.colors['bg_input'],
                                                     fg=self.colors['text'],
                                                     insertbackground=self.colors['text'],
                                                     relief=tk.FLAT,
                                                     bd=0,
                                                     highlightthickness=1,
                                                     highlightbackground=self.colors['border'],
                                                     highlightcolor=self.colors['accent'],
                                                     selectbackground=self.colors['accent'],
                                                     selectforeground='white')
        self.output_text.pack(fill=tk.BOTH, expand=True)
    
    def setup_statistics_tab(self):
        """Setup statistics tab"""
        self.stats_frame = tk.Frame(self.notebook, bg=self.colors['bg_main'])
        self.notebook.add(self.stats_frame, text="📈 Statistics")
        
        # Stats display
        self.stats_text = scrolledtext.ScrolledText(self.stats_frame,
                                                    wrap=tk.WORD,
                                                    font=("Segoe UI", 10),
                                                    bg=self.colors['bg_input'],
                                                    fg=self.colors['text'],
                                                    relief=tk.FLAT,
                                                    bd=0,
                                                    highlightthickness=1,
                                                    highlightbackground=self.colors['border'],
                                                    highlightcolor=self.colors['accent'])
        self.stats_text.pack(fill=tk.BOTH, expand=True, padx=20, pady=20)
        self.stats_text.insert(1.0, "No data decoded yet. Please decode a file first.")
        self.stats_text.config(state=tk.DISABLED)
    
    def setup_visualization_tab(self):
        """Setup visualization tab"""
        viz_frame = tk.Frame(self.notebook, bg=self.colors['bg_main'])
        self.notebook.add(viz_frame, text="📉 Visualization")
        
        # Try to import matplotlib
        try:
            import matplotlib.pyplot as plt
            from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
            from matplotlib.figure import Figure
            MATPLOTLIB_AVAILABLE = True
        except ImportError:
            MATPLOTLIB_AVAILABLE = False
        
        if not MATPLOTLIB_AVAILABLE:
            error_label = tk.Label(viz_frame,
                                  text="Matplotlib not available.\n\nInstall with: pip install matplotlib",
                                  font=("Segoe UI", 12),
                                  bg=self.colors['bg_main'],
                                  fg=self.colors['error'],
                                  justify=tk.CENTER)
            error_label.pack(expand=True)
            self.fig = None
            self.canvas = None
            return
        
        # Control frame
        control_frame = tk.Frame(viz_frame, bg=self.colors['bg_main'])
        control_frame.pack(fill=tk.X, padx=20, pady=(20, 10))
        
        plot_label = tk.Label(control_frame,
                             text="Chart Type:",
                             font=("Segoe UI", 10, "bold"),
                             bg=self.colors['bg_main'],
                             fg=self.colors['text'])
        plot_label.pack(side=tk.LEFT, padx=(0, 10))
        
        self.plot_type_var = tk.StringVar(value="Message Count")
        plot_types = [
            "Message Count",
            "CAN ID Distribution",
            "Data Rate",
            "Message Rate",
            "Signal Values"
        ]
        plot_combo = ttk.Combobox(control_frame,
                                 textvariable=self.plot_type_var,
                                 values=plot_types,
                                 state="readonly",
                                 width=25,
                                 font=("Segoe UI", 9))
        plot_combo.pack(side=tk.LEFT, padx=(0, 10))

        # Single dropdown kept for chart type only
        
        plot_btn = tk.Button(control_frame,
                            text="📊 Generate Chart",
                            font=("Segoe UI", 10, "bold"),
                            bg=self.colors['accent'],
                            fg='white',
                            activebackground=self.colors['accent_hover'],
                            relief=tk.FLAT,
                            padx=20,
                            pady=8,
                            cursor='hand2',
                            bd=0,
                            command=self.generate_plot)
        plot_btn.pack(side=tk.LEFT)
        
        # Matplotlib figure
        try:
            self.fig = Figure(figsize=(12, 6), facecolor='white', dpi=100)
            self.canvas = FigureCanvasTkAgg(self.fig, viz_frame)
            self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True, padx=20, pady=(0, 20))
        except Exception as e:
            print(f"Warning: Could not create matplotlib figure: {e}")
            self.fig = None
            self.canvas = None
    
    def setup_export_tab(self):
        """Setup export tab"""
        export_frame = tk.Frame(self.notebook, bg=self.colors['bg_main'])
        self.notebook.add(export_frame, text="💾 Export")
        
        # Export format selection
        format_frame = tk.Frame(export_frame, bg=self.colors['bg_card'], relief=tk.FLAT, bd=0)
        format_frame.pack(fill=tk.X, padx=20, pady=20)
        format_frame.config(highlightbackground=self.colors['border'], highlightthickness=1)
        
        format_title = tk.Label(format_frame,
                               text="📤 Export Decoded Data",
                               font=("Segoe UI", 14, "bold"),
                               bg=self.colors['bg_card'],
                               fg=self.colors['text'],
                               anchor='w')
        format_title.pack(fill=tk.X, padx=20, pady=(15, 10))
        
        format_inner = tk.Frame(format_frame, bg=self.colors['bg_card'])
        format_inner.pack(fill=tk.X, padx=20, pady=(0, 15))
        
        format_label = tk.Label(format_inner,
                               text="Export Format:",
                               font=("Segoe UI", 10, "bold"),
                               bg=self.colors['bg_card'],
                               fg=self.colors['text_secondary'],
                               anchor='w')
        format_label.pack(side=tk.LEFT, padx=(0, 10))
        
        formats = ["CSV", "XLSX", "MAT", "JSON", "TXT", "HDF5", "PARQUET", "SQLITE", "MF4", "MDF", "PROMETHEUS"]
        self.export_format_var = tk.StringVar(value="CSV")
        format_combo = ttk.Combobox(format_inner,
                                   textvariable=self.export_format_var,
                                   values=formats,
                                   state="readonly",
                                   width=20,
                                   font=("Segoe UI", 9))
        format_combo.pack(side=tk.LEFT, padx=(0, 15))
        
        export_btn = tk.Button(format_inner,
                              text="💾 Export Data",
                              font=("Segoe UI", 11, "bold"),
                              bg=self.colors['button'],
                              fg='white',
                              activebackground=self.colors['button_hover'],
                              relief=tk.FLAT,
                              padx=25,
                              pady=10,
                              cursor='hand2',
                              bd=0,
                              command=self.export_decoded_data)
        export_btn.pack(side=tk.LEFT)
        
        # Info label
        info_label = tk.Label(format_frame,
                             text="💡 Select a format and click Export to save your decoded data",
                             font=("Segoe UI", 9, "italic"),
                             bg=self.colors['bg_card'],
                             fg=self.colors['text_muted'])
        info_label.pack(padx=20, pady=(0, 15))
        
    def browse_csv_file(self):
        filename = filedialog.askopenfilename(
            title="Select Log File",
            filetypes=[
                ("Encrypted Logs (*.nxt)", "*.nxt"),
                ("CSV files", "*.csv"),
                ("All files", "*.*")
            ]
        )
        if filename:
            self.csv_file_path.set(filename)
            
    def browse_dbc_file(self):
        filename = filedialog.askopenfilename(
            title="Select DBC File",
            filetypes=[("DBC files", "*.dbc"), ("All files", "*.*")]
        )
        if filename:
            self.dbc_file_path.set(filename)
            
    def update_status(self, message):
        """Update status label - can be called from any thread"""
        if threading.current_thread() == threading.main_thread():
            # Called from main thread - update directly
            self.status_label.config(text=f"● {message}", fg=self.colors['accent'])
            self.root.update_idletasks()
        else:
            # Called from background thread - use thread-safe update
            self.safe_gui_update(lambda msg=message: self.status_label.config(text=f"● {msg}", fg=self.colors['accent']))
    
    def clear_output(self):
        self.output_text.delete(1.0, tk.END)
        self.stats_label.config(text="No messages decoded yet", fg=self.colors['text_muted'])
        self.update_status("Ready")
    
    def append_output(self, text):
        """Append text to output - can be called from any thread"""
        if threading.current_thread() == threading.main_thread():
            # Called from main thread - update directly
            self.output_text.insert(tk.END, text + "\n")
            self.output_text.see(tk.END)
            self.root.update_idletasks()
        else:
            # Called from background thread - use thread-safe update
            self.safe_gui_update(lambda txt=text: (
                self.output_text.insert(tk.END, txt + "\n"),
                self.output_text.see(tk.END)
            ))

    def _ensure_timestamps_column(self, df):
        """Ensure a 'timestamps' column exists for plotting/export."""
        if df is None or 'timestamps' in df.columns:
            return df
        if 'Date' in df.columns and 'Time' in df.columns:
            df['timestamps'] = df['Date'].astype(str) + " " + df['Time'].astype(str)
        elif 'Timestamp' in df.columns:
            df['timestamps'] = df['Timestamp'].astype(str)
        return df

    def _get_timestamp_series(self, df):
        """Return pandas datetime series for plotting if possible."""
        if df is None or pd is None:
            return None
        if 'timestamps' in df.columns:
            t = pd.to_datetime(df['timestamps'], errors='coerce')
        elif 'Date' in df.columns and 'Time' in df.columns:
            t = pd.to_datetime(df['Date'].astype(str) + " " + df['Time'].astype(str), errors='coerce')
        elif 'Timestamp' in df.columns:
            t = pd.to_datetime(df['Timestamp'], errors='coerce')
        else:
            return None
        if t is None or t.isna().all():
            return None
        return t
    
    def view_dbc_messages(self):
        if cantools is None:
            messagebox.showerror(
                "Missing Dependency",
                "The decoder can launch, but DBC features require 'cantools'.\n\n"
                "Install it with:\n"
                "  python -m pip install cantools\n\n"
                "If you are using Python 3.14, you may need Microsoft C++ Build Tools\n"
                "or use Python 3.12/3.11 where wheels are commonly available."
            )
            return

        dbc_file = self.dbc_file_path.get()
        
        if not dbc_file:
            messagebox.showwarning("Warning", "Please select a DBC file first!")
            return
        
        if not Path(dbc_file).exists():
            messagebox.showerror("Error", f"DBC file not found:\n{dbc_file}")
            return
        
        try:
            self.update_status("Loading DBC file...")
            db = _load_dbc_with_fallback(dbc_file)
            
            # Clear output and show DBC messages
            self.output_text.delete(1.0, tk.END)
            self.append_output(f"DBC File: {dbc_file}")
            self.append_output(f"Total Messages: {len(db.messages)}\n")
            self.append_output("=" * 80)
            self.append_output("")
            
            for message in db.messages:
                # Format matches dbc_decoder_gui.py and dbc_decode_csv.py exactly
                self.append_output(f"ID: 0x{message.frame_id:03X} ({message.frame_id}) | "
                                 f"Name: {message.name} | Length: {message.length} bytes")
                if message.senders:
                    self.append_output(f"  Senders: {', '.join(message.senders)}")
                if message.signals:
                    self.append_output(f"  Signals ({len(message.signals)}):")
                    for signal in message.signals:
                        # Match format from dbc_decoder_gui.py
                        self.append_output(f"    - {signal.name}: {signal.length} bits "
                                         f"@ {signal.start} (factor: {signal.scale}, offset: {signal.offset})")
                self.append_output("")
            
            self.update_status(f"Loaded {len(db.messages)} message definitions")
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load DBC file:\n{str(e)}")
            self.update_status("Error loading DBC file")
    
    def start_decode(self):
        if self.decoding:
            # Check if decoding is actually running by checking progress bar state
            try:
                # If progress bar exists and is running, decoding is in progress
                if self.progress.winfo_exists():
                    progress_mode = self.progress.cget('mode')
                    # If progress bar is in indeterminate mode, it's running
                    if progress_mode == 'indeterminate':
                        messagebox.showinfo("Info", "Decoding is already in progress!")
                        return
                # If we get here, decoding flag is stuck - reset it
                self.decoding = False
                self.decode_button.config(state='normal')
                self.progress.stop()
            except:
                # If there's any error checking, assume stuck and reset
                self.decoding = False
                try:
                    self.decode_button.config(state='normal')
                    self.progress.stop()
                except:
                    pass
        
        csv_file = self.csv_file_path.get()
        dbc_file = self.dbc_file_path.get()
        
        # Validate inputs
        if not csv_file:
            messagebox.showwarning("Warning", "Please select a log file!")
            return
        
        if not dbc_file:
            messagebox.showwarning("Warning", "Please select a DBC file!")
            return
        
        if not Path(csv_file).exists():
            messagebox.showerror("Error", f"Log file not found:\n{csv_file}")
            return

        if Path(csv_file).suffix.lower() not in ('.csv', '.nxt'):
            messagebox.showerror("Error", "Unsupported log file type. Select a .nxt or .csv file.")
            return
        
        if not Path(dbc_file).exists():
            messagebox.showerror("Error", f"DBC file not found:\n{dbc_file}")
            return
        
        # Start decoding in a separate thread
        thread = threading.Thread(target=self.decode_messages, 
                                 args=(csv_file, dbc_file))
        thread.daemon = True
        thread.start()
    
    def decode_messages(self, csv_file, dbc_file):
        # Dependencies are optional for launching; decoding requires them.
        if pd is None or cantools is None:
            missing = []
            if pd is None:
                missing.append("pandas")
            if cantools is None:
                missing.append("cantools")

            msg = (
                "The decoder GUI launched, but decoding requires: "
                + ", ".join(missing)
                + "\n\nInstall with:\n  python -m pip install "
                + " ".join(missing)
            )
            self.safe_gui_update(lambda m=msg: messagebox.showerror("Missing Dependency", m))
            self.decoding = False
            self.safe_gui_update(lambda: self.decode_button.config(state='normal'))
            try:
                self.safe_gui_update(lambda: self.progress.stop())
            except Exception:
                pass
            return

        self.decoding = True
        self.safe_gui_update(lambda: self.decode_button.config(state='disabled'))
        self.safe_gui_update(lambda: self.progress.start(10))
        self.safe_gui_update(lambda: self.update_status("Starting decoding..."))
        
        try:
            # Clear previous output
            self.output_text.delete(1.0, tk.END)
            self.append_output("=" * 80)
            self.append_output("CAN BUS LOG DECODER")
            self.append_output("=" * 80)
            self.append_output("")
            
            # Load DBC database
            self.update_status("Loading DBC file...")
            self.append_output(f"Loading DBC file: {dbc_file}")
            db = _load_dbc_with_fallback(dbc_file)
            self.append_output(f"Loaded {len(db.messages)} message definitions")
            self.append_output("")
            
            # Build signal-to-unit mapping for all signals in DBC
            # This will be used to add units row to output CSV
            self.signal_units = {}  # {signal_name: unit_string}
            self.signal_offsets = {}  # {signal_name: offset_value} for verification
            self.signal_scales = {}  # {signal_name: scale_value} for verification
            
            for message in db.messages:
                for signal in message.signals:
                    signal_name = signal.name
                    # Store unit (empty string if not specified)
                    self.signal_units[signal_name] = signal.unit or ''
                    # Store offset and scale for verification
                    self.signal_offsets[signal_name] = signal.offset
                    self.signal_scales[signal_name] = signal.scale
                    
                    # Debug output for signals with offsets (especially Bus_current)
                    if signal.offset != 0 or 'current' in signal_name.lower() or 'bus' in signal_name.lower():
                        self.append_output(f"Signal: {signal_name} | Offset: {signal.offset} | Scale: {signal.scale} | Unit: {signal.unit or 'N/A'}")
            
            if self.signal_units:
                self.append_output(f"\nExtracted units for {len(self.signal_units)} signals")
            self.append_output("")
            
            temp_plaintext = None
            input_path = csv_file
            try:
                if csv_file.lower().endswith('.nxt'):
                    self.update_status("Decrypting encrypted log...")
                    self.append_output(f"Decrypting encrypted log: {csv_file}")
                    temp_plaintext = decrypt_nxt_file(csv_file)
                    input_path = temp_plaintext
                self.update_status("Reading log file...")
                self.append_output(f"Reading log file: {input_path}")
                # Robust CSV load: tolerate non-UTF8 bytes or malformed lines
                def _read_csv_with_kwargs(**kwargs):
                    try:
                        return pd.read_csv(input_path, **kwargs)
                    except TypeError as e:
                        # pandas < 1.3 doesn't support on_bad_lines
                        if "on_bad_lines" in str(e):
                            kwargs.pop("on_bad_lines", None)
                            kwargs["error_bad_lines"] = False
                            kwargs["warn_bad_lines"] = True
                            return pd.read_csv(input_path, **kwargs)
                        raise
                try:
                    df = pd.read_csv(input_path, encoding="utf-8")
                except (UnicodeDecodeError, ParserError):
                    try:
                        df = _read_csv_with_kwargs(
                            encoding="utf-8",
                            engine="python",
                            on_bad_lines="skip",
                        )
                    except (UnicodeDecodeError, ParserError):
                        try:
                            df = pd.read_csv(input_path, encoding="latin-1")
                        except (UnicodeDecodeError, ParserError):
                            try:
                                df = _read_csv_with_kwargs(
                                    encoding="latin-1",
                                    engine="python",
                                    on_bad_lines="skip",
                                )
                            except Exception:
                                # Last-resort: replace invalid bytes before parsing
                                import io
                                with open(input_path, "r", encoding="utf-8", errors="replace") as fh:
                                    df = pd.read_csv(
                                        io.StringIO(fh.read()),
                                        engine="python",
                                        on_bad_lines="skip",
                                    )
            finally:
                if temp_plaintext and os.path.exists(temp_plaintext):
                    try:
                        os.remove(temp_plaintext)
                    except OSError:
                        pass

            self.append_output(f"Loaded {len(df)} CAN messages")
            self.append_output("")
            self.append_output("Decoding messages...")
            self.append_output("-" * 80)
            self.append_output("")
            
            # Normalize column names (strip whitespace/BOM)
            df.columns = [c.strip().lstrip("\ufeff") for c in df.columns]

            # Compatibility: normalize common column names
            col_map = {}
            if "ID" not in df.columns:
                for alt in ["Id", "CAN_ID", "CanID", "CANID", "Identifier", "ArbID"]:
                    if alt in df.columns:
                        col_map[alt] = "ID"
                        break
            if "DLC" not in df.columns:
                for alt in ["Len", "Length", "DataLength", "Dlc"]:
                    if alt in df.columns:
                        col_map[alt] = "DLC"
                        break
            if col_map:
                df = df.rename(columns=col_map)

            # Check file format - support both raw data (Data0-7) and pre-decoded signals
            has_raw_data = all(f'Data{i}' in df.columns for i in range(8))
            has_decoded_signals = any('.' in col for col in df.columns)  # MessageName.SignalName format
            
            # Verify basic required columns
            required_columns = ['ID', 'DLC']
            missing_columns = [col for col in required_columns if col not in df.columns]
            if missing_columns:
                raise ValueError(f"CSV file missing required columns: {missing_columns}")
            
            # If no raw data and no decoded signals, we can't decode
            if not has_raw_data and not has_decoded_signals:
                raise ValueError("CSV file must have either Data0-7 columns (raw format) or decoded signal columns (MessageName.SignalName format)")
            
            if has_raw_data:
                self.append_output("Detected format: Raw CAN data (Data0-7 columns)")
            elif has_decoded_signals:
                self.append_output("Detected format: Pre-decoded signals (MessageName.SignalName columns)")
                self.append_output("Note: This file appears to already be decoded. Re-decoding with DBC...")
            self.append_output("")
            
            # Build per-frame output rows to match new.csv (magnetometer removed):
            # Date, Time, CAN_ID, LinearAccelX, LinearAccelY, LinearAccelZ,
            # Bus_current, Bus_voltage, Controller_temp, Gear_status, HW_version, Handle_opening,
            # Id_current, Iq_current, Miles_remaining, Motor_speed, Motor_temp, Obligate,
            # Phase_current_RMS, Power_mode, SW_version, Speed, Status_feedback1, Status_feedback2,
            # Status_feedback3, Subtotal_mileage, Vendor_code, Wheel_circumference
            decoded_rows = []
            decoded_count = 0
            error_count = 0
            all_signal_names = set()
            db_ids = set()
            try:
                db_ids = {m.frame_id for m in db.messages}
            except Exception:
                db_ids = set()
            last_bus_current = None

            # Base column order (new.csv compatible). Extra decoded signals will be appended.
            base_cols = [
                "Date", "Time", "CAN_ID",
                "LinearAccelX", "LinearAccelY", "LinearAccelZ", "Gravity",
                "GPS_Lat", "GPS_Lon", "GPS_Alt", "GPS_Speed", "GPS_Course", "GPS_Sats", "GPS_HDOP", "GPS_Time",
                "Bus_current", "Bus_voltage", "Controller_temp", "Gear_status",
                "HW_version", "Handle_opening", "Id_current", "Iq_current",
                "Miles_remaining", "Motor_speed", "Motor_temp", "Obligate",
                "Phase_current_RMS", "Power_mode", "SW_version", "Speed",
                "Status_feedback1", "Status_feedback2", "Status_feedback3",
                "Subtotal_mileage", "Vendor_code", "Wheel_circumference",
            ]
            extra_signal_cols = set()

            signal_to_col = [
                "Bus_current", "Bus_voltage", "Controller_temp", "Gear_status",
                "HW_version", "Handle_opening", "Id_current", "Iq_current",
                "Miles_remaining", "Motor_speed", "Motor_temp", "Obligate",
                "Phase_current_RMS", "Power_mode", "SW_version", "Speed",
                "Status_feedback1", "Status_feedback2", "Status_feedback3",
                "Subtotal_mileage", "Vendor_code", "Wheel_circumference"
            ]

            # Iterate every row 1:1
            for idx, row in df.iterrows():
                try:
                    if not has_raw_data:
                        # Pass-through for already-decoded files
                        row_out = {c: "" for c in final_cols}

                        # Date/Time handling
                        if "Date" in row and "Time" in row:
                            row_out["Date"] = row.get("Date", "")
                            row_out["Time"] = row.get("Time", "")
                        else:
                            ts_formatted = _format_timestamp_high_res(row.get("Timestamp", ""), row.get("Microseconds", None))
                            if " " in ts_formatted:
                                date_part, time_part = ts_formatted.split(" ", 1)
                            else:
                                date_part, time_part = ts_formatted, ""
                            row_out["Date"] = date_part
                            row_out["Time"] = time_part

                        # CAN ID
                        if "CAN_ID" in row and str(row.get("CAN_ID", "")).strip():
                            row_out["CAN_ID"] = row.get("CAN_ID", "")
                        elif "ID" in row and str(row.get("ID", "")).strip():
                            can_id_val = _safe_int_hex_or_dec(row.get("ID", "0"))
                            row_out["CAN_ID"] = f"0x{can_id_val:X}"

                        # Linear acceleration
                        row_out["LinearAccelX"] = row.get("LinearAccelX", "")
                        row_out["LinearAccelY"] = row.get("LinearAccelY", "")
                        row_out["LinearAccelZ"] = row.get("LinearAccelZ", "")
                        row_out["Gravity"] = row.get("Gravity", "")

                        # GPS fields
                        row_out["GPS_Lat"] = row.get("GPS_Lat", 0)
                        row_out["GPS_Lon"] = row.get("GPS_Lon", 0)
                        row_out["GPS_Alt"] = row.get("GPS_Alt", 0)
                        row_out["GPS_Speed"] = row.get("GPS_Speed", 0)
                        row_out["GPS_Course"] = row.get("GPS_Course", 0)
                        row_out["GPS_Sats"] = row.get("GPS_Sats", 0)
                        row_out["GPS_HDOP"] = row.get("GPS_HDOP", 0)
                        row_out["GPS_Time"] = row.get("GPS_Time", "0")

                        # Copy signals if already present
                        for name in signal_to_col:
                            row_out[name] = row.get(name, "")

                        # Preserve any extra decoded columns already in the file
                        for col in df.columns:
                            if col in row_out:
                                continue
                            if col in ("Date", "Time", "Timestamp", "Microseconds", "ID", "CAN_ID", "DLC"):
                                continue
                            row_out[col] = row.get(col, "")
                            extra_signal_cols.add(col)


                        decoded_rows.append(row_out)
                        decoded_count += 1
                        continue

                    raw_id = row.get('ID', '')
                    if raw_id is None or (isinstance(raw_id, float) and pd.isna(raw_id)) or str(raw_id).strip() == "":
                        error_count += 1
                        continue
                    can_id = _safe_int_hex_or_dec(raw_id)
                    if db_ids and can_id not in db_ids:
                        try:
                            dec_id = int(str(raw_id).strip(), 10)
                            if dec_id in db_ids:
                                can_id = dec_id
                        except Exception:
                            pass

                    # DLC and data bytes
                    try:
                        dlc = int(row.get('DLC', 8))
                    except Exception:
                        dlc = 8
                    data_bytes = _parse_data_bytes_from_row(row, max(min(dlc, 8), 0))
                    data = bytes(data_bytes[:8])

                    # Raw data string (hex)
                    raw_data_str = ",".join(f"{b:02X}" for b in data_bytes[:dlc])

                    # Timestamp formatting: derive from UnixTime + Microseconds when available
                    ts_formatted = ''
                    try:
                        unix_time = row.get('UnixTime', '')
                        micros_val = row.get('Microseconds', None)
                        if unix_time and not pd.isna(unix_time):
                            from datetime import datetime
                            unix_int = int(unix_time)
                            base_dt = datetime.fromtimestamp(unix_int)
                            if micros_val is not None and str(micros_val).strip():
                                micros_int = int(micros_val)
                                base_dt = base_dt.replace(microsecond=micros_int % 1_000_000)
                            ts_formatted = base_dt.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                        else:
                            ts_formatted = _format_timestamp_high_res(row.get('Timestamp', ''), row.get('Microseconds', None))
                    except Exception:
                        ts_formatted = _format_timestamp_high_res(row.get('Timestamp', ''), row.get('Microseconds', None))

                    # Split to Date and Time
                    if " " in ts_formatted:
                        date_part, time_part = ts_formatted.split(" ", 1)
                    else:
                        date_part, time_part = ts_formatted, ""

                    # Decode using DBC
                    decoded = {}
                    try:
                        decoded = db.decode_message(can_id, data, decode_choices=False, scaling=True)
                        all_signal_names.update(decoded.keys())
                    except Exception:
                        decoded = {}
                    # Sanity fix for Bus_current if it is out of expected range
                    try:
                        if "Bus_current" in decoded:
                            bus_val = decoded.get("Bus_current")
                            bus_float = float(bus_val)
                            trigger_threshold = 80.0
                            valid_min = -100.0
                            valid_max = 120.0
                            if abs(bus_float) > trigger_threshold:
                                msg = None
                                try:
                                    msg = db.get_message_by_frame_id(can_id)
                                except Exception:
                                    msg = None
                                if msg is not None:
                                    raw_map = msg.decode(data, decode_choices=False, scaling=False)
                                    raw_bus = None
                                    bus_sig = None
                                    for s in msg.signals:
                                        if str(s.name).strip().lower() == "bus_current":
                                            bus_sig = s
                                            break
                                    for k, v in raw_map.items():
                                        if str(k).strip().lower() == "bus_current":
                                            raw_bus = v
                                            break
                                    if raw_bus is not None and bus_sig is not None:
                                        scale = float(getattr(bus_sig, "scale", 1.0))
                                        offset = float(getattr(bus_sig, "offset", 0.0))
                                        length = int(getattr(bus_sig, "length", 16))
                                        start_bit = int(getattr(bus_sig, "start", 0))
                                        is_signed = bool(getattr(bus_sig, "is_signed", False))

                                        candidates = []
                                        def _add_candidates(raw):
                                            candidates.append(raw * scale + offset)
                                            candidates.append(raw * scale)

                                        _add_candidates(raw_bus)
                                        if length == 16:
                                            swapped = ((int(raw_bus) & 0xFF) << 8) | ((int(raw_bus) >> 8) & 0xFF)
                                            _add_candidates(swapped)

                                        # Alternate endian extraction from bytes
                                        raw_le = _extract_raw_signal(data, start_bit, length, "little_endian")
                                        raw_be = _extract_raw_signal(data, start_bit, length, "big_endian")
                                        if is_signed:
                                            sign_mask = 1 << (length - 1)
                                            if raw_le & sign_mask:
                                                raw_le -= (1 << length)
                                            if raw_be & sign_mask:
                                                raw_be -= (1 << length)
                                        _add_candidates(raw_le)
                                        _add_candidates(raw_be)

                                        # Byte-aligned 16-bit word candidates (common logger layouts)
                                        try:
                                            byte_index = start_bit // 8
                                            if byte_index + 1 < len(data):
                                                word_be = (data[byte_index] << 8) | data[byte_index + 1]
                                                word_le = data[byte_index] | (data[byte_index + 1] << 8)
                                                _add_candidates(word_be)
                                                _add_candidates(word_le)
                                        except Exception:
                                            pass

                                        valid = [v for v in candidates if valid_min <= v <= valid_max]
                                        if valid:
                                            if last_bus_current is not None:
                                                best = min(valid, key=lambda v: abs(v - last_bus_current))
                                            else:
                                                best = min(valid, key=lambda v: abs(v))
                                            decoded["Bus_current"] = best
                                            last_bus_current = best
                                        else:
                                            last_bus_current = bus_float
                            else:
                                last_bus_current = bus_float
                    except Exception:
                        pass


                    # Build output row following new.csv schema
                    row_out = {}
                    row_out["Date"] = date_part
                    row_out["Time"] = time_part
                    row_out["CAN_ID"] = f"0x{can_id:X}"

                    # Linear acceleration (from logger if present)
                    row_out["LinearAccelX"] = row.get("LinearAccelX", "")
                    row_out["LinearAccelY"] = row.get("LinearAccelY", "")
                    row_out["LinearAccelZ"] = row.get("LinearAccelZ", "")
                    row_out["Gravity"] = row.get("Gravity", 0)

                    # GPS fields (from logger if present)
                    row_out["GPS_Lat"] = row.get("GPS_Lat", 0)
                    row_out["GPS_Lon"] = row.get("GPS_Lon", 0)
                    row_out["GPS_Alt"] = row.get("GPS_Alt", 0)
                    row_out["GPS_Speed"] = row.get("GPS_Speed", 0)
                    row_out["GPS_Course"] = row.get("GPS_Course", 0)
                    row_out["GPS_Sats"] = row.get("GPS_Sats", 0)
                    row_out["GPS_HDOP"] = row.get("GPS_HDOP", 0)
                    row_out["GPS_Time"] = row.get("GPS_Time", "0")

                    # Map decoded signals into fixed columns (case/format-insensitive)
                    decoded_norm = {}
                    for key, val in decoded.items():
                        norm_key = "".join(ch.lower() for ch in str(key) if ch.isalnum())
                        decoded_norm[norm_key] = val
                    for name in signal_to_col:
                        if name in decoded:
                            row_out[name] = decoded.get(name, "")
                        else:
                            norm = "".join(ch.lower() for ch in name if ch.isalnum())
                            row_out[name] = decoded_norm.get(norm, "")

                    # Add any other decoded signals as separate columns
                    for sig_name, sig_val in decoded.items():
                        if sig_name in row_out:
                            continue
                        row_out[sig_name] = sig_val
                        extra_signal_cols.add(sig_name)

                    decoded_rows.append(row_out)
                    decoded_count += 1

                    if decoded_count % 500 == 0:
                        self.safe_gui_update(lambda c=decoded_count: self.update_status(f"Processed {c} CAN frames..."))

                except Exception as e:
                    error_count += 1
                    if error_count <= 8:
                        self.append_output(f"ERROR processing row {idx}: {e}")

            if not decoded_rows:
                raise ValueError("No CAN frames were processed into decoded rows.")

            final_cols = base_cols + sorted(extra_signal_cols - set(base_cols))
            output_df = pd.DataFrame(decoded_rows, columns=final_cols).fillna("")

            # Synchronize signals: forward-fill last known values to avoid blank rows
            fill_cols = [c for c in output_df.columns if c not in ("Date", "Time", "CAN_ID")]
            # Ensure columns exist (safe if schema changes)
            fill_cols = [c for c in fill_cols if c in output_df.columns]
            if fill_cols:
                # Sort by timestamp when possible, then forward-fill
                try:
                    time_key = output_df["Date"].astype(str) + " " + output_df["Time"].astype(str)
                    output_df["_time_key"] = pd.to_datetime(time_key, errors="coerce")
                    output_df = output_df.sort_values(by=["_time_key"], kind="stable")
                except Exception:
                    pass
                output_df[fill_cols] = output_df[fill_cols].replace("", pd.NA).ffill()
                # Drop rows that still have no signal data after fill
                mask = output_df[fill_cols].isna().all(axis=1)
                output_df = output_df[~mask].copy()
                if "_time_key" in output_df.columns:
                    output_df = output_df.drop(columns=["_time_key"])
                # Replace any remaining missing values with 0 for signal columns
                output_df[fill_cols] = output_df[fill_cols].fillna(0).replace("", 0)
            raw_df = output_df.copy()

            # Units row for CSV/TXT exports
            units_row = {c: "" for c in output_df.columns}
            units_row["LinearAccelX"] = "m/s^2"
            units_row["LinearAccelY"] = "m/s^2"
            units_row["LinearAccelZ"] = "m/s^2"
            units_row["Gravity"] = "m/s^2"
            units_row["GPS_Lat"] = "deg"
            units_row["GPS_Lon"] = "deg"
            units_row["GPS_Alt"] = "m"
            units_row["GPS_Speed"] = "km/h"
            units_row["GPS_Course"] = "deg"
            units_row["GPS_Sats"] = "count"
            units_row["GPS_HDOP"] = "unitless"
            units_row["GPS_Time"] = "UTC"
            for name in all_signal_names:
                if name in self.signal_units:
                    units_row[name] = self.signal_units.get(name, "")
            self.decoded_units_row = units_row

            # Display decoding results
            self.append_output("")
            self.append_output("=" * 80)
            self.append_output("DECODING COMPLETE - ALL MESSAGES PRESERVED")
            self.append_output("=" * 80)
            self.append_output(f"Input messages: {len(df)}")
            self.append_output(f"Output rows: {len(output_df)} (1:1 with input frames)")
            self.append_output(f"Successfully decoded: {decoded_count}")
            self.append_output(f"Errors: {error_count}")
            self.append_output(f"Columns: {len(output_df.columns)}")
            # Compute unique CAN IDs from CAN_ID column
            try:
                uniq_ids = set()
                for cid in output_df.get("CAN_ID", []):
                    if isinstance(cid, str) and cid.startswith("0x"):
                        try:
                            uniq_ids.add(int(cid, 16))
                        except Exception:
                            pass
                self.append_output(f"Unique CAN IDs: {len(uniq_ids)}")
            except Exception:
                self.append_output("Unique CAN IDs: (unable to compute)")
            self.append_output(f"Unique signals: {len(all_signal_names)}")
            self.append_output("")
            
            # Statistics for display
            try:
                can_id_distribution = output_df['CAN_ID'].value_counts(dropna=False).to_dict()
            except Exception:
                can_id_distribution = {cid: 0 for cid in set(output_df.get('CAN_ID', []))}
            self.stats_data = {
                'total_messages': len(df),
                'decoded_count': decoded_count,
                'error_count': error_count,
                'success_rate': (decoded_count/len(df)*100) if len(df) > 0 else 0,
                'unique_signals': len(all_signal_names),
                'total_rows': len(output_df),
                'can_id_distribution': can_id_distribution,
            }
            
            # Store decoded data
            self.decoded_df = output_df
            self.raw_df = raw_df
            self.all_signal_names = all_signal_names

            # Update statistics display
            self.update_statistics_display()
            
            # Show success message
            self.safe_gui_update(lambda: messagebox.showinfo("Success", 
                              f"Decoding complete!\n\n"
                              f"Input: {len(df)} messages\n"
                              f"Output: {len(output_df)} rows (synchronized cycles)\n"
                              f"Success rate: {(decoded_count/len(df)*100):.1f}%\n"
                              f"Unique CAN IDs: (see output log)\n"
                              f"Unique signals: {len(all_signal_names)}\n\n"
                              f"Synchronized rows created and signals decoded via DBC scaling."))
            
            self.safe_gui_update(lambda: self.update_status(f"Decoding complete! {len(output_df)} rows ready"))
            
        except Exception as e:
            import traceback
            error_msg = f"Error during decoding:\n{str(e)}\n\n{traceback.format_exc()}"
            self.append_output("")
            self.append_output("ERROR: " + error_msg)
            self.safe_gui_update(lambda: messagebox.showerror("Error", error_msg))
        finally:
            self.decoding = False
            self.safe_gui_update(lambda: self.decode_button.config(state='normal'))
            self.safe_gui_update(lambda: self.progress.stop())
    
    def generate_plot(self):
        """Generate visualization plot"""
        try:
            import matplotlib.pyplot as plt
            MATPLOTLIB_AVAILABLE = True
        except ImportError:
            MATPLOTLIB_AVAILABLE = False
        
        if not MATPLOTLIB_AVAILABLE or self.fig is None or self.canvas is None:
            messagebox.showwarning("Warning", "Matplotlib not available. Install with: pip install matplotlib")
            return
        
        if not hasattr(self, 'stats_data') or not self.stats_data:
            messagebox.showwarning("Warning", "No data to plot. Please decode messages first.")
            return
        
        plot_type = self.plot_type_var.get()
        self.fig.clear()
        ax = self.fig.add_subplot(111)
        ax.set_facecolor('white')
        ax.grid(True, alpha=0.2)
        
        if plot_type == "Message Count":
            can_ids = self.stats_data.get('can_id_distribution', {})
            if can_ids:
                ids = list(can_ids.keys())[:20]  # Limit to top 20
                counts = [can_ids[id] for id in ids]
                ids_str = [str(id) for id in ids]
                ax.bar(ids_str, counts, color=self.colors['accent'])
                ax.set_xlabel("CAN ID")
                ax.set_ylabel("Message Count")
                ax.set_title("Message Count by CAN ID")
                plt.setp(ax.xaxis.get_majorticklabels(), rotation=45, ha='right')
            else:
                ax.text(0.5, 0.5, 'No data available', ha='center', va='center', transform=ax.transAxes)
        
        elif plot_type == "CAN ID Distribution":
            can_ids = self.stats_data.get('can_id_distribution', {})
            if can_ids:
                ids = list(can_ids.keys())
                counts = [can_ids[id] for id in ids]
                ax.pie(counts, labels=[str(id) for id in ids], autopct='%1.1f%%', startangle=90)
                ax.set_title("CAN ID Distribution")
            else:
                ax.text(0.5, 0.5, 'No data available', ha='center', va='center', transform=ax.transAxes)
        
        elif plot_type == "Data Rate":
            # Simple data rate visualization
            total = self.stats_data.get('total_messages', 0)
            decoded = self.stats_data.get('decoded_count', 0)
            errors = self.stats_data.get('error_count', 0)
            
            categories = ['Total', 'Decoded', 'Errors']
            values = [total, decoded, errors]
            ax.bar(categories, values, color=[self.colors['accent'], self.colors['success'], self.colors['error']])
            ax.set_ylabel("Count")
            ax.set_title("Data Processing Summary")

        elif plot_type == "Message Rate":
            t = self._get_timestamp_series(self.decoded_df)
            if t is None:
                ax.text(0.5, 0.5, 'No timestamps available for rate plot.', 
                        ha='center', va='center', transform=ax.transAxes)
                ax.set_title("Message Rate (per second)")
            else:
                t = t.dropna()
                if t.empty:
                    ax.text(0.5, 0.5, 'No valid timestamps available.', 
                            ha='center', va='center', transform=ax.transAxes)
                    ax.set_title("Message Rate (per second)")
                else:
                    rate = t.dt.floor('S').value_counts().sort_index()
                    ax.plot(rate.index, rate.values, color=self.colors['accent'], linewidth=1.5)
                    ax.set_xlabel("Time")
                    ax.set_ylabel("Messages/sec")
                    ax.set_title("Message Rate (per second)")
                    plt.setp(ax.xaxis.get_majorticklabels(), rotation=30, ha='right')
        
        elif plot_type == "Signal Values":
            if self.decoded_df is None or self.decoded_df.empty:
                ax.text(0.5, 0.5, 'No decoded data available.\nDecode a file first.', 
                       ha='center', va='center', transform=ax.transAxes)
                ax.set_title("Signal Values")
            else:
                sig = None
                excluded = {
                    'Date', 'Time', 'Timestamp', 'timestamps',
                    'UnixTime', 'Microseconds', 'ID', 'CAN_ID',
                    'Extended', 'RTR', 'DLC'
                }
                for col in self.decoded_df.columns:
                    if col in excluded or col.startswith('Data'):
                        continue
                    series = pd.to_numeric(self.decoded_df[col], errors='coerce')
                    if series.notna().any():
                        sig = col
                        break

                if not sig:
                    ax.text(0.5, 0.5, 'No numeric signal columns available.',
                            ha='center', va='center', transform=ax.transAxes)
                    ax.set_title("Signal Values")
                else:
                    t = self._get_timestamp_series(self.decoded_df)
                    if t is None:
                        x = list(range(len(self.decoded_df)))
                        x_label = "Sample #"
                    else:
                        x = t
                        x_label = "Time"

                    y_raw = self.decoded_df[sig]
                    y = pd.to_numeric(y_raw, errors='coerce')
                    ax.plot(x, y, color=self.colors['accent'], linewidth=1.2)
                    unit = ''
                    try:
                        unit = self.signal_units.get(sig, '')
                    except Exception:
                        unit = ''
                    ax.set_xlabel(x_label)
                    ax.set_ylabel(f"{sig} {f'({unit})' if unit else ''}")
                    ax.set_title(f"{sig} vs Time")
        
        self.fig.tight_layout()
        self.canvas.draw()
    
    def update_statistics_display(self):
        """Update statistics tab with comprehensive data analysis"""
        if not hasattr(self, 'stats_data') or not self.stats_data:
            return
        
        # Clear existing content
        try:
            for widget in self.stats_frame.winfo_children():
                widget.destroy()
        except:
            pass
        
        # Create scrollable text widget for statistics
        stats_text = scrolledtext.ScrolledText(
            self.stats_frame,
            wrap=tk.WORD,
            bg=self.colors['bg_input'],
            fg=self.colors['text'],
            font=('Segoe UI', 10),
            relief=tk.FLAT,
            borderwidth=1
        )
        stats_text.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)
        
        # Generate statistics text
        stats_content = f"""
DECODING STATISTICS
{'=' * 80}

Total Messages Processed: {self.stats_data.get('total_messages', 0)}
Successfully Decoded: {self.stats_data.get('decoded_count', 0)}
Errors: {self.stats_data.get('error_count', 0)}
Success Rate: {self.stats_data.get('success_rate', 0):.1f}%
Unique CAN IDs: {len(self.stats_data.get('can_id_distribution', {}))}
Unique Signals: {self.stats_data.get('unique_signals', 0)}
Output Rows: {self.stats_data.get('total_rows', 0)} (1:1 with input)

CAN ID Distribution:
{'-' * 80}
"""
        for can_id, count in sorted(self.stats_data.get('can_id_distribution', {}).items()):
            stats_content += f"  {can_id}: {count} messages\n"
        
        stats_text.insert('1.0', stats_content)
        stats_text.config(state='disabled')
    
    def export_decoded_data(self):
        """Export decoded data to various formats"""
        if self.decoded_df is None or self.decoded_df.empty:
            messagebox.showwarning("Warning", "No decoded data to export. Please decode messages first.")
            return

        try:
            if pd is None:
                messagebox.showerror("Error", "pandas is required for exporting data.")
                return
            # Get export format from user
            export_format = self.export_format_var.get()
            
            # Helper: dataframe used for exports (optionally with units row for text formats)
            export_df = self.decoded_df.copy()

            # Excel cannot handle certain control characters in cell values.
            illegal_re = re.compile(r"[\x00-\x08\x0B\x0C\x0E-\x1F]")
            def _sanitize_excel_value(value):
                if isinstance(value, str):
                    return illegal_re.sub("", value)
                return value
            def _sanitize_excel_df(df):
                cleaned = df.copy()
                for col in cleaned.columns:
                    if cleaned[col].dtype == object:
                        cleaned[col] = cleaned[col].map(_sanitize_excel_value)
                return cleaned

            if export_format == "CSV":
                filename = filedialog.asksaveasfilename(
                    title="Save Decoded Data as CSV",
                    defaultextension=".csv",
                    filetypes=[("CSV files", "*.csv"), ("All files", "*.*")]
                )
                if filename:
                    # Write header + units row (like `23-01.csv`) + data rows
                    with open(filename, 'w', newline='', encoding='utf-8') as f:
                        writer = csv.writer(f)
                        writer.writerow(list(export_df.columns))
                        units = getattr(self, 'decoded_units_row', None)
                        if isinstance(units, dict):
                            writer.writerow([units.get(c, '') for c in export_df.columns])
                        for _, r in export_df.iterrows():
                            writer.writerow([r.get(c, '') for c in export_df.columns])
                    messagebox.showinfo("Success", f"Data exported to {filename}")
                    self.update_status(f"Exported to {filename}")
            elif export_format == "XLSX":
                try:
                    filename = filedialog.asksaveasfilename(
                        title="Save Decoded Data as Excel (.xlsx)",
                        defaultextension=".xlsx",
                        filetypes=[("Excel files", "*.xlsx"), ("All files", "*.*")]
                    )
                    if filename:
                        with pd.ExcelWriter(filename, engine='openpyxl') as writer:
                            safe_df = _sanitize_excel_df(export_df)
                            base_df = self.raw_df.copy() if getattr(self, 'raw_df', None) is not None else export_df
                            safe_base = _sanitize_excel_df(base_df)
                            units = getattr(self, 'decoded_units_row', None)
                            safe_units = None
                            if isinstance(units, dict):
                                safe_units = {k: _sanitize_excel_value(v) for k, v in units.items()}
            
                            def _safe_sheet_name(name, used):
                                base = re.sub(r"[\\/?*\[\]:]", "_", str(name)).strip()
                                if not base:
                                    base = "Unknown"
                                base = base[:31]
                                candidate = base
                                i = 2
                                while candidate in used:
                                    suffix = f"_{i}"
                                    candidate = base[:31 - len(suffix)] + suffix
                                    i += 1
                                used.add(candidate)
                                return candidate
            
                            used_names = set()
                            if "CAN_ID" in safe_base.columns:
                                id_series = safe_base["CAN_ID"].fillna("").astype(str)
                                for can_id in id_series.unique():
                                    if can_id == "":
                                        can_id = "Unknown"
                                    df_id = safe_base[id_series == can_id]
                                    cols_keep = []
                                    for col in df_id.columns:
                                        if col in ("Date", "Time", "CAN_ID"):
                                            cols_keep.append(col)
                                            continue
                                        series = df_id[col]
                                        if series.replace("", pd.NA).isna().all():
                                            continue
                                        cols_keep.append(col)
                                    df_id = df_id[cols_keep]
                                    sheet_name = _safe_sheet_name(can_id, used_names)
                                    df_id.to_excel(writer, index=False, sheet_name=sheet_name)
                            else:
                                data_name = _safe_sheet_name("data", used_names)
                                safe_base.to_excel(writer, index=False, sheet_name=data_name)
            
                            # Combined sheet with all IDs
                            all_name = _safe_sheet_name("ALL_IDS", used_names)
                            safe_df.to_excel(writer, index=False, sheet_name=all_name)
            
                            if safe_units is not None:
                                units_name = _safe_sheet_name("units", used_names)
                                pd.DataFrame([safe_units]).to_excel(writer, index=False, sheet_name=units_name)
                        messagebox.showinfo("Success", f"Data exported to {filename}")
                        self.update_status(f"Exported to {filename}")
                except ImportError:
                    messagebox.showerror("Error", "XLSX export requires openpyxl. Install with: pip install openpyxl")


            
            elif export_format == "MAT":
                try:
                    import scipy.io
                    filename = filedialog.asksaveasfilename(
                        title="Save Decoded Data as MATLAB (.mat)",
                        defaultextension=".mat",
                        filetypes=[("MATLAB files", "*.mat"), ("All files", "*.*")]
                    )
                    if filename:
                        # Convert DataFrame to dictionary for MATLAB
                        mat_data = {}
                        for col in export_df.columns:
                            mat_data[col] = export_df[col].values
                        scipy.io.savemat(filename, mat_data)
                        messagebox.showinfo("Success", f"Data exported to {filename}")
                        self.update_status(f"Exported to {filename}")
                except ImportError:
                    messagebox.showerror("Error", "scipy library not installed. Please install it using: pip install scipy")
            
            elif export_format == "SQLITE":
                try:
                    import sqlite3
                    filename = filedialog.asksaveasfilename(
                        title="Save Decoded Data as SQLite Database",
                        defaultextension=".db",
                        filetypes=[("SQLite files", "*.db"), ("All files", "*.*")]
                    )
                    if filename:
                        conn = sqlite3.connect(filename)
                        export_df.to_sql('decoded_data', conn, if_exists='replace', index=False)
                        conn.close()
                        messagebox.showinfo("Success", f"Data exported to {filename}")
                        self.update_status(f"Exported to {filename}")
                except Exception as e:
                    messagebox.showerror("Error", f"Failed to export to SQLite: {str(e)}")
            elif export_format in ("MF4", "MDF"):
                try:
                    from asammdf import MDF, Signal  # type: ignore
                    if not NUMPY_AVAILABLE:
                        raise ImportError("numpy not installed")
                    filename = filedialog.asksaveasfilename(
                        title=f"Save Decoded Data as {export_format}",
                        defaultextension=(".mf4" if export_format == "MF4" else ".mdf"),
                        filetypes=[("MDF files", "*.mf4;*.mdf"), ("All files", "*.*")]
                    )
                    if filename:
                        export_df_ts = self.decoded_df.copy()
                        # Ensure numeric UnixTime/Microseconds if present
                        if "UnixTime" in export_df_ts.columns:
                            export_df_ts["UnixTime"] = pd.to_numeric(export_df_ts["UnixTime"], errors="coerce").fillna(0)
                        if "Microseconds" in export_df_ts.columns:
                            export_df_ts["Microseconds"] = pd.to_numeric(export_df_ts["Microseconds"], errors="coerce").fillna(0)

                        def build_time_seconds(df):
                            # Prefer UnixTime + Microseconds when available (as per MDF corrections)
                            if "UnixTime" in df.columns:
                                unix = pd.to_numeric(df["UnixTime"], errors="coerce").fillna(0)
                                if "Microseconds" in df.columns:
                                    micro = pd.to_numeric(df["Microseconds"], errors="coerce").fillna(0)
                                else:
                                    micro = pd.Series(0, index=df.index)
                                if (unix != 0).any() or (micro != 0).any():
                                    unix = unix.fillna(method="ffill").fillna(0).astype(float)
                                    micro = micro.fillna(0).astype(float)
                                    return (unix + (micro / 1e6)).to_numpy()
                            # Fallback to timestamps/Date/Time columns
                            if "timestamps" in df.columns:
                                t = pd.to_datetime(df["timestamps"], errors="coerce")
                            elif "Date" in df.columns and "Time" in df.columns:
                                t = pd.to_datetime(
                                    df["Date"].astype(str) + " " + df["Time"].astype(str),
                                    errors="coerce",
                                )
                            elif "Timestamp" in df.columns:
                                t = pd.to_datetime(df["Timestamp"], errors="coerce")
                            else:
                                t = None

                            if t is None or t.isna().all():
                                return (pd.Series(range(len(df))) * 0.0).to_numpy()
                            t = t.fillna(method="ffill")
                            t0 = t.dropna().iloc[0] if t.notna().any() else pd.Timestamp.utcnow()
                            return (t - t0).dt.total_seconds().fillna(0).to_numpy()

                        time_s = build_time_seconds(export_df_ts)

                        def ensure_monotonic_timestamps(ts):
                            ts = np.asarray(ts, dtype=float)
                            if ts.size == 0:
                                return ts
                            # Replace non-finite values
                            for i in range(len(ts)):
                                if not np.isfinite(ts[i]):
                                    ts[i] = ts[i - 1] if i > 0 else 0.0
                            for i in range(1, len(ts)):
                                if ts[i] <= ts[i - 1]:
                                    ts[i] = ts[i - 1] + 1e-6
                            return ts

                        mdf = MDF()

                        if "CAN_ID" in export_df_ts.columns:
                            for can_id, id_df in export_df_ts.groupby("CAN_ID"):
                                if id_df.empty:
                                    continue
                                idx = id_df.index.to_numpy()
                                id_ts = time_s[idx]
                                order = id_ts.argsort(kind="mergesort")
                                id_ts = id_ts[order]
                                id_ts = ensure_monotonic_timestamps(id_ts)
                                id_df = id_df.iloc[order]
            
                                # Normalize CAN ID value
                                can_id_str = str(can_id)
                                try:
                                    if can_id_str.lower().startswith("0x"):
                                        can_id_int = int(can_id_str, 16)
                                    else:
                                        can_id_int = int(float(can_id_str))
                                except Exception:
                                    can_id_int = -1
            
                                channels = []
                                channels.append(Signal(
                                    samples=np.full(len(id_df), can_id_int),
                                    timestamps=id_ts,
                                    name="CAN_ID",
                                    unit="",
                                    comment=f"CAN Message ID {can_id_str}",
                                ))
            
                                for col in id_df.columns:
                                    if col in ("Date", "Time", "CAN_ID", "Timestamp", "timestamps", "UnixTime", "Microseconds"):
                                        continue
                                    vals = pd.to_numeric(id_df[col], errors="coerce")
                                    if vals.isna().all():
                                        continue
                                    unit = ""
                                    try:
                                        unit = self.signal_units.get(col, "")
                                    except Exception:
                                        unit = ""
                                    channels.append(Signal(
                                        samples=vals.fillna(method="ffill").fillna(0).to_numpy(),
                                        timestamps=id_ts,
                                        name=str(col),
                                        unit=str(unit) if unit else "",
                                        comment=str(unit) if unit else "",
                                    ))
            
                                if channels:
                                    mdf.append(channels, common_timebase=True)
                        else:
                            # Fallback: single group
                            id_ts = ensure_monotonic_timestamps(time_s)
                            channels = []
                            for col in export_df_ts.columns:
                                if col in ("Date", "Time", "CAN_ID", "Timestamp", "timestamps", "UnixTime", "Microseconds"):
                                    continue
                                vals = pd.to_numeric(export_df_ts[col], errors="coerce")
                                if vals.isna().all():
                                    continue
                                unit = ""
                                try:
                                    unit = self.signal_units.get(col, "")
                                except Exception:
                                    unit = ""
                                channels.append(Signal(
                                    samples=vals.fillna(method="ffill").fillna(0).to_numpy(),
                                    timestamps=id_ts,
                                    name=str(col),
                                    unit=str(unit) if unit else "",
                                    comment=str(unit) if unit else "",
                                ))
                            if channels:
                                mdf.append(channels, common_timebase=True)

                        mdf.save(filename, overwrite=True)
                        messagebox.showinfo("Success", f"Data exported to {filename}")
                        self.update_status(f"Exported to {filename}")
                except ImportError:
                    messagebox.showerror("Error", "MDF/MF4 export requires asammdf and numpy.\nInstall with: pip install asammdf numpy")

            elif export_format == "JSON":

                filename = filedialog.asksaveasfilename(
                    title="Save Decoded Data as JSON",
                    defaultextension=".json",
                    filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
                )
                if filename:
                    export_df.to_json(filename, orient='records', indent=2)
                    messagebox.showinfo("Success", f"Data exported to {filename}")
                    self.update_status(f"Exported to {filename}")

            elif export_format == "TXT":
                filename = filedialog.asksaveasfilename(
                    title="Save Decoded Data as TXT (tab-separated)",
                    defaultextension=".txt",
                    filetypes=[("Text files", "*.txt"), ("All files", "*.*")]
                )
                if filename:
                    # include units row
                    units = getattr(self, 'decoded_units_row', None)
                    out_df = export_df.copy()
                    if isinstance(units, dict):
                        out_df = pd.concat([pd.DataFrame([units]), out_df], ignore_index=True)
                    out_df.to_csv(filename, sep='\t', index=False)
                    messagebox.showinfo("Success", f"Data exported to {filename}")
                    self.update_status(f"Exported to {filename}")

            elif export_format == "HDF5":
                filename = filedialog.asksaveasfilename(
                    title="Save Decoded Data as HDF5",
                    defaultextension=".h5",
                    filetypes=[("HDF5 files", "*.h5;*.hdf5"), ("All files", "*.*")]
                )
                if filename:
                    try:
                        import tables  # noqa: F401
                        export_df.to_hdf(filename, key='decoded_data', mode='w')
                        messagebox.showinfo("Success", f"Data exported to {filename}")
                        self.update_status(f"Exported to {filename}")
                    except Exception as e:
                        messagebox.showerror("Error", f"HDF5 export requires pytables. Install with: pip install tables\n\nDetails: {e}")

            elif export_format == "PARQUET":
                filename = filedialog.asksaveasfilename(
                    title="Save Decoded Data as Parquet",
                    defaultextension=".parquet",
                    filetypes=[("Parquet files", "*.parquet"), ("All files", "*.*")]
                )
                if filename:
                    try:
                        engine = None
                        try:
                            import pyarrow  # noqa: F401
                            engine = "pyarrow"
                        except Exception:
                            try:
                                import fastparquet  # noqa: F401
                                engine = "fastparquet"
                            except Exception:
                                engine = None
                        if engine is None:
                            raise ImportError("pyarrow or fastparquet not installed")
                        export_df.to_parquet(filename, index=False, engine=engine)
                        messagebox.showinfo("Success", f"Data exported to {filename}")
                        self.update_status(f"Exported to {filename}")
                    except Exception as e:
                        messagebox.showerror("Error", f"Parquet export requires pyarrow or fastparquet.\nInstall with: pip install pyarrow\n\nDetails: {e}")

            elif export_format == "PROMETHEUS":
                filename = filedialog.asksaveasfilename(
                    title="Save Decoded Data as Prometheus exposition format",
                    defaultextension=".prom",
                    filetypes=[("Prometheus files", "*.prom;*.txt"), ("All files", "*.*")]
                )
                if filename:
                    # Write a simple exposition file: one sample per row per numeric column.
                    # NOTE: can be large; intended for smaller datasets.
                    export_df_ts = self._ensure_timestamps_column(export_df.copy())
                    ts_series = None
                    if 'timestamps' in export_df_ts.columns:
                        ts_series = pd.to_datetime(export_df_ts['timestamps'], errors='coerce')
                    with open(filename, 'w', encoding='utf-8') as f:
                        for col in export_df_ts.columns:
                            if col in ('timestamps',) or col.startswith('Data') or col.startswith('ID'):
                                continue
                            vals = pd.to_numeric(export_df_ts[col], errors='coerce')
                            if vals.isna().all():
                                continue
                            metric = "can_signal"
                            f.write(f"# HELP {metric} Decoded CAN signal values\n")
                            f.write(f"# TYPE {metric} gauge\n")
                            for i, v in enumerate(vals):
                                if pd.isna(v):
                                    continue
                                # Prometheus timestamps are in milliseconds
                                if ts_series is not None and not ts_series.isna().all():
                                    tsv = ts_series.iloc[i]
                                    if pd.isna(tsv):
                                        t_ms = i
                                    else:
                                        t_ms = int(tsv.value // 1_000_000)
                                else:
                                    t_ms = i
                                f.write(f'{metric}{{signal="{col}"}} {float(v)} {t_ms}\n')
                    messagebox.showinfo("Success", f"Data exported to {filename}")
                    self.update_status(f"Exported to {filename}")
            
            self.append_output(f"\nExport completed: {export_format} format")
            self.append_output(f"  Rows: {len(self.decoded_df)}")
            self.append_output(f"  Columns: {len(self.decoded_df.columns)}")
            
        except Exception as e:
            import traceback
            error_msg = f"Export failed: {str(e)}\n\nPlease ensure required packages are installed."
            messagebox.showerror("Error", error_msg)
            print(f"Export error: {traceback.format_exc()}")


def main():
    root = tk.Tk()
    app = DBCDecoderGUI(root)
    root.mainloop()


if __name__ == '__main__':
    main()
