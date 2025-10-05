**Firmware Version:** 2025-10 snapshot

![Firmware Version](https://img.shields.io/badge/Firmware-2025--10-blue)
![Modbus](https://img.shields.io/badge/Protocol-Modbus%20RTU-brightgreen)
![License](https://img.shields.io/badge/License-GPLv3%20%2F%20CERN--OHL--W-blue)

# DIM-420-R1 — Module for Dual-Channel AC Dimming

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="MODULE photo">

### Module Description

The **DIM-420-R1** is a configurable smart I/O module designed for **dimming AC loads via leading/trailing edge phase control**.  
It includes **4 digital inputs**, **2 dimming outputs**, **4 user buttons**, and **4 LEDs**, with configuration via **WebConfig** using **USB-C (Web Serial)**.  
It connects over **RS-485 (Modbus RTU)** to a **MicroPLC/MiniPLC**, enabling use in **smart lighting, scene control, and automation of resistive or LED loads**.

---


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

## 1.1 Overview of the DIM-420-R1 Module ✨

The **DIM‑420‑R1** is a modular dimmer I/O device for **dual‑channel phase‑cut AC dimming** in the **HomeMaster MicroPLC / MiniPLC** ecosystem. It exposes **2 dimming channels**, **4 opto‑isolated digital inputs**, **4 configurable user buttons**, **4 user LEDs**, and an **RS‑485 Modbus RTU** interface. Setup and diagnostics are performed in‑browser via **WebConfig over USB‑C (Web Serial)**—no special software required. 

It integrates seamlessly with **MiniPLC / MicroPLC controllers, third‑party Modbus masters, ESPHome / Home Assistant, and SCADA/PLC systems**. Typical use: connect wall switches to the DIs, pick **Leading/Trailing edge** per load, set **Lower/Upper thresholds**, and control scenes from a PLC or locally with press‑logic. 

> **Quick use case:**  
> Wire DI1–DI4 to wall switches → select *Momentary* or *Latching* with Short/Long/Double logic → choose **Cut Mode** and **Load Type** per channel → map LEDs/Buttons → connect RS‑485 A/B → control and monitor via PLC / ESPHome. 

---

## 1.2 Features & Architecture

### Core Capabilities

| Subsystem       | Qty    | Description |
|-----------------|--------|-------------|
| **Digital Inputs** | 4      | **Opto‑isolated** dry‑contact inputs (ISO1212 front‑end); modes: *Momentary*/*Latching* with **Short / Long / Double / Short‑then‑Long** press types. Debounced and firmware‑interpreted for actions.  |
| **Dimming Outputs** | 2      | MOSFET‑based **phase‑cut** AC outputs; **Leading/Trailing** per channel with **Lower/Upper** threshold limits and zero‑cross sync/monitoring.  |
| **Relays**         | 0      | – |
| **User LEDs**      | 4      | Steady/Blink; sources: CH1/CH2 state, DI1–DI4, or AC presence (**ZC OK**).  |
| **User Buttons**   | 4      | Local acknowledge/override; firmware press‑logic for toggle, ramp (ping‑pong), preset, max.  |
| **Config UI**      | Web Serial | **WebConfig** in Chromium browser over **USB‑C**; edit Modbus addr/baud, thresholds, cut mode, presets; live log & JSON snapshot.  |
| **Modbus RTU**     | RS‑485 | Multi‑drop slave; **FC01/05/02/03/06/16** with discrete inputs, coils, and holding registers. Defaults **ID=3, 19200, 8N1**.  |
| **MCU**            | RP2350A | Dual‑core MCU; **QSPI flash** for firmware/config; Arduino/PlatformIO supported.  |
| **Power**          | 24 VDC | Protected 24 V input; on‑board **isolated 5 V rails** for power stages (B2405S‑2WR3) and local 3.3 V regulation.  |
| **Protection**     | TVS, PTC | Surge/ESD protection and resettable fuses on field I/O and RS‑485/USB/power paths.  |

---

## 1.3 System Role & Communication 🔌

The **DIM‑420‑R1** is a **standalone Modbus RTU slave** on an **RS‑485** multi‑drop trunk. It executes local input press‑logic and dimming behavior, mirrors state to **discrete inputs/holding registers**, and accepts control via **coils/holding writes** from a master (PLC/SCADA/ESPHome). A live **JSON snapshot** and event log stream over Web Serial for commissioning and diagnostics. 

| Role                | Description |
|---------------------|-------------|
| **System Position** | Expansion/field module on RS‑485 bus (A/B/COM).  |
| **Master Controller** | MiniPLC / MicroPLC or any third‑party **Modbus RTU master**.  |
| **Address / Baud**  | Configurable via WebConfig; **default Slave ID = 3**, **19200 baud, 8N1** (persisted to flash).  |
| **Bus Type**        | RS‑485 multi‑drop with proper termination and biasing.  |
| **USB‑C Port**      | Setup/diagnostics, UF2 firmware updates via RP2350 bootloader; in‑browser WebConfig.  |
| **Polling Model**   | Master polls **DI/LED/AC status** and writes **coils/registers** for ON/OFF, presets, levels, and config.  |

> ⚠️ **Note:** If multiple DIM‑420‑R1 modules share the same RS‑485 line, assign unique **Modbus IDs** in WebConfig and verify termination/bias. 

---

<a id="2-use-cases"></a>

# 2. Use Cases

Document **3–5 real-world examples**, such as:
- Safety zone monitoring
- Relay control with manual override
- Environmental alarms (e.g. temperature + smoke)
- Staged automation

Each example should include:
- A title (e.g., “Zone Alarm with Manual Reset”)
- 1–2 lines of what it does
- A step-by-step bullet list of setup instructions

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

