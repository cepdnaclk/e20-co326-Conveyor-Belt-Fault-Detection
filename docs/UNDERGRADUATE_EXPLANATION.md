# Conveyor Belt Fault Detection System Explained for Undergraduates

This document gives a full, beginner-friendly explanation of the project: what problem it solves, what hardware is used, how the software stack works, how the fault detection logic makes decisions, and how the whole system behaves from sensor to dashboard.

## 1. What This Project Is About

The project is an IoT-based conveyor belt fault detection system.

In simple words, it watches a conveyor belt, looks for unusual vibration patterns, classifies the kind of problem, and can stop the belt automatically if the fault is serious enough.

Why is this useful?

- Conveyor belts are used in factories and industrial systems.
- If a belt slips, jams, goes out of alignment, or a roller fails, the machine can be damaged.
- Detecting the fault early saves time, money, and equipment.

The project combines electronics, embedded programming, networking, databases, and dashboards. That is why it is a good undergraduate-level project: it connects hardware and software into one working system.

## 2. The Big Picture

At a high level, the system has two parts:

1. The physical layer, which includes the ESP32, the MPU-6050 accelerometer, and the relay.
2. The software layer, which includes Mosquitto, Node-RED, InfluxDB, and Grafana.

The flow is:

1. The MPU-6050 measures vibration.
2. The ESP32 reads that sensor data.
3. The ESP32 analyses the readings locally and decides whether the belt looks normal or faulty.
4. The ESP32 sends the data over WiFi using MQTT.
5. Mosquitto receives the MQTT messages.
6. Node-RED listens to the data topic, reformats the readings, and writes them into InfluxDB.
7. Grafana reads InfluxDB and shows live charts, gauges, and fault history.
8. If needed, Node-RED or the operator can send a control message back to the ESP32 to stop or resume the belt.

So the system is not just sensing something. It is also storing it, visualizing it, and allowing control.

## 3. What Each Hardware Component Does

### ESP32 Dev Module

The ESP32 is the main microcontroller.

Think of it as the brain at the edge of the system. It:

- reads the accelerometer values,
- calculates fault indicators,
- connects to WiFi,
- publishes sensor data to MQTT,
- listens for stop or resume commands,
- drives the relay that controls the belt.

### MPU-6050 Accelerometer

The MPU-6050 measures acceleration on three axes:

- X-axis,
- Y-axis,
- Z-axis.

This matters because a healthy conveyor belt has a fairly repeatable vibration pattern. If the vibration suddenly changes, that can mean something is wrong.

The firmware converts the raw readings into physical acceleration values and also computes the vibration magnitude.

### Relay Module

The relay acts like an electronically controlled switch.

In the project, it is used as the emergency stop mechanism:

- relay OFF means the belt can run,
- relay ON means the belt is stopped.

### Wiring Summary

- ESP32 GPIO 21 and GPIO 22 connect to the MPU-6050 I2C lines.
- ESP32 GPIO 10 connects to the relay input.
- The sensor and relay share power and ground with the ESP32.

## 4. What the Software Stack Does

The backend runs in Docker Compose and contains four main services.

### Mosquitto MQTT Broker

Mosquitto is the message hub.

MQTT is a lightweight protocol used a lot in IoT. Instead of the ESP32 talking directly to every other service, it publishes messages to the broker, and other services subscribe to what they need.

In this project, Mosquitto is configured with authentication, so devices need valid credentials to connect.

### Node-RED

Node-RED is the integration layer.

It listens to the MQTT data topic, parses the JSON payload, converts it into a format InfluxDB understands, and sends it to the database using HTTP.

It also contains manual inject buttons for sending relay commands back to the ESP32.

### InfluxDB

InfluxDB is a time-series database.

Time-series data means data recorded over time, such as sensor values collected every half-second. That is exactly what this project generates.

It stores:

- x, y, z acceleration,
- magnitude,
- temperature,
- anomaly score,
- fault flag,
- fault type,
- relay state,
- consecutive fault count,
- mode.

### Grafana

Grafana is the visualization layer.

It queries InfluxDB and turns the stored data into:

- stat panels,
- gauges,
- line graphs,
- a fault history timeline.

This makes it easy to understand what the system is doing without reading raw logs.

## 5. The Data Flow in Detail

Here is the full path of one sensor sample.

### Step 1: Sensor reading

The MPU-6050 measures acceleration on the belt structure.

### Step 2: Calibration

When the ESP32 starts, it first collects a baseline from 100 samples.

This baseline represents what normal looks like for that specific conveyor setup.

The firmware calculates:

- average vibration magnitude,
- standard deviation of magnitude,
- average Y-axis acceleration,
- standard deviation of Y-axis acceleration.

### Step 3: Real-time analysis

For each new reading, the firmware computes:

- vibration magnitude,
- Z-score for magnitude,
- Z-score for the Y-axis,
- whether the reading looks like a stall or jam.

### Step 4: Fault classification

The firmware turns the analysis into a fault type such as:

- normal,
- belt slip,
- roller fault,
- misalignment,
- belt jam.

### Step 5: MQTT publication

The ESP32 publishes a JSON message to the MQTT data topic.

### Step 6: Node-RED processing

Node-RED reads that message, converts it into InfluxDB line protocol, and writes it to the `conveyor_data` bucket.

### Step 7: Dashboard display

Grafana reads the stored points and updates the dashboard.

### Step 8: Control commands

If a stop or resume command is sent on the command topic, the ESP32 receives it and changes the relay state.

## 6. How the Fault Detection Works

This is the heart of the project.

The firmware does not use machine learning. Instead, it uses a simple statistical method called Z-score anomaly detection.

### Why Z-score?

The Z-score tells you how far a measurement is from the average, in units of standard deviation.

If a value is close to the mean, the Z-score is small.
If it is far away, the Z-score is large.

That makes it a good way to detect unusual vibration.

### What the firmware checks

It looks at:

- the total vibration magnitude,
- the Y-axis specifically,
- whether the vibration is almost zero compared to the baseline.

### Fault categories

The firmware maps abnormal patterns to faults:

- Belt slip: moderate anomaly score.
- Roller fault: high anomaly score.
- Misalignment: strong Y-axis deviation.
- Belt jam: acceleration becomes very small, almost near zero.

### Automatic stop logic

The system does not stop on a single bad reading.

It waits for 5 consecutive fault readings before triggering the relay.

That is important because a single noisy sensor sample should not stop the belt unnecessarily.

## 7. Demo Mode and Hardware Mode

The firmware supports two modes.

### Hardware mode

In hardware mode, the ESP32 uses only real sensor readings from the MPU-6050.

### Demo mode

In demo mode, the firmware injects simulated faults into the sensor data.

This is useful if:

- you do not have a real conveyor belt,
- you want to show the detection pipeline working,
- you want to test the dashboard and alerts.

The demo cycle goes through normal operation, belt slip, roller fault, misalignment, and belt jam scenarios.

## 8. MQTT Topics Used

The project uses a Sparkplug B-style topic structure.

The main topics are:

- `spBv1.0/conveyorLine/DDATA/belt01/beltDrive` for sensor data from ESP32 to broker.
- `spBv1.0/conveyorLine/DCMD/belt01/beltDrive` for commands from broker to ESP32.
- `spBv1.0/conveyorLine/NBIRTH/belt01` for online status.
- `spBv1.0/conveyorLine/NDEATH/belt01` for offline status.

If you are new to MQTT, you can think of topics as named channels.

## 9. What the Dashboard Shows

The Grafana dashboard is designed to answer the main questions quickly:

- Is the belt running or stopped?
- What kind of fault, if any, is happening?
- How severe is the anomaly?
- Are faults happening repeatedly?
- How do vibration values change over time?
- What happened in the recent fault history?

Panels are configured for:

- fault type,
- anomaly score,
- relay state,
- temperature,
- consecutive faults,
- magnitude,
- raw X/Y/Z trends,
- fault events timeline.

## 10. How To Run the System

The intended workflow is:

1. Start the Docker stack from the `docker` folder using `docker-compose up -d`.
2. Import the Node-RED flow from `docker/nodered/flows.json`.
3. Make sure the InfluxDB token in Node-RED matches the one configured in Docker.
4. Upload the ESP32 firmware from `Hardware/sketch_feb14a/sketch_feb14a.ino`.
5. Open Grafana on port 3000 to view the dashboard.

The default service ports are:

- MQTT broker: 1883
- Node-RED: 1880
- InfluxDB: 8086
- Grafana: 3000

## 11. Important Defaults and Credentials

These are the defaults used in the repository:

- InfluxDB admin password: `co326password`
- InfluxDB token: `co326-conveyor-token`
- Grafana admin password: `co326`
- MQTT username in firmware: `esp32user`
- MQTT password in firmware: `password`

The WiFi SSID and password in the firmware should be updated before uploading if your network is different.

## 12. What Makes This a Good Undergraduate Project

This project is valuable because it demonstrates several important computer engineering ideas in one system:

- sensor interfacing,
- embedded C++ programming,
- real-time data acquisition,
- statistical fault detection,
- message-based communication with MQTT,
- cloud/edge data storage,
- dashboard visualization,
- actuator control through a relay,
- basic industrial IoT architecture.

It is a complete pipeline, not just a single piece of code.

## 13. Short Summary

The project watches a conveyor belt using vibration sensing. The ESP32 reads the MPU-6050, detects abnormal patterns with Z-score-based logic, publishes data over MQTT, stores it in InfluxDB, and visualizes it in Grafana. If the fault persists, the relay stops the belt automatically.

In one sentence: it is a smart conveyor safety system that senses, decides, stores, displays, and controls.