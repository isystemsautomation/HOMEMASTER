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

| Subsystem       | Qty      | Description |
|-----------------|----------|-------------|
| **Digital Inputs** | 5        | Opto‑isolated dry‑contact / pulse (flow meters); debounced, noise‑protected |
| **Analog Outputs** | 0        | — |
| **Relays**         | 2        | **SPDT** dry contacts, ~3 A @ 250 VAC (valves/pumps/siren) |
| **LEDs**           | 4 + status | Solid/Blink modes, sources: DI, Relays, Irrigation, Overrides |
| **Buttons**        | 4        | Acknowledge/override/user actions (toggle/pulse, start/stop zones) |
| **Modbus RTU**     | Yes      | RS‑485, slave device; typical **19200 8N1** |
| **USB‑C**          | Yes      | WebConfig over **Web Serial** (driverless in Chrome/Edge) |
| **Power**          | 24 VDC   | Fused & reverse‑protected; isolated **+5 V / +12 V** sensor rails |
| **MCU**            | RP2350A  | Dual‑core MCU with external QSPI flash |
| **Protection**     | TVS, PTC | ESD/surge on RS‑485 & USB; opto‑isolated inputs; snubbed relays |

> Optional: **1‑Wire bus** for DS18B20 sensors (ΔT calorimetry support).

---

## 1.3 System Role & Communication

- **Bus connection:** Wire **A↔A**, **B↔B**, and a common reference (**COM/GND**) to the RS‑485 trunk; terminate both bus ends with ~**120 Ω**.  
- **Control model:** The module is a **Modbus RTU slave**. It can run **standalone local logic** (leak→relay, 2 irrigation zones with flow supervision & interlocks) while the **PLC/ESPHome** polls telemetry and issues commands. Local **override** and **zone ownership** take priority to ensure safety.  
- **Polling & entities:** Masters typically read **coils/discrete/holding** at ~1 s for live DI, flow rate/total, ΔT, zone state; write coils for **relay ON/OFF**, **zone START/STOP/RESET**, and optional **midnight sync**.  
- **Defaults:** **Address = 3**, **Baud = 19200**, **8N1**. Change these in **WebConfig** over USB‑C, then match the settings in your controller YAML.

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

