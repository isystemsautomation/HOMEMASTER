**Firmware Version:** 2025-10 snapshot

![Firmware Version](https://img.shields.io/badge/Firmware-2025--10-blue)
![Modbus](https://img.shields.io/badge/Protocol-Modbus%20RTU-brightgreen)
![License](https://img.shields.io/badge/License-GPLv3%20%2F%20CERN--OHL--W-blue)

# WLD-521-R1 — Water Meter & Leak Detection Module

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="WLD-521-R1 module photo">

### Module Description

The **WLD-521-R1** is a configurable smart I/O module designed for **leak detection**, **water flow metering**, **heat energy monitoring**, and **local irrigation control**.  
It includes **5 opto-isolated digital inputs**, **2 SPDT relays**, and optional **4 buttons and 4 LEDs** for manual control and local status indication. Configuration is performed via the **WebConfig interface** over **USB-C (Web Serial)**.  
The module connects over **RS-485 (Modbus RTU)** to a **MicroPLC or MiniPLC**, enabling deployment in **water management**, **hydronic heating**, **garden irrigation**, and **safety automation systems**.

## Table of Contents

* [1. Introduction](#1-introduction)
* [2. Use Cases](#2-use-cases)
* [3. Safety Information](#3-safety-information)
* [4. Installation & Quick Start](#4-installation-quick-start)
* [5. MODULE-CODE — Technical Specification](#5-module-code--technical-specification)
* [6. Modbus RTU Communication](#6-modbus-rtu-communication)
* [7. ESPHome Integration Guide (if applicable)](#7-esphome-integration-guide)
* [8. Programming & Customization](#8-programming--customization)
* [9. Maintenance & Troubleshooting](#9-maintenance--troubleshooting)
* [10. Open Source & Licensing](#10-open-source--licensing)
* [11. Downloads](#11-downloads)
* [12. Support](#12-support)

<br clear="left"/>

---

<a id="1-introduction"></a>

# 1. Introduction

## 1.1 Overview of the WLD-521-R1

The **WLD-521-R1** is a DIN‑rail smart I/O module for **leak detection**, **pulse water metering**, **ΔT heat monitoring**, and **local irrigation control**. It exposes **5 opto‑isolated digital inputs**, **2 SPDT relays**, **4 user buttons**, and **4 status LEDs** and is serviced over **USB‑C**.

It integrates with a **MiniPLC/MicroPLC** (or other PLC/SCADA/HA controllers) via **Modbus RTU over RS‑485**. Configuration is done in a browser using the **WebConfig** tool (Web Serial over USB‑C): set Modbus params, choose per‑input modes (sensor/counter), link 1‑Wire temperature sensors, and enable autonomous irrigation/flow‑safety logic.  
**In one line:** a resilient water‑safety/flow module with local logic that still plays perfectly with your PLC and Home Assistant stack.

---

## 1.2 Features & Architecture

| Subsystem         | Qty        | Description |
|-------------------|------------|-------------|
| **Digital Inputs** | 5          | Opto‑isolated inputs with MOSFET front‑ends, debouncing, and isolated return (**GND_ISO**); suitable for dry contacts or pulse flowmeters. |
| **Analog Outputs** | 0          | — |
| **Relays**         | 2          | SPDT dry contacts (~3 A @ 250 VAC); driven via opto‑isolated stages and RC/snubber-protected. Terminals NO/COM/NC exposed. |
| **1‑Wire Bus**     | 1          | Protected 3‑pin header (+5 V / DATA / GND) with level shifting and ESD protection; supports DS18B20 sensors. |
| **LEDs**           | 4 user + status | 4 user LEDs controlled via transistor drivers; additional LEDs for power and RS‑485 TX/RX activity. |
| **Buttons**        | 4          | Front-panel tactile buttons connected to MCU GPIOs for relay override, irrigation control, and test functions. |
| **Modbus RTU**     | Yes        | RS‑485 transceiver with surge/bias/ESD protection and DE/RE control. Typical config: 19200 baud, 8N1. |
| **USB‑C**          | Yes        | Type-C port with ESD protection and Web Serial interface; used for configuration via WebConfig. |
| **Power**          | 24 VDC     | Fused, reverse-protected input. Internal buck regulator provides +5 V and +3.3 V. Isolated +5 V and +12 V rails available for sensor power. |
| **MCU**            | RP2350A    | Dual-core MCU with QSPI flash and 12 MHz crystal; SWD debug header available. |
| **Protection**     | TVS, PTC, ESD | Multi-stage protection on RS‑485 and USB lines; isolated sensor rails; opto-isolated inputs; snubbers on relays. |

> Optional: **1‑Wire bus** for DS18B20 sensors (e.g. supply/return temperatures for heat energy monitoring).

---

## 1.3 System Role & Communication 💧

The **WLD-521-R1** is a smart Modbus RTU slave. It can operate autonomously for leak/flow/irrigation safety logic, while exposing its I/O and sensors to a PLC, ESPHome controller, or SCADA system.

| Role                | Description |
|---------------------|-------------|
| **System Position** | Expansion module on RS-485 trunk |
| **Master Controller** | MiniPLC / MicroPLC or any third-party Modbus RTU master |
| **Address / Baud**  | Configurable via WebConfig (1–255, 9600–115200 baud) |
| **Bus Type**        | RS‑485 multi-drop (A/B/COM terminals) |
| **USB‑C Port**      | For configuration/diagnostics using Web Serial (Chrome/Edge) |
| **Default Modbus ID** | `3` (user-changeable per module) |
| **Daisy-Chaining**  | Multiple modules supported; assign unique IDs to each device |

> ⚠️ **Note:** If multiple WLD modules are connected to the same RS‑485 segment, make sure to assign **unique Modbus addresses** using WebConfig.


---

<a id="2-use-cases"></a>

# 2. Use Cases

The **WLD-521-R1** supports a range of real-world applications in leak detection, flow metering, hydronic energy monitoring, and irrigation control. Below are practical scenarios with step-by-step configuration.

---

### 💧 1) Basement Leak Alarm + Auto Shut-off

**Goal:** Detect water leaks and immediately shut off the water supply using a relay-controlled valve.

**Steps:**
- Set **DI1** as **Water sensor**
- Enable DI1 and set **Action = Toggle**
- Set **Control target = Relay 1**
- Wire a **motorized shut-off valve** (normally open) to **R1**
- (Optional) Assign **LED1** to blink on **DI1** or **R1**

---

### 🌿 2) Garden Irrigation with Flow Supervision

**Goal:** Automate watering safely with flow monitoring and environmental interlocks.

**Steps:**
- Go to **Irrigation → Zone 1**
- Set **Valve relay = Relay 2**, **Flow DI = DI2**
- Enable **Use flow supervision**
- Configure:
  - **Min rate = 0.2 L/min**
  - **Grace = 8 s**
  - **Timeout = 1200 s**
  - **Target liters = 50**
- Add interlocks:
  - **DI_moist = DI3** (dry = run)
  - **DI_rain = DI4** (rain = block)
  - **R_pump = Relay 1** (pump ON when watering)
- Enable **Window: 06:00–08:00** with **Auto-start**

---

### 📈 3) Water Consumption Metering (Billing)

**Goal:** Track water usage in liters using pulse flow meters.

**Steps:**
- Set **DI2** to **Water counter**
- Enter **Pulses per liter = 450** (typical)
- Adjust **Rate × / Total ×** as needed for calibration
- Use **Reset total** to baseline reading
- Use **Calc from external** after external validation
- View **Live rate (L/min)** and **Total (L)** in WebConfig

---

### 🔥 4) Heat Energy Monitoring (Hydronic ΔT Loops)

**Goal:** Measure heat power and energy from flow and temperature sensors.

**Steps:**
- Set **DI3 = Water counter**
- Enable **Heat** on DI3
- Assign **Sensor A = #1 (supply)**, **Sensor B = #2 (return)**
- Set thermal constants:
  - **cp = 4186 J/kg·°C**
  - **ρ = 1.0 kg/L**
- Adjust **Calibration ×** if needed
- View **TA**, **TB**, **ΔT**, **Power (W)**, **Energy (J/kWh)**
- Use **Reset energy** to zero counters

---


<a id="3-safety-information"></a>

# 3. Safety Information

## 3.1 General Requirements

| Requirement            | Detail |
|------------------------|--------|
| Qualified Personnel     | Required for all installation tasks |
| Power Isolation         | Disconnect before working on terminals |
| Rated Voltages Only     | SELV only; no mains |
| Grounding               | Proper panel grounding |
| Enclosure               | Use clean/dry cabinet; avoid dust/moisture |

## 3.2 Installation Practices

Give best practices for:
- DIN mounting
- Isolation domain respect (e.g., GND vs GND_ISO)
- Relay wiring
- Sensor power connection

## 3.3 Interface Warnings

Create tables for:

- Power (24 VDC, 12 V sensor rail)
- Inputs (dry contact only, debounce notes)
- Relays (max current/voltage, snubber required)
- RS-485 (termination, A/B polarity)
- USB-C (setup only, not for field devices)

---

<a id="4-installation-quick-start"></a>

# 4. Installation & Quick Start

## 4.1 What You Need

| Item | Description |
|------|-------------|
| Module | MODULE-CODE unit |
| Controller | MiniPLC/MicroPLC or Modbus RTU master |
| PSU | Regulated 24 VDC |
| Cable | USB-C and RS-485 twisted pair |
| Software | Browser with Web Serial support |

## 4.2 Power

- Describe 24 VDC input
- List expected current
- Explain isolated sensor power if present

## 4.3 Communication

- RS-485 pinout
- Address & baudrate setup
- Use of COM/GND reference

<a id="installation-wiring"></a>

## 4.4 Installation & Wiring

Use diagrams and explain:
- Inputs
- Relays
- Sensor rails (12/5V)
- RS-485 terminals
- USB port

<a id="software-ui-configuration"></a>

## 4.5 Software & UI Configuration

Cover:
- WebConfig setup (address, baud)
- Input enable/invert/group
- Relay logic mode (group/manual)
- LED and Button mapping

<a id="4-6-getting-started"></a>

## 4.6 Getting Started

Summarize steps in 3 phases:
1. Wiring
2. Configuration
3. Integration

---

<a id="5-module-code--technical-specification"></a>

# 5. MODULE-CODE — Technical Specification

## 5.1 Diagrams & Pinouts

Add photos/diagrams:
- System block diagram
- Board layouts
- Terminal maps

## 5.2 I/O Summary

Summarize in a table:

| Interface | Qty | Description |
|-----------|-----|-------------|
| Inputs |   | Opto-isolated |
| Relays |   | SPST/SPDT |
| LEDs |   | Status indication |
| USB-C | 1 | Setup only |

## 5.3 Electrical Specs

Cover:
- Input voltage range
- Current consumption
- Sensor rail current
- Relay contact ratings
- Isolation details

## 5.4 Firmware Behavior

Explain:
- Alarm logic (latched/momentary)
- Override priority
- LED feedback modes

---

<a id="6-modbus-rtu-communication"></a>

# 6. Modbus RTU Communication

Include:
- Address range and map
- Input/holding register layout
- Coil/discrete inputs
- Register use examples
- Polling recommendations

---

<a id="7-esphome-integration-guide"></a>

# 7. ESPHome Integration Guide

Only if supported. Cover:
- YAML setup (`uart`, `modbus`, `package`)
- Entity list (inputs, relays, buttons, LEDs)
- Acknowledge, override controls
- Home Assistant integration tips

---

<a id="8-programming--customization"></a>

# 8. Programming & Customization

## 8.1 Supported Languages

- Arduino
- C++
- MicroPython

## 8.2 Flashing

Steps for:
- USB-C flashing
- BOOT/RESET button use
- PlatformIO / Arduino IDE setup

## 8.3 Arduino / PlatformIO Notes

Mention:
- Required libraries
- Pin mapping
- Board config

## 8.4 Firmware Updates

- How to update
- Preserving config
- Recovery methods

---

<a id="9-maintenance--troubleshooting"></a>

# 9. Maintenance & Troubleshooting

Optional section. Add:
- Status LED meanings
- Reset methods
- Common issues (no comms, relay won’t trigger, etc.)

---

<a id="10-open-source--licensing"></a>

# 10. Open Source & Licensing

- **Hardware:** CERN-OHL-W v2
- **Firmware:** GPLv3
- **Config Tools:** MIT or other as applicable

---

<a id="11-downloads"></a>

# 11. Downloads

Include links to:

- Firmware binaries
- YAML configs
- WebConfig tool
- Schematics (PDF)
- Images and diagrams
- Datasheets

---

<a id="12-support"></a>

# 12. Support

- [Official Support Portal](https://www.home-master.eu/support)
- [WebConfig Tool](https://www.home-master.eu/configtool-[module-code])
- [YouTube](https://youtube.com/@HomeMaster)
- [Hackster](https://hackster.io/homemaster)
- [Reddit](https://reddit.com/r/HomeMaster)
- [Instagram](https://instagram.com/home_master.eu)

