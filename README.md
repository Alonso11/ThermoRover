# ThermoBeacon 
> A temperature “beacon” broadcasting via BL.

 ![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.1-orange.svg)

## Features

-  **Real-time Temperature & Humidity Monitoring** using DHT22 sensor
-  **OLED Display (SSD1306)** - 128x64 visual feedback
-  **Bluetooth LE (BLE)** server with GATT characteristics
-  **Web Server** with responsive UI for remote monitoring
-  **WiFi Connectivity** for IoT integration
-  **Low Power Design** optimized for ESP32-C3
-  **System Health Monitoring** and diagnostics

##  Hardware Requirements

| Component | Description | Link |
|-----------|-------------|------|
| ESP32-C3 Super Mini | Main microcontroller | [Aliexpress](https://www.aliexpress.com) |
| DHT22 | Temperature & Humidity sensor | [Datasheet](https://www.sparkfun.com/datasheets/Sensors/Temperature/DHT22.pdf) |
| SSD1306 OLED | 128x64 I2C display | 0.96" variant recommended |
| Breadboard | Prototyping | Standard |
| Jumper Wires | Connections | Male-to-Male |
| LED| (built-in on GPIO2) | - |
|10kΩ  | Pull-up resistor for DHT22 | -|



## Project Structure
```
ThermoBeacon/
├── CMakeLists.txt              # Root CMake file
├── README.md                   # This file
├── LICENSE                     # MIT License
├── .gitignore                  # Git ignore rules
├── sdkconfig.defaults          # Default configuration
├── components/
│   └── esp-idf-lib/           # External component library
├── main/
│   ├── CMakeLists.txt         # Main component CMake
│   ├── idf_component.yml      # Component dependencies
│   ├── main.c                 # Main application entry
│   ├── config.h               # Configuration constants
│   ├── ble_server.c/.h        # BLE GATT server
│   ├── oled_display.c/.h      # OLED display driver
│   ├── sensor_dht.c/.h        # DHT22 sensor interface
│   ├── web_server.c/.h        # HTTP web server
│   └── wifi_manager.c/.h      # WiFi connection manager
└── docs/
    ├── API.md                 # API documentation
    ├── TROUBLESHOOTING.md     # Common issues
    └── CHANGELOG.md           # Version history
```

##  API Reference
