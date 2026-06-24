#pragma once

// ──────────────────────────────────────────────
//  General timing
// ──────────────────────────────────────────────
inline constexpr unsigned long SENSOR_READ_INTERVAL_MS = 2000;  // Firebase push interval

// ──────────────────────────────────────────────
//  Firebase Realtime Database
// ──────────────────────────────────────────────
inline const char* DB_URL               = "https://dualcare-8b27c-default-rtdb.firebaseio.com/";
inline const char* DB_SENSORS_PATH      = "/sensors/belt";
inline const char* DB_ALERT_EVENTS_PATH = "/alert_events";
inline const char* FIREBASE_AUTH_TOKEN = "ZO7HvLMG0UlIsbseouBL65AyEVlPRaKWtuTd4Q8Q";

// ---------------------------------------------------------------------------
//  WiFi credentials
// ---------------------------------------------------------------------------
inline const char* WIFI_SSID = "Judy";
inline const char* WIFI_PASS = "jude777778";
// ──────────────────────────────────────────────
//  MPU6050 accelerometer
// ──────────────────────────────────────────────
inline constexpr float MPU6050_COUNTS_PER_G = 16384.0f;          // ±2 g full-scale (default)
inline constexpr unsigned long MOVEMENT_SAMPLE_INTERVAL_MS = 50; // 20 Hz sampling

// ──────────────────────────────────────────────
//  Kick / fetal-movement detection thresholds
//
//  A "kick" is registered when the deviation from the
//  running baseline acceleration is:
//      MOVEMENT_LOW_G  <  deviation  <  MOVEMENT_HIGH_G
//
//  Deviations below LOW_G are sensor noise / maternal
//  breathing. Deviations above HIGH_G are likely hard
//  external bumps (car, standing up, etc.) and are
//  rejected so they don't inflate the kick count.
// ──────────────────────────────────────────────
inline constexpr float MOVEMENT_LOW_G  = 0.015f;   // minimum deviation (g) to count as kick
inline constexpr float MOVEMENT_HIGH_G = 0.10f;    // maximum deviation (g) — above = external bump
inline constexpr unsigned long KICK_COOLDOWN_MS = 500;  // debounce between consecutive kicks
inline constexpr int SENSOR_VALUE_DECIMALS = 4;         // decimal places in serial prints

// ──────────────────────────────────────────────
//  No-movement (reduced fetal activity) alerts
// ──────────────────────────────────────────────
inline constexpr unsigned long NO_MOVEMENT_WARNING_MS  = 3600000UL;  // 60 min  → warning alert
inline constexpr unsigned long NO_MOVEMENT_CRITICAL_MS = 7200000UL;  // 120 min → critical alert
inline constexpr unsigned long ALERT_COOLDOWN_MS       = 300000UL;   // 5 min between repeated alerts