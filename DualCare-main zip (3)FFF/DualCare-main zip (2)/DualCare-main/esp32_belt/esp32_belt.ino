// ============================================================
//  DualCare — ESP32 #2  (Belt / Fetal-Movement Monitor)
//
//  Reads an MPU6050 accelerometer strapped to the maternal
//  abdomen, detects fetal kicks via deviation from a running
//  baseline, and pushes kick counts + alerts to Firebase RTDB.
//
//  Hardware : ESP32 Dev Module + MPU6050 (I2C @ 0x68)
//  Libraries: FirebaseClient (LegacyToken), ArduinoJson,
//             MPU6050 (jrowberg), Wire
// ============================================================

#define ENABLE_LEGACY_TOKEN
#define ENABLE_DATABASE

#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <Wire.h>
#include <MPU6050.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// ── Sensor ───────────────────────────────────────────────────
MPU6050 mpu;
bool mpuAvailable = false;

// ── Firebase plumbing ────────────────────────────────────────
LegacyToken legacy_token(FIREBASE_AUTH_TOKEN);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient async_client(ssl_client);
RealtimeDatabase Database;

// ── Kick detection state ─────────────────────────────────────
int      kickCount          = 0;       // daily kick counter
String   lastKickTimeUtc    = "";      // ISO-8601 string of last kick
unsigned long lastKickMs    = 0;       // millis() of last kick (for no-movement timer)
float    baselineG          = 1.0f;    // exponential moving average of accel magnitude
float    latestMagnitudeG   = 1.0f;    // most recent reading (for telemetry)

// ── No-movement alert bookkeeping ────────────────────────────
bool     warningSent        = false;   // true once 60-min warning has been sent
bool     criticalSent       = false;   // true once 120-min critical has been sent
unsigned long lastAlertMs   = 0;       // millis() of last alert dispatch

// ── Daily-reset tracking ─────────────────────────────────────
int      currentUtcDay      = -1;      // day-of-month used for midnight reset

// ── Forward declarations ─────────────────────────────────────
void  syncClockUtc();
String getUtcIsoTimestamp();
void  addTimestampFields(JsonDocument &doc);
String makeEventId(const char *prefix);
float  readAccelerationMagnitudeG();
void  updateKickDetector(float magnitudeG);
void  pushSensorData();
void  checkNoMovementAlerts();
void  checkDailyReset();
bool  writeAlertEvent(const char *level, const char *message);
void  logFirebaseError(const char *context);

// =============================================================
//  setup()
// =============================================================
void setup() {
  Serial.begin(115200);

  // ── I2C + MPU6050 ──────────────────────────────────────────
  Wire.begin();
  delay(100);
  Wire.setClock(400000);

  mpu.initialize();
  mpu.setSleepEnabled(false);

  Serial.println(F("Testing MPU6050 connection..."));
  mpuAvailable = mpu.testConnection();
  if (!mpuAvailable) {
    Serial.println(F("MPU6050 connection failed. Continuing without accelerometer."));
  } else {
    Serial.println(F("MPU6050 connection successful."));
  }

  // ── WiFi ───────────────────────────────────────────────────
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print(F("Connecting to Wi-Fi"));
  unsigned long wifiStartMs = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStartMs >= 15000UL) break;
    Serial.print(F("."));
    delay(300);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("IP: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("Wi-Fi connection failed. Continuing offline."));
  }

  // ── NTP clock sync ─────────────────────────────────────────
  syncClockUtc();

  // ── Firebase init ──────────────────────────────────────────
  ssl_client.setInsecure();

  initializeApp(async_client, app, getAuth(legacy_token));
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DB_URL);

  // ── Seed timers ────────────────────────────────────────────
  lastKickMs = millis();   // start the no-movement clock from boot

  Serial.println(F("Belt firmware ready."));
}

// =============================================================
//  loop()
// =============================================================
void loop() {
  static unsigned long lastSampleMs     = 0;
  static unsigned long lastFirebasePushMs = 0;

  unsigned long now = millis();

  // ── 1. High-frequency accelerometer sampling (20 Hz) ───────
  if (now - lastSampleMs >= MOVEMENT_SAMPLE_INTERVAL_MS) {
    lastSampleMs = now;

    float mag = readAccelerationMagnitudeG();
    if (isfinite(mag)) {
      latestMagnitudeG = mag;
      updateKickDetector(mag);
    }
  }

  // ── 2. Periodic Firebase push (every 2 s) ─────────────────
  if (now - lastFirebasePushMs >= SENSOR_READ_INTERVAL_MS) {
    lastFirebasePushMs = now;

    checkDailyReset();
    pushSensorData();
    checkNoMovementAlerts();
  }
}

// =============================================================
//  Kick Detection — Exponential Moving Average + Band Filter
//
//  At rest the accelerometer reads ≈ 1 g.  A kick produces a
//  small, brief deviation.  We keep a slow-moving baseline via
//  an EMA (α = 0.02) and compare the instantaneous magnitude
//  against it.
//
//  Only deviations inside [LOW_G .. HIGH_G] are counted:
//    • Below LOW_G  → sensor noise / breathing artefact
//    • Above HIGH_G → external bump (car, standing up)
// =============================================================
void updateKickDetector(float magnitudeG) {
  // Update exponential moving average (slow α ⇒ stable baseline)
  constexpr float EMA_ALPHA = 0.02f;
  baselineG = (EMA_ALPHA * magnitudeG) + ((1.0f - EMA_ALPHA) * baselineG);

  float deviation = fabsf(magnitudeG - baselineG);

  // Band-pass filter: only count mid-range deviations as kicks
  if (deviation > MOVEMENT_LOW_G && deviation < MOVEMENT_HIGH_G) {
    unsigned long now = millis();
    if (now - lastKickMs >= KICK_COOLDOWN_MS) {
      kickCount++;
      lastKickMs = now;
      lastKickTimeUtc = getUtcIsoTimestamp();
      if (lastKickTimeUtc.length() == 0) {
        lastKickTimeUtc = String("UPTIME_MS_") + String(now);
      }

      // Reset no-movement alert flags on every new kick
      warningSent  = false;
      criticalSent = false;

      Serial.print(F("Kick #"));
      Serial.print(kickCount);
      Serial.print(F("  deviation="));
      Serial.print(deviation, SENSOR_VALUE_DECIMALS);
      Serial.print(F(" g  time="));
      Serial.println(lastKickTimeUtc);
    }
  }
}

// =============================================================
//  Push latest sensor data to Firebase
// =============================================================
void pushSensorData() {
  if (!app.ready()) return;

  DynamicJsonDocument doc(256);

  doc["kick_count"]      = kickCount;
  doc["last_kick_time"]  = lastKickTimeUtc.length() > 0 ? lastKickTimeUtc : "none";
  doc["accel_magnitude"] = serialized(String(latestMagnitudeG, SENSOR_VALUE_DECIMALS));
  addTimestampFields(doc);

  if (doc.overflowed()) {
    Serial.println(F("WARNING: sensor payload JSON truncated, increase doc capacity"));
  }

  String json;
  serializeJson(doc, json);
  object_t payload(json);

  if (!Database.update<object_t>(async_client, DB_SENSORS_PATH, payload)) {
    logFirebaseError("sensor update");
  }
}

// =============================================================
//  No-movement (reduced fetal activity) alerts
// =============================================================
void checkNoMovementAlerts() {
  if (!app.ready()) return;

  unsigned long elapsed = millis() - lastKickMs;

  // ── Critical (120 min) ─────────────────────────────────────
  if (!criticalSent && elapsed >= NO_MOVEMENT_CRITICAL_MS) {
    unsigned long now = millis();
    if (now - lastAlertMs >= ALERT_COOLDOWN_MS) {
      if (writeAlertEvent("critical", "No fetal movement detected for 120 minutes")) {
        criticalSent = true;
        lastAlertMs  = now;
        Serial.println(F("ALERT [critical]: no movement for 120 min"));
      }
    }
    return;  // skip warning if critical already qualifies
  }

  // ── Warning (60 min) ──────────────────────────────────────
  if (!warningSent && elapsed >= NO_MOVEMENT_WARNING_MS) {
    unsigned long now = millis();
    if (now - lastAlertMs >= ALERT_COOLDOWN_MS) {
      if (writeAlertEvent("warning", "No fetal movement detected for 60 minutes")) {
        warningSent = true;
        lastAlertMs = now;
        Serial.println(F("ALERT [warning]: no movement for 60 min"));
      }
    }
  }
}

// =============================================================
//  Daily kick-count reset (at UTC midnight)
// =============================================================
void checkDailyReset() {
  time_t now = time(nullptr);
  if (now < 1700000000) return;  // clock not synced yet

  struct tm utcTm;
  gmtime_r(&now, &utcTm);

  if (currentUtcDay < 0) {
    // First run — seed the day tracker
    currentUtcDay = utcTm.tm_mday;
    return;
  }

  if (utcTm.tm_mday != currentUtcDay) {
    Serial.print(F("New UTC day detected ("));
    Serial.print(currentUtcDay);
    Serial.print(F(" → "));
    Serial.print(utcTm.tm_mday);
    Serial.println(F("). Resetting kick count."));

    kickCount       = 0;
    lastKickTimeUtc = "";
    currentUtcDay   = utcTm.tm_mday;
  }
}

// =============================================================
//  Write an alert event to /alert_events
// =============================================================
bool writeAlertEvent(const char *level, const char *message) {
  String eventId = makeEventId("belt");
  String path    = String(DB_ALERT_EVENTS_PATH) + "/events/" + eventId;

  DynamicJsonDocument doc(256);

  doc["event_id"]   = eventId;
  doc["source"]     = "esp32_belt";
  doc["level"]      = level;
  doc["message"]    = message;
  doc["kick_count"] = kickCount;
  addTimestampFields(doc);

  if (doc.overflowed()) {
    Serial.println(F("WARNING: alert event payload JSON truncated, increase doc capacity"));
  }

  String json;
  serializeJson(doc, json);
  object_t payload(json);

  bool eventStored = Database.set<object_t>(async_client, path, payload);
  if (!eventStored) logFirebaseError("alert event store");

  bool latestUpdated = Database.set<string_t>(
      async_client,
      String(DB_ALERT_EVENTS_PATH) + "/latest",
      string_t(eventId));
  if (!latestUpdated) logFirebaseError("alert event latest");

  return eventStored && latestUpdated;
}

// =============================================================
//  Accelerometer read  (raw → g magnitude)
// =============================================================
float readAccelerationMagnitudeG() {
  if (!mpuAvailable) return NAN;

  int16_t ax = 0, ay = 0, az = 0;
  mpu.getAcceleration(&ax, &ay, &az);

  float x = static_cast<float>(ax) / MPU6050_COUNTS_PER_G;
  float y = static_cast<float>(ay) / MPU6050_COUNTS_PER_G;
  float z = static_cast<float>(az) / MPU6050_COUNTS_PER_G;

  return sqrtf((x * x) + (y * y) + (z * z));
}

// =============================================================
//  UTC helpers
// =============================================================
void syncClockUtc() {
  configTime(0, 0, "pool.ntp.org", "time.google.com");

  Serial.print(F("Syncing UTC clock"));
  time_t now = time(nullptr);
  uint8_t attempts = 0;
  while (now < 1700000000 && attempts < 20) {
    Serial.print(F("."));
    delay(250);
    now = time(nullptr);
    attempts++;
  }
  Serial.println();

  if (now < 1700000000) {
    Serial.println(F("UTC sync failed; timestamps will use uptime fallback."));
  } else {
    Serial.println(F("UTC clock synced."));
  }
}

String getUtcIsoTimestamp() {
  time_t now = time(nullptr);
  if (now < 1700000000) return String();

  struct tm utcTm;
  gmtime_r(&now, &utcTm);
  char buffer[25];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTm);
  return String(buffer);
}

void addTimestampFields(JsonDocument &doc) {
  String timestampUtc = getUtcIsoTimestamp();
  bool synced = timestampUtc.length() > 0;
  if (!synced) {
    timestampUtc = String("UNSYNCED_UPTIME_MS_") + String(millis());
  }
  doc["timestamp_utc"]        = timestampUtc;
  doc["timestamp_utc_synced"] = synced;
}

// =============================================================
//  Event ID generator
// =============================================================
String makeEventId(const char *prefix) {
  String timestampUtc = getUtcIsoTimestamp();
  if (timestampUtc.length() > 0) {
    String sanitized = timestampUtc;
    sanitized.replace("-", "");
    sanitized.replace(":", "");
    sanitized.replace("T", "_");
    sanitized.replace("Z", "");
    return String(prefix) + "_utc_" + sanitized + "_" + String(millis());
  }
  return String(prefix) + "_boot_" + String(millis());
}

// =============================================================
//  Firebase error logger
// =============================================================
void logFirebaseError(const char *context) {
  Serial.printf.printf("Firebase error [%s]: msg: %s, code: %d\n",
                  context,
                  async_client.lastError().message().c_str(),
                  async_client.lastError().code());
}
