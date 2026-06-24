import flet as ft
from datetime import datetime, timedelta, timezone
from models import UserProfile, AppSettings, CareArticle, DailyMovement

class Database:
    def __init__(self):
        self.profile = UserProfile()
        self.settings = AppSettings()
        self.seen_events = set()
        
        # توليد سجل ركلات الأيام السابقة لعرض الرسم البياني فوراً
        self.movement_history = {
            (datetime.now(timezone.utc) - timedelta(days=5)).strftime("%Y-%m-%d"): 52,
            (datetime.now(timezone.utc) - timedelta(days=4)).strftime("%Y-%m-%d"): 49,
            (datetime.now(timezone.utc) - timedelta(days=3)).strftime("%Y-%m-%d"): 50,
            (datetime.now(timezone.utc) - timedelta(days=2)).strftime("%Y-%m-%d"): 44,
            (datetime.now(timezone.utc) - timedelta(days=1)).strftime("%Y-%m-%d"): 31,
            datetime.now(timezone.utc).strftime("%Y-%m-%d"): 0   # start at 0; real kicks come from Firebase
        }
        self.last_movement = "n/a"
        
    def get_profile(self) -> UserProfile:
        return self.profile
        
    def get_settings(self) -> AppSettings:
        return self.settings

    def is_event_seen(self, event_id: str) -> bool:
        return event_id in self.seen_events

    def mark_event_seen(self, event_id: str, event_type: str):
        self.seen_events.add(event_id)

    def add_fetal_movement(self, count: int, timestamp: str, source: str, event_id: str):
        today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        self.movement_history[today] = self.movement_history.get(today, 0) + count
        self.last_movement = datetime.now(timezone.utc).strftime("%H:%M")

    def set_today_kick_count(self, count: int, last_kick_time: str = ""):
        """Set today's kick count directly from Firebase belt data."""
        today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        self.movement_history[today] = count
        if last_kick_time:
            self.last_movement = last_kick_time

    def get_today_movement_count(self) -> int:
        today = datetime.now(timezone.utc).strftime("%Y-%m-%d")
        return self.movement_history.get(today, 0)

    def get_last_movement_time(self) -> str:
        return self.last_movement

    def get_daily_movement_history(self, days: int) -> list[DailyMovement]:
        history = []
        for i in range(days - 1, -1, -1):
            date_obj = datetime.now(timezone.utc) - timedelta(days=i)
            date_str = date_obj.strftime("%Y-%m-%d")
            day_name = date_obj.strftime("%a")
            count = self.movement_history.get(date_str, 0)
            history.append(DailyMovement(date_str=day_name, count=count))
        return history

    def get_care_articles(self) -> list[CareArticle]:
        return [
            CareArticle(
                title="Signs of Preeclampsia", 
                category="Emergency Guide", 
                content="Watch for sudden swelling in your face or hands, severe headaches that won't go away, or blurred vision. Contact your provider immediately.",
                icon=ft.Icons.REPORT_PROBLEM_ROUNDED
            ),
            CareArticle(
                title="Week 32: Growth & Milestones", 
                category="Pregnancy Education", 
                content="Your baby is now about the size of a jicama! They are gaining fat rapidly, and their lungs are practicing breathing.",
                icon=ft.Icons.CHILD_CARE_ROUNDED
            ),
            CareArticle(
                title="Third Trimester Nutrition Tips", 
                category="Nutrition", 
                content="Focus on calcium, iron, and staying hydrated. Small, frequent meals help combat third-trimester heartburn.",
                icon=ft.Icons.RESTAURANT_ROUNDED
            ),
            CareArticle(
                title="Safe Third Trimester Exercises", 
                category="Exercise", 
                content="Prenatal yoga, walking, and pelvic tilts are excellent to prepare your body for birth without overexertion.",
                icon=ft.Icons.FITNESS_CENTER_ROUNDED
            ),
        ]
