@echo off
REM Set Firebase URL to a localhost mock to avoid connection delays
set DUALCARE_FIREBASE_DB_URL=http://localhost:9999

REM Run the app as a web app (recommended for faster startup)
cd /d "%~dp0"
echo Starting DualCare app...
echo Open your browser and go to: http://localhost:8000
timeout /t 2
.\.venv\Scripts\python.exe -m flet.cli run src --web

REM Alternative: Run as desktop app (uncomment below)
REM .\.venv\Scripts\flet.exe run src\main.py
