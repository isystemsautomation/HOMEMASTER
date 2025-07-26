# HOMEMASTER – Open Automation Hardware & Firmware

Welcome to the official GitHub repository for **HOMEMASTER** – a suite of open-source, industrial-grade smart automation controllers with a modular family of I/O expansion modules. 
This repository includes schematics, firmware and manuals for:

- ✅ MiniPLC & MicroPLC controllers  
- ✅ OpenTherm Gateway  
- ✅ A full range of extension modules  

---

## 🔧 Core Controllers

### 🟢 MiniPLC
![HomeMaster MiniPLC](https://github.com/isystemsautomation/HOMEMASTER/blob/main/MiniPLC/Images/MiniPLC2.png "HomeMaster MiniPLC")

A compact, DIN-rail mountable programmable logic controller featuring:
- 6 relay outputs
- Isolated digital inputs (24V with ISO1212 support)
- Analog I/O (ADS1115 / MCP4725)
- RTD (MAX31865) temperature input
- RS-485 with Modbus
- MicroSD logging
- OLED display, RTC, buzzer
- ESP32-WROOM-32U core
- ESPHome/Home Assistant ready

### 🔵 MicroPLC
![HomeMaster MicroPLC](https://github.com/isystemsautomation/HOMEMASTER/blob/main/MicroPLC/Images/MicroPLC.png "HomeMaster MicroPLC")

A smaller, more affordable controller for basic automation tasks:
- Relay output
- 1-wire
- RS-485 with Modbus
- RTC
- ESP32-WROOM-32U core
- ESPHome/Home Assistant ready

### 🔶 Opentherm Gateway
A communication bridge between ESP-based systems and OpenTherm-compatible boilers:
- ESP32 with OpenTherm protocol
- Home Assistant integration via ESPHome
- Useful for custom heating control setups

## 🧩 Extension Modules

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

## 📁 Repository Structure

/schematics/ → PDF project files for all products

/firmware/ → ESPHome YAMLs & MicroPython for RP2350A extension modules

/manuals/ → PDF setup and wiring guides

/resources/ → Logos, icons, branding, and images

## 📬 Contact & Support

- 🌐 Website: [www.home-master.eu](https://www.home-master.eu)  
- 📧 Email: contact@home-master.eu

---

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Open Automation. Real Control.**
