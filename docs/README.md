---
layout: home
permalink: index.html

repository-name: e20-co326-Conveyor-Belt-Fault-Detection
title: Conveyor Belt Fault Detection System
---

# Conveyor Belt Fault Detection System

---

## Team

- E/20/377, Team Member 1, [email](mailto:e20377@eng.pdn.ac.lk)

## Table of Contents

1. [Introduction](#introduction)
2. [System Architecture](#system-architecture)
3. [Fault Detection Algorithm](#fault-detection-algorithm)
4. [Hardware Setup](#hardware-setup)
5. [Software Stack](#software-stack)
6. [Links](#links)

---

## Introduction

Conveyor belts are critical components in industrial automation. Undetected faults such as belt slippage, roller failures, misalignment, or belt jams can lead to costly downtime and safety hazards.

This project implements an **IoT-based real-time fault detection system** using vibration analysis. An MPU-6050 accelerometer mounted on the conveyor belt structure continuously monitors vibration patterns. An ESP32 microcontroller processes the data using **Z-score anomaly detection**, classifies fault types, and automatically stops the belt via a relay when critical faults persist.

All sensor data is transmitted via MQTT to a cloud/edge stack (InfluxDB + Grafana) for visualization, historical analysis, and remote monitoring.

## System Architecture

The system consists of two layers:

**Physical Layer (Edge):**
- ESP32 Dev Module — WiFi-enabled microcontroller
- MPU-6050 — 3-axis accelerometer (I2C, ±8g range)
- Relay Module — Emergency stop actuator

**Software Layer (Server):**
- Mosquitto MQTT Broker — Message transport
- Node-RED — Data pipeline (MQTT → InfluxDB)
- InfluxDB 2.7 — Time-series database
- Grafana — Real-time visualization dashboard

Communication uses the **Sparkplug B** topic namespace for industrial IoT compatibility.

## Fault Detection Algorithm

The system uses a **calibration-based Z-score anomaly detection** approach:

1. **Calibration Phase**: 100 samples collected over 2 seconds while the belt operates normally. Baseline mean and standard deviation are computed for acceleration magnitude and Y-axis values.

2. **Real-time Analysis**: Each reading is compared against the baseline:
   - **Z-score of magnitude** detects overall vibration anomalies
   - **Z-score of Y-axis** detects lateral misalignment
   - **Magnitude threshold** (< 20% baseline) detects belt jams

3. **Fault Classification**:

| Fault Type | Detection Criteria | Anomaly Score |
|-----------|-------------------|---------------|
| Normal | All values within baseline | < 0.3 |
| Belt Slip | Moderate vibration anomaly | 0.3 - 0.6 |
| Roller Fault | High vibration anomaly | > 0.6 |
| Misalignment | Y-axis Z-score > 3 | Up to 0.9 |
| Belt Jam | Acceleration < 20% of baseline | 0.95 |

4. **Auto-Stop**: After 5 consecutive fault readings, the relay is triggered to stop the belt.

## Hardware Setup

### Wiring

| ESP32 Pin | Component | Pin |
|-----------|-----------|-----|
| GPIO 21 | MPU-6050 | SDA |
| GPIO 22 | MPU-6050 | SCL |
| 3.3V | MPU-6050 | VCC |
| GND | MPU-6050 | GND |
| GPIO 10 | Relay | IN |
| 3.3V/5V | Relay | VCC |
| GND | Relay | GND |

## Software Stack

The backend runs via Docker Compose:

```bash
cd docker
docker-compose up -d
```

| Service | Port | Purpose |
|---------|------|---------|
| Mosquitto | 1883 | MQTT broker |
| Node-RED | 1880 | Data pipeline |
| InfluxDB | 8086 | Time-series DB |
| Grafana | 3000 | Dashboard |

## Links

- [Project Repository](https://github.com/cepdnaclk/e20-co326-Conveyor-Belt-Fault-Detection){:target="\_blank"}
- [Project Page](https://cepdnaclk.github.io/e20-co326-Conveyor-Belt-Fault-Detection){:target="\_blank"}
- [Department of Computer Engineering](http://www.ce.pdn.ac.lk/)
- [University of Peradeniya](https://eng.pdn.ac.lk/)

[//]: # "Please refer this to learn more about Markdown syntax"
[//]: # "https://github.com/adam-p/markdown-here/wiki/Markdown-Cheatsheet"
