**Firmware Version:** YYYY-MM snapshot

![Firmware Version](https://img.shields.io/badge/Firmware-YYYY--MM-blue)
![Modbus](https://img.shields.io/badge/Protocol-Modbus%20RTU-brightgreen)
![License](https://img.shields.io/badge/License-GPLv3%20%2F%20CERN--OHL--W-blue)

# MODULE-CODE — Module for XYZ Functionality

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="MODULE photo">

### Module Description

The **MODULE-CODE** is a configurable smart I/O module designed for **[INSERT FUNCTION]**.  
It includes **[# inputs/outputs]**, **[# relays]**, and optional **buttons/LEDs**, with configuration via **WebConfig** using **USB-C (Web Serial)**.  
It connects over **RS-485 (Modbus RTU)** to a **MicroPLC/MiniPLC**, enabling use in **[insert example application like HVAC, lighting, alarms, etc.]**.

---

## Table of Contents

* [1. Introduction](#1-introduction)
* [2. Use Cases](#2-use-cases)
* [3. Safety Information](#3-safety-information)
* [4. Installation & Quick Start](#4-installation-quick-start)
  * [4.4 Installation & Wiring](#installation-wiring)
  * [4.5 Software & UI Configuration](#software-ui-configuration)
  * [4.6 Getting Started](#4-6-getting-started)
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

## 1.1 Overview of the MODULE-CODE

Briefly explain what the module does in 1–2 paragraphs:
- What kind of I/O it exposes (e.g. DI, AO, relays, LEDs, buttons)
- What systems it integrates with (PLC, SCADA, HA)
- Configuration mechanism (Web Serial, USB-C, etc.)
- One-sentence summary of its main purpose or role

## 1.2 Features & Architecture

Include a table like this:

| Subsystem         | Qty | Description |
|------------------|-----|-------------|
| Digital Inputs    | X   | Opto-isolated, dry contacts, noise-protected |
| Analog Outputs    | X   | 0–10V or 4–20mA, isolated |
| Relays            | X   | SPST/SPDT, dry contacts |
| LEDs              | X   | Steady/Blink modes, configurable sources |
| Buttons           | X   | Acknowledge, override, user input |
| Modbus RTU        | Yes | RS-485 interface |
| USB-C             | Yes | WebConfig over Web Serial |
| Power             | 24 VDC | Fused, reverse-protected |
| MCU               | e.g. RP2350 | Dual-core with QSPI flash |
| Protection        | TVS, PTC | ESD, surge, short-circuit |

## 1.3 System Role & Communication

Explain:
- How the module connects to RS-485 bus
- Whether it's standalone logic or master-controlled
- Communication with controller, polling setup
- Default address/baudrate

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

