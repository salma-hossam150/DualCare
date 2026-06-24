#pragma once

// ============================================================================
//  DualCare – ESP32 #1 Wrist Device  (config.h)
//  Maternal vitals: MAX30102 (HR + SpO2) + MAX30205 (body temperature)
// ============================================================================

// ---------------------------------------------------------------------------
//  General timing
// ---------------------------------------------------------------------------
inline constexpr unsigned long SENSOR_READ_INTERVAL_MS = 2000;

// ---------------------------------------------------------------------------
//  Firebase Realtime Database
// ---------------------------------------------------------------------------
inline const char* DB_URL              = "https://dualcare2-88208-default-rtdb.europe-west1.firebasedatabase.app/";
inline const char* DB_SENSORS_PATH     = "/sensors/wrist";
inline const char* DB_ALERT_EVENTS_PATH = "/alert_events";
inline const char* FIREBASE_AUTH_TOKEN = "ZO7HvLMG0UlIsbseouBL65AyEVlPRaKWtuTd4Q8Q";
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
//  WiFi credentials
// ---------------------------------------------------------------------------
inline const char* WIFI_SSID = "Judy";
inline const char* WIFI_PASS = "jude777778";

// ---------------------------------------------------------------------------
//  MAX30102 – Pulse oximeter / heart-rate sensor
// ---------------------------------------------------------------------------
inline constexpr uint8_t  MAX30102_LED_BRIGHTNESS       = 60;
inline constexpr uint8_t  MAX30102_SAMPLE_AVERAGE        = 4;
inline constexpr uint8_t  MAX30102_LED_MODE              = 2;    // Red + IR
inline constexpr uint8_t  MAX30102_SAMPLE_RATE           = 100;
inline constexpr int      MAX30102_PULSE_WIDTH           = 411;
inline constexpr int      MAX30102_ADC_RANGE             = 4096;
inline constexpr uint8_t  MAX30102_GREEN_PULSE_AMPLITUDE = 0;
inline constexpr int      MAX30102_VITALS_BUFFER_SIZE    = 100;
inline constexpr int      MAX30102_RECALC_EVERY_SAMPLES  = 25;

// ---------------------------------------------------------------------------
//  Heart-rate & SpO2 alert thresholds
// ---------------------------------------------------------------------------
inline constexpr int32_t  HR_LOW_WARNING       = 50;
inline constexpr int32_t  HR_HIGH_WARNING      = 120;
inline constexpr int32_t  SPO2_LOW_WARNING     = 94;
inline constexpr int32_t  SPO2_CRITICAL        = 90;

// Alert cooldown – one alert per sensor per 5 minutes
inline constexpr unsigned long VITALS_ALERT_COOLDOWN_MS = 300000UL;

// ---------------------------------------------------------------------------
//  MAX30205 – Clinical-grade body temperature sensor (I2C)
// ---------------------------------------------------------------------------
inline constexpr uint8_t  MAX30205_I2C_ADDR        = 0x48;
inline constexpr uint8_t  MAX30205_REG_TEMPERATURE = 0x00;
inline constexpr float    TEMP_LOW_WARNING         = 36.0f;   // hypothermia
inline constexpr float    TEMP_FEVER_MILD          = 37.8f;   // low-grade fever
inline constexpr float    TEMP_FEVER_SEVERE        = 39.0f;   // high fever

// ---------------------------------------------------------------------------
//  MAX30102 circular-buffer state (shared between .ino helpers)
// ---------------------------------------------------------------------------
struct Max30102VitalsState {
  uint32_t irBuffer[MAX30102_VITALS_BUFFER_SIZE];
  uint32_t redBuffer[MAX30102_VITALS_BUFFER_SIZE];
  uint16_t head             = 0;
  uint16_t count            = 0;
  uint16_t samplesSinceCalc = 0;
  int32_t  heartRate        = 0;
  int8_t   heartRateValid   = 0;
  int32_t  spo2             = 0;
  int8_t   spo2Valid        = 0;
};
