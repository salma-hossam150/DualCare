from dataclasses import dataclass, field
from typing import List

@dataclass
class NavEntry:
    icon: str
    icon_outline: str
    title: str
    route: str

@dataclass
class UserProfile:
    name: str = "Sarah"
    pregnancy_week: int = 32
    due_date_countdown_days: int = 56

@dataclass
class AppSettings:
    theme_mode: str = "light"

@dataclass
class CareArticle:
    title: str
    category: str
    content: str
    icon: str

@dataclass
class VitalReading:
    value: float
    unit: str
    normal_min: float
    normal_max: float
    status: str = "Normal"

@dataclass
class DailyMovement:
    date_str: str
    count: int

@dataclass
class FetalData:
    daily_count: int = 0
    last_movement_time: str = "n/a"
    weekly_avg: float = 0.0
    change_percentage: float = 0.0
    daily_history: List[DailyMovement] = field(default_factory=list)
    monthly_history: List[DailyMovement] = field(default_factory=list)

@dataclass
class RiskFactor:
    text: str
    is_safe: bool

@dataclass
class RiskAssessment:
    level: str = "Low Risk"
    factors: List[RiskFactor] = field(default_factory=list)

@dataclass
class Alert:
    title: str
    description: str
    timestamp: str
    severity: str = "warning"
    source: str = ""          # "wrist" or "belt" — identifies which ESP produced it

@dataclass
class ConnectionStatus:
    """Tracks real-time connectivity for each ESP32 device."""
    wrist_online: bool = False
    belt_online: bool = False
    wrist_last_seen: str = "never"
    belt_last_seen: str = "never"
    firebase_reachable: bool = False
