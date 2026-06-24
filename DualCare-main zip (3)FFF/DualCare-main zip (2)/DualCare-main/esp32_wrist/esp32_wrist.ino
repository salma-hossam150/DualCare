// ============================================================================
//  DualCare – ESP32 #1 Wrist Device  (esp32_wrist.ino)
//
//  Reads maternal vitals:
//    • MAX30102  – heart rate (HR) + blood-oxygen saturation (SpO2)
//    • MAX30205  – clinical-grade body temperature
//  Pushes sensor readings and threshold alerts to Firebase RTDB.
//
//  Library requirements (Arduino Library Manager / PlatformIO):
//    - FirebaseClient  (mobizt)
//    - SparkFun MAX3010x Pulse and Proximity Sensor Library
//    - ArduinoJson
// ============================================================================

#define ENABLE_LEGACY_TOKEN
#define ENABLE_DATABASE

#include "config.h"
#if defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#elif defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <WiFiClientSecureBearSSL.h>
using WiFiClientSecure = BearSSL::WiFiClientSecure;
#else
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif
#include <FirebaseClient.h>
#include <Wire.h>
#include "MAX30105.h"          // SparkFun MAX3010x library
#include "spo2_algorithm.h"    // SparkFun SpO2 / HR algorithm
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>

// ============================================================================
//  Global objects
// ============================================================================
MAX30105 max30102;

LegacyToken legacy_token(FIREBASE_AUTH_TOKEN);
FirebaseApp app;
WiFiClientSecure ssl_client;
using AsyncClient = AsyncClientClass;
AsyncClient async_client(ssl_client);
RealtimeDatabase Database;

bool max30102Available = false;
Max30102VitalsState max30102Vitals;

// Alert cooldown trackers (one per sensor class)
unsigned long lastHrAlertMs   = 0;
unsigned long lastSpo2AlertMs = 0;
unsigned long lastTempAlertMs = 0;

// ============================================================================
//  Forward declarations
// ============================================================================
void syncClockUtc();
String getUtcIsoTimestamp();
void addTimestampFields(JsonDocument &doc);
String makeEventId(const char *prefix);
void logFirebaseError(const char *context);

void initMax30102();
void updateMax30102Vitals();
void resetMax30102VitalsState();
void pushMax30102Sample(uint32_t red, uint32_t ir);
bool buildMax30102SampleWindow(uint32_t *irSamples, uint32_t *redSamples);

float readMax30205Temperature();
void buildSensorPayload(object_t &payload,
                        int32_t hrBpm, bool hrValid,
                        int32_t spo2Percent, bool spo2Valid,
                        float temperatureC);
bool writeVitalsAlert(const char *sensor, const char *level,
                      float value, float threshold);

// ============================================================================
//  setup()
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println(F("=== DualCare Wrist Device – ESP32 #1 ==="));

  // ----- I2C bus ------------------------------------------------------------
  Wire.begin();
  delay(100);
  Wire.setClock(400000);

  // ----- MAX30102 (HR + SpO2) -----------------------------------------------
  initMax30102();

  // ----- WiFi ---------------------------------------------------------------
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

  // ----- NTP clock sync -----------------------------------------------------
  syncClockUtc();

  // ----- Firebase (LegacyToken auth, insecure TLS for ESP32) ----------------
  ssl_client.setInsecure();

  initializeApp(async_client, app, getAuth(legacy_token));
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DB_URL);

  // ----- MAX30205 – no library init needed, raw I2C -------------------------
  Serial.print(F("MAX30205 temperature sensor at I2C 0x"));
  Serial.println(MAX30205_I2C_ADDR, HEX);

  Serial.println(F("Setup complete.\n"));
}

// ============================================================================
//  loop()
// ============================================================================
void loop() {
  static unsigned long lastSensorReadMs = 0;

  // Continuously feed the MAX30102 ring buffer
  updateMax30102Vitals();

  unsigned long now = millis();
  if (now - lastSensorReadMs < SENSOR_READ_INTERVAL_MS) return;
  lastSensorReadMs = now;

  // ---- Read MAX30205 temperature -------------------------------------------
  float temperatureC = readMax30205Temperature();
  Serial.print(F("MAX30205 temperature: "));
  Serial.print(temperatureC, 2);
  Serial.println(F(" °C"));

  // ---- Validate MAX30102 HR / SpO2 ----------------------------------------
  bool hrValid   = false;
  bool spo2Valid = false;

  if (max30102Available) {
    hrValid   = max30102Vitals.heartRateValid &&
                max30102Vitals.heartRate > 20 &&
                max30102Vitals.heartRate < 250;
    spo2Valid = max30102Vitals.spo2Valid &&
                max30102Vitals.spo2 >= 70 &&
                max30102Vitals.spo2 <= 100;

    if (hrValid && spo2Valid) {
      Serial.print(F("HR: "));
      Serial.print(max30102Vitals.heartRate);
      Serial.print(F(" bpm   SpO2: "));
      Serial.print(max30102Vitals.spo2);
      Serial.println(F("%"));
    } else {
      Serial.println(F("MAX30102: collecting samples for HR/SpO2..."));
    }

    // Update validity flags (clamp to valid range)
    max30102Vitals.heartRateValid = hrValid   ? 1 : 0;
    max30102Vitals.spo2Valid      = spo2Valid ? 1 : 0;
  }

  // ---- Push sensor payload to Firebase -------------------------------------
  if (app.ready()) {
    object_t payload;
    buildSensorPayload(payload,
                       max30102Vitals.heartRate, hrValid,
                       max30102Vitals.spo2,      spo2Valid,
                       temperatureC);

    if (!Database.update<object_t>(async_client, DB_SENSORS_PATH, payload)) {
      logFirebaseError("sensor update");
    }

    // ---- Threshold alerts --------------------------------------------------
    checkAndSendAlerts(hrValid, spo2Valid, temperatureC, now);
  }
}

// ============================================================================
//  Threshold alert logic
// ============================================================================
void checkAndSendAlerts(bool hrValid, bool spo2Valid,
                        float temperatureC, unsigned long now) {

  // Heart-rate alerts
  if (hrValid) {
    if (max30102Vitals.heartRate < HR_LOW_WARNING &&
        (now - lastHrAlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("heart_rate", "low",
                       (float)max30102Vitals.heartRate, (float)HR_LOW_WARNING);
      lastHrAlertMs = now;
    } else if (max30102Vitals.heartRate > HR_HIGH_WARNING &&
               (now - lastHrAlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("heart_rate", "high",
                       (float)max30102Vitals.heartRate, (float)HR_HIGH_WARNING);
      lastHrAlertMs = now;
    }
  }

  // SpO2 alerts
  if (spo2Valid) {
    if (max30102Vitals.spo2 < SPO2_CRITICAL &&
        (now - lastSpo2AlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("spo2", "critical",
                       (float)max30102Vitals.spo2, (float)SPO2_CRITICAL);
      lastSpo2AlertMs = now;
    } else if (max30102Vitals.spo2 < SPO2_LOW_WARNING &&
               (now - lastSpo2AlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("spo2", "low",
                       (float)max30102Vitals.spo2, (float)SPO2_LOW_WARNING);
      lastSpo2AlertMs = now;
    }
  }

  // Temperature alerts
  if (isfinite(temperatureC)) {
    if (temperatureC >= TEMP_FEVER_SEVERE &&
        (now - lastTempAlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("temperature", "severe_fever",
                       temperatureC, TEMP_FEVER_SEVERE);
      lastTempAlertMs = now;
    } else if (temperatureC >= TEMP_FEVER_MILD &&
               (now - lastTempAlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("temperature", "mild_fever",
                       temperatureC, TEMP_FEVER_MILD);
      lastTempAlertMs = now;
    } else if (temperatureC < TEMP_LOW_WARNING && temperatureC > 0.0f &&
               (now - lastTempAlertMs >= VITALS_ALERT_COOLDOWN_MS)) {
      writeVitalsAlert("temperature", "low",
                       temperatureC, TEMP_LOW_WARNING);
      lastTempAlertMs = now;
    }
  }
}

// ============================================================================
//  MAX30205 – Read temperature via raw I2C
// ============================================================================
float readMax30205Temperature() {
  Wire.beginTransmission(MAX30205_I2C_ADDR);
  Wire.write(MAX30205_REG_TEMPERATURE);
  if (Wire.endTransmission(false) != 0) {
    Serial.println(F("MAX30205 I2C error during register write."));
    return NAN;
  }

  uint8_t bytesReceived = Wire.requestFrom((uint8_t)MAX30205_I2C_ADDR, (uint8_t)2);
  if (bytesReceived != 2) {
    Serial.println(F("MAX30205 I2C error: did not receive 2 bytes."));
    return NAN;
  }

  uint8_t msb = Wire.read();
  uint8_t lsb = Wire.read();

  // Combine into a signed 16-bit value; LSB resolution = 0.00390625 °C
  int16_t raw = ((int16_t)msb << 8) | lsb;
  return raw * 0.00390625f;
}

// ============================================================================
//  MAX30102 – Initialisation
// ============================================================================
void initMax30102() {
  Serial.println(F("Testing MAX30102 connection..."));
  max30102Available = max30102.begin(Wire, I2C_SPEED_FAST);
  if (!max30102Available) {
    Serial.println(F("MAX30102 not found. Continuing without pulse sensor."));
    return;
  }

  max30102.setup(MAX30102_LED_BRIGHTNESS,
                 MAX30102_SAMPLE_AVERAGE,
                 MAX30102_LED_MODE,
                 MAX30102_SAMPLE_RATE,
                 MAX30102_PULSE_WIDTH,
                 MAX30102_ADC_RANGE);
  max30102.setPulseAmplitudeGreen(MAX30102_GREEN_PULSE_AMPLITUDE);
  resetMax30102VitalsState();
  Serial.println(F("MAX30102 connection successful."));
}

// ============================================================================
//  MAX30102 – Continuous vitals update (call every loop iteration)
// ============================================================================
void updateMax30102Vitals() {
  if (!max30102Available) return;

  max30102.check();

  while (max30102.available()) {
    pushMax30102Sample(max30102.getRed(), max30102.getIR());
    max30102.nextSample();
  }

  // Recalculate HR / SpO2 once enough new samples have arrived
  if (max30102Vitals.count >= MAX30102_VITALS_BUFFER_SIZE &&
      max30102Vitals.samplesSinceCalc >= MAX30102_RECALC_EVERY_SAMPLES) {

    uint32_t irSamples[MAX30102_VITALS_BUFFER_SIZE];
    uint32_t redSamples[MAX30102_VITALS_BUFFER_SIZE];

    if (!buildMax30102SampleWindow(irSamples, redSamples)) return;

    maxim_heart_rate_and_oxygen_saturation(
      irSamples,
      MAX30102_VITALS_BUFFER_SIZE,
      redSamples,
      &max30102Vitals.spo2,
      &max30102Vitals.spo2Valid,
      &max30102Vitals.heartRate,
      &max30102Vitals.heartRateValid);

    max30102Vitals.samplesSinceCalc = 0;
  }
}

// ============================================================================
//  MAX30102 – Ring-buffer helpers
// ============================================================================
void resetMax30102VitalsState() {
  max30102Vitals.head             = 0;
  max30102Vitals.count            = 0;
  max30102Vitals.samplesSinceCalc = 0;
  max30102Vitals.heartRate        = 0;
  max30102Vitals.heartRateValid   = 0;
  max30102Vitals.spo2             = 0;
  max30102Vitals.spo2Valid        = 0;
}

void pushMax30102Sample(uint32_t red, uint32_t ir) {
  max30102Vitals.redBuffer[max30102Vitals.head] = red;
  max30102Vitals.irBuffer[max30102Vitals.head]  = ir;
  max30102Vitals.head = (max30102Vitals.head + 1) % MAX30102_VITALS_BUFFER_SIZE;

  if (max30102Vitals.count < MAX30102_VITALS_BUFFER_SIZE) {
    max30102Vitals.count++;
  }

  max30102Vitals.samplesSinceCalc++;
}

bool buildMax30102SampleWindow(uint32_t *irSamples, uint32_t *redSamples) {
  if (max30102Vitals.count < MAX30102_VITALS_BUFFER_SIZE) return false;

  uint16_t index = max30102Vitals.head;
  for (int i = 0; i < MAX30102_VITALS_BUFFER_SIZE; i++) {
    irSamples[i]  = max30102Vitals.irBuffer[index];
    redSamples[i] = max30102Vitals.redBuffer[index];
    index = (index + 1) % MAX30102_VITALS_BUFFER_SIZE;
  }
  return true;
}

// ============================================================================
//  NTP / timestamp helpers  (identical pattern to imwhs_microctl)
// ============================================================================
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

// ============================================================================
//  Firebase helpers
// ============================================================================
void logFirebaseError(const char *context) {
   Serial.printf("Firebase error [%s]: msg: %s, code: %d\n",
                  context,
                  async_client.lastError().message().c_str(),
                  async_client.lastError().code());
}

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

// ============================================================================
//  Sensor payload builder
// ============================================================================
void buildSensorPayload(object_t &payload,
                        int32_t hrBpm, bool hrValid,
                        int32_t spo2Percent, bool spo2Valid,
                        float temperatureC) {
  DynamicJsonDocument doc(384);

  if (hrValid)   doc["heart_rate"] = hrBpm;
  if (spo2Valid) doc["spo2"]       = spo2Percent;
  if (isfinite(temperatureC)) doc["temperature"] = temperatureC;
  addTimestampFields(doc);

  if (doc.overflowed()) {
    Serial.println(F("WARNING: sensor payload JSON truncated, increase doc capacity"));
  }

  String json;
  serializeJson(doc, json);
  payload = object_t(json);
}

// ============================================================================
//  Vitals alert writer  →  /alert_events/<eventId>
// ============================================================================
bool writeVitalsAlert(const char *sensor, const char *level,
                      float value, float threshold) {
  String eventId = makeEventId(sensor);
  String path    = String(DB_ALERT_EVENTS_PATH) + "/events/" + eventId;

  DynamicJsonDocument doc(256);
  doc["event_id"]   = eventId;
  doc["sensor"]     = sensor;
  doc["level"]      = level;
  doc["value"]      = value;
  doc["threshold"]  = threshold;
  doc["source"]     = "wrist";
  addTimestampFields(doc);

  if (doc.overflowed()) {
    Serial.println(F("WARNING: alert event JSON truncated, increase doc capacity"));
  }

  String json;
  serializeJson(doc, json);
  object_t alertPayload(json);

  bool eventStored = Database.set<object_t>(async_client, path, alertPayload);
  if (!eventStored) logFirebaseError("vitals alert store");

  // Update the /alert_events/latest/<sensor> pointer
  String latestPath = String(DB_ALERT_EVENTS_PATH) + "/latest/" + String(sensor);
  bool latestUpdated = Database.set<string_t>(async_client, latestPath, string_t(eventId));
  if (!latestUpdated) logFirebaseError("vitals alert latest");

  Serial.print(F("Alert sent: "));
  Serial.print(sensor);
  Serial.print(F(" ["));
  Serial.print(level);
  Serial.print(F("] value="));
  Serial.print(value, 1);
  Serial.print(F("  threshold="));
  Serial.println(threshold, 1);

  return eventStored && latestUpdated;
}
