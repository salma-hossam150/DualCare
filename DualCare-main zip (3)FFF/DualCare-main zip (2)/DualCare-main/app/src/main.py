import asyncio
import flet as ft
import engine

# ---------------------------------------------------------------------------
# Polling interval (seconds) — matches ESP32 send rate
# ---------------------------------------------------------------------------
_POLL_SECONDS = engine.POLL_INTERVAL_SECONDS


def main(page: ft.Page):
    page.title = "DualCare — Maternal & Fetal Health Platform"
    page.bgcolor = engine.Colors.BG
    page.window_width = 420
    page.window_height = 820
    page.scroll = "auto"

    # Initial data load
    engine.update_vitals_pipeline()

    tab_content = ft.Column(expand=True, scroll="auto")
    current_tab_index = 0  # track which tab is active for auto-refresh

    # ------------------------------------------------------------------
    # Connection‑status badge (updated every poll)
    # ------------------------------------------------------------------
    status_badge = ft.Container(
        bgcolor=engine.Colors.LIGHT_PINK,
        padding=6,
        border_radius=10,
        content=ft.Text("⏳ Connecting…", color=engine.Colors.WARNING, size=11, weight="bold"),
    )

    def _update_status_badge():
        cs = engine.CONN_STATUS
        if cs.wrist_online and cs.belt_online:
            status_badge.bgcolor = engine.Colors.LIGHT_SUCCESS
            status_badge.content = ft.Text("🟢 Live", color=engine.Colors.SUCCESS, size=11, weight="bold")
        elif cs.wrist_online or cs.belt_online:
            status_badge.bgcolor = engine.Colors.LIGHT_WARNING
            which = "Wrist" if cs.wrist_online else "Belt"
            status_badge.content = ft.Text(f"🟡 {which} only", color=engine.Colors.WARNING, size=11, weight="bold")
        elif cs.firebase_reachable:
            status_badge.bgcolor = engine.Colors.LIGHT_WARNING
            status_badge.content = ft.Text("🟡 Stale", color=engine.Colors.WARNING, size=11, weight="bold")
        else:
            status_badge.bgcolor = engine.Colors.LIGHT_DANGER
            status_badge.content = ft.Text("🔴 Offline", color=engine.Colors.DANGER, size=11, weight="bold")

    # ------------------------------------------------------------------
    # Header
    # ------------------------------------------------------------------
    def build_header():
        return ft.Container(
            content=ft.Column([
                ft.Row([
                    ft.Text(f"Good morning, {engine.USER_PROFILE.name} 👋", size=22, weight="bold", color=engine.Colors.TEXT_PRIMARY),
                    status_badge,
                ], alignment="spaceBetween"),
                ft.Row([
                    ft.Text(f"Week {engine.USER_PROFILE.pregnancy_week} of pregnancy", size=14, color=engine.Colors.TEXT_SECONDARY),
                    ft.Text(f"• {engine.USER_PROFILE.due_date_countdown_days} days countdown", size=14, color=engine.Colors.TEAL, weight="bold")
                ])
            ]),
            padding=ft.Padding(left=15, right=15, top=20, bottom=5)
        )

    # ------------------------------------------------------------------
    # Tabs
    # ------------------------------------------------------------------
    def _hr_trend():
        v = engine.HEART_RATE.value
        if v == 0:
            return "⏳ Waiting"
        return "📈 Stable" if 60 <= v <= 100 else "⚠️ Abnormal"

    def _spo2_trend():
        v = engine.SPO2.value
        if v == 0:
            return "⏳ Waiting"
        return "🟢 Optimal" if v >= 95 else "⚠️ Low"

    def _temp_trend():
        v = engine.TEMPERATURE.value
        if v == 0:
            return "⏳ Waiting"
        return "🟢 Normal" if 36.0 <= v <= 37.5 else "⚠️ Abnormal"

    def _kick_trend():
        v = engine.FETAL_DATA.daily_count
        if v >= 10:
            return "📈 Stable"
        elif v > 0:
            return "🟡 Low"
        return "⏳ Waiting"

    def render_home():
        return ft.Column([
            # Health Score Card
            ft.Container(
                content=ft.Column([
                    ft.Text("Health Score Status", weight="bold", size=16, color=engine.Colors.TEXT_PRIMARY),
                    ft.Row([ft.Text("🟢 Mother Status:", size=14), ft.Text("Healthy" if engine.CONN_STATUS.wrist_online else "Waiting for data", weight="bold", color=engine.Colors.SUCCESS if engine.CONN_STATUS.wrist_online else engine.Colors.WARNING)]),
                    ft.Row([ft.Text("🟢 Baby Status:", size=14), ft.Text("Normal" if engine.CONN_STATUS.belt_online else "Waiting for data", weight="bold", color=engine.Colors.SUCCESS if engine.CONN_STATUS.belt_online else engine.Colors.WARNING)]),
                    ft.Row([ft.Text("🟡 Risk Level:", size=14), ft.Text(engine.RISK.level, weight="bold", color=engine.Colors.WARNING)]),
                ], spacing=8),
                bgcolor=engine.Colors.CARD, padding=15, border_radius=15
            ),

            # ESP Connection Status
            ft.Container(
                content=ft.Column([
                    ft.Text("Device Status", weight="bold", size=14, color=engine.Colors.TEXT_PRIMARY),
                    ft.Row([
                        ft.Icon(ft.Icons.WATCH, color=engine.Colors.SUCCESS if engine.CONN_STATUS.wrist_online else engine.Colors.DANGER, size=18),
                        ft.Text("Wrist Band", size=13, weight="bold"),
                        ft.Text(f"{'Online' if engine.CONN_STATUS.wrist_online else 'Offline'} • Last: {engine.CONN_STATUS.wrist_last_seen}", size=12, color=engine.Colors.TEXT_SECONDARY),
                    ]),
                    ft.Row([
                        ft.Icon(ft.Icons.PREGNANT_WOMAN, color=engine.Colors.SUCCESS if engine.CONN_STATUS.belt_online else engine.Colors.DANGER, size=18),
                        ft.Text("Belly Belt", size=13, weight="bold"),
                        ft.Text(f"{'Online' if engine.CONN_STATUS.belt_online else 'Offline'} • Last: {engine.CONN_STATUS.belt_last_seen}", size=12, color=engine.Colors.TEXT_SECONDARY),
                    ]),
                ], spacing=6),
                bgcolor=engine.Colors.CARD, padding=12, border_radius=12,
            ),

            ft.Text("Live Monitoring Cards", size=16, weight="bold", color=engine.Colors.TEXT_PRIMARY),
            ft.ResponsiveRow([
                create_vital_card("❤️ Heart Rate", f"{engine.HEART_RATE.value} BPM" if engine.HEART_RATE.value else "-- BPM", "60-100 BPM", _hr_trend(), engine.Colors.LAVENDER),
                create_vital_card("🩸 SpO₂", f"{engine.SPO2.value} %" if engine.SPO2.value else "-- %", "95-100 %", _spo2_trend(), engine.Colors.TEAL),
                create_vital_card("🌡️ Body Temp", f"{engine.TEMPERATURE.value} °C" if engine.TEMPERATURE.value else "-- °C", "36-37.5 °C", _temp_trend(), engine.Colors.PINK),
                create_vital_card("👶 Fetal Kicks", f"{engine.FETAL_DATA.daily_count} Kicks", "Min 10/day", _kick_trend(), engine.Colors.TEAL),
            ], spacing=10)
        ], spacing=15)

    def render_baby():
        return ft.Column([
            ft.Container(
                content=ft.Column([
                    ft.Text("Daily Kick Count", weight="bold", size=16, color=engine.Colors.TEXT_SECONDARY),
                    ft.Row([
                        ft.Text(str(engine.FETAL_DATA.daily_count), size=44, weight="bold", color=engine.Colors.TEAL),
                        ft.Text("movements today", size=14, color=engine.Colors.TEXT_SECONDARY)
                    ], alignment="baseline"),
                    ft.Row([
                        ft.Icon(ft.Icons.ACCESS_TIME, size=14, color=engine.Colors.TEXT_SECONDARY),
                        ft.Text(f"Last kick: {engine.FETAL_DATA.last_movement_time}", size=13, color=engine.Colors.TEXT_SECONDARY),
                    ]),
                ]),
                bgcolor=engine.Colors.CARD, padding=15, border_radius=15
            ),
            ft.Text("Trend Graph (Weekly)", size=16, weight="bold", color=engine.Colors.TEXT_PRIMARY),
            ft.Container(
                content=ft.Column([
                    graph_bar(d.date_str, d.count, d.count < 10)
                    for d in engine.FETAL_DATA.daily_history[-5:]
                ], spacing=12), bgcolor=engine.Colors.CARD, padding=15, border_radius=15
            ),
            ft.Container(
                content=ft.Row([
                    ft.Icon(ft.Icons.REPORT_PROBLEM_ROUNDED, color=engine.Colors.WARNING, size=24),
                    ft.Container(content=ft.Text(
                        f"⚠️ Fetal activity changed by {engine.FETAL_DATA.change_percentage}% compared to prior period. "
                        + ("Consider contacting your healthcare provider." if engine.FETAL_DATA.change_percentage > 20 else "Monitoring continues."),
                        color=engine.Colors.TEXT_PRIMARY, size=13), expand=True)
                ]), bgcolor=engine.Colors.LIGHT_WARNING, padding=12, border_radius=12
            ) if engine.FETAL_DATA.change_percentage > 0 else ft.Container(),
        ], spacing=15)

    def render_mother():
        return ft.Column([
            ft.Text("Mother Health Indicators", size=16, weight="bold", color=engine.Colors.TEXT_PRIMARY),
            # Live vitals detail cards
            ft.Container(
                content=ft.Column([
                    ft.Text("Live Vitals", weight="bold", size=15, color=engine.Colors.TEXT_PRIMARY),
                    ft.Divider(color=engine.Colors.SURFACE_BORDER),
                    _detail_row("❤️ Heart Rate", f"{engine.HEART_RATE.value} BPM" if engine.HEART_RATE.value else "Waiting…", engine.HEART_RATE.status),
                    _detail_row("🩸 SpO₂", f"{engine.SPO2.value} %" if engine.SPO2.value else "Waiting…", engine.SPO2.status),
                    _detail_row("🌡️ Temperature", f"{engine.TEMPERATURE.value} °C" if engine.TEMPERATURE.value else "Waiting…", engine.TEMPERATURE.status),
                ], spacing=8),
                bgcolor=engine.Colors.CARD, padding=15, border_radius=15
            ),
            ft.Container(
                content=ft.Column([
                    ft.Text(f"Risk Assessment: {engine.RISK.level.upper()}", weight="bold", size=15, color=engine.Colors.SUCCESS if engine.RISK.level == "Low Risk" else engine.Colors.WARNING),
                    ft.Divider(color=engine.Colors.SURFACE_BORDER),
                    ft.Text("Risk Factors Section:", weight="w500", size=14, color=engine.Colors.TEXT_SECONDARY),
                    ft.Column([ft.Row([ft.Icon(ft.Icons.CHECK_CIRCLE if f.is_safe else ft.Icons.ERROR, color=engine.Colors.SUCCESS if f.is_safe else engine.Colors.WARNING, size=18), ft.Text(f.text, size=14)]) for f in engine.RISK.factors], spacing=8)
                ]), bgcolor=engine.Colors.CARD, padding=15, border_radius=15
            )
        ], spacing=15)

    def render_alerts():
        alerts_list = engine.FIREBASE_ALERTS
        if alerts_list:
            tiles = [
                alert_tile(a.title, a.description,
                           engine.Colors.DANGER if a.severity == "danger" else engine.Colors.WARNING,
                           engine.Colors.LIGHT_DANGER if a.severity == "danger" else engine.Colors.LIGHT_WARNING)
                for a in alerts_list
            ]
        else:
            # Fallback — no alerts from Firebase yet
            tiles = [
                ft.Container(
                    content=ft.Column([
                        ft.Icon(ft.Icons.CHECK_CIRCLE_OUTLINE, color=engine.Colors.SUCCESS, size=48),
                        ft.Text("No alerts", size=16, weight="bold", color=engine.Colors.TEXT_PRIMARY),
                        ft.Text("All vitals are within normal ranges. Alerts from the ESP32 sensors will appear here automatically.",
                                size=13, color=engine.Colors.TEXT_SECONDARY, text_align="center"),
                    ], spacing=10, horizontal_alignment="center"),
                    bgcolor=engine.Colors.CARD, padding=30, border_radius=15,
                )
            ]
        return ft.Column([
            ft.Text("Alerts Center", size=16, weight="bold", color=engine.Colors.TEXT_PRIMARY),
        ] + tiles, spacing=10)

    def render_care():
        tiles = []
        for item in engine.db.get_care_articles():
            tiles.append(
                ft.Container(
                    content=ft.ListTile(
                        leading=ft.Icon(item.icon, color=engine.Colors.TEAL),
                        title=ft.Text(item.title, weight="bold", color=engine.Colors.TEXT_PRIMARY),
                        subtitle=ft.Text(f"[{item.category}]\n{item.content}", size=12, color=engine.Colors.TEXT_SECONDARY)
                    ), bgcolor=engine.Colors.CARD, border_radius=12
                )
            )
        return ft.Column([ft.Text("Care Hub & Guides", size=16, weight="bold", color=engine.Colors.TEXT_PRIMARY)] + tiles, spacing=10)

    # ------------------------------------------------------------------
    # UI Helpers
    # ------------------------------------------------------------------
    def create_vital_card(title, value, normal, trend, color_accent):
        return ft.Container(
            content=ft.Column([
                ft.Text(title, size=13, color=engine.Colors.TEXT_SECONDARY, weight="bold"),
                ft.Text(value, size=20, weight="bold", color=engine.Colors.TEXT_PRIMARY),
                ft.Text(f"Normal: {normal}", size=11, color=engine.Colors.TEXT_SECONDARY),
                ft.Container(height=3, width=50, bgcolor=color_accent, border_radius=2),
                ft.Text(trend, size=12, color=engine.Colors.SUCCESS if "Stable" in trend or "Optimal" in trend or "Normal" in trend else engine.Colors.WARNING, weight="bold")
            ], spacing=6),
            bgcolor=engine.Colors.CARD, padding=12, border_radius=15, col={"xs": 6, "sm": 6}
        )

    def graph_bar(day, val, is_alert):
        bar_width = max(val * 4, 4)  # minimum visible bar
        return ft.Row([
            ft.Container(content=ft.Text(day, size=12, weight="bold"), width=35),
            ft.Container(height=14, width=bar_width, bgcolor=engine.Colors.DANGER if is_alert else engine.Colors.TEAL, border_radius=4),
            ft.Text(f"{val} { '⚠️' if is_alert else ''}", size=12, weight="bold", color=engine.Colors.DANGER if is_alert else engine.Colors.TEXT_PRIMARY)
        ], alignment="start")

    def alert_tile(title, desc, color, bg_color):
        return ft.Container(
            content=ft.ListTile(
                title=ft.Text(title, weight="bold", size=14, color=engine.Colors.TEXT_PRIMARY),
                subtitle=ft.Text(desc, size=12, color=engine.Colors.TEXT_SECONDARY),
                leading=ft.Icon(ft.Icons.WARNING_AMBER_ROUNDED, color=color)
            ), bgcolor=bg_color, border_radius=12
        )

    def _detail_row(label: str, value: str, status: str):
        status_color = engine.Colors.SUCCESS if status == "Normal" else engine.Colors.WARNING
        return ft.Row([
            ft.Text(label, size=14, weight="bold"),
            ft.Text(value, size=14, color=engine.Colors.TEXT_PRIMARY),
            ft.Container(
                bgcolor=engine.Colors.LIGHT_SUCCESS if status == "Normal" else engine.Colors.LIGHT_WARNING,
                padding=ft.Padding(left=8, right=8, top=2, bottom=2),
                border_radius=8,
                content=ft.Text(status, size=11, color=status_color, weight="bold"),
            ),
        ], alignment="spaceBetween")

    # ------------------------------------------------------------------
    # Tab navigation
    # ------------------------------------------------------------------
    def _rebuild_current_tab():
        """Rebuild whichever tab is currently visible."""
        tab_content.controls.clear()
        if current_tab_index == 0:
            tab_content.controls.append(render_home())
        elif current_tab_index == 1:
            tab_content.controls.append(render_baby())
        elif current_tab_index == 2:
            tab_content.controls.append(render_mother())
        elif current_tab_index == 3:
            tab_content.controls.append(render_alerts())
        elif current_tab_index == 4:
            tab_content.controls.append(render_care())

    def on_nav_change(e):
        nonlocal current_tab_index
        current_tab_index = e.control.selected_index
        engine.update_vitals_pipeline()
        _rebuild_current_tab()
        page.update()

    # ------------------------------------------------------------------
    # Auto-polling background task
    # ------------------------------------------------------------------
    async def _poll_loop():
        """Background coroutine: fetch Firebase data every N seconds and refresh UI."""
        while True:
            await asyncio.sleep(_POLL_SECONDS)
            try:
                engine.update_vitals_pipeline()
                _update_status_badge()
                _rebuild_current_tab()
                page.update()
            except Exception:
                pass  # keep polling even if a single cycle fails

    # ------------------------------------------------------------------
    # Build initial UI
    # ------------------------------------------------------------------
    _update_status_badge()
    tab_content.controls.append(render_home())

    page.navigation_bar = ft.NavigationBar(
        selected_index=0,
        destinations=[
            ft.NavigationBarDestination(icon=ft.Icons.HOME_OUTLINED, selected_icon=ft.Icons.HOME_ROUNDED, label="Home"),
            ft.NavigationBarDestination(icon=ft.Icons.CHILD_CARE_OUTLINED, selected_icon=ft.Icons.CHILD_CARE_ROUNDED, label="Baby"),
            ft.NavigationBarDestination(icon=ft.Icons.FAVORITE_BORDER, selected_icon=ft.Icons.FAVORITE, label="Mother"),
            ft.NavigationBarDestination(icon=ft.Icons.NOTIFICATIONS_OUTLINED, selected_icon=ft.Icons.NOTIFICATIONS, label="Alerts"),
            ft.NavigationBarDestination(icon=ft.Icons.HEALING_OUTLINED, selected_icon=ft.Icons.HEALING, label="Care"),
        ],
        on_change=on_nav_change,
    )

    page.add(build_header(), tab_content)

    # Start the live‑polling background task
    page.run_task(_poll_loop)


ft.run(main)
