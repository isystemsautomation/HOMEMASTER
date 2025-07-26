# MicroPLC – Open-Source Automation Controller for Home & Industry

**MicroPLC** is a compact and powerful open-source automation controller based on the **ESP32-WROOM-32U**. Designed for seamless integration with **Home Assistant** using **ESPHome**, it enables control of smart home devices, sensors, actuators, and industrial systems through RS-485 Modbus and wireless communication.

## 💡 Overview

MicroPLC serves as a central unit for managing digital and analog I/O extension modules, executing automation logic, and connecting sensors and actuators. With its modular design and DIN-rail mount enclosure, it’s ideal for electrical panels in smart homes, HVAC systems, lighting control, and industrial process monitoring.

Out of the box, MicroPLC comes with ESPHome pre-installed and ready to integrate with Home Assistant or other MQTT-based platforms.

---

## 🔧 Features

- **ESP32-WROOM-32U** microcontroller with Wi-Fi and Bluetooth
- **ESPHome compatible firmware** for seamless Home Assistant integration
- **RS-485 Modbus RTU** interface for extension module communication
- **USB Type-C port** for programming, debugging, and power
- **1-Wire interface** with ESD and overvoltage protection
- **PCF8563 RTC** for accurate time-based automation
- **One industrial-grade relay** with varistor and opto-isolation
- **One 24V digital input** with surge protection (ISO1212)
- **Four front-panel buttons** and **status LEDs** for local control and diagnostics
- **DIN-rail mountable** for standard electrical enclosures

---

## 🔌 Connectivity

- **RS-485 Modbus RTU**: For reliable communication with analog/digital I/O modules, relays, sensors, etc.
- **Wi-Fi / Bluetooth**: Built-in wireless communication for integration with smart home platforms.
- **USB Type-C**: For firmware upload, console access, and device power.
- **1-Wire**: Support for DS18B20 and other compatible digital temperature sensors.

---


## 🧩 Compatible Extension Modules

| Module Code    | Name / Function |
|----------------|------------------|
| **AIO-422-R1** | Analog I/O Module with 2 RTD, 4 analog inputs (0–10V), 2 analog outputs (0–10V) |
| **ALM-173-R1** | Alarm Input & Relay Output Module with 17 alarm inputs, 3 relay outputs |
| **DIM-420-R1** | 2-Channel Dimmer Module with 4 digital inputs – ideal for smart lighting |
| **DIO-430-R1** | General-purpose Digital 4 Input 3 Relay Output Module |
| **ENM-223-R1** | 3-Phase Energy Monitor with 2 relays |
| **RGB-620-R1** | RGB Lighting Control Module – 6 MOSFET LED channels, 2 DI, Modbus |
| **STR-3221-R1**| Stair LED Controller – 32 LED outputs, 2 presence sensors, 1 switch input |
| **WLD-521-R1** | Water Leak Detector & Valve Controller – 5 leak/pulse inputs, 2 relays, 1-Wire bus |

---

## 📦 Specifications

| Feature              | Details                              |
|----------------------|--------------------------------------|
| Microcontroller      | ESP32-WROOM-32U                      |
| Power Supply         | 5V via USB-C for programming or 24V via terminal     |
| Relay Output         | 1x 16A (optically isolated)     |
| Digital Input        | 1x 24V DI (ISO1212-based)            |
| Communication        | RS-485, Wi-Fi, Bluetooth, USB-C      |
| RTC                  | PCF8563                              |
| 1-Wire               | 1 channel (ESD/OVP protected)        |
| Mounting             | DIN-rail                             |
| Firmware             | ESPHome (pre-installed), Arduino |

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
