#!/usr/bin/env python3
"""
Simple app launcher for DualCare
This script wraps the main.py to handle Flet version compatibility
"""
import sys
sys.path.insert(0, 'src')

import flet as ft

# Import and run the main app
from main import main

if __name__ == "__main__":
    # Run with ft.run() instead of ft.app()
    ft.run(main)
