# HOMEMASTER – Modular, Resilient Smart Automation System

![License: CERN-OHL-W v2 + GPLv3 + MIT](https://img.shields.io/badge/license-CERN--OHL--W_v2%20%7C%20GPLv3%20%7C%20MIT-informational)
![Status: Open Hardware](https://img.shields.io/badge/hardware-open--source-brightgreen)
![Works with: ESPHome & Home Assistant](https://img.shields.io/badge/works%20with-ESPHome%20%26%20Home%20Assistant-blue)

**Version: 2025‑09** — Fully open‑source hardware, firmware, and configuration tools.

---

## 📑 Quick navigation
- [1. Introduction](#1-introduction)
  - [1.1 Overview of the HomeMaster ecosystem](#11-overview-of-the-homemaster-ecosystem)
  - [1.2 Modules & controllers](#12-modules--controllers)
  - [1.3 Use cases](#13-use-cases)
  - [1.4 Why HomeMaster? (Mission)](#14-why-homemaster-mission)
- [2. Quick start](#2-quick-start)
- [3. Safety information](#3-safety-information)
- [4. System overview](#4-system-overview)
  - [4.1 Topology diagram](#41-topology-diagram)
  - [4.2 MicroPLC vs MiniPLC](#42-microplc-vs-miniplc)
  - [4.3 Integration with Home Assistant](#43-integration-with-home-assistant)
- [5. Networking & communication](#5-networking--communication)
- [6. Software & UI configuration](#6-software--ui-configuration)
- [7. Programming & customization](#7-programming--customization)
- [8. Troubleshooting & FAQ](#8-troubleshooting--faq)
- [9. Open source & licensing](#9-open-source--licensing)
- [10. Downloads](#10-downloads)
- [11. Support](#11-support)
- [Appendix A. MiniPLC power supply and protection](#appendix-a-miniplc-power-supply-and-protection)
- [Appendix B. I/O circuitry details](#appendix-b-io-circuitry-details)
- [Appendix C. Web server and OTA updates](#appendix-c-web-server-and-ota-updates)

---

## 1. Introduction

### 1.1 Overview of the HomeMaster ecosystem
HomeMaster is an **industrial‑grade, modular automation system** for smart homes, labs, and professional installations. It features:

- ESP32‑based PLC controllers (**MiniPLC & MicroPLC**)
- A family of smart I/O modules (energy monitoring, lighting, alarms, analog I/O, etc.)
- **RS‑485 Modbus RTU** communication
- **ESPHome** compatibility for **Home Assistant**
- **USB‑C** & **WebConfig** UI for driverless configuration

> **Local resilience:** Modules include onboard logic and continue functioning even if the controller or network is offline.

### 1.2 Modules & controllers

<table>
<tr>
<td align="center" width="50%">
  <strong>🔵 MicroPLC</strong><br>
  <a href="./MicroPLC/Images/MicroPLC.png">
    <img src="./MicroPLC/Images/MicroPLC.png" alt="MicroPLC" height="220">
  </a><br><sub>Click to view full size</sub>
</td>
<td align="center" width="50%">
  <strong>🟢 MiniPLC</strong><br>
  <a href="./MiniPLC/Images/MiniPLC2.png">
    <img src="./MiniPLC/Images/MiniPLC2.png" alt="MiniPLC" height="220">
  </a><br><sub>Click to view full size</sub>
</td>
</tr>
</table>

#### Controllers

| Controller | Description |
|-----------|-------------|
| **MiniPLC**  | Advanced DIN‑rail controller with Ethernet, relays, analog I/O, RTD, display, SD logging |
| **MicroPLC** | Compact controller with RS‑485, relay, input, 1‑Wire, RTC, USB‑C |

#### Extension modules (detailed)

> Images on the left, capabilities on the right. Click a photo to open full resolution.

<table>
  <tr>
    <td width="35%" align="center" valign="top">
      <a href="./ENM-223-R1/Images/photo1.png">
        <img src="./ENM-223-R1/Images/photo1.png" alt="ENM‑223‑R1" width="260">
      </a>
      <br/><sub><b>ENM‑223‑R1 — 3‑Phase Energy Meter</b></sub>
    </td>
    <td width="65%" valign="top">
      <ul>
        <li><b>Metering IC:</b> ATM90E32AS — per‑phase <i>Urms, Irms, P/Q/S, PF, frequency</i> with totals.</li>
        <li><b>Relays:</b> 2× SPDT (NO/NC) for load control (5 A rated).</li>
        <li><b>Current inputs:</b> external CTs (333 mV or 1 V). <b>Voltage:</b> 3‑phase direct (L1/L2/L3 + N, PE).</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU (default 19200 8N1), USB‑C Web Serial Config.</li>
        <li><b>UI:</b> 4 buttons + 4 user LEDs; live diagnostics via Config Tool.</li>
        <li><b>Supply:</b> 24 VDC; DIN‑rail enclosure.</li>
        <li><b>Use cases:</b> sub‑metering, power quality KPIs, load shedding via relays.</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./ALM-173-R1/Images/photo1.png">
        <img src="./ALM-173-R1/Images/photo1.png" alt="ALM‑173‑R1" width="260">
      </a>
      <br/><sub><b>ALM‑173‑R1 — Alarm I/O</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>Inputs:</b> 17 opto‑isolated digital inputs (5 V logic) for PIR, magnetic contacts, tamper, panic, etc.</li>
        <li><b>Relays:</b> 3× SPDT (NO/NC), up to 16 A; follow alarm groups or manual overrides.</li>
        <li><b>Aux rails:</b> isolated 12 V & 5 V for powering sensors.</li>
        <li><b>UI:</b> 4 buttons, 4 LEDs; button overrides & acknowledgements.</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU; USB‑C Web Serial Config Tool.</li>
        <li><b>MCU:</b> RP2350A; persistent config in flash.</li>
        <li><b>Supply:</b> 24 VDC; DIN‑rail.</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./DIM-420-R1/Images/photo1.png">
        <img src="./DIM-420-R1/Images/photo1.png" alt="DIM‑420‑R1" width="260">
      </a>
      <br/><sub><b>DIM‑420‑R1 — Dual‑Channel AC Dimmer</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>Channels:</b> 2 phase‑cut dimming outputs (per‑channel Leading/Trailing edge).</li>
        <li><b>Inputs:</b> 4 digital inputs, configurable as <i>momentary</i> or <i>latching</i> with rich press logic (short/long/double/short‑then‑long).</li>
        <li><b>UI:</b> 4 buttons + 4 LEDs (steady/blink sources: channel/AC/DI).</li>
        <li><b>Zero‑cross:</b> dual ZC sensing; reports mains frequency per channel.</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU; Web Serial Config Tool; ESPHome YAML available.</li>
        <li><b>MCU:</b> RP2350A; autosave with LittleFS.</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./AIO-422-R1/Images/photo1.png">
        <img src="./AIO-422-R1/Images/photo1.png" alt="AIO‑422‑R1" width="260">
      </a>
      <br/><sub><b>AIO‑422‑R1 — Analog I/O + RTD</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>Analog in:</b> 4× 0–10 V via ADS1115 (16‑bit).</li>
        <li><b>Analog out:</b> 2× 0–10 V via MCP4725 (12‑bit + op‑amp).</li>
        <li><b>RTD:</b> 2 channels PT100/PT1000 using MAX31865 (2/3/4‑wire).</li>
        <li><b>UI:</b> 4 buttons + status LEDs; USB‑C for flashing/diagnostics.</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU; seamless ESPHome/Home Assistant integration.</li>
        <li><b>Supply:</b> 24 VDC; DIN‑rail (≈3M wide).</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./DIO-430-R1/Images/photo1.png">
        <img src="./DIO-430-R1/Images/photo1.png" alt="DIO‑430‑R1" width="260">
      </a>
      <br/><sub><b>DIO‑430‑R1 — Digital I/O</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>Relays:</b> 3× SPDT (NO/NC), up to 16 A; pulse/toggle/override modes.</li>
        <li><b>Inputs:</b> 4× isolated 24 VDC; per‑input action & target mapping.</li>
        <li><b>UI:</b> 3 buttons + 3 LEDs; assignable override/functions.</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU; USB‑C Web Serial Config Tool.</li>
        <li><b>Persistence:</b> LittleFS autosave; CRC‑guarded config structure.</li>
        <li><b>Supply:</b> 24 VDC; DIN‑rail.</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./RGB-620-R1/Images/photo1.png">
        <img src="./RGB-620-R1/Images/photo1.png" alt="RGB‑620‑R1" width="260">
      </a>
      <br/><sub><b>RGB‑620‑R1 — RGBCCT LED Control</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>PWM:</b> 5 independent channels for RGB + Tunable White (CCT); smooth fades & transitions.</li>
        <li><b>Inputs:</b> 2 isolated digital inputs for wall switches/motion.</li>
        <li><b>Relay:</b> 1× NO relay for driver/aux load switching.</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU; USB‑C; ESPHome/Home Assistant ready.</li>
        <li><b>MCU & Supply:</b> RP2350A; 24 VDC; DIN‑rail.</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./STR-3221-R1/Images/photo1.png">
        <img src="./STR-3221-R1/Images/photo1.png" alt="STR‑3221‑R1" width="260">
      </a>
      <br/><sub><b>STR‑3221‑R1 — Staircase LED Controller</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>Channels:</b> 32 constant‑current LED outputs; animated sequences for stair effects.</li>
        <li><b>Inputs:</b> 3 opto‑isolated (top/bottom motion + override).</li>
        <li><b>Interface:</b> RS‑485 Modbus RTU; USB‑C configuration.</li>
        <li><b>Power:</b> 24 VDC; reverse‑polarity protected; DIN‑rail.</li>
      </ul>
    </td>
  </tr>

  <tr>
    <td align="center" valign="top">
      <a href="./WLD-521-R1/Images/photo1.png">
        <img src="./WLD-521-R1/Images/photo1.png" alt="WLD‑521‑R1" width="260">
      </a>
      <br/><sub><b>WLD‑521‑R1 — Water/Leak Detection</b></sub>
    </td>
    <td valign="top">
      <ul>
        <li><b>Inputs:</b> 5 opto‑isolated digital inputs for leak probes & pulse meters (up to 1 kHz configurable).</li>
        <li><b>Relays:</b> 2× (NO/NC) for valves, pumps, alarms.</li>
        <li><b>1‑Wire:</b> DS18B20 support for temperature‑aware logic.</li>
        <li><b>Aux power:</b> isolated 12 V & 5 V rails for sensors.</li>
        <li><b>UI & Interface:</b> 4 buttons + LEDs; RS‑485 Modbus RTU; USB‑C.</li>
        <li><b>Supply:</b> 24 VDC; DIN‑rail.</li>
      </ul>
    </td>
  </tr>
</table>

### 1.3 Use cases
- [x] Smart energy monitoring and control  
- [x] Smart lighting and climate control  
- [x] Leak detection and safety automation  
- [x] Modbus‑connected distributed systems  
- [x] Industrial and home lab control  

### 1.4 Why HomeMaster? (Mission)
- **Resilient by design:** Local logic ensures core functions continue without network/cloud.
- **Industrial yet maker‑friendly:** DIN‑rail hardware with ESPHome simplicity.
- **Open & repairable:** Open hardware, firmware, and tools; long‑term maintainability.

[Back to top ↑](#-quick-navigation)

---


## 2. Choosing the Right PLC and Modules

### 2.1 MiniPLC vs MicroPLC – Selection Guide

| Feature / Use Case             | 🟢 **MiniPLC**                                   | 🔵 **MicroPLC**                               |
|-------------------------------|--------------------------------------------------|-----------------------------------------------|
| Size                          | Full-width DIN enclosure                         | Compact DIN enclosure                         |
| Onboard I/O                   | 6x Relays, 4x DI, 2x RTD, 2x AI/O, Display       | 1x Relay, 1x DI, RTC, 1-Wire                   |
| Connectivity                  | Ethernet, USB‑C, Wi‑Fi                           | USB‑C, Wi‑Fi, Bluetooth (Improv)              |
| Storage                       | microSD card slot                                | Internal flash only                           |
| Ideal for                     | Full homes, labs, smart HVAC, solar controllers  | Rooms, lighting control, energy sub-metering  |
| Power input                   | AC/DC wide range or 24 VDC                       | 24 VDC only                                   |
| ESPHome integration           | Yes, with rich entity exposure                   | Yes, ideal for smaller YAML configs           |
| Installation type             | Centralized, all-in-one                          | Distributed, compact use                      |

---

### 2.2 Module Comparison Table

| Module Code     | Digital Inputs | Analog / RTD      | Relay Outputs | Special Features                          | Typical Use Cases                         |
|-----------------|----------------|-------------------|----------------|--------------------------------------------|-------------------------------------------|
| **ENM‑223‑R1**  | —              | Voltage + CTs     | 2 relays       | 3‑phase metering, power KPIs              | Grid, solar, energy sub-metering          |
| **ALM‑173‑R1**  | 17 DI          | —                 | 3 relays       | Sensor AUX power, alarm logic             | Security, panic, tamper, window contacts  |
| **DIM‑420‑R1**  | 4 DI           | —                 | 2 dim outputs  | AC dimming, press logic, LED feedback     | Room lighting, stair lighting             |
| **AIO‑422‑R1**  | —              | 4 AI + 2 RTD      | 2 AO           | 0–10 V input/output, PT100/PT1000         | HVAC, environmental sensors               |
| **DIO‑430‑R1**  | 4 DI           | —                 | 3 relays       | Logic mapping, override buttons           | Generic input/output, control boards      |
| **RGB‑620‑R1**  | 2 DI           | —                 | 1 relay        | 5x PWM (RGB+CCT), LED fades               | RGB lighting, wall-switch control         |
| **STR‑3221‑R1** | 3 DI           | —                 | —              | 32-channel LED sequencing (TLC59208F)      | Stair lights, animation control           |
| **WLD‑521‑R1**  | 5 DI           | 1‑Wire Temp       | 2 relays       | Leak detection, pulse metering            | Bathrooms, kitchens, utility rooms        |

---

### 2.3 Recommended Setups

- 🏠 **Starter Setup (Lighting + I/O)**  
  🔹 MicroPLC + DIO‑430‑R1 + RGB‑620‑R1  
  👉 For basic lighting control, wall switch input, RGB strip control.

- ⚡ **Energy Monitoring Setup**  
  🔹 MicroPLC + ENM‑223‑R1  
  👉 For tracking grid power, solar production, or 3-phase loads.

- 🧪 **Lab / Professional Setup**  
  🔹 MiniPLC + any mix of modules  
  👉 Best for complex automation with analog, temperature, safety logic.

- 💧 **Safety & Leak Detection**  
  🔹 MicroPLC + WLD‑521‑R1 + ALM‑173‑R1  
  👉 Secure your home with leak sensors, alarm inputs, and auto-valve control.

- 🌈 **RGB + Dimming + Scenes**  
  🔹 MiniPLC or MicroPLC + RGB‑620‑R1 + DIM‑420‑R1  
  👉 Create scenes with ESPHome automations and HA dashboards.

---


## 3. Safety information

### 3.1 General electrical safety
- Only trained personnel should install or service modules.
- Disconnect all power sources before wiring or reconfiguring.
- Always follow local electrical codes and standards.

### 3.2 Handling & installation
- Mount on 35 mm DIN rails inside protective enclosures.
- Separate low‑voltage and high‑voltage wiring paths.
- Avoid exposure to moisture, chemicals, or extreme temperatures.

### 3.3 Device‑specific warnings
- Connect PE/N properly for metering modules.
- Use correct CTs (1 V or 333 mV) — never connect 5 A CTs directly.
- Avoid reverse polarity on RS‑485 lines.

[Back to top ↑](#-quick-navigation)

---

## 4. System overview

### 4.1 Topology diagram
> **Diagram placeholder:** include an `Images/system_topology.svg` illustrating:  
> Home Assistant ↔ (Wi‑Fi/Ethernet) ↔ **MiniPLC/MicroPLC** ↔ (RS‑485 Modbus RTU) ↔ **Extension Modules** (ENM/DIO/DIM/…);  
> Local logic highlighted inside each module.

### 4.2 MicroPLC vs MiniPLC

| Feature      | MiniPLC        | MicroPLC      |
|--------------|----------------|---------------|
| Size         | Full DIN       | Compact DIN   |
| I/O          | Rich onboard   | Basic onboard |
| Connectivity | Ethernet + USB | USB only      |
| Expansion    | Modbus RTU     | Modbus RTU    |
| Target use   | Large systems  | Small systems |

### 4.3 Integration with Home Assistant
- ESPHome integration: modules appear as devices with sensors, switches, and alarms.
- Home Assistant can use entities for dashboards, automations, and energy monitoring.
- Use **YAML packages** to add ENM, ALM, DIM, etc. quickly.

[Back to top ↑](#-quick-navigation)

---

## 5. Networking & communication

### 5.1 RS‑485 Modbus
- All modules use Modbus RTU (slave) over RS‑485.
- Default: `19200 8N1` (configurable).
- Bus topology supported; use **120 Ω termination** at ends; observe biasing.

### 5.2 USB‑C configuration
- Use `ConfigToolPage.html` (no drivers needed) in Chrome/Edge.
- Enables calibration, phase mapping, relay control, alarm config, etc.
- Available for each module type.

### 5.3 Wi‑Fi and Bluetooth
- Wi‑Fi on **MiniPLC** and **MicroPLC**.
- **Improv Wi‑Fi** onboarding via BLE supported (MicroPLC only).
- Once connected, modules communicate over RS‑485; controllers expose them wirelessly.

### 5.4 Ethernet
- Available on **MiniPLC** only.
- Enables fast and stable connection to Home Assistant or MQTT brokers.

[Back to top ↑](#-quick-navigation)

---

## 6. Software & UI configuration

### 6.1 Web Config Tool (USB Web Serial)
- HTML file that runs locally in the browser (no install needed).
- Features per module:
  - Modbus address & baud rate
  - Relay control
  - Alarm rules
  - Input mappings
  - LED behavior
  - Calibration / phase mapping

### 6.2 ESPHome Wi‑Fi setup (via controller)
- MiniPLC/MicroPLC expose connected modules using `modbus_controller:` in ESPHome.
- Use `packages:` with variable overrides for each ENM or DIM module.
- Add ESPHome device to Home Assistant and select energy sensors or switches.

[Back to top ↑](#-quick-navigation)

---

## 7. Programming & customization

### 7.1 Supported languages
- **Arduino IDE**
- **PlatformIO**
- **MicroPython** (via Thonny)
- **ESPHome YAML** (default config for most users)

### 7.2 Flashing via USB‑C
- All controllers and modules support auto‑reset via USB‑C.
- No need to press buttons — supports drag‑and‑drop UF2 (RP2040/RP2350) or ESPHome Web Flasher.

### 7.3 PlatformIO & Arduino
- Clone firmware repository.
- Use `default_xxx.ino` sketches for each module.
- Add libraries: `ModbusSerial`, `LittleFS`, `Arduino_JSON`, `SimpleWebSerial`.

[Back to top ↑](#-quick-navigation)

---

## 10. Downloads

- 📥 **Firmware (INO / YAML examples):** <https://github.com/isystemsautomation/HOMEMASTER/tree/main/Firmware>
- 🛠 **Config Tools (HTML):** <https://github.com/isystemsautomation/HOMEMASTER/tree/main/tools>
- 📷 **Images & Diagrams:** <https://github.com/isystemsautomation/HOMEMASTER/tree/main/Images>
- 📐 **Schematics:** <https://github.com/isystemsautomation/HOMEMASTER/tree/main/Schematics>
- 📖 **Manuals (PDF):** <https://github.com/isystemsautomation/HOMEMASTER/tree/main/Manuals>

[Back to top ↑](#-quick-navigation)

---

## 11. Support

- 🌐 **Official Support Portal:** <https://www.home-master.eu/support>  
- 🧠 **Hackster.io Projects:** <https://www.hackster.io/homemaster>  
- 🎥 **YouTube Channel:** <https://www.youtube.com/channel/UCD_T5wsJrXib3Rd21JPU1dg>  
- 💬 **Reddit /r/HomeMaster:** <https://www.reddit.com/r/HomeMaster>  
- 📷 **Instagram:** <https://www.instagram.com/home_master.eu>

[Back to top ↑](#-quick-navigation)

---
