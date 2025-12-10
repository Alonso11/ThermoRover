# ThermoRover
> Remote-Controlled Rover with Real-Time Telemetry and Environmental Monitoring

 ![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-orange.svg)

# Overview
ThermoBeacon is an embedded real-time system based on ESP32-P4 running FreeRTOS for remote control of a terrestrial rover via WiFi/WebSocket with real-time telemetry capabilities. The system integrates motor control via PWM, optical encoder reading for odometry, fuzzy control algorithms for differential traction, and optional environmental sensing with DHT22.
The system provides an interactive web interface with a virtual joystick for remote control, multiple driving modes, and telemetry transmission including motor speeds, distance traveled, RPM, and system status. The dual-core FreeRTOS architecture enables concurrent execution of critical control tasks and communications, ensuring real-time response and robust operation.

## Features

-  **Remote Control via WiFi/WebSocket** - Low-latency bidirectional communication (<30ms)
-  **Interactive Web Interface** - Virtual joystick accessible from any browser
-  **Multiple Driving Modes**:
  - **Arcade**: Intuitive control for beginners
  - **Tank**: Independent left/right traction control
  - **Car**: Ackermann-style steering behavior
  - **Smooth**: Temporal smoothing for gradual transitions
-  **Real-Time Telemetry** (10 Hz):
  - Motor speeds and duty cycles
  - RPM measurements
  - Distance traveled (odometry)
  - System health (uptime, free heap, battery voltage)
-  **Fuzzy Control** - Advanced differential drive algorithms
-  **Encoder Feedback** - Quadrature optical encoders (1000 pulses/3 rotations)
-  **Environmental Monitoring** - DHT12 temperature and humidity sensor
-  **Dual-Core Architecture** - FreeRTOS task distribution for optimal performance
-  **Robust Operation** - Automatic WiFi recovery and safety features
  
# Hardware Requirements
Component | Specification | Purpose|
|-----------|-------------|------|
ESP32-P4 | Dual-core main MCU |Primary processing unit (no integrated WiFi)
ESP32-C6 | WiFi module |Wireless connectivity via SDIO interface
Rover 5 Chassis | SparkFun 4-wheel platform |Mobile platform with 4 DC motors
L298N Driver | Dual H-bridge, 2A per channel |Motor control driverOptical EncodersQuadrature, 1000 pulses/3 rotationsSpeed and position feedback
DHT12 | AM2301 sensor | Temperature (-40°C to 80°C) and humidity (0-100% RH)
Battery Pack |Rechargeable |Power supply with voltage regulator


**Pin Configuration**

**Motor Control (MCPWM)**

- **Left Motor**: PWM + Direction GPIO
- **Right Motor**: PWM + Direction GPIO
- **PWM Frequency**: 1 kHz
- **Resolution**: 8-bit (0-255)

**Encoders (PCNT)**

- **Left Encoder**: Unit 0, quadrature mode
- **Right Encoder**: Unit 1, quadrature mode
- **Glitch Filter**: 1 µs

**Communication**

- **SDIO**: ESP32-P4 ↔ ESP32-C6
- **WiFi**: Access Point mode
- **WebSocket**: Port 80, endpoint /ws

# Project Structure
```
ThermoBeacon/
├── CMakeLists.txt              # Root CMake configuration
├── README.md                   # This file
├── sdkconfig.defaults          # ESP-IDF default configuration
├── main/
    ├── CMakeLists.txt          # Main component CMake
    ├── idf_component.yml       # Component dependencies
    ├── main.c                  # Main entry point, task orchestration
    ├── motor_control.c/.h      # PWM control via MCPWM, L298N driver
    ├── encoder.c/.h            # PCNT reading, RPM calculation, odometry
    ├── fuzzy_control.c/.h      # Joystick mapping, driving mode algorithms
    ├── wifi_manager.c/.h       # AP/STA configuration, connection management
    ├── websocket_server.c/.h   # HTTP server, WebSocket, JSON messaging
    ├── dht_sensor.c/.h         # DHT22 temperature & humidity reading
    ├── webpage.h               # Embedded HTML interface
    └── index.html              # Source HTML file (embedded as webpage.h)
```

# Quick Start
Prerequisites
```
ESP-IDF v5.5.1 or later

Python 3.8+

USB cable for ESP32-P4 programming

Build and Flash

bash# Clone the repository

git clone https://github.com/yourusername/ThermoBeacon.git

cd ThermoBeacon

## Configure the project (optional)
idf.py menuconfig

## Build the project
idf.py build

## Flash to ESP32-P4
idf.py -p /dev/ttyUSB0 flash

## Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```
## Usage

Connecting to the Rover

Power on the rover system

Connect to the WiFi network:

SSID: ThermoBeacon-XXXX (configurable)

Password: (see configuration)


Open browser and navigate to: http://192.168.4.1

Control the rover using the virtual joystick

# Web Interface Features

- Virtual Joystick: Touch or mouse control
- Mode Selection: Switch between Arcade, Tank, Car, and Smooth modes
- Response Curve: Choose Linear, Quadratic, Cubic, or Square Root
- Telemetry Display: Real-time system status


# Architecture

FreeRTOS Tasks

Task | Core | Priority| Stack | Rate | Function |
|-----------|-------------|------|---------|------|---|
Motor Control | 0 |5 |4KB |50 Hz |Process commands, update PWM|
Telemetry | 1 |3 |4KB |10 Hz |Collect and transmit telemetry

**Control Modes**
**Arcade Mode**
```
forward = magnitude × cos(θ)
turn = magnitude × sin(θ)
left_speed = forward + turn
right_speed = forward - turn
```

**Tank Mode**
```
left_speed = magnitude × cos(θ + 45°)
right_speed = magnitude × cos(θ - 45°)
```

**Car Mode**

Applies speed reduction to inner wheel during turns based on steering angle.



# Testing
All functional and non-functional requirements have been validated:

- ✅ Motor PWM control (8-bit resolution)
- ✅ Bidirectional motor control
- ✅ Quadrature encoder reading
- ✅ Odometry calculation
- ✅ Multiple control modes (4 modes)
- ✅ WebSocket communication
- ✅ Interactive web interface
- ✅ Real-time telemetry (10 Hz)
- ✅ Runtime parameter configuration
- ✅  DHT12 environmental sensing (optional)

# Configuration
Key parameters can be configured in config.h or via the web interface:

- Motor Dead Zone: Default 8% to prevent drift
- Minimum Duty Cycle: 35/255 to overcome static friction
- Wheel Diameter: 72 mm (default, configurable for odometry)
- Encoder Resolution: 1000 pulses per 3 wheel rotations
- Control Mode: Arcade, Tank, Car, or Smooth
- Response Curve: Linear, Quadratic, Cubic, or Square Root


# License
This project is licensed under the MIT License - see the LICENSE file for details.

# Authors

~~~
A. Blanco
L. González
F. Gómez

Escuela de Ingeniería Electrónica

Instituto Tecnológico de Costa Rica

Taller de Sistemas Embebidos - II Semestre 2025
~~~
