from __future__ import annotations

import json
import os
import threading
import time
from datetime import datetime
from urllib.parse import urlencode
from urllib.request import Request, urlopen

import flet as ft

from database import Database
from models import (
    Alert,
    AppSettings,
    CareArticle,
    ConnectionStatus,
    DailyMovement,
    FetalData,
    NavEntry,
    RiskAssessment,
    RiskFactor,
    UserProfile,
    VitalReading,
)

# ---------------------------------------------------------------------------
# Singleton state
# ---------------------------------------------------------------------------
db = Database()
USER_PROFILE: UserProfile = db.get_profile()
APP_SETTINGS: AppSettings = db.get_settings()

NAV_ENTRIES: list[NavEntry] = [
    NavEntry(ft.Icons.HOME_ROUNDED,          ft.Icons.HOME_OUTLINED,           "Home",    "/home"),
    NavEntry(ft.Icons.CHILD_CARE_ROUNDED,    ft.Icons.CHILD_CARE_OUTLINED,     "Baby",    "/baby"),
    NavEntry(ft.Icons.FAVORITE_ROUNDED,      ft.Icons.FAVORITE_BORDER,         "Mother",  "/mother"),
    NavEntry(ft.Icons.NOTIFICATIONS_ROUNDED, ft.Icons.NOTIFICATIONS_OUTLINED,  "Alerts",  "/alerts"),
    NavEntry(ft.Icons.MENU_BOOK_ROUNDED,     ft.Icons.MENU_BOOK_OUTLINED,      "Care",    "/care"),
]

# ---------------------------------------------------------------------------
# Firebase configuration
# ---------------------------------------------------------------------------
FIREBASE_DB_URL = os.getenv(
    "DUALCARE_FIREBASE_DB_URL",
    "https://dualcare-8b27c-default-rtdb.firebaseio.com",
)
FIREBASE_DB_SECRET = os.getenv(
    "DUALCARE_FIREBASE_DB_SECRET", "",
)
POLL_INTERVAL_SECONDS = float(os.getenv("DUALCARE_POLL_INTERVAL_SECONDS", "2.0"))

# ---------------------------------------------------------------------------
# Global vitals / state objects (the UI reads these)
# ---------------------------------------------------------------------------
HEART_RATE     = VitalReading(value=0,    unit="BPM", normal_min=60,   normal_max=100, status="Waiting…")
SPO2           = VitalReading(value=0,    unit="%",   normal_min=95,   normal_max=100, status="Waiting…")
TEMPERATURE    = VitalReading(value=0.0,  unit="°C",  normal_min=36.0, normal_max=37.5, status="Waiting…")
BLOOD_PRESSURE = VitalReading(value=0,    unit="mmHg", normal_min=90,  normal_max=120,  status="Waiting…")
FETAL_DATA     = FetalData()
RISK           = RiskAssessment()
CONN_STATUS    = ConnectionStatus()
FIREBASE_ALERTS: list[Alert] = []

# Thread‑safety lock for data that the UI reads
_data_lock = threading.Lock()


class Colors:
    LAVENDER       = "#B8A9C9"
    TEAL           = "#7EC8C8"
    PINK           = "#F4B9C5"
    BG             = "#F5F6FA"
    CARD           = "#FFFFFF"
    TEXT_PRIMARY    = "#2D3436"
    TEXT_SECONDARY  = "#636E72"
    SUCCESS        = "#00B894"
    WARNING        = "#FDCB6E"
    DANGER         = "#D63031"
    INFO           = "#74B9FF"
    SURFACE_BORDER = "#E8E8EE"
    LIGHT_TEAL     = "#E0F4F4"
    LIGHT_PINK     = "#FDE8EE"
    LIGHT_LAVENDER = "#EDE7F3"
    LIGHT_SUCCESS  = "#E6F9F1"
    LIGHT_WARNING  = "#FFF8E1"
    LIGHT_DANGER   = "#FDEDED"


# ---------------------------------------------------------------------------
# Firebase helpers
# ---------------------------------------------------------------------------
def _build_url(path: str) -> str:
    """Build a full Firebase REST URL for *path* (e.g. '/sensors/wrist')."""
    base = FIREBASE_DB_URL.rstrip("/")
    url = f"{base}{path}.json"
    if FIREBASE_DB_SECRET:
        url += "?" + urlencode({"auth": FIREBASE_DB_SECRET})
    return url


def _fetch_json(path: str, timeout: int = 5) -> dict | None:
    """GET a single Firebase path and return parsed JSON, or None on error."""
    url = _build_url(path)
    try:
        req = Request(url=url, method="GET")
        with urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8")
        data = json.loads(raw) if raw else None
        return data if isinstance(data, dict) else None
    except Exception:
        return None


def fetch_wrist_data() -> dict | None:
    """Fetch maternal vitals from ESP32 #1 (wrist)."""
    return _fetch_json("/sensors/wrist")


def fetch_belt_data() -> dict | None:
    """Fetch fetal kick data from ESP32 #2 (belt)."""
    return _fetch_json("/sensors/belt")


def fetch_alert_events() -> dict | None:
    """Fetch alert events written by either ESP32."""
    return _fetch_json("/alert_events")


# ---------------------------------------------------------------------------
# Staleness helper — check if a timestamp_utc is recent
# ---------------------------------------------------------------------------
_STALE_THRESHOLD_SECONDS = 30  # data older than 30 s → device probably offline


def _is_recent(timestamp_str: str | None) -> bool:
    """Return True if *timestamp_str* (ISO‑8601 UTC) is within the staleness window."""
    if not timestamp_str or not isinstance(timestamp_str, str):
        return False
    try:
        # Handle "2026-06-24T00:00:00Z" format
        ts = timestamp_str.replace("Z", "+00:00")
        dt = datetime.fromisoformat(ts)
        age = (datetime.now(dt.tzinfo) - dt).total_seconds()
        return age < _STALE_THRESHOLD_SECONDS
    except Exception:
        return False


def _short_time(timestamp_str: str | None) -> str:
    """Extract HH:MM from an ISO timestamp, or return 'n/a'."""
    if not timestamp_str or not isinstance(timestamp_str, str):
        return "n/a"
    try:
        ts = timestamp_str.replace("Z", "+00:00")
        dt = datetime.fromisoformat(ts)
        return dt.strftime("%H:%M")
    except Exception:
        return "n/a"


# ---------------------------------------------------------------------------
# Vital‑status helpers
# ---------------------------------------------------------------------------
def _vital_status(value: float, lo: float, hi: float) -> str:
    if value == 0:
        return "Waiting…"
    if lo <= value <= hi:
        return "Normal"
    return "Abnormal"


# ---------------------------------------------------------------------------
# Main pipeline — called every poll cycle
# ---------------------------------------------------------------------------
def update_vitals_pipeline():
    """Fetch latest data from both ESP32s via Firebase and refresh global state."""
    wrist = fetch_wrist_data()
    belt  = fetch_belt_data()

    with _data_lock:
        # ── Wrist (ESP32 #1) — maternal vitals ──
        if wrist:
            hr_val = wrist.get("heart_rate", 0)
            spo2_val = wrist.get("spo2", 0)
            temp_val = wrist.get("temperature", 0.0)

            if hr_val:
                HEART_RATE.value = int(hr_val)
                HEART_RATE.status = _vital_status(HEART_RATE.value, 60, 100)
            if spo2_val:
                SPO2.value = int(spo2_val)
                SPO2.status = _vital_status(SPO2.value, 95, 100)
            if temp_val:
                TEMPERATURE.value = round(float(temp_val), 1)
                TEMPERATURE.status = _vital_status(TEMPERATURE.value, 36.0, 37.5)

            wrist_ts = wrist.get("timestamp_utc")
            CONN_STATUS.wrist_online = _is_recent(wrist_ts)
            CONN_STATUS.wrist_last_seen = _short_time(wrist_ts)
        else:
            CONN_STATUS.wrist_online = False

        # ── Belt (ESP32 #2) — fetal kicks ──
        if belt:
            kick_count = belt.get("kick_count", 0)
            last_kick  = belt.get("last_kick_time", "")

            try:
                kick_count = int(kick_count)
            except (TypeError, ValueError):
                kick_count = 0

            db.set_today_kick_count(kick_count, _short_time(last_kick))

            belt_ts = belt.get("timestamp_utc")
            CONN_STATUS.belt_online = _is_recent(belt_ts)
            CONN_STATUS.belt_last_seen = _short_time(belt_ts)
        else:
            CONN_STATUS.belt_online = False

        CONN_STATUS.firebase_reachable = (wrist is not None) or (belt is not None)

        # ── Refresh fetal data aggregates ──
        _refresh_fetal_data()

        # ── Risk assessment ──
        RISK.factors = [
            RiskFactor("✔ Normal oxygen level",  SPO2.value >= 95 if SPO2.value else True),
            RiskFactor("✔ Stable heart rate",     60 <= HEART_RATE.value <= 100 if HEART_RATE.value else True),
            RiskFactor("⚠ Elevated body temperature", TEMPERATURE.value <= 37.5 if TEMPERATURE.value else True),
        ]
        if (TEMPERATURE.value and TEMPERATURE.value > 37.5) or (BLOOD_PRESSURE.value and BLOOD_PRESSURE.value > 130):
            RISK.level = "Moderate Risk"
        else:
            RISK.level = "Low Risk"

        # ── Firebase alerts ──
        _refresh_alerts()


def _refresh_fetal_data() -> None:
    FETAL_DATA.daily_count = db.get_today_movement_count()
    FETAL_DATA.last_movement_time = db.get_last_movement_time()
    FETAL_DATA.daily_history = db.get_daily_movement_history(7)
    FETAL_DATA.monthly_history = db.get_daily_movement_history(30)

    counts_7 = [d.count for d in FETAL_DATA.daily_history]
    FETAL_DATA.weekly_avg = round(sum(counts_7) / max(len(counts_7), 1), 1)

    if len(counts_7) >= 6:
        recent = sum(counts_7[-3:])
        prior  = sum(counts_7[-6:-3])
        if prior > 0:
            diff = prior - recent
            FETAL_DATA.change_percentage = round((diff / prior) * 100, 1)
        else:
            FETAL_DATA.change_percentage = 0.0
    else:
        FETAL_DATA.change_percentage = 0.0


def _refresh_alerts() -> None:
    """Pull alert events from Firebase and populate FIREBASE_ALERTS."""
    global FIREBASE_ALERTS
    raw = fetch_alert_events()
    if not raw or not isinstance(raw, dict):
        return

    alerts = []
    # alert_events may have nested structure; iterate the top-level keys
    for key, val in raw.items():
        if isinstance(val, dict):
            # Could be a single event or a container with sub‑events
            if "event_id" in val or "event_type" in val:
                alerts.append(_parse_alert(key, val))
            else:
                # Container — iterate inner events
                for sub_key, sub_val in val.items():
                    if isinstance(sub_val, dict):
                        alerts.append(_parse_alert(sub_key, sub_val))

    # Sort newest first, keep last 20
    alerts.sort(key=lambda a: a.timestamp, reverse=True)
    FIREBASE_ALERTS = alerts[:20]


def _parse_alert(event_id: str, data: dict) -> Alert:
    level = data.get("level", "warning")
    sensor = data.get("sensor", "")
    source = data.get("source", sensor or "unknown")
    ts = data.get("timestamp_utc", "")
    message = data.get("message", "")

    severity = "danger" if level in ("critical", "severe_fever") else "warning"

    if source == "esp32_belt" or "movement" in message.lower():
        title = "⚠ No fetal movement detected"
        desc = message or f"No kicks registered for an extended period ({level})."
        source = "belt"
    elif sensor == "heart_rate":
        title = "⚠ Heart rate alert"
        desc = message or f"Heart rate {data.get('value', '')} BPM ({level}, threshold {data.get('threshold', '')})."
        source = "wrist"
    elif sensor == "spo2":
        title = "⚠ Oxygen level alert"
        desc = message or f"SpO₂ {data.get('value', '')}% ({level}, threshold {data.get('threshold', '')})."
        source = "wrist"
    elif sensor == "temperature":
        title = "⚠ Temperature alert"
        desc = message or f"Body temperature {data.get('value', '')}°C ({level}, threshold {data.get('threshold', '')})."
        source = "wrist"
    else:
        title = f"⚠ {(sensor or level).replace('_', ' ').title()} alert"
        desc = message or json.dumps({k: v for k, v in data.items() if k not in ("event_id", "timestamp_utc", "timestamp_utc_synced")})
        source = sensor or "unknown"

    return Alert(
        title=title,
        description=desc,
        timestamp=ts,
        severity=severity,
        source=source,
    )