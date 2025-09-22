# HOMEMASTER – Modular, Resilient Smart Automation System

[![License: CERN-OHL-W v2 \| GPLv3 \| MIT](https://img.shields.io/badge/license-CERN--OHL--W_v2%20%7C%20GPLv3%20%7C%20MIT-informational)](./LICENSE)
![Status: Open Hardware](https://img.shields.io/badge/hardware-open--source-brightgreen)
![Works with: ESPHome & Home Assistant](https://img.shields.io/badge/works%20with-ESPHome%20%26%20Home%20Assistant-blue)

**Version: 2025‑09** — Fully open‑source hardware, firmware, and configuration tools.

> **Local resilience:** Every extension module includes onboard logic and keeps core functions running even if the controller or network goes offline.

---

## 📑 Quick navigation
- [1. Introduction](#1-introduction)
  - [1.1 Overview of the HomeMaster ecosystem](#11-overview-of-the-homemaster-ecosystem)
  - [1.2 Controllers](#12-controllers)
  - [1.3 Modules overview](#13-modules-overview)
  - [1.4 Mission & use cases](#14-mission--use-cases)
- [2. Quick start](#2-quick-start)
- [3. Safety information](#3-safety-information)
- [4. System overview](#4-system-overview)
  - [4.1 Topology diagram](#41-topology-diagram)
  - [4.2 MiniPLC vs MicroPLC](#42-miniplc-vs-microplc)
  - [4.3 Integration with Home Assistant](#43-integration-with-home-assistant)
- [5. Networking & communication](#5-networking--communication)
- [6. Software & configuration tools](#6-software--configuration-tools)
- [7. Programming & customization](#7-programming--customization)
- [8. Troubleshooting & FAQ](#8-troubleshooting--faq)
- [9. Open source & licensing](#9-open-source--licensing)
- [10. Downloads](#10-downloads)
- [11. Support](#11-support)

---

## 1. Introduction

### 1.1 Overview of the HomeMaster ecosystem
HomeMaster is an **industrial‑grade, modular automation system** for smart homes, labs, and professional installations. It features:

- ESP32‑based PLC controllers (**MiniPLC & MicroPLC**) — same MCU and memory class
- A family of smart I/O extension modules (energy monitoring, lighting, alarms, analog I/O, etc.)
- **RS‑485 Modbus RTU** communication between controllers and modules
- **ESPHome** out of the box for **Home Assistant**
- **USB‑C** & **WebConfig** (browser) for driverless configuration


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
  ... (truncated for now — we'll paste in full below)
</table>
 Modules overview
Below is a concise overview. Each module has its **own page** with wiring, manuals, and ESPHome configs.

| Module | What it does | Highlights |
|---|---|---|
| **ENM‑223‑R1** – 3‑phase energy meter | Sub‑metering & KPIs | ATM90E32AS; per‑phase Urms/Irms/P/Q/S/PF; 2× relays; CT 333 mV/1 V |
| **ALM‑173‑R1** – Alarm I/O | Security inputs + high‑current relays | 17 opto DIs; 3× SPDT to 16 A; isolated 12 V/5 V rails |
| **DIM‑420‑R1** – AC dimmer (2‑ch) | Phase‑cut lighting | Leading/trailing per‑channel; rich wall‑switch logic |
| **AIO‑422‑R1** – Analog I/O + RTD | 0–10 V in/out, PT100/1000 | 4× AI (ADS1115), 2× AO (MCP4725), 2× RTD (MAX31865) |
| **DIO‑430‑R1** – Digital I/O | General‑purpose control | 4× isolated DIs, 3× SPDT relays, override buttons |
| **RGB‑620‑R1** – RGBCCT LED | 5‑ch PWM + relay | RGB+CCT, smooth fades, 2× DIs |
| **STR‑3221‑R1** – Staircase LED | Animated stair effects | 32 constant‑current outputs; 3 opto inputs |
| **WLD‑521‑R1** – Water/leak | Leak probes + pulse metering | 5 opto DIs; 2 relays; 1‑Wire temperature |

### 1.4 Mission & use cases
- **Resilient by design:** Local logic on modules keeps essentials running without network/cloud.
- **Industrial yet maker‑friendly:** DIN‑rail hardware with ESPHome simplicity.
- **Open & repairable:** Open hardware, firmware, and tools; long‑term maintainability.

**Typical use cases**
- Smart energy monitoring and control
- Smart lighting and climate control
- Leak detection and safety automation
- Modbus‑connected distributed systems
- Industrial and home lab control

---

## 2. Quick start
1. **Choose your controller** — MiniPLC (full‑featured, standalone‑capable) or MicroPLC (compact + extensions).
2. **Wire the RS‑485 bus** — A/B differential pair, **120 Ω termination** at both ends; add biasing as needed.
3. **Power up the controller** — **ESPHome is preinstalled.** Use **Improv** (BLE or Serial) to set Wi‑Fi.
4. **Configure each extension module** — Connect via **USB‑C** and use **WebConfig** (browser) to set **Modbus address and module settings**.
5. **Adopt in Home Assistant** — Add the ESPHome controller; entities for connected modules will appear per your ESPHome config.

---

## 3. Safety information
- Installation by qualified personnel; always follow local electrical codes.
- Disconnect all power sources before wiring or reconfiguring.
- Mount on 35 mm DIN rails in protective enclosures; separate low‑ and high‑voltage paths.
- Metering modules: connect PE/N properly; use the correct CT type (333 mV or 1 V) — **never** 5 A CTs directly.
- RS‑485: observe polarity; terminate bus ends with **120 Ω**.

---

## 4. System overview

### 4.1 Topology diagram
> **Diagram placeholder:** include an `Images/system_topology.svg` illustrating:  
> Home Assistant ↔ (Ethernet/Wi‑Fi) ↔ **MiniPLC / MicroPLC** ↔ (RS‑485 Modbus RTU) ↔ **Extension Modules** (ENM/DIO/DIM/…);  
> Local logic highlighted inside each module.

### 4.2 MiniPLC vs MicroPLC
(See the **summary table** in [1.2 Controllers](#12-controllers) for a concise comparison.)

### 4.3 Integration with Home Assistant
- Controllers run **ESPHome**; modules speak **Modbus RTU** via `modbus_controller:` in ESPHome.
- Home Assistant discovers entities for sensors, switches, dimmers, alarms, etc.
- Use **YAML packages** to add ENM, ALM, DIM, and other modules quickly.

---

## 5. Networking & communication

### 5.1 RS‑485 Modbus
- All modules use Modbus RTU (slave) over RS‑485.
- Default: `19200 8N1` (configurable).
- Bus topology supported; use **120 Ω termination** at ends; provide biasing.

### 5.2 USB‑C configuration (WebConfig)
- Use the WebConfig HTML (no drivers) in Chrome/Edge.
- Per‑module functions include: **Modbus address & baud**, relay control, input mapping, LED behavior, calibration/phase mapping, alarm rules.

### 5.3 Wi‑Fi & Bluetooth
- **Both controllers**: Wi‑Fi, **BLE**, and **Improv** onboarding (BLE & Serial).

### 5.4 Ethernet
- **MiniPLC only** — for fast, robust connection to Home Assistant/MQTT.

---

## 6. Software & configuration tools

### 6.1 WebConfig Tool (USB Web Serial)
- Runs locally in the browser; no install required.
- Unified configuration across all modules.

### 6.2 ESPHome setup
- MiniPLC/MicroPLC expose connected modules using `modbus_controller:` in ESPHome.
- Use `packages:` with variable overrides for each extension module.

---

## 7. Programming & customization
- **ESPHome YAML** (default for most users; preinstalled on both controllers)
- **Arduino IDE** / **PlatformIO**
- **MicroPython** (via Thonny)
- USB‑C supports auto‑reset/programming (no boot/reset buttons).

> Developer details (libraries, sketches) live in the firmware repo and per‑module docs.

---

## 8. Troubleshooting & FAQ
- **I can’t see modules in Home Assistant** → Check RS‑485 wiring and termination; verify Modbus addresses in WebConfig match your ESPHome YAML.
- **Improv isn’t pairing** → Try Serial Improv via USB‑C; ensure BLE is enabled on your host.
- **Energy readings look off** → Verify CT type and orientation; run calibration in WebConfig.
- **Dimmers flicker** → Confirm leading/trailing‑edge selection and zero‑cross detection; check driver compatibility.

---

## 9. Open source & licensing
- **Hardware:** CERN‑OHL‑W v2  
- **Firmware:** GPLv3  
- **Tools & examples:** MIT

See `LICENSE` for full terms and how to contribute under these licenses.

---

## 10. Downloads
- 📥 **Firmware & ESPHome packages:** `Firmware/`
- 🛠 **WebConfig tools (HTML):** `tools/`
- 📷 **Images & diagrams:** `Images/`
- 📐 **Schematics:** `Schematics/`
- 📖 **Manuals (PDF):** `Manuals/`

---

## 11. Support
- 🌐 **Official Support Portal:** <https://www.home-master.eu/support>  
- 🧠 **Hackster.io Projects:** <https://www.hackster.io/homemaster>  
- 🎥 **YouTube Channel:** <https://www.youtube.com/channel/UCD_T5wsJrXib3Rd21JPU1dg>  
- 💬 **Reddit /r/HomeMaster:** <https://www.reddit.com/r/HomeMaster>  
- 📷 **Instagram:** <https://www.instagram.com/home_master.eu>

---
