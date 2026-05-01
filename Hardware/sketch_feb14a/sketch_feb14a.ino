#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>           


#define HARDWARE_MODE false

const char* WIFI_SSID   = "Dialog 4G 767";
const char* WIFI_PASS   = "B4651Ff3";
const char* MQTT_BROKER = "192.168.8.123";
const char* MQTT_USER   = "esp32user";
const char* MQTT_PASS   = "password";


//MQTT topic paths 
const char* T_DATA  = "spBv1.0/conveyorLine/DDATA/belt01/beltDrive";
const char* T_CMD   = "spBv1.0/conveyorLine/DCMD/belt01/beltDrive";
const char* T_BIRTH = "spBv1.0/conveyorLine/NBIRTH/belt01";
const char* T_DEATH = "spBv1.0/conveyorLine/NDEATH/belt01";

#define RELAY_PIN     4
#define RELAY_STOP    LOW     
#define RELAY_RUN     HIGH    
#define LED_PIN       2      
#define I2C_SDA      21     
#define I2C_SCL      22      
#define MPU_ADDR   0x68


#define PWR_MGMT_1   0x6B
#define ACCEL_XOUT_H 0x3B
#define TEMP_OUT_H   0x41
#define ACCEL_CONFIG 0x1C  
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

//Baseline statistics
float base_mean = 0, base_std = 1;
float base_y_mean = 0, base_y_std = 1;
bool  calibrated = false;
int   consecutive_faults = 0;

unsigned long startTime = 0;

//NTP Configuration 
const char* NTP_SERVER = "pool.ntp.org";
const long  GMT_OFFSET = 0;       
const int   DST_OFFSET = 0;


#define BUFFER_SIZE 50
struct SensorReading {
  float x, y, z, mag, tempC, score;
  int fault_flag, relay_state, consec;
  String fault_type, mode;
  unsigned long elapsed;
  char timestamp[30];
};
SensorReading buffer[BUFFER_SIZE];
int bufHead = 0, bufCount = 0;

//Raw MPU6050 helpers(adafruit library doesnt work properly)
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
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(100);

  //Check WHO_AM_I
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  byte whoami = Wire.read();
  Serial.printf("WHO_AM_I: 0x%02X\n", whoami);

  if (whoami != 0x68 && whoami != 0x70 && whoami != 0x71 && whoami != 0x73) {
    Serial.println("WARNING: MPU6050 not detected! Check wiring.");
  } else {
    Serial.println("MPU sensor detected successfully.");
  }

  writeReg(PWR_MGMT_1, 0x00);    
  delay(100);
  writeReg(ACCEL_CONFIG, 0x10);  
  delay(10);

  Serial.println("MPU6050 raw init done.");
}

//±8g range → 4096 LSB/g → multiply by 9.81 for m/s²
void mpuRead(float &x, float &y, float &z, float &tempC) {
  static unsigned long lastConfig = 0;
  if (millis() - lastConfig > 5000) {
    writeReg(ACCEL_CONFIG, 0x10);
    lastConfig = millis();
  }

  int16_t ax = readWord(ACCEL_XOUT_H);
  int16_t ay = readWord(ACCEL_XOUT_H + 2);
  int16_t az = readWord(ACCEL_XOUT_H + 4);
  int16_t rt = readWord(TEMP_OUT_H);

  x     = (ax / 4096.0) * 9.81;
  y     = (ay / 4096.0) * 9.81;
  z     = (az / 4096.0) * 9.81;
  tempC = (rt / 340.0) + 36.53;
}

// Fault simulation with GRADUAL RAMP-UP for RUL demonstration
//
// Phase timeline (200s cycle):
//   0–40s    : Normal operation
//   40–80s   : belt_slip     (15s ramp + 25s sustained)
//   80–120s  : roller_fault  (15s ramp + 25s sustained) → triggers AUTO-STOP
//   120–160s : misalignment  (15s ramp + 25s sustained)
//   160–200s : belt_jam      (15s ramp + 25s sustained)
//   200s+    : Cycle restarts

void applyFaultPhase(float &x, float &y, float &z, unsigned long t) {
  t = t % 200000;

  #define RAMP_MS 15000  // 15 seconds to ramp from 0% to 100%

  if (t < 40000) {
    // Normal no injection
  }
  else if (t < 80000) {
    // belt_slip: gradual vibration increase
    unsigned long phase_t = t - 40000;
    float progress = (phase_t < RAMP_MS) ? (float)phase_t / RAMP_MS : 1.0;
    x += progress * (0.8 + random(0,5)/10.0);
    z += progress * (0.5 + random(0,3)/10.0);
  }
  else if (t < 120000) {
    // roller_fault: gradual severe vibration
    unsigned long phase_t = t - 80000;
    float progress = (phase_t < RAMP_MS) ? (float)phase_t / RAMP_MS : 1.0;
    x += progress * (1.8 + random(0,5)/10.0);
    z += progress * (1.5 + random(0,3)/10.0);
  }
  else if (t < 160000) {
    // misalignment: gradual Y-axis drift
    unsigned long phase_t = t - 120000;
    float progress = (phase_t < RAMP_MS) ? (float)phase_t / RAMP_MS : 1.0;
    y += progress * 1.8;
  }
  else if (t < 200000) {
    // belt_jam: gradual signal drop
    unsigned long phase_t = t - 160000;
    float progress = (phase_t < RAMP_MS) ? (float)phase_t / RAMP_MS : 1.0;
    float dampen = 1.0 - (progress * 0.95);  // Goes from 1.0 down to 0.05
    x *= dampen; y *= dampen; z *= dampen;
  }
}

// Calibration
//Takes 100 samples over ~2 seconds to establish baseline
void calibrate() {
  float sumMag=0, sumMag2=0, sumY=0, sumY2=0;
  Serial.println("Calibrating — keep sensor still...");

  for (int i = 0; i < 100; i++) {
    float x, y, z, t;
    mpuRead(x, y, z, t);
    float mag = sqrt(x*x + y*y + z*z);
    sumMag  += mag;  sumMag2 += mag*mag;
    sumY    += y;    sumY2   += y*y;
    digitalWrite(LED_PIN, (i % 10 < 5) ? HIGH : LOW);
    delay(20);
  }

  base_mean   = sumMag  / 100.0;
  base_std    = sqrt(sumMag2/100.0 - base_mean*base_mean);
  base_y_mean = sumY    / 100.0;
  base_y_std  = sqrt(sumY2/100.0  - base_y_mean*base_y_mean);

  if (base_std   < 0.02) base_std   = 0.02;
  if (base_y_std < 0.05) base_y_std = 0.05;

  calibrated = true;
  digitalWrite(LED_PIN, HIGH); 
  Serial.printf("Calibration complete. base_mean=%.3f base_std=%.3f\n", base_mean, base_std);
  Serial.printf("  base_y_mean=%.3f base_y_std=%.3f\n", base_y_mean, base_y_std);
}

// MQTT callback (receive relay commands)
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  String msg = "";
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  Serial.print("CMD received: "); Serial.println(msg);

  StaticJsonDocument<128> doc;
  if (!deserializeJson(doc, msg)) {
    String cmd = doc["relay_command"] | "";
    if (cmd == "ON")  {
      digitalWrite(RELAY_PIN, RELAY_STOP);
      Serial.println("RELAY ON  — belt stopped");
    }
    if (cmd == "OFF") {
      digitalWrite(RELAY_PIN, RELAY_RUN);
      consecutive_faults = 0;  
      Serial.println("RELAY OFF — belt resumed");
    }
  }
}

//WiFi Connection
void connectWiFi() {
  Serial.print("WiFi connecting to ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts > 40) { 
      Serial.println("\nWiFi FAILED — restarting...");
      ESP.restart();
    }
  }
  Serial.println(" OK. IP: " + WiFi.localIP().toString());
}

//NTP Time Sync
void syncNTP() {
  configTime(GMT_OFFSET, DST_OFFSET, NTP_SERVER);
  Serial.print("NTP syncing");
  int tries = 0;
  struct tm ti;
  while (!getLocalTime(&ti) && tries < 10) {
    Serial.print(".");
    delay(500);
    tries++;
  }
  if (tries < 10) {
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &ti);
    Serial.print(" OK: "); Serial.println(buf);
  } else {
    Serial.println(" FAILED (will use millis)");
  }
}

//Get timestamp 
void getTimestamp(char* buf, size_t len) {
  struct tm ti;
  if (getLocalTime(&ti)) {
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &ti);
  } else {
    snprintf(buf, len, "1970-01-01T00:00:00Z");
  }
}

//Flush circular buffer (send stored readings)
void flushBuffer() {
  if (bufCount == 0) return;
  Serial.printf("Flushing %d buffered readings...\n", bufCount);
  int idx = (bufHead - bufCount + BUFFER_SIZE) % BUFFER_SIZE;
  for (int i = 0; i < bufCount; i++) {
    SensorReading &r = buffer[idx];
    StaticJsonDocument<512> doc;
    doc["x"]             = round(r.x*1000)/1000.0;
    doc["y"]             = round(r.y*1000)/1000.0;
    doc["z"]             = round(r.z*1000)/1000.0;
    doc["magnitude"]     = round(r.mag*1000)/1000.0;
    doc["temperature"]   = round(r.tempC*100)/100.0;
    doc["anomaly_score"] = round(r.score*1000)/1000.0;
    doc["fault_flag"]    = r.fault_flag;
    doc["fault_type"]    = r.fault_type;
    doc["relay_state"]   = r.relay_state;
    doc["elapsed_s"]     = r.elapsed / 1000;
    doc["consecutive_faults"] = r.consec;
    doc["mode"]          = r.mode;
    doc["timestamp"]     = r.timestamp;
    doc["buffered"]      = true;
    char out[512];
    serializeJson(doc, out);
    mqttClient.publish(T_DATA, out);
    idx = (idx + 1) % BUFFER_SIZE;
    delay(50);  
  }
  bufCount = 0;
  Serial.println("Buffer flushed.");
}


void connectMQTT() {
  mqttClient.setBufferSize(512);     
  mqttClient.setKeepAlive(60);       
  mqttClient.setServer(MQTT_BROKER, 1883);
  mqttClient.setCallback(mqttCallback);
  while (!mqttClient.connected()) {
    Serial.print("MQTT connecting to ");
    Serial.print(MQTT_BROKER);
    Serial.print("...");
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


void setup() {
  Serial.begin(115200);
  delay(2000);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_RUN);   
  digitalWrite(LED_PIN, LOW);

  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║  CO326 Conveyor Belt Fault Detection     ║");
  #if HARDWARE_MODE
  Serial.println("║  Mode: HARDWARE (real sensor data)       ║");
  #else
  Serial.println("║  Mode: DEMO (simulated fault injection)  ║");
  #endif
  Serial.println("╚══════════════════════════════════════════╝");

  mpuBegin();
  connectWiFi();
  syncNTP();
  connectMQTT();
  calibrate();
  startTime = millis();

  Serial.println("System ready — entering main loop.");
}


void loop() {
  // Reconnect if connection dropped
  if (!mqttClient.connected()) {
    connectMQTT();
    flushBuffer();  //Send any readings stored during outage
  }
  mqttClient.loop();

  // Read raw sensor data
  float x, y, z, tempC;
  mpuRead(x, y, z, tempC);

  unsigned long elapsed = millis() - startTime;

  //In DEMO mode, inject simulated faults on top of real data
  #if !HARDWARE_MODE
  applyFaultPhase(x, y, z, elapsed);
  #endif

  //Anomaly Detection (Z-score based)
  float mag   = sqrt(x*x + y*y + z*z);
  float z_mag = (mag - base_mean) / base_std;
  float z_y   = fabs((y - base_y_mean) / base_y_std);
  bool  stall = (mag < base_mean * 0.2 && calibrated);

  // Calculate anomaly score (0.0 = normal, 1.0 = severe fault)
  float score = 0;
  if (stall)        score = 0.95;
  else if (z_y > 3) score = min(0.9f, z_y / 10.0f);
  else              score = min(1.0f, max(0.0f, z_mag / 10.0f));

  int fault_flag = (score > 0.3) ? 1 : 0;

  //Classify fault type
  String fault_type = "normal";
  if      (stall)        fault_type = "belt_jam";
  else if (z_y > 3)      fault_type = "misalignment";
  else if (score > 0.6)  fault_type = "roller_fault";
  else if (score > 0.3)  fault_type = "belt_slip";

  // Auto-stop after 5 consecutive faults
  if (fault_flag) {
    consecutive_faults++;
  } else {
    
    if (consecutive_faults > 0) {
      Serial.println("Fault cleared — resuming normal operation");
    }
    consecutive_faults = 0;
    digitalWrite(RELAY_PIN, RELAY_RUN); 
  }

  if (consecutive_faults >= 5 && digitalRead(RELAY_PIN) == RELAY_RUN) {
    digitalWrite(RELAY_PIN, RELAY_STOP);
    Serial.println("⚠ AUTO-STOP triggered — 5 consecutive faults");
  }

  // LED indicator: blink on fault, solid on normal
  if (fault_flag) {
    digitalWrite(LED_PIN, (millis() / 250) % 2); 
  } else {
    digitalWrite(LED_PIN, HIGH);  
  }

  //Get NTP timestamp 
  char timestamp[30];
  getTimestamp(timestamp, sizeof(timestamp));

  //Determine mode string
  #if HARDWARE_MODE
  String modeStr = "hardware";
  #else
  String modeStr = "demo";
  #endif

  //If MQTT disconnected, store in circular buffer
  if (!mqttClient.connected()) {
    SensorReading &r = buffer[bufHead];
    r.x = x; r.y = y; r.z = z; r.mag = mag; r.tempC = tempC;
    r.score = score; r.fault_flag = fault_flag;
    r.relay_state = digitalRead(RELAY_PIN);
    r.consec = consecutive_faults; r.fault_type = fault_type;
    r.mode = modeStr; r.elapsed = elapsed;
    strncpy(r.timestamp, timestamp, sizeof(r.timestamp));
    bufHead = (bufHead + 1) % BUFFER_SIZE;
    if (bufCount < BUFFER_SIZE) bufCount++;
    Serial.printf("[BUFFERED %d/%d] %s\n", bufCount, BUFFER_SIZE, fault_type.c_str());
    delay(500);
    return;
  }

  //Build and publish MQTT JSON payload
  StaticJsonDocument<512> doc;
  doc["x"]             = round(x*1000)/1000.0;
  doc["y"]             = round(y*1000)/1000.0;
  doc["z"]             = round(z*1000)/1000.0;
  doc["magnitude"]     = round(mag*1000)/1000.0;
  doc["temperature"]   = round(tempC*100)/100.0;
  doc["anomaly_score"] = round(score*1000)/1000.0;
  doc["fault_flag"]    = fault_flag;
  doc["fault_type"]    = fault_type;
  doc["relay_state"]   = (digitalRead(RELAY_PIN) == RELAY_STOP) ? 1 : 0;
  doc["elapsed_s"]     = elapsed / 1000;
  doc["consecutive_faults"] = consecutive_faults;
  doc["mode"]          = modeStr;
  doc["timestamp"]     = timestamp;

  char buf[512];
  serializeJson(doc, buf);
  mqttClient.publish(T_DATA, buf);
  Serial.println(buf);

  delay(500); 
}