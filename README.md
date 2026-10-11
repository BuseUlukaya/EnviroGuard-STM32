# EnviroGuard-STM32
STM32-based IoT environmental monitoring system featuring ESP8266 Wi-Fi communication, real-time data visualization, SQLite logging, and AI-powered anomaly detection.
#  EnviroGuard — STM32-Based IoT Environmental Monitoring System

**EnviroGuard** is an IoT-based environmental monitoring system developed using the STM32F407 Discovery board, DHT22 sensor, and ESP8266 Wi-Fi module.

The project combines **embedded systems, sensor interfacing, UART communication, TCP networking, real-time data visualization, SQLite database management, and AI-assisted anomaly detection**.

## Key Features

- **Temperature & Humidity Monitoring:** Environmental data collection using the DHT22 sensor.
- **STM32 Microcontroller:** Sensor reading, data processing, and GPIO control using STM32 HAL.
- **OLED Display:** Real-time temperature and humidity readings displayed on an SSD1306 OLED screen.
- **UART Communication:** Data transmission from STM32 to ESP8266.
- **Wi-Fi & TCP Networking:** Wireless data transmission through the ESP8266 module.
- **LED Status Indicators:** Temperature-based control of red and green LEDs.
- **Real-Time Visualization:** Live temperature and humidity graphs using Python.
- **SQLite Database:** Local storage and management of environmental measurements.
- **AI-Assisted Analysis:** Separate temperature and humidity analysis to identify potential anomalous measurements.

##  Hardware Components

| Component                              | Function                        |
| -------------------------------------- | ------------------------------- |
| STM32F407 Discovery                    | Main microcontroller            |
| DHT22                                  | Temperature and humidity sensor |
| ESP8266 NodeMCU                        | Wi-Fi and TCP communication     |
| SSD1306 OLED                           | Real-time display               |
| Green LED                              | Normal temperature indication   |
| Red LED                                | High temperature indication     |
| Breadboard, resistors and jumper wires | Circuit connections             |

##  Software & Technologies

- **Embedded Development:** C, STM32 HAL, STM32CubeIDE
- **Communication:** UART, I2C, Wi-Fi, TCP/IP
- **ESP8266 Programming:** Arduino IDE
- **Data Processing:** Python
- **Database:** SQLite
- **Visualization:** Real-time temperature and humidity graphs
- **AI Analysis:** Anomaly detection and outlier identification
- **Network Testing:** Hercules TCP Client
- **Version Control:** Git & GitHub

##  System Architecture

```text
                   DHT22 Sensor
                        |
                        v
                STM32F407 Discovery
                 |       |       |
                 |       |       |
                 v       v       v
               OLED    LEDs     UART
                                  |
                                  v
                            ESP8266 Wi-Fi
                                  |
                                  v
                              TCP Server
                                  |
                                  v
                           Python Application
                             |          |
                             v          v
                         Live Graphs   SQLite
                                         |
                                         v
                                   AI Analysis
```

##  Temperature-Based LED Control

The system uses LED indicators to represent temperature conditions.

| Temperature    | LED Status      |
| -------------- | --------------- |
| Below 25°C     | 🟢 Green LED ON |
| 25°C and above | 🔴 Red LED ON   |

The temperature threshold is configurable in the STM32 firmware.

##  Communication System

The STM32F407 microcontroller collects temperature and humidity measurements from the DHT22 sensor.

Measurements are transmitted to the ESP8266 module using **UART communication**.

The ESP8266 provides Wi-Fi connectivity and operates as a **TCP server**, allowing environmental data to be received on a computer over the local network.

**TCP Port:** `8080`

Hercules TCP Client can be used to monitor the transmitted data.

##  Real-Time Data Visualization

A Python-based visualization application displays incoming environmental measurements through temperature and humidity graphs.

This allows users to observe environmental changes and monitor sensor readings over time.

##  SQLite Database Integration

Environmental measurements are stored locally using SQLite.

The database supports historical data inspection and provides input for further analysis.

The monitoring application is configured to retain the **latest 25 measurements**.

##  AI-Assisted Anomaly Detection

EnviroGuard includes an AI-assisted analysis component for evaluating environmental measurements.

Temperature and humidity are analyzed separately to help identify unusual readings and potential outliers.

The analysis distinguishes between normal observations and measurements that may require further investigation.

**Note:** Anomaly candidates are not necessarily sensor failures or dangerous environmental conditions.

## 📁 Repository Structure

```text
EnviroGuard-STM32/
├── firmware/
│   ├── STM32/
│   └── ESP8266/
├── visualization/
├── analysis/
├── database/
├── media/
├── docs/
└── README.md
```

##  Project Objectives

The main objectives of EnviroGuard are to:

1. Develop practical experience with STM32 microcontrollers and embedded C programming.
2. Implement sensor communication and peripheral control.
3. Establish UART and Wi-Fi-based TCP communication.
4. Integrate embedded hardware with Python software.
5. Store and visualize environmental measurements.
6. Explore AI-assisted analysis of real-world sensor data.

## 👩‍💻 Developed By

**Buse Ulukaya**
Computer Engineering Student

---

*EnviroGuard demonstrates the integration of embedded hardware, wireless networking, data visualization, database systems, and AI-assisted environmental monitoring.*
