# Conveyor Belt Fault Detection System

An IoT-based conveyor belt monitoring system that detects abnormal vibration patterns, classifies common faults, and can stop the belt automatically when a fault persists. The project combines an ESP32, an MPU-6050 accelerometer, MQTT messaging, InfluxDB, Grafana, and Docker-based backend services.

## Overview

The system monitors conveyor vibration in real time and looks for signs of belt slip, roller faults, misalignment, and belt jams. Sensor data is published over MQTT, stored in a time-series database, and visualized on a dashboard for live monitoring and historical analysis.

The firmware uses a calibration-based Z-score approach rather than machine learning. During startup, the ESP32 builds a baseline from normal readings and then compares each new sample against that baseline to determine whether the belt is operating normally.

For a beginner-friendly explanation of the project, see [docs/UNDERGRADUATE_EXPLANATION.md](docs/UNDERGRADUATE_EXPLANATION.md). For the MQTT topic structure, see [docs/mqtt-topic-hierarchy.md](docs/mqtt-topic-hierarchy.md).

## Key Features

- Real-time vibration monitoring using an MPU-6050 accelerometer.
- Local fault detection on the ESP32 using baseline statistics and Z-score thresholds.
- Fault classification for belt slip, roller fault, misalignment, and belt jam.
- Automatic stop logic after repeated fault readings.
- MQTT-based communication with authenticated broker access.
- Dashboarding and historical storage with InfluxDB and Grafana.
- Docker Compose setup for the backend services.

## System Architecture

The project is split into two layers:

| Layer | Components | Responsibility |
|-------|------------|----------------|
| Edge / hardware | ESP32 Dev Module, MPU-6050, relay module | Read sensor data, classify faults, and control the belt |
| Backend / software | Mosquitto, Node-RED, InfluxDB, Grafana | Transport, process, store, and visualize telemetry |

The current Docker stack includes Mosquitto, InfluxDB 2.7, Node-RED, Grafana, and a Modbus server for integration testing.

## Hardware

| ESP32 Pin | Component | Signal |
|-----------|-----------|--------|
| GPIO 21 | MPU-6050 | SDA |
| GPIO 22 | MPU-6050 | SCL |
| 3.3V | MPU-6050 | VCC |
| GND | MPU-6050 | GND |
| GPIO 10 | Relay module | IN |
| 3.3V / 5V | Relay module | VCC |
| GND | Relay module | GND |

The firmware supports two operating modes:

- Hardware mode: uses real sensor readings from the MPU-6050.
- Demo mode: injects simulated faults to demonstrate the pipeline without a full conveyor setup.

## MQTT Topics

The project uses a Sparkplug B-style unified namespace.

| Topic | Direction | Purpose |
|-------|-----------|---------|
| `spBv1.0/conveyorLine/DDATA/belt01/beltDrive` | ESP32 -> broker | Sensor and fault telemetry |
| `spBv1.0/conveyorLine/DCMD/belt01/beltDrive` | broker -> ESP32 | Relay control commands |
| `spBv1.0/conveyorLine/NBIRTH/belt01` | ESP32 -> broker | Device online message |
| `spBv1.0/conveyorLine/NDEATH/belt01` | ESP32 -> broker | Device offline message |

## Data Model

Each telemetry message contains the following fields:

- `x`, `y`, `z`: acceleration values in m/s².
- `magnitude`: overall vibration magnitude.
- `temperature`: MPU-6050 temperature reading.
- `anomaly_score`: normalized fault severity.
- `fault_flag`: binary indicator for abnormal readings.
- `fault_type`: `normal`, `belt_slip`, `roller_fault`, `misalignment`, or `belt_jam`.
- `relay_state`: current relay output state.
- `elapsed_s`: elapsed time since startup.
- `consecutive_faults`: number of fault readings in a row.
- `mode`: `hardware` or `demo`.
- `timestamp`: ISO 8601 time stamp.

## Fault Detection Logic

The firmware performs a short calibration phase at startup and then continuously evaluates incoming readings.

1. It computes a baseline mean and standard deviation from normal vibration samples.
2. It measures the current acceleration magnitude and Y-axis deviation.
3. It assigns an anomaly score based on the Z-score and stall conditions.
4. It classifies the fault type from the score and axis deviation.
5. It stops the belt after 5 consecutive fault readings and resumes automatically when the readings return to normal.

## Backend Services

Start the backend stack from the `docker/` directory:

```bash
cd docker
docker-compose up -d
```

| Service | Port | Purpose |
|---------|------|---------|
| Mosquitto | 1883 | MQTT broker |
| Node-RED | 1880 | MQTT processing and integration |
| InfluxDB | 8086 | Time-series storage |
| Grafana | 3000 | Monitoring dashboard |
| Modbus server | 502 | Auxiliary integration service |

## Project Structure

| Path | Description |
|------|-------------|
| `Hardware/sketch_feb14a/` | ESP32 firmware |
| `docker/` | Docker Compose backend stack and service configuration |
| `docs/` | Project documentation and explanations |
| `docs/images/architecture-diagram.mmd` | Architecture diagram source |

## Documentation

- [Project home page](docs/README.md)
- [Project report](docs/Group03_Technical_Report.pdf)
- [Undergraduate explanation](docs/UNDERGRADUATE_EXPLANATION.md)
- [MQTT topic hierarchy](docs/mqtt-topic-hierarchy.md)
- [Cybersecurity summary](docs/cybersecurity-summary.md)

## Links

- [Project Repository](https://github.com/cepdnaclk/e20-co326-Conveyor-Belt-Fault-Detection)
- [Project Page](https://cepdnaclk.github.io/e20-co326-Conveyor-Belt-Fault-Detection)
- [Department of Computer Engineering](http://www.ce.pdn.ac.lk/)
- [University of Peradeniya](https://eng.pdn.ac.lk/)
