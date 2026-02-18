#!/usr/bin/env python3
"""
Plotly Dash implementation of the real-time CAN dashboard.

The app connects to the ESP32 logger over WiFi, pulls JSON frames from the /live
endpoint, decodes them with a user-supplied DBC file, and displays the most
recent signal values in a fixed layout organized by CAN message.
"""

import base64
import os
import re
import uuid
from typing import Dict, List

import cantools
import pandas as pd
import requests
from dash import (
    Dash,
    Input,
    Output,
    State,
    dash_table,
    dcc,
    html,
    ctx,
    no_update,
)
from dash.dependencies import MATCH, ALL
import dash_bootstrap_components as dbc
import plotly.graph_objects as go
from flask import Response

# Optional Prometheus support
try:
    from prometheus_client import (
        CollectorRegistry,
        Gauge,
        CONTENT_TYPE_LATEST,
        generate_latest,
    )
    PROMETHEUS_AVAILABLE = True
except ImportError:
    PROMETHEUS_AVAILABLE = False
    # Create dummy classes if prometheus_client is not available
    class CollectorRegistry:
        pass
    class Gauge:
        def __init__(self, *args, **kwargs):
            pass
        def set(self, *args, **kwargs):
            pass
    CONTENT_TYPE_LATEST = "text/plain"
    def generate_latest(*args, **kwargs):
        return b"# Prometheus client not installed\n"


# Match ESP32 AP configuration in CAN_Data_Logger_Only.ino
def _load_default_base_url():
    default_url = "http://192.168.10.1"
    try:
        base_dir = os.path.dirname(os.path.abspath(__file__))
        ino_path = os.path.join(
            base_dir, "CAN_Data_Logger_Only", "CAN_Data_Logger_Only.ino"
        )
        if not os.path.exists(ino_path):
            return default_url
        with open(ino_path, "r", encoding="utf-8", errors="ignore") as f:
            text = f.read()
        match = re.search(
            r"WIFI_AP_IP\\s+IPAddress\\((\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\s*,\\s*(\\d+)\\)",
            text,
        )
        if match:
            ip = ".".join(match.groups())
            return f"http://{ip}"
    except Exception:
        pass
    return default_url


def _get_env_int(name: str, default: int) -> int:
    try:
        return int(os.getenv(name, str(default)))
    except Exception:
        return default


def _get_env_float(name: str, default: float) -> float:
    try:
        return float(os.getenv(name, str(default)))
    except Exception:
        return default


def _normalize_base_url(base_url: str) -> str:
    if not base_url:
        return DEFAULT_BASE_URL
    if not re.match(r"^https?://", base_url, re.IGNORECASE):
        return f"http://{base_url}"
    return base_url


DEFAULT_BASE_URL = _load_default_base_url()
LIVE_FETCH_LIMIT = _get_env_int("LIVE_FETCH_LIMIT", 50)
MAX_HISTORY_ROWS = _get_env_int("LIVE_MAX_HISTORY_ROWS", 2000)
if MAX_HISTORY_ROWS <= 0:
    MAX_HISTORY_ROWS = None
BUS_CURRENT_TRIGGER = _get_env_float("BUS_CURRENT_TRIGGER", 80.0)
BUS_CURRENT_MIN = _get_env_float("BUS_CURRENT_MIN", -100.0)
BUS_CURRENT_MAX = _get_env_float("BUS_CURRENT_MAX", 120.0)
LIVE_POLL_TIMEOUT = _get_env_float("LIVE_POLL_TIMEOUT", 3.0)
GRAPH_MAX_POINTS = _get_env_int("GRAPH_MAX_POINTS", 600)
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DBC_DIR = os.path.join(BASE_DIR, "DBC_Dump")

ENABLE_PROM_METRICS = os.getenv("ENABLE_PROM_METRICS", "true").lower() == "true" and PROMETHEUS_AVAILABLE

# Prometheus registry and gauges (initialized lazily)
metrics_registry = None
signal_value_gauge = None
poll_status_gauge = None
last_seq_gauge = None


def init_metrics():
    """
    Create Prometheus gauges once to avoid duplicate registration on reloads.
    """
    global metrics_registry, signal_value_gauge, poll_status_gauge, last_seq_gauge
    if not ENABLE_PROM_METRICS or metrics_registry is not None:
        return
    
    if not PROMETHEUS_AVAILABLE:
        print("Warning: prometheus_client not available. Prometheus metrics disabled.")
        return

    metrics_registry = CollectorRegistry()
    signal_value_gauge = Gauge(
        "can_signal_value",
        "Latest decoded CAN signal value",
        ["signal", "message", "can_id", "unit"],
        registry=metrics_registry,
    )
    poll_status_gauge = Gauge(
        "can_poll_status",
        "Poll status (1=ok, 0=error)",
        registry=metrics_registry,
    )
    last_seq_gauge = Gauge(
        "can_last_sequence",
        "Latest sequence counter from ESP logger",
        registry=metrics_registry,
    )

dbc_cache: Dict[str, cantools.database.can.Database] = {}
last_bus_current_by_dbc: Dict[str, float] = {}
http_session = requests.Session()
http_session.headers.update({"Connection": "keep-alive"})


def _normalize_signal_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", str(name).lower())


def _extract_raw_signal(data_bytes: bytes, start_bit: int, length: int, byte_order: str) -> int:
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
        for i in range(length):
            bit_index = start_bit - i
            byte_index = bit_index // 8
            bit_in_byte = bit_index % 8
            if byte_index < 0 or byte_index >= len(data_bytes):
                break
            bit = (data_bytes[byte_index] >> bit_in_byte) & 0x1
            raw = (raw << 1) | bit
    return raw


def list_dbc_files() -> List[Dict[str, str]]:
    try:
        if not os.path.isdir(DBC_DIR):
            return []
        names = [f for f in os.listdir(DBC_DIR) if f.lower().endswith(".dbc")]
        names.sort(key=str.lower)
        return [{"label": name, "value": os.path.join(DBC_DIR, name)} for name in names]
    except Exception:
        return []


DBC_FILE_OPTIONS = list_dbc_files()


def parse_dbc_upload(contents: str, filename: str) -> Dict:
    """
    Decode an uploaded DBC file, load it with cantools, and capture the layout.
    """
    _, encoded = contents.split(",", 1)
    decoded_bytes = base64.b64decode(encoded)
    db = cantools.database.load_string(decoded_bytes.decode("latin-1"))

    layout = []
    for message in db.messages:
        layout.append(
            {
                "message": message.name,
                "signals": [
                    {"name": signal.name, "unit": signal.unit or ""}
                    for signal in message.signals
                ],
            }
        )

    dbc_id = str(uuid.uuid4())
    dbc_cache[dbc_id] = db
    return {"id": dbc_id, "name": filename, "layout": layout}


def parse_dbc_file(path: str) -> Dict:
    """
    Decode a DBC file from disk (from DBC_Dump) and capture the layout.
    """
    db = cantools.database.load_file(path)

    layout = []
    for message in db.messages:
        layout.append(
            {
                "message": message.name,
                "signals": [
                    {"name": signal.name, "unit": signal.unit or ""}
                    for signal in message.signals
                ],
            }
        )

    dbc_id = str(uuid.uuid4())
    dbc_cache[dbc_id] = db
    return {"id": dbc_id, "name": os.path.basename(path), "layout": layout}


def fetch_live_frames(base_url: str, since: int, limit: int = LIVE_FETCH_LIMIT) -> Dict:
    url = _normalize_base_url(base_url).rstrip("/") + "/live"
    resp = http_session.get(
        url,
        params={"since": since, "limit": limit},
        timeout=LIVE_POLL_TIMEOUT,
    )
    resp.raise_for_status()
    return resp.json()


def _safe_int(value, default: int = 0) -> int:
    try:
        return int(value)
    except Exception:
        return default


def _filter_new_frames(frames: List[Dict], last_seq: int) -> List[Dict]:
    if not frames:
        return []
    if last_seq is None:
        return frames
    filtered = []
    for frame in frames:
        seq = _safe_int(frame.get("seq", -1), -1)
        if seq <= last_seq:
            continue
        filtered.append(frame)
    return filtered


def _apply_bus_current_correction(
    dbc_id: str,
    message: cantools.database.can.Message,
    data_bytes: bytes,
    decoded: Dict,
) -> Dict:
    bus_key = None
    for key in decoded.keys():
        if _normalize_signal_name(key) == "buscurrent":
            bus_key = key
            break
    if not bus_key:
        return decoded

    try:
        bus_float = float(decoded.get(bus_key))
    except Exception:
        return decoded

    if abs(bus_float) <= BUS_CURRENT_TRIGGER:
        last_bus_current_by_dbc[dbc_id] = bus_float
        return decoded

    try:
        raw_map = message.decode(data_bytes, decode_choices=False, scaling=False)
    except Exception:
        raw_map = {}

    raw_bus = None
    bus_sig = None
    try:
        for s in message.signals:
            if _normalize_signal_name(s.name) == "buscurrent":
                bus_sig = s
                break
        for k, v in raw_map.items():
            if _normalize_signal_name(k) == "buscurrent":
                raw_bus = v
                break
    except Exception:
        raw_bus = None
        bus_sig = None

    if raw_bus is None or bus_sig is None:
        return decoded

    try:
        scale = float(getattr(bus_sig, "scale", 1.0))
    except Exception:
        scale = 1.0
    try:
        offset = float(getattr(bus_sig, "offset", 0.0))
    except Exception:
        offset = 0.0
    try:
        length = int(getattr(bus_sig, "length", 16))
    except Exception:
        length = 16
    try:
        start_bit = int(getattr(bus_sig, "start", 0))
    except Exception:
        start_bit = 0
    try:
        is_signed = bool(getattr(bus_sig, "is_signed", False))
    except Exception:
        is_signed = False

    candidates = []

    def _add_candidates(raw_val: int) -> None:
        candidates.append(raw_val * scale + offset)
        candidates.append(raw_val * scale)

    try:
        _add_candidates(int(raw_bus))
    except Exception:
        pass

    if length == 16:
        try:
            swapped = ((int(raw_bus) & 0xFF) << 8) | ((int(raw_bus) >> 8) & 0xFF)
            _add_candidates(swapped)
        except Exception:
            pass

    raw_le = _extract_raw_signal(data_bytes, start_bit, length, "little_endian")
    raw_be = _extract_raw_signal(data_bytes, start_bit, length, "big_endian")
    if is_signed:
        sign_mask = 1 << (length - 1)
        if raw_le & sign_mask:
            raw_le -= (1 << length)
        if raw_be & sign_mask:
            raw_be -= (1 << length)
    _add_candidates(raw_le)
    _add_candidates(raw_be)

    try:
        byte_index = start_bit // 8
        if byte_index + 1 < len(data_bytes):
            word_be = (data_bytes[byte_index] << 8) | data_bytes[byte_index + 1]
            word_le = data_bytes[byte_index] | (data_bytes[byte_index + 1] << 8)
            _add_candidates(word_be)
            _add_candidates(word_le)
    except Exception:
        pass

    valid = [v for v in candidates if BUS_CURRENT_MIN <= v <= BUS_CURRENT_MAX]
    if valid:
        last_val = last_bus_current_by_dbc.get(dbc_id)
        if last_val is not None:
            best = min(valid, key=lambda v: abs(v - last_val))
        else:
            best = min(valid, key=lambda v: abs(v))
        decoded[bus_key] = best
        last_bus_current_by_dbc[dbc_id] = best
    else:
        last_bus_current_by_dbc[dbc_id] = bus_float

    return decoded


def decode_frames(dbc_id: str, frames: List[Dict]) -> List[Dict]:
    """
    Return a list of decoded signal rows from the raw frame payload.
    """
    db = dbc_cache.get(dbc_id)
    if not db:
        raise RuntimeError("Loaded DBC not found. Please select again.")

    rows = []
    for frame in frames:
        try:
            frame_id = int(frame.get("id", "0"), 16)
            data_bytes = bytes(int(byte, 16) for byte in frame.get("data", []))
        except (ValueError, TypeError):
            continue

        try:
            message = db.get_message_by_frame_id(frame_id)
            decoded = message.decode(data_bytes)
            decoded = _apply_bus_current_correction(
                dbc_id,
                message,
                data_bytes,
                decoded,
            )
        except Exception:
            continue

        timestamp = frame.get("time", "")
        unix_time = frame.get("unix", 0)
        seq = frame.get("seq", 0)
        can_hex = f"0x{frame_id:03X}"
        for signal_name, value in decoded.items():
            signal = message.get_signal_by_name(signal_name)
            rows.append(
                {
                    "Seq": seq,
                    "Time": timestamp,
                    "Unix": unix_time,
                    "CAN_ID": can_hex,
                    "Message": message.name,
                    "Signal": signal_name,
                    "Value": value,
                    "Unit": signal.unit or "",
                }
            )
    return rows


def limit_history(history: List[Dict], new_rows: List[Dict]) -> List[Dict]:
    """
    Append new rows to the end of history in chronological order
    and (optionally) enforce a maximum length.
    """

    if not new_rows:
        return history

    history = history or []

    # --- NEW: make sure the batch itself is in time order ---
    # Prefer Unix (numeric) if present, otherwise fall back to Seq
    if isinstance(new_rows, list) and new_rows:
        if "Unix" in new_rows[0]:
            new_rows = sorted(new_rows, key=lambda r: r.get("Unix", 0))
        elif "Seq" in new_rows[0]:
            new_rows = sorted(new_rows, key=lambda r: r.get("Seq", 0))
    # ---------------------------------------------------------

    updated = history + new_rows

    if MAX_HISTORY_ROWS is not None and len(updated) > MAX_HISTORY_ROWS:
        updated = updated[-MAX_HISTORY_ROWS:]

    return updated



def build_message_cards(
    layout: List[Dict], latest: Dict[str, Dict], theme: str = "light"
) -> List[dbc.Row]:
    """
    Assemble the colored cards grouped by message.
    """

    if not layout:
        return dbc.Alert(
            "Select a DBC file to see live metrics.",
            color="info",
            className="mt-3",
        )

    accent_colors = ["#F97316", "#0EA5E9", "#10B981", "#F43F5E", "#A855F7", "#EAB308"]

    if theme == "dark":
        card_bg = "#0B1220"
        label_color = "#94A3B8"
        title_color = "#E2E8F0"
        value_color = "#F8FAFC"
        timestamp_color = "#64748B"
        row_bg = "rgba(148,163,184,0.08)"
    else:
        card_bg = "#FFFFFF"
        label_color = "#64748B"
        title_color = "#0F172A"
        value_color = "#0F172A"
        timestamp_color = "#94A3B8"
        row_bg = "rgba(14,116,144,0.08)"

    cards = []
    for idx, entry in enumerate(sorted(layout, key=lambda e: e.get("message", ""))):
        message_name = entry.get("message", "")
        signals = sorted(entry.get("signals", []), key=lambda s: s.get("name", ""))
        border_color = accent_colors[idx % len(accent_colors)]

        signal_rows = []
        for signal in signals:
            latest_data = latest.get(signal["name"])
            if latest_data:
                value = latest_data["Value"]
                value_str = f"{value:.3f}" if isinstance(value, float) else str(value)
                timestamp = latest_data.get("Time", "")
            else:
                value_str = "--"
                timestamp = ""

            unit = signal["unit"]
            label_text = f"{signal['name']}{f' ({unit})' if unit else ''}"

            signal_rows.append(
                html.Div(
                    [
                        html.Div(
                            [
                                html.Span(
                                    label_text,
                                    style={
                                        "textTransform": "uppercase",
                                        "fontSize": "14px",
                                        "fontWeight": "600",
                                        "color": label_color,
                                    },
                                ),
                                html.Span(
                                    value_str,
                                    style={
                                        "marginLeft": "auto",
                                        "fontSize": "20px",
                                        "fontWeight": "700",
                                        "color": value_color,
                                    },
                                ),
                            ],
                            className="d-flex justify-content-between align-items-center",
                        ),
                        html.Div(
                            timestamp,
                            style={
                                "fontSize": "11px",
                                "color": timestamp_color,
                                "marginTop": "1px",
                            },
                        ),
                    ],
                    className="px-2 py-2 mb-2 rounded-3",
                    style={"backgroundColor": row_bg},
                )
            )

        cards.append(
            dbc.Card(
                [
                    html.Div(
                        message_name,
                        style={
                            "fontWeight": "700",
                            "textTransform": "uppercase",
                            "marginBottom": "6px",
                            "fontSize": "24px",
                            "color": title_color,
                        },
                    ),
                    html.Div(signal_rows),
                ],
                className="shadow-sm message-card",
                style={
                    "borderLeft": f"6px solid {border_color}",
                    "borderRadius": "18px",
                    "backgroundColor": card_bg,
                    "boxShadow": "0 14px 34px rgba(15, 23, 42, 0.12)",
                },
            )
        )

    return html.Div(cards, className="message-grid")


def build_status_cards(
    connected: bool,
    seq: int,
    signal_count: int,
    theme: str = "light",
) -> dbc.Row:
    if theme == "dark":
        card_bg = "#0F172A"
        title_color = "#94A3B8"
        value_color = "#F8FAFC"
        border_color = "rgba(148,163,184,0.18)"
    else:
        card_bg = "#FFFFFF"
        title_color = "#475569"
        value_color = "#0F172A"
        border_color = "rgba(15,23,42,0.08)"

    def status_card(label: str, value: str) -> dbc.Col:
        return dbc.Col(
            dbc.Card(
                [
                    html.Div(
                        label,
                        style={
                            "textTransform": "uppercase",
                            "fontSize": "11px",
                            "color": title_color,
                        },
                    ),
                    html.Div(
                        value,
                        style={"fontSize": "24px", "fontWeight": "700", "color": value_color},
                    ),
                ],
                className="p-3 shadow-sm",
                style={
                    "backgroundColor": card_bg,
                    "borderRadius": "16px",
                    "border": f"1px solid {border_color}",
                },
            ),
            md=4,
        )

    cards = [
        status_card("Connection", "Live" if connected else "Waiting"),
        status_card("Latest Sequence", str(seq)),
        status_card("Signals Tracked", str(signal_count)),
    ]
    return dbc.Row(cards, className="g-3")



def make_status_row():
    return html.Div(
        [
            dbc.Card(
                [
                    html.Div("System Status", className="panel-title"),
                    html.Div(id="status-cards"),
                ],
                className="panel-card",
            ),
        ]
    )


external_stylesheets = [dbc.themes.BOOTSTRAP]
app = Dash(
    __name__,
    external_stylesheets=external_stylesheets,
    suppress_callback_exceptions=True,
    title="CAN Live Dashboard",
)
server = app.server

# Initialize Prometheus metrics (safe to call even if disabled)
init_metrics()

if ENABLE_PROM_METRICS:

    @server.route("/metrics")
    def metrics():
        # Return Prometheus exposition format
        return Response(
            generate_latest(metrics_registry),
            mimetype=CONTENT_TYPE_LATEST,
        )


app.layout = html.Div(
    id="page-wrapper",
    children=dbc.Container(
        [
            dcc.Store(id="dbc-store"),
            dcc.Store(id="latest-store", data={}),
            dcc.Store(id="history-store", data=[]),
            dcc.Store(id="seq-store", data=0),
            dcc.Store(id="poll-store", data={"ok": False, "last_seq": 0, "frames": 0, "decoded": 0}),
            dcc.Store(id="boot-store", data=None),
            dcc.Store(id="graph-list-store", data=[]),
            dcc.Store(id="theme-store", data="dark"),
            dcc.Store(id="live-enabled", data=False),

            html.Div(
                [
                    html.Div(
                        [
                            html.Img(
                                src="/assets/naxatra_labs_logo.png",
                                className="hero-logo",
                            ),
                            html.Div(
                                [
                                    html.Div("Naxatra Labs CAN Live Console", id="main-title"),
                                    html.Div(
                                        "Continuous decoding and signal monitoring.",
                                        id="main-subtitle",
                                    ),
                                ],
                                className="hero-text",
                            ),
                        ],
                        className="hero-left",
                    ),
                    html.Div(
                        [
                            dbc.Switch(
                                id="theme-toggle",
                                label="Night mode",
                                value=True,
                                className="mb-2",
                                label_style={"color": "#E2E8F0"},
                            ),
                            html.Div(
                                "Live stream paused",
                                id="session-status",
                                className="session-pill session-pill--paused",
                            ),
                        ],
                        className="hero-actions",
                    ),
                ],
                className="hero-wrap",
            ),

            dbc.Row(
                [
                    dbc.Col(
                        dbc.Card(
                            [
                                html.Div("Connection Dock", className="panel-title"),
                                dbc.Label("Logger Address", id="logger-label"),
                                dcc.Input(
                                    id="base-url",
                                    type="text",
                                    value=DEFAULT_BASE_URL,
                                    debounce=True,
                                    className="form-control",
                                ),
                                html.Div(
                                    f"Default ESP32 AP URL is {DEFAULT_BASE_URL}",
                                    className="small mt-1",
                                    id="logger-hint",
                                ),
                                html.Hr(),
                                dbc.Label("DBC File", id="upload-label"),
                                dcc.Dropdown(
                                    id="dbc-dropdown",
                                    options=DBC_FILE_OPTIONS,
                                    value=DBC_FILE_OPTIONS[0]["value"] if DBC_FILE_OPTIONS else None,
                                    placeholder="Select a DBC file from DBC_Dump",
                                    clearable=False,
                                    className="dbc-dropdown",
                                    disabled=not bool(DBC_FILE_OPTIONS),
                                ),
                                html.Div(
                                    "DBC folder: DBC_Dump",
                                    className="small mt-1",
                                    style={"color": "#94A3B8"},
                                ),
                                html.Div(
                                    id="upload-status",
                                    className="mt-2",
                                    style={"color": "#10B981"},
                                    children="" if DBC_FILE_OPTIONS else "No DBC files found in DBC_Dump.",
                                ),
                                html.Hr(),
                                dbc.Button(
                                    "Start Live Data",
                                    id="live-start-btn",
                                    color="primary",
                                    className="w-100",
                                    disabled=not bool(DBC_FILE_OPTIONS),
                                ),
                                html.Div(
                                    "Live mode: polling every 0.5s",
                                    className="small",
                                    style={"color": "#64748B"},
                                ),
                                html.Div(
                                    id="fetch-status",
                                    className="mt-2",
                                    style={"color": "#EF4444"},
                                    children="Click Start Live Data to begin.",
                                ),
                            ],
                            className="panel-card control-card",
                        ),
                        md=5,
                    ),
                    dbc.Col(
                        dbc.Card(
                            [
                                html.Div("Recent Signals", id="table-title", className="panel-title"),
                                dash_table.DataTable(
                                    id="history-table",
                                    columns=[
                                        {"name": "Time", "id": "Time"},
                                        {"name": "Message", "id": "Message"},
                                        {"name": "Signal", "id": "Signal"},
                                        {"name": "Value", "id": "Value"},
                                        {"name": "Unit", "id": "Unit"},
                                    ],
                                    data=[],
                                    page_size=10,
                                    fill_width=True,
                                    sort_action="native",
                                ),
                            ],
                            className="panel-card table-card",
                        ),
                        md=7,
                    ),
                    dbc.Col(make_status_row(), md=12),
                ],
                className="g-3",
            ),

            html.Div(
                [
                    html.Div("Message Matrix", className="panel-title"),
                    html.Div(id="message-grid"),
                ],
                className="panel-card mt-4",
            ),

            html.Div(
                [
                    dbc.Row(
                        [
                            dbc.Col(
                                html.Div("Signal Lab", className="panel-title"),
                                md=6,
                            ),
                            dbc.Col(
                                dbc.Button(
                                    "Add Graph",
                                    id="add-graph-btn",
                                    color="primary",
                                ),
                                md=6,
                                className="text-md-end",
                            ),
                        ],
                        align="center",
                        className="mb-2",
                    ),
                    html.Div(id="graph-container"),
                ],
                className="panel-card mt-4",
            ),
            dcc.Interval(id="poller", interval=500, disabled=True),
        ],
        fluid=True,
        className="pb-5 pt-3 page-font",
    ),
    style={
        "minHeight": "100vh",
        "backgroundColor": "#0B0F14",
        "--panel-bg": "#0F172A",
        "--panel-border": "rgba(148,163,184,0.18)",
        "--panel-shadow": "0 20px 44px rgba(2,6,23,0.7)",
        "--accent": "#06B6D4",
        "--accent-2": "#F97316",
        "--text-strong": "#F8FAFC",
        "--text-muted": "#94A3B8",
        "--header-bg": "linear-gradient(120deg, #0B0F14 0%, #111827 55%, #0F766E 100%)",
    },
)


# ---------- THEME CALLBACKS ----------


@app.callback(
    Output("theme-store", "data"),
    Input("theme-toggle", "value"),
)
def set_theme(value):
    return "dark" if value else "light"


@app.callback(
    Output("page-wrapper", "style"),
    Output("main-title", "style"),
    Output("main-subtitle", "style"),
    Output("logger-label", "style"),
    Output("upload-label", "style"),
    Output("logger-hint", "style"),
    Output("table-title", "style"),
    Output("history-table", "style_table"),
    Output("history-table", "style_header"),
    Output("history-table", "style_data"),
    Output("base-url", "style"),
    Output("dbc-dropdown", "style"),
    Input("theme-store", "data"),
)
def update_theme_styles(theme):
    if theme == "dark":
        page_style = {
            "minHeight": "100vh",
            "backgroundColor": "#0B0F14",
            "backgroundImage": (
                "radial-gradient(circle at 12% 18%, rgba(6,182,212,0.22), transparent 45%), "
                "radial-gradient(circle at 82% 10%, rgba(249,115,22,0.22), transparent 45%), "
                "linear-gradient(135deg, #0B0F14 0%, #111827 55%, #0F172A 100%)"
            ),
            "--panel-bg": "#0F172A",
            "--panel-border": "rgba(148,163,184,0.18)",
            "--panel-shadow": "0 20px 44px rgba(2,6,23,0.7)",
            "--accent": "#06B6D4",
            "--accent-2": "#F97316",
            "--text-strong": "#F8FAFC",
            "--text-muted": "#94A3B8",
            "--header-bg": "linear-gradient(120deg, #0B0F14 0%, #111827 55%, #0F766E 100%)",
        }
        main_title = {"color": "#F8FAFC", "fontSize": "28px", "fontWeight": "800"}
        subtitle = {"color": "#E2E8F0", "fontSize": "14px"}
        label_style = {"color": "#E2E8F0", "fontWeight": "600"}
        hint_style = {"color": "#94A3B8", "fontSize": "12px"}
        table_title = {"color": "#E2E8F0"}

        table_style_table = {"overflowX": "auto", "backgroundColor": "transparent"}
        table_style_header = {
            "backgroundColor": "#0F172A",
            "color": "#E2E8F0",
            "fontWeight": "600",
            "border": "1px solid #1F2937",
        }
        table_style_data = {
            "backgroundColor": "#0F172A",
            "color": "#E2E8F0",
            "border": "1px solid #1F2937",
        }

        input_style = {
            "backgroundColor": "#0F172A",
            "color": "#F8FAFC",
            "border": "1px solid #1F2937",
        }
        upload_style = {
            "backgroundColor": "#0F172A",
            "border": "1px dashed #1F2937",
            "color": "#E2E8F0",
            "borderRadius": "14px",
        }
    else:
        page_style = {
            "minHeight": "100vh",
            "backgroundColor": "#F8FAFC",
            "backgroundImage": (
                "radial-gradient(circle at 12% 18%, rgba(6,182,212,0.16), transparent 45%), "
                "radial-gradient(circle at 82% 10%, rgba(249,115,22,0.16), transparent 45%), "
                "linear-gradient(135deg, #F8FAFC 0%, #E0F2FE 45%, #FFF7ED 100%)"
            ),
            "--panel-bg": "#FFFFFF",
            "--panel-border": "rgba(15,23,42,0.08)",
            "--panel-shadow": "0 16px 40px rgba(15,23,42,0.08)",
            "--accent": "#06B6D4",
            "--accent-2": "#F97316",
            "--text-strong": "#0F172A",
            "--text-muted": "#475569",
            "--header-bg": "linear-gradient(120deg, #0F172A 0%, #1F2937 55%, #0F766E 100%)",
        }
        main_title = {"color": "#F8FAFC", "fontSize": "28px", "fontWeight": "800"}
        subtitle = {"color": "#E2E8F0", "fontSize": "14px"}
        label_style = {"color": "#0F172A", "fontWeight": "600"}
        hint_style = {"color": "#64748B", "fontSize": "12px"}
        table_title = {"color": "#0F172A"}

        table_style_table = {"overflowX": "auto"}
        table_style_header = {
            "backgroundColor": "#FFFFFF",
            "color": "#0F172A",
            "fontWeight": "600",
            "border": "1px solid #E2E8F0",
        }
        table_style_data = {
            "backgroundColor": "#FFFFFF",
            "color": "#0F172A",
            "border": "1px solid #E2E8F0",
        }

        input_style = {
            "backgroundColor": "#FFFFFF",
            "color": "#0F172A",
            "border": "1px solid #CBD5E1",
        }
        upload_style = {
            "backgroundColor": "#FFFFFF",
            "border": "1px dashed #CBD5E1",
            "color": "#0F172A",
            "borderRadius": "14px",
        }

    return (
        page_style,
        main_title,
        subtitle,
        label_style,
        label_style,
        hint_style,
        table_title,
        table_style_table,
        table_style_header,
        table_style_data,
        input_style,
        upload_style,
    )



@app.callback(
    Output("dbc-store", "data"),
    Output("upload-status", "children"),
    Output("history-store", "data"),
    Output("latest-store", "data"),
    Output("seq-store", "data"),
    Input("dbc-dropdown", "value"),
)
def handle_dbc_selection(dbc_path):
    if not dbc_path:
        return no_update, no_update, no_update, no_update, no_update
    try:
        dbc_info = parse_dbc_file(dbc_path)
        return (
            dbc_info,
            f"Loaded DBC: {dbc_info['name']}",
            [],
            {},
            0,
        )
    except Exception as exc:
        return (
            no_update,
            f"Failed to load DBC: {exc}",
            no_update,
            no_update,
            no_update,
        )


@app.callback(
    Output("poller", "disabled"),
    Output("live-enabled", "data"),
    Output("live-start-btn", "children"),
    Output("live-start-btn", "color"),
    Output("live-start-btn", "disabled"),
    Output("session-status", "children"),
    Output("session-status", "className"),
    Output("fetch-status", "children"),
    Input("live-start-btn", "n_clicks"),
    State("live-enabled", "data"),
    prevent_initial_call=True,
)
def start_live_data(_, enabled):
    if enabled:
        return (
            False,
            True,
            "Live Running",
            "success",
            True,
            "Live stream active",
            "session-pill session-pill--active",
            "",
        )
    return (
        False,
        True,
        "Live Running",
        "success",
        True,
        "Live stream active",
        "session-pill session-pill--active",
        "",
    )


@app.callback(
    Output("latest-store", "data", allow_duplicate=True),
    Output("history-store", "data", allow_duplicate=True),
    Output("seq-store", "data", allow_duplicate=True),
    Output("fetch-status", "children", allow_duplicate=True),
    Output("poll-store", "data", allow_duplicate=True),
    Output("boot-store", "data", allow_duplicate=True),
    Input("poller", "n_intervals"),
    State("dbc-store", "data"),
    State("base-url", "value"),
    State("seq-store", "data"),
    State("history-store", "data"),
    State("latest-store", "data"),
    State("boot-store", "data"),
    State("poll-store", "data"),
    prevent_initial_call=True,
)
def poll_live_data(
    _,
    dbc_data,
    base_url,
    last_seq,
    history_data,
    latest_data,
    boot_state,
    poll_state,
):
    if not dbc_data:
        return (
            latest_data,
            history_data,
            last_seq,
            "Select a DBC file to decode data.",
            poll_state,
            boot_state,
        )

    try:
        payload = fetch_live_frames(
            base_url or DEFAULT_BASE_URL, last_seq, LIVE_FETCH_LIMIT
        )
        payload_latest = _safe_int(payload.get("latest", last_seq), last_seq)
        payload_boot = payload.get("boot", None)
        raw_frames = payload.get("frames", [])
        reset_needed = False
        if boot_state is not None and payload_boot is not None and payload_boot != boot_state:
            reset_needed = True
        if payload_latest < (last_seq or 0):
            reset_needed = True
        if reset_needed:
            last_seq = 0
            history_data = []
            latest_data = {}

        frames = _filter_new_frames(raw_frames, last_seq or 0)
        next_seq = max(
            last_seq or 0,
            payload_latest,
            max((_safe_int(f.get("seq", -1), -1) for f in frames), default=-1),
        )
        rows = decode_frames(dbc_data["id"], frames)
    except Exception as exc:
        if ENABLE_PROM_METRICS and poll_status_gauge:
            poll_status_gauge.set(0)
        return (
            latest_data,
            history_data,
            last_seq,
            f"Connection error: {exc}",
            {"ok": False, "error": str(exc)},
            boot_state,
        )

    if ENABLE_PROM_METRICS and signal_value_gauge:
        for row in rows:
            val = row.get("Value")
            if isinstance(val, (int, float)):
                signal_value_gauge.labels(
                    signal=row.get("Signal", ""),
                    message=row.get("Message", ""),
                    can_id=row.get("CAN_ID", ""),
                    unit=row.get("Unit", ""),
                ).set(float(val))
        poll_status_gauge.set(1)
        last_seq_gauge.set(next_seq)

    updated_history = limit_history(history_data or [], rows)
    latest_map = dict(latest_data or {})
    for row in rows:
        latest_map[row["Signal"]] = row

    status_prefix = "ESP reboot detected; resyncing. " if reset_needed else ""
    if rows:
        status_text = status_prefix
    elif raw_frames:
        status_text = status_prefix + "Live frames received, but no signals decoded. Check DBC."
    else:
        status_text = status_prefix + "Connected, waiting for CAN frames..."

    poll_info = {
        "ok": True,
        "last_seq": next_seq,
        "frames": len(raw_frames),
        "decoded": len(rows),
    }
    if reset_needed:
        poll_info["reset"] = True

    boot_out = payload_boot if payload_boot is not None else boot_state

    if not rows and not reset_needed:
        return no_update, no_update, no_update, status_text, poll_info, boot_out

    return latest_map, updated_history, next_seq, status_text, poll_info, boot_out


@app.callback(
    Output("message-grid", "children"),
    Input("dbc-store", "data"),
    Input("latest-store", "data"),
    Input("theme-store", "data"),
)
def update_cards(dbc_data, latest_store, theme):
    layout = dbc_data["layout"] if dbc_data else []
    return build_message_cards(layout, latest_store or {}, theme or "light")


@app.callback(
    Output("history-table", "data"),
    Input("history-store", "data"),
)
def update_history_table(history_data):
    if not history_data:
        return []
    df = pd.DataFrame(history_data[-100:])
    return df.to_dict("records")


@app.callback(
    Output("status-cards", "children"),
    Input("seq-store", "data"),
    Input("latest-store", "data"),
    Input("poll-store", "data"),
    Input("theme-store", "data"),
)
def update_status_cards(seq_value, latest_store, poll_store, theme):
    connected = bool(poll_store and poll_store.get("ok")) or bool(latest_store)
    signal_count = len(latest_store or {})
    return build_status_cards(
        connected,
        seq_value or 0,
        signal_count,
        theme or "light",
    )


# ---------- GRAPH MANAGEMENT CALLBACKS ----------

@app.callback(
    Output("graph-list-store", "data"),
    Input("add-graph-btn", "n_clicks"),
    Input({"type": "remove-graph", "index": ALL}, "n_clicks"),
    Input({"type": "move-graph-up", "index": ALL}, "n_clicks"),
    Input({"type": "move-graph-down", "index": ALL}, "n_clicks"),
    State("graph-list-store", "data"),
    prevent_initial_call=True,
)
def modify_graph_list(add_clicks, remove_clicks, move_up, move_down, graph_list):
    graph_list = graph_list or []
    triggered = ctx.triggered_id

    if triggered == "add-graph-btn":
        graph_id = f"graph-{len(graph_list) + 1}"
        graph_list.append(graph_id)
        return graph_list

    if isinstance(triggered, dict):
        g_type = triggered.get("type")
        g_id = triggered.get("index")

        if g_type == "remove-graph":
            graph_list = [g for g in graph_list if g != g_id]

        elif g_type == "move-graph-up":
            if g_id in graph_list:
                i = graph_list.index(g_id)
                if i > 0:
                    graph_list[i - 1], graph_list[i] = graph_list[i], graph_list[i - 1]

        elif g_type == "move-graph-down":
            if g_id in graph_list:
                i = graph_list.index(g_id)
                if i < len(graph_list) - 1:
                    graph_list[i + 1], graph_list[i] = graph_list[i], graph_list[i + 1]

    return graph_list


@app.callback(
    Output("graph-container", "children"),
    Input("graph-list-store", "data"),
    State("dbc-store", "data"),
    State("theme-store", "data"),
)
def render_graph_blocks(graph_list, dbc_data, theme):
    if not graph_list:
        return []

    signals = []
    if dbc_data:
        for msg in dbc_data["layout"]:
            for sig in msg["signals"]:
                if sig["name"] not in signals:
                    signals.append(sig["name"])

    dark = theme == "dark"

    blocks = []
    for graph_id in graph_list:
        card_bg = "#0B1220" if dark else "#FFFFFF"
        text_color = "#E2E8F0" if dark else "#0F172A"
        dropdown_style = {
            "backgroundColor": "#0B1220",
            "color": "#E2E8F0",
            "border": "1px solid #1F2937",
        } if dark else {
            "backgroundColor": "#FFFFFF",
            "color": "#0F172A",
            "border": "1px solid #CBD5E1",
        }
        blocks.append(
            html.Div(
                [
                    html.Div(
                        [
                            html.H5(
                                f"Graph: {graph_id}",
                                className="mt-1 mb-2",
                                style={"color": text_color},
                            ),
                            dbc.ButtonGroup(
                                [
                                    dbc.Button(
                                        "▲",
                                        id={"type": "move-graph-up", "index": graph_id},
                                        size="sm",
                                        outline=True,
                                        color="secondary",
                                    ),
                                    dbc.Button(
                                        "▼",
                                        id={
                                            "type": "move-graph-down",
                                            "index": graph_id,
                                        },
                                        size="sm",
                                        outline=True,
                                        color="secondary",
                                    ),
                                    dbc.Button(
                                        "Remove",
                                        id={
                                            "type": "remove-graph",
                                            "index": graph_id,
                                        },
                                        size="sm",
                                        color="danger",
                                        outline=True,
                                        className="ms-1",
                                    ),
                                ],
                                className="ms-auto",
                            ),
                        ],
                        className="d-flex justify-content-between align-items-center",
                    ),
                    dcc.Dropdown(
                        id={"type": "signal-dropdown", "index": graph_id},
                        options=[{"label": s, "value": s} for s in signals],
                        multi=True,
                        placeholder="Select signals to plot...",
                        className="mb-2",
                        style=dropdown_style,
                    ),
                    dcc.Graph(
                        id={"type": "plot", "index": graph_id},
                        style={"height": "260px"},
                    ),
                ],
                className="mb-4 p-3 rounded-3 shadow-sm",
                style={
                    "backgroundColor": card_bg,
                    "border": "1px solid rgba(148,163,184,0.2)" if dark else "1px solid rgba(15,23,42,0.08)",
                },
            )
        )
    return blocks


@app.callback(
    Output({"type": "plot", "index": MATCH}, "figure"),
    Input("history-store", "data"),
    Input({"type": "signal-dropdown", "index": MATCH}, "value"),
    Input("theme-store", "data"),
)
def update_graph(history, selected_signals, theme):
    dark = theme == "dark"
    template = "plotly_dark" if dark else "plotly_white"
    paper_bg = "#020617" if dark else "#FFFFFF"
    plot_bg = "#020617" if dark else "#FFFFFF"

    fig = go.Figure()

    if history and selected_signals:
        # normalise single value -> list
        if isinstance(selected_signals, str):
            selected_signals = [selected_signals]

        df = pd.DataFrame(history)

        # build a proper datetime column from Unix seconds
        # (this is filled in decode_frames as "Unix")
        if "Unix" in df.columns:
            df["Timestamp"] = pd.to_datetime(df["Unix"], unit="s", errors="coerce")
        else:
            # fallback: try to parse the human-readable 'Time' string
            df["Timestamp"] = pd.to_datetime(df["Time"], errors="coerce")

        for sig in selected_signals:
            sig_df = df[df["Signal"] == sig].copy()
            if sig_df.empty:
                continue

            # --- NEW: sort by Timestamp and then by Seq (if available) ---
            sort_cols = []
            if "Timestamp" in sig_df.columns:
                sort_cols.append("Timestamp")
            if "Seq" in sig_df.columns:
                sort_cols.append("Seq")
            if sort_cols:
                sig_df = sig_df.sort_values(sort_cols)
            # -------------------------------------------------------------
            if GRAPH_MAX_POINTS and len(sig_df) > GRAPH_MAX_POINTS:
                sig_df = sig_df.iloc[-GRAPH_MAX_POINTS:]

            fig.add_trace(
                go.Scatter(
                    x=sig_df["Timestamp"],
                    y=sig_df["Value"],
                    mode="lines",
                    name=sig,
                )
            )


    fig.update_layout(
        margin=dict(l=10, r=10, t=30, b=30),
        height=260,
        template=template,
        xaxis=dict(
            title="Time",
            type="date",       # time axis
            autorange=True,    # always show entire min..max of history
        ),
        yaxis=dict(
            title="Value",
            autorange=True,
        ),
        legend_title="Signal",
        paper_bgcolor=paper_bg,
        plot_bgcolor=plot_bg,
    )

    return fig



if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8600, debug=False)
