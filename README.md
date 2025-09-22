# HOMEMASTER – Modular, Resilient Smart Automation System

**HOMEMASTER** is an open-source, industrial-grade control ecosystem designed for resilient smart home and automation applications. Powered by ESPHome and built for Home Assistant integration, it offers both **MiniPLC** and **MicroPLC** core controllers, alongside a family of RS‑485 Modbus-based I/O expansion modules.

---

## 🚀 Why HomeMaster?

- ✅ **Local automation logic** — modules continue operating even without a controller or network connection.
- ✅ **ESPHome pre-installed** — seamless integration with Home Assistant.
- ✅ **WebConfig** — browser-based, driverless setup for all modules.
- ✅ **Built to last** — surge-protected, industrial-grade hardware with repairable design.
- ✅ **100% Open Source** — firmware, schematics, and config tools available.

---

## 🧠 Core Controllers

### 🟢 MiniPLC
![MiniPLC](./MiniPLC/Images/MiniPLC2.png)

DIN-rail mountable flagship controller for advanced installations.

- ESP32-WROOM-32U (Wi-Fi, Bluetooth, Ethernet)
- 6 industrial relays
- 4 opto-isolated digital inputs (ISO1212)
- 2 analog inputs (ADS1115), 1 analog output (MCP4725)
- RTD temperature input (MAX31865)
- 1-Wire support, RS‑485 Modbus RTU
- OLED display, microSD logging, RTC, buzzer
- Wide-range power input (9–30V DC)
- WebConfig & ESPHome-ready

---

### 🔵 MicroPLC
![MicroPLC](./MicroPLC/Images/MicroPLC.png)

Compact controller for distributed or basic setups.

- ESP32-WROOM-32U
- RS‑485 Modbus RTU
- Relay output + digital inputs
- 1-Wire, RTC, USB-C programming
- DIN-rail housing, auto-reset USB flashing
- WebConfig & ESPHome-ready

---

## 🔌 Expansion Modules

All modules communicate via **RS‑485 (Modbus RTU)** and support **local logic**. Configurable via **WebConfig**, each module continues to operate autonomously even if the controller goes offline.

### AIO-422-R1
![AIO-422-R1](./AIO-422-R1/Images/photo1.png)

**Analog I/O Module with 2 RTD, 4 analog inputs (0–10V), 2 analog outputs (0–10V)**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### ALM-173-R1
![ALM-173-R1](./ALM-173-R1/Images/photo1.png)

**Alarm Input & Relay Output Module with 17 alarm inputs, 3 relay outputs**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### DIM-420-R1
![DIM-420-R1](./DIM-420-R1/Images/photo1.png)

**2-Channel Dimmer Module with 4 digital inputs – ideal for smart lighting**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### DIO-430-R1
![DIO-430-R1](./DIO-430-R1/Images/photo1.png)

**General-purpose Digital 4 Input 3 Relay Output Module**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### ENM-223-R1
![ENM-223-R1](./ENM-223-R1/Images/photo1.png)

**3-Phase Energy Monitor with 2 relays**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### RGB-620-R1
![RGB-620-R1](./RGB-620-R1/Images/photo1.png)

**RGB Lighting Control Module – 6 MOSFET LED channels, 2 DI, Modbus**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### STR-3221-R1
![STR-3221-R1](./STR-3221-R1/Images/photo1.png)

**Stair LED Controller – 32 LED outputs, 2 presence sensors, 1 switch input**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

### WLD-521-R1
![WLD-521-R1](./WLD-521-R1/Images/photo1.png)

**Water Leak Detector & Valve Controller – 5 leak/pulse inputs, 2 relays, 1-Wire bus**  
- Local logic & Modbus RTU  
- WebConfig browser setup  
- ESPHome/Home Assistant compatible  

---

## 🧰 Quick Setup

- All controllers and modules ship with firmware pre-installed.
- Configuration takes minutes using [WebConfig](https://www.home-master.eu).
- Add devices to ESPHome YAML and they appear in Home Assistant.

## 🗂️ Repository Structure

- `/MiniPLC`, `/MicroPLC` – Core controller folders
- `/AIO-422-R1`, `/DIM-420-R1`, etc. – Extension modules with schematics & firmware
- `/manuals` – PDF wiring guides
- `/resources` – Icons, images, and branding

## 📦 Designed for Production

- Hardware and enclosures are already built and tested.
- Modules are fully assembled and functionally verified in-house.
- Shipping ready from EU & US locations.

## 📄 License

- Hardware: **CERN-OHL-W 2.0**
- Firmware: **GPLv3**
- Fully open-source and modifiable

---

> 🔧 **HomeMaster – Resilient control. Open automation. Built to last.**
