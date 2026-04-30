# Conveyor Belt Fault Detection — Complete Explanation Guide

> This document explains every single part of the project in plain language, as if explaining to a non-technical person. Use this to prepare for your presentation.

---

## 1. THE PROBLEM — Why Does This Project Exist?

Imagine a factory with a long conveyor belt carrying packages. This belt runs 24/7. If something goes wrong — a roller breaks, the belt slips, or the belt jams — three bad things happen:

1. **Production stops** — the factory loses money every minute
2. **Workers could get hurt** — a jammed belt can suddenly snap free
3. **Nobody knows it's failing** — by the time a human notices, it's too late

**Current reality**: Most factories rely on a worker walking around checking belts by hand. They might check once every few hours. A lot can go wrong in between.

**Our solution**: We attached a tiny sensor to the belt that checks it **twice every second, 24/7**, detects problems **before** a human could, and **automatically stops the belt** if things get dangerous — all within 2.5 seconds.

---

## 2. THE HARDWARE — What Did We Build?

### The Three Physical Components

Think of it like a human body:

| Component | What It Is | Human Analogy |
|-----------|-----------|---------------|
| **MPU-6050 Accelerometer** | A tiny chip that measures vibrations (shaking) in 3 directions (left-right, forward-back, up-down) | The **inner ear** — senses motion and balance |
| **ESP32 Microcontroller** | A tiny $3 computer with WiFi built in. It reads the sensor, does math, makes decisions, and sends data wirelessly | The **brain** — processes information and decides |
| **Relay Module** | An electrically-controlled switch that can turn the belt motor on/off | The **hand** — takes physical action (flips a switch) |

### How They're Wired

```
MPU-6050 Sensor ──(4 wires)──> ESP32 Brain ──(1 wire)──> Relay Switch ──> Motor
     SDA (data)                 GPIO 21
     SCL (clock)                GPIO 22
     VCC (power 3.3V)
     GND (ground)                GPIO 10 ────> Relay IN
```

The sensor talks to the ESP32 using a protocol called **I2C** (Inter-Integrated Circuit). Think of I2C like a two-lane road:
- **SDA** = the data lane (carries the actual vibration numbers)
- **SCL** = the clock lane (keeps both sides synchronized, like a metronome)

---

## 3. THE SENSOR — How Does The MPU-6050 Work?

The MPU-6050 measures **acceleration** in three axes:
- **X-axis**: Left ↔ Right
- **Y-axis**: Forward ↔ Backward
- **Z-axis**: Up ↔ Down

When the sensor sits still on a table, it reads approximately:
- X ≈ 0, Y ≈ 0, Z ≈ 9.81 m/s² (that's gravity pulling downward!)

When the conveyor belt vibrates, these numbers shake around. **More vibration = bigger numbers = something is wrong.**

We combine all three axes into one number called **magnitude**:
```
magnitude = √(x² + y² + z²)
```

Think of it as: "How much total shaking is there, regardless of direction?"

A healthy belt might have magnitude ≈ 11.4 m/s² (gravity + slight vibration).
A failing belt might spike to 13+ m/s².

### The ±8g Range

We configured the sensor to measure up to ±8 times Earth's gravity. This means it can detect vibrations up to 78.4 m/s² — far more than a conveyor belt would ever produce. We chose this range because it gives us good sensitivity without overloading the sensor.

### Temperature

The MPU-6050 also has a built-in thermometer. We read this to monitor if the motor is overheating. The formula to convert the raw number is:
```
temperature = (raw_value / 340) + 36.53 °C
```

---

## 4. CALIBRATION — How Does The System Learn "Normal"?

Before the system can detect faults, it needs to know what "normal" looks like. This is **calibration**.

### What Happens During Calibration

1. The belt runs normally
2. The ESP32 takes **100 readings** over 2 seconds
3. It calculates two statistics:
   - **Mean (average)**: "What's the typical vibration level?"
   - **Standard Deviation**: "How much does it normally vary?"

**Example from our system:**
- `base_mean = 11.394` → Normal vibration magnitude is about 11.4 m/s²
- `base_std = 0.055` → It normally varies by only ±0.055 m/s²

Think of it like a doctor taking your baseline blood pressure. Once they know YOUR normal, they can tell when something is off.

---

## 5. ANOMALY DETECTION — How Does It Detect Faults?

This is the "AI" part — though it's really clever statistics, not a neural network.

### The Z-Score

A **Z-score** answers the question: "How many standard deviations away from normal is this reading?"

```
Z-score = (current_reading - normal_average) / normal_variation
```

**Real example:**
- Normal magnitude: 11.394 (average), 0.055 (std dev)
- Current reading: 11.600
- Z-score = (11.600 - 11.394) / 0.055 = **3.74**

Translation: "This reading is 3.74 standard deviations above normal." That's very unusual!

### Anomaly Score (0 to 1)

We convert the Z-score into a simpler 0–1 scale:
```
anomaly_score = Z-score / 10    (capped at 1.0)
```

| Score Range | Meaning | Color on Dashboard |
|-------------|---------|-------------------|
| 0.0 – 0.3 | ✅ Normal | Green |
| 0.3 – 0.6 | ⚠️ Something's off | Yellow |
| 0.6 – 1.0 | 🔴 Serious fault | Red |

### How The 4 Fault Types Are Classified

Not all faults look the same. We use different detection rules for each:

#### 1. Belt Slip (Score 0.3–0.6)
**What it is**: The belt is sliding over the rollers instead of gripping
**How we detect it**: Moderate increase in overall vibration (Z-score 3–6)
**Real-world cause**: Belt tension too loose, wet surface, overloaded

#### 2. Roller Fault (Score > 0.6)
**What it is**: A roller bearing is damaged or seized
**How we detect it**: Severe vibration spike (Z-score > 6)
**Real-world cause**: Bearing wear, lack of lubrication, debris stuck in roller

#### 3. Misalignment (Score up to 0.9)
**What it is**: The belt is drifting to one side
**How we detect it**: The **Y-axis specifically** deviates more than 3 standard deviations from normal
**Real-world cause**: Uneven loading, frame not level, roller misalignment

#### 4. Belt Jam (Score 0.95)
**What it is**: The belt has completely stopped moving
**How we detect it**: Vibration magnitude drops below 20% of the normal baseline
**Real-world cause**: Motor failure, something physically blocking the belt

### Decision Logic (in order of priority)

```
IF magnitude < 20% of normal    → BELT JAM
ELSE IF Y-axis Z-score > 3      → MISALIGNMENT
ELSE IF anomaly score > 0.6     → ROLLER FAULT
ELSE IF anomaly score > 0.3     → BELT SLIP
ELSE                             → NORMAL
```

---

## 6. AUTO-STOP — How Does It Stop The Belt?

The system doesn't panic over a single bad reading — vibrations are naturally noisy. Instead, it uses a **consecutive fault counter**:

1. Each reading with `anomaly_score > 0.3` increments the counter
2. Each normal reading **resets** the counter to zero
3. When the counter reaches **5** (= 2.5 seconds at 2 readings/sec), the relay triggers

**Why 5?** This prevents false alarms from random vibration spikes while still responding fast enough for real faults. 2.5 seconds is fast enough to stop a belt before a broken roller causes a fire.

**Auto-recovery**: When readings return to normal, the relay automatically releases and the belt can restart.

---

## 7. MQTT COMMUNICATION — How Does Data Get From Sensor To Dashboard?

### What Is MQTT?

MQTT (Message Queuing Telemetry Transport) is a messaging protocol designed for IoT devices. Think of it like a WhatsApp group:

- The **Broker** (Mosquitto) = the WhatsApp server
- The **ESP32** = a member who posts messages (sensor data)
- **Node-RED** = a member who reads messages and takes action
- **Topics** = different group chats for different purposes

### Our Topic Structure (Sparkplug B)

We use an industrial standard called **Sparkplug B** for topic naming:

```
spBv1.0 / conveyorLine / DDATA / belt01 / beltDrive
   │          │            │        │         │
   │          │            │        │         └─ Device name
   │          │            │        └─ Node (physical location)
   │          │            └─ Message type (Device DATA)
   │          └─ Group (factory line)
   └─ Protocol version
```

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `DDATA` | ESP32 → Dashboard | Sensor readings every 0.5 seconds |
| `DCMD` | Dashboard → ESP32 | Remote relay commands (ON/OFF) |
| `NBIRTH` | ESP32 → Dashboard | "I just powered on!" announcement |
| `NDEATH` | Broker auto-sends | "The ESP32 disappeared!" alert |

### Last Will and Testament (LWT)

When the ESP32 first connects, it tells the broker: *"If I disappear without saying goodbye, publish the word OFFLINE on my death topic."*

This way, if someone accidentally unplugs the ESP32, the dashboard instantly knows it's dead — even though the ESP32 can't send a goodbye message.

### The JSON Message

Every 0.5 seconds, the ESP32 sends a JSON message like this:
```json
{
  "x": -0.927,              ← X-axis acceleration (m/s²)
  "y": -2.191,              ← Y-axis acceleration (m/s²)
  "z": 11.209,              ← Z-axis acceleration (m/s²)
  "magnitude": 11.458,      ← Total vibration level
  "temperature": 46.13,     ← Sensor temperature (°C)
  "anomaly_score": 0.106,   ← How abnormal? (0=normal, 1=severe)
  "fault_flag": 0,           ← 0=OK, 1=fault detected
  "fault_type": "normal",   ← Specific fault classification
  "relay_state": 0,          ← 0=belt running, 1=belt stopped
  "elapsed_s": 8,           ← Seconds since system started
  "consecutive_faults": 0,  ← Fault streak counter
  "mode": "demo",           ← demo or hardware mode
  "timestamp": "2026-04-30T20:52:35Z"  ← Exact UTC time
}
```

---

## 8. THE SOFTWARE STACK — What Runs On The Server?

Everything runs inside **Docker containers** on a laptop/PC. Docker is like having separate virtual computers, each doing one job:

```
  ESP32 ──(WiFi)──> [Mosquitto MQTT] ──> [Node-RED] ──> [InfluxDB] ──> [Grafana]
                     Port 1883           Port 1880       Port 8086      Port 3000
                     Message broker      Data pipeline   Database       Dashboard
```

### Mosquitto MQTT Broker (Port 1883)
**What it does**: Receives messages from ESP32, forwards them to Node-RED
**Analogy**: A post office that sorts and delivers mail

### Node-RED (Port 1880)
**What it does**: Receives MQTT messages → parses JSON → converts to database format → writes to InfluxDB. Also calculates RUL (Remaining Useful Life).
**Analogy**: A secretary who reads incoming mail, reformats it into spreadsheet rows, and files it

### InfluxDB (Port 8086)
**What it does**: Stores every single sensor reading with a timestamp. Optimized for time-series data.
**Analogy**: A filing cabinet organized by date and time

### Grafana (Port 3000)
**What it does**: Reads data from InfluxDB and displays it as beautiful real-time charts, gauges, and alerts.
**Analogy**: A TV screen that shows live graphs from the filing cabinet

---

## 9. NODE-RED DATA PIPELINE — The Details

### Step 1: Receive MQTT Message
Node-RED subscribes to the `DDATA` topic and receives the raw JSON string.

### Step 2: Parse JSON
Converts the text string into a structured JavaScript object that code can work with.

### Step 3: Format for InfluxDB (Line Protocol)
InfluxDB requires a special format called "line protocol":
```
conveyor_belt x=-0.927,y=-2.191,z=11.209,magnitude=11.458,temperature=46.13,anomaly_score=0.106,fault_flag=0,fault_type="normal",relay_state=0,consecutive_faults=0
```

### Step 4: Write to InfluxDB
Sends an HTTP POST request to InfluxDB's write API with the formatted data.

### Step 5: RUL Estimation (Remaining Useful Life)

This is the predictive maintenance feature — it answers: **"How many minutes until the belt fails?"**

**How it works** (OLS Linear Regression):
1. Keep a rolling window of the last **60 anomaly scores** (30 seconds of data)
2. Fit a straight line through these 60 points (this is called Ordinary Least Squares / OLS regression)
3. The line has a **slope** — if the slope is positive, faults are getting worse over time
4. Extend the line forward to predict when it will hit the failure threshold (score = 1.0)
5. Convert that time to minutes → that's the RUL

**Example**:
- Current score: 0.4, trending upward at +0.01 per second
- Time to reach 1.0: (1.0 - 0.4) / 0.01 = 60 seconds = 1 minute
- RUL = 1 minute (URGENT!)

If the scores are stable or decreasing, RUL = 999 (displayed as "✅ Stable" on the dashboard).

---

## 10. GRAFANA DASHBOARD — The 15 Panels Explained

### Row 1: System Status (Overview Gauges)

| Panel | Type | What It Shows |
|-------|------|---------------|
| **Current Fault Type** | Stat | The specific fault: Normal, Belt Slip, Roller Fault, Misalignment, or Belt Jam. Color-coded with emojis. |
| **Anomaly Score** | Gauge | A semicircle dial showing 0–1. Green = safe, Yellow = warning, Red = danger. |
| **Belt Status** | Stat | RUNNING (green) or STOPPED (red). Changes when the relay activates. |
| **Temperature** | Gauge | Sensor temperature in °C. Useful for detecting motor overheating. |
| **Consecutive Faults** | Stat | How many bad readings in a row. Turns red when approaching the auto-stop threshold of 5. |
| **Vibration Magnitude** | Gauge | Total vibration level in m/s². Shows how much the belt is shaking. |

### Row 2: Vibration Data (Time Charts)

| Panel | What It Shows |
|-------|---------------|
| **Acceleration (X,Y,Z) over Time** | Three colored lines showing each axis. If Y suddenly shifts, that's misalignment. |
| **Anomaly Score over Time** | The score plotted over time with a red threshold line at 0.3. When the line goes above 0.3, faults are occurring. |

### Row 3: More Trends

| Panel | What It Shows |
|-------|---------------|
| **Magnitude over Time** | Total vibration plotted over time. Drops to near-zero during belt jam. |
| **Temperature over Time** | Temperature trend — a rising temperature could mean motor problems. |

### Row 4: Predictive Maintenance

| Panel | What It Shows |
|-------|---------------|
| **RUL Countdown** | "How many minutes until failure?" Shows "✅ Stable" when healthy, or a countdown in minutes when degrading. |
| **RUL over Time** | RUL plotted as a timeline with a 5-minute warning threshold line. |

### Row 5: Fault History

| Panel | What It Shows |
|-------|---------------|
| **Fault Events Timeline** | A horizontal timeline with colored bands: green=normal, yellow=belt_slip, orange=roller_fault, red=misalignment, dark=belt_jam. |
| **Consecutive Faults over Time** | Shows the fault counter climbing toward the auto-stop threshold (red line at 5). |
| **Relay State over Time** | Shows when the relay was ON (belt stopped) vs OFF (belt running). |

---

## 11. DIGITAL TWIN — Bidirectional Control

A Digital Twin is a virtual copy of a physical machine. Our system is a **Level 3 Digital Twin** because it has **bidirectional communication**:

**Direction 1: Physical → Digital** (monitoring)
- Real sensor data → Dashboard visualization
- The digital dashboard mirrors the real belt's condition in real-time

**Direction 2: Digital → Physical** (control)
- An operator can send commands FROM the dashboard TO the physical belt
- Example: A maintenance engineer in an office can remotely stop a belt on the factory floor

### How Remote Control Works

```
Operator sends HTTP POST request
    → Node-RED receives at /belt-control endpoint
    → Node-RED publishes to MQTT DCMD topic
    → ESP32 receives the command
    → ESP32 activates/deactivates the relay
    → Belt motor stops/starts
```

The command JSON is simply: `{"relay_command": "ON"}` to stop or `{"relay_command": "OFF"}` to resume.

---

## 12. CYBERSECURITY — How Is The System Protected?

An industrial control system is dangerous if hacked. Imagine a hacker turning on a belt while a worker is repairing it. We implemented three layers of security:

### Layer 1: Authentication (Passwords)
No anonymous connections to the MQTT broker. Every device must log in:
- The ESP32 logs in as `esp32user` with a password
- Node-RED logs in as `nodered` with a different password
- Passwords are **hashed** using PBKDF2 — even if someone steals the password file, they can't read the actual passwords. PBKDF2 scrambles the password thousands of times, making it computationally infeasible to reverse.

### Layer 2: Authorization (ACLs — Access Control Lists)
Even with a valid password, each user can only access specific topics:
- `esp32user` can **publish** sensor data but **cannot subscribe** to other devices' data
- `nodered` can both publish (relay commands) and subscribe (sensor data)
- A hacker who steals the ESP32's password cannot read data from other factory machines

### Layer 3: Secrets Management (.env)
Sensitive values (passwords, API tokens) are stored in a `.env` file that:
- Is loaded by Docker Compose at startup
- Is listed in `.gitignore` so it's **never uploaded to GitHub**
- A `.env.example` template with placeholder values is provided for anyone setting up the system

---

## 13. RELIABILITY — What Happens When Things Break?

### Circular Buffer (WiFi Outage Survival)
If the WiFi router reboots, the ESP32 cannot send data. Instead of throwing readings away, it stores the last **50 readings** in memory. When WiFi returns, it "flushes" all 50 readings to the dashboard — no data is lost.

### NTP Timestamps
Every reading includes an exact UTC timestamp from an internet atomic clock (`pool.ntp.org`). This means even if data arrives late (after a WiFi outage), the dashboard shows the correct time the reading was taken.

### Auto-Reconnect & Watchdog
- If MQTT disconnects → ESP32 automatically retries every 3 seconds
- If WiFi freezes for 20+ seconds → a hardware watchdog **restarts the entire ESP32** to clear the glitch
- After restart, it reconnects and resumes automatically

### Self-Healing Sensor Config
The MPU-6050 can sometimes lose its configuration if the I2C bus glitches (e.g., a loose wire). Every 5 seconds, the ESP32 re-writes the sensor configuration to ensure it stays in ±8g mode.

---

## 14. DEMO MODE — How We Prove It Works

Since we can't break a real conveyor belt during a presentation, we built a **Demo Mode** that injects artificial faults on top of real sensor data.

### The 150-Second Cycle

| Time | Phase | What's Injected | Expected Dashboard |
|------|-------|-----------------|-------------------|
| 0–40s | Normal | Nothing — real sensor data only | Green, score ~0 |
| 40–70s | Belt Slip | Moderate vibration added to X and Z axes | Yellow, score 0.3–0.6 |
| 70–100s | Roller Fault | Severe vibration added + **AUTO-STOP triggers** | Orange/Red, relay clicks |
| 100–130s | Misalignment | Y-axis shifted by +1.8 m/s² | Red, Y-axis chart shifts |
| 130–150s | Belt Jam | All axes multiplied by 0.05 (near-zero) | Dark, magnitude drops |
| 150s+ | Restart | Cycle repeats from normal | Back to green |

**Key point**: The injected faults are added ON TOP of real sensor readings, so the data looks realistic, not perfectly synthetic.

---

## 15. COMPLETE DATA FLOW — End to End

Here is everything that happens from sensor reading to dashboard pixel, in order:

```
1.  MPU-6050 sensor measures vibration (X, Y, Z acceleration)
2.  ESP32 reads sensor via I2C (2 times per second)
3.  [DEMO only] Fault injection modifies the readings
4.  ESP32 calculates magnitude = √(x² + y² + z²)
5.  ESP32 calculates Z-score = (magnitude - baseline_mean) / baseline_std
6.  ESP32 converts Z-score to anomaly_score (0 to 1)
7.  ESP32 classifies fault type (normal/belt_slip/roller_fault/misalignment/belt_jam)
8.  ESP32 checks consecutive faults → auto-stop if ≥ 5
9.  ESP32 builds JSON message with all 13 fields
10. ESP32 publishes JSON via MQTT to Mosquitto broker
11. Node-RED subscribes to MQTT → receives JSON
12. Node-RED parses JSON → converts to InfluxDB line protocol
13. Node-RED HTTP POSTs data to InfluxDB
14. Node-RED also feeds anomaly scores to RUL Estimator
15. RUL Estimator runs OLS regression on last 60 scores
16. RUL estimate written to InfluxDB as separate measurement
17. Grafana queries InfluxDB every 1-5 seconds
18. Grafana renders 15 dashboard panels with live data
19. If RUL < 5 minutes → Grafana alert fires
```

**Total latency from sensor to screen: < 1 second**

---

## 16. KEY TALKING POINTS FOR PRESENTATION

### "Why Z-Score Instead of a Neural Network?"
- Z-score runs in **< 1 millisecond** on the ESP32 (neural networks need > 100ms)
- Requires **zero external libraries** — just basic math (`sqrt`, division)
- **Self-calibrating** — works on any conveyor belt without pre-training
- For vibration anomaly detection, statistical methods are **proven in real industry**
- The ESP32 has only 520KB RAM — not enough for a real neural network

### "What Makes This an IoT System?"
- Physical sensor → Edge computing → Network transport → Cloud storage → Visualization
- Industrial-standard protocols (MQTT Sparkplug B)
- Bidirectional Digital Twin (monitor AND control)
- Predictive maintenance (RUL estimation)

### "What's the Cybersecurity?"
- Three layers: Authentication (PBKDF2 passwords) → Authorization (topic ACLs) → Secrets management (.env)
- Defense in depth: even if one layer is compromised, the others limit damage

### "What Makes This Reliable?"
- Circular buffer survives WiFi outages (50 readings saved)
- NTP timestamps ensure correct time ordering
- Auto-reconnect and hardware watchdog for self-recovery
- LWT tells the dashboard when the device dies unexpectedly

---

## 17. NUMBERS TO REMEMBER

| Metric | Value |
|--------|-------|
| Sample rate | 2 Hz (2 readings per second) |
| Anomaly detection speed | < 1 millisecond |
| Auto-stop response time | 2.5 seconds (5 consecutive faults) |
| WiFi outage buffer | 50 readings (~25 seconds) |
| RUL prediction window | 60 samples (30 seconds) |
| Dashboard panels | 15 |
| Fault types detected | 4 + normal |
| Total sensor-to-screen latency | < 1 second |
| Demo cycle duration | 150 seconds (2.5 minutes) |
| Calibration time | 2 seconds (100 samples) |
| ESP32 cost | ~$3 USD |
| Total system cost | ~$15 USD (ESP32 + MPU-6050 + Relay) |
