// CO326 Project #6 — Conveyor Belt Fault Detection
// Hardware: ESP32-S3 + MPU-6050 + Relay

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <math.h>

// ╔══════════════════════════════════════════════════════╗
// ║  CHANGE THESE 5 VALUES BEFORE UPLOADING              ║
// ╚══════════════════════════════════════════════════════╝
const char* WIFI_SSID   = "YourWiFiName";
const char* WIFI_PASS   = "YourWiFiPassword";
const char* MQTT_BROKER = "192.168.x.x";
const char* MQTT_USER   = "esp32user";
const char* MQTT_PASS   = "esp32password";
// ═══════════════════════════════════════════════════════

// MQTT topic paths — Sparkplug B Unified Namespace
const char* T_DATA  = "spBv1.0/conveyorLine/DDATA/belt01/beltDrive";
const char* T_CMD   = "spBv1.0/conveyorLine/DCMD/belt01/beltDrive";
const char* T_BIRTH = "spBv1.0/conveyorLine/NBIRTH/belt01";
const char* T_DEATH = "spBv1.0/conveyorLine/NDEATH/belt01";

#define RELAY_PIN  10
#define MPU_ADDR   0x68

// MPU6050 Register addresses
#define PWR_MGMT_1   0x6B
#define ACCEL_XOUT_H 0x3B
#define TEMP_OUT_H   0x41
#define ACCEL_CONFIG 0x1C  // for ±8g range

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

// Baseline statistics
float base_mean = 0, base_std = 1;
float base_y_mean = 0, base_y_std = 1;
bool  calibrated = false;
int   consecutive_faults = 0;

unsigned long startTime = 0;

// ── Raw MPU6050 helpers ──────────────────────────────────
void writeReg(byte reg, byte value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

int16_t readWord(byte reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2);
  return (Wire.read() << 8) | Wire.read();
}

void mpuBegin() {
  Wire.begin(21, 22);
  delay(100);

  // Check WHO_AM_I
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  byte whoami = Wire.read();
  Serial.printf("WHO_AM_I: 0x%02X\n", whoami);

  writeReg(PWR_MGMT_1, 0x00);    // Wake up, use internal 8MHz oscillator
  delay(100);
  writeReg(ACCEL_CONFIG, 0x10);  // ±8g range (matches original code)
  delay(10);

  Serial.println("MPU6050 raw init done.");
}

// ±8g range → 4096 LSB/g → multiply by 9.81 for m/s²
void mpuRead(float &x, float &y, float &z, float &tempC) {
  int16_t ax = readWord(ACCEL_XOUT_H);
  int16_t ay = readWord(ACCEL_XOUT_H + 2);
  int16_t az = readWord(ACCEL_XOUT_H + 4);
  int16_t rt = readWord(TEMP_OUT_H);

  x     = (ax / 4096.0) * 9.81;
  y     = (ay / 4096.0) * 9.81;
  z     = (az / 4096.0) * 9.81;
  tempC = (rt / 340.0) + 36.53;
}

// ── Fault simulation phases ──────────────────────────────
void applyFaultPhase(float &x, float &y, float &z, unsigned long t) {
  if      (t < 60000)  { /* normal */ }
  else if (t < 90000)  {
    if (random(10) < 3) { x += random(5,15)/10.0; z += random(3,10)/10.0; }
  }
  else if (t < 120000) {
    if ((t / 500) % 4 == 0) { x += 1.5; z += 1.2; }
  }
  else if (t < 150000) { y += 1.8; }
  else if (t < 165000) { x *= 0.05; y *= 0.05; z *= 0.05; }
}

// ── Calibration ──────────────────────────────────────────
void calibrate() {
  float sumMag=0, sumMag2=0, sumY=0, sumY2=0;
  Serial.println("Calibrating — keep sensor still...");
  for (int i = 0; i < 100; i++) {
    float x, y, z, t;
    mpuRead(x, y, z, t);
    float mag = sqrt(x*x + y*y + z*z);
    sumMag  += mag;  sumMag2 += mag*mag;
    sumY    += y;    sumY2   += y*y;
    delay(20);
  }
  base_mean   = sumMag  / 100.0;
  base_std    = sqrt(sumMag2/100.0 - base_mean*base_mean);
  base_y_mean = sumY    / 100.0;
  base_y_std  = sqrt(sumY2/100.0  - base_y_mean*base_y_mean);
  if (base_std   < 0.01) base_std   = 0.01;
  if (base_y_std < 0.01) base_y_std = 0.01;
  calibrated = true;
  Serial.println("Calibration complete.");
}

// ── MQTT callback ─────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, msg)) {
    String cmd = doc["relay_command"] | "";
    if (cmd == "ON")  { digitalWrite(RELAY_PIN, HIGH); Serial.println("RELAY ON  — belt stopped"); }
    if (cmd == "OFF") { digitalWrite(RELAY_PIN, LOW);  Serial.println("RELAY OFF — belt resumed"); }
  }
}

// ── WiFi ──────────────────────────────────────────────────
void connectWiFi() {
  Serial.print("WiFi connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" OK. IP: " + WiFi.localIP().toString());
}

// ── MQTT ──────────────────────────────────────────────────
void connectMQTT() {
  mqttClient.setServer(MQTT_BROKER, 1883);
  mqttClient.setCallback(mqttCallback);
  while (!mqttClient.connected()) {
    Serial.print("MQTT connecting...");
    if (mqttClient.connect("beltDrive", MQTT_USER, MQTT_PASS,
                           T_DEATH, 0, true, "OFFLINE")) {
      mqttClient.publish(T_BIRTH, "{\"status\":\"ONLINE\"}", true);
      mqttClient.subscribe(T_CMD);
      Serial.println(" connected.");
    } else {
      Serial.print(" failed rc="); Serial.println(mqttClient.state());
      delay(3000);
    }
  }
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  mpuBegin();
  connectWiFi();
  connectMQTT();
  calibrate();
  startTime = millis();
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  float x, y, z, tempC;
  mpuRead(x, y, z, tempC);

  unsigned long elapsed = millis() - startTime;
  applyFaultPhase(x, y, z, elapsed);

  float mag   = sqrt(x*x + y*y + z*z);
  float z_mag = (mag - base_mean) / base_std;
  float z_y   = fabs((y - base_y_mean) / base_y_std);
  bool  stall = (mag < base_mean * 0.2 && calibrated);

  float score = 0;
  if (stall)        score = 0.95;
  else if (z_y > 3) score = min(0.9f, z_y / 10.0f);
  else              score = min(1.0f, max(0.0f, z_mag / 10.0f));

  int fault_flag = (score > 0.3) ? 1 : 0;

  String fault_type = "normal";
  if      (stall)        fault_type = "belt_jam";
  else if (z_y > 3)      fault_type = "misalignment";
  else if (score > 0.6)  fault_type = "roller_fault";
  else if (score > 0.3)  fault_type = "belt_slip";

  if (fault_flag) consecutive_faults++;
  else consecutive_faults = 0;
  if (consecutive_faults >= 5) {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("AUTO-STOP triggered");
  }

  StaticJsonDocument<256> doc;
  doc["x"]             = round(x*1000)/1000.0;
  doc["y"]             = round(y*1000)/1000.0;
  doc["z"]             = round(z*1000)/1000.0;
  doc["magnitude"]     = round(mag*1000)/1000.0;
  doc["anomaly_score"] = round(score*1000)/1000.0;
  doc["fault_flag"]    = fault_flag;
  doc["fault_type"]    = fault_type;
  doc["relay_state"]   = digitalRead(RELAY_PIN);
  doc["elapsed_s"]     = elapsed / 1000;

  char buf[256];
  serializeJson(doc, buf);
  mqttClient.publish(T_DATA, buf);
  Serial.println(buf);

  delay(500);
}