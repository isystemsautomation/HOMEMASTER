**Firmware Version:** 2025-07 snapshot

![Firmware Version](https://img.shields.io/badge/Firmware-2025--07-blue)
![Modbus](https://img.shields.io/badge/Protocol-Modbus%20RTU-brightgreen)
![License](https://img.shields.io/badge/License-GPLv3%20%2F%20CERN--OHL--W-blue)

# RGB-621-R1 — Module for RGB+CCT LED Control

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="RGB-621-R1 photo">

### Module Description

The **RGB-621-R1** is a configurable smart I/O lighting module designed for **RGB + Tunable White (CCT)** LED control.  
It includes **5 PWM outputs**, **2 isolated digital inputs**, and **1 relay**, with configuration via **WebConfig** using **USB-C (Web Serial)**.  
It connects over **RS-485 (Modbus RTU)** to a **MicroPLC/MiniPLC**, enabling use in **home automation, ambient and architectural lighting, and color scene control**.

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

## 1.1 Overview of the RGB-621-R1

The **RGB-621-R1** is a **smart RGB + CCT LED controller module** designed for use in **HomeMaster automation systems**.  
It provides **five high-current PWM outputs** for controlling **RGB and Tunable White (CCT)** LED channels, along with **two isolated digital inputs** for wall switches or sensors, and **one relay output** for switching external loads or LED drivers.

Powered by the **Raspberry Pi RP2350A** microcontroller, the module features robust **RS-485 (Modbus RTU)** communication, **on-board WebConfig via USB-C**, and comprehensive surge and short-circuit protection.  
It connects directly to **HomeMaster MicroPLC** and **MiniPLC** controllers or can act as a standalone Modbus slave within any automation network.  

Configuration and diagnostics are performed via **Web Serial (USB-C)** using the integrated WebConfig interface — no drivers or external software required.  
Its isolated I/O architecture and dual-board design ensure electrical resilience, accurate dimming, and noise-free operation in demanding lighting environments.

---

## 1.2 Features & Architecture

| Subsystem         | Qty | Description |
|-------------------|-----|-------------|
| **Digital Inputs** | 2 | Galvanically isolated (ISO1212) dry-contact inputs with surge and reverse protection |
| **PWM Outputs** | 5 | N-channel MOSFET drivers (AP9990GH-HF), 12 V / 24 V LED channels for R / G / B / CW / WW |
| **Relay Output** | 1 | SPST-NO relay (HF115F/005-1ZS3), 5 V coil, rated 16 A @ 250 VAC / 30 VDC |
| **Buttons** | 2 | Local control or configuration triggers (SW1 / SW2) |
| **LED Indicators** | 8 | Power, TX/RX, input, and status LEDs for feedback and diagnostics |
| **Modbus RTU** | Yes | RS-485 interface via MAX485CSA+T transceiver; 120 Ω termination selectable |
| **USB-C** | Yes | WebConfig & firmware flashing with PRTR5V0U2X ESD protection |
| **Power Input** | 24 V DC | Protected by resettable fuses (1206L series), TVS (SMBJ33A), and reverse-blocking (STPS340U) |
| **Logic Supply** | — | AP64501SP-13 buck (5 V) + AMS1117-3.3 LDO chain |
| **MCU** | RP2350A | Dual-core Arm Cortex-M33 @ 133 MHz with 32 Mbit QSPI Flash (W25Q32JVUUIQ) |
| **Isolation & Protection** | — | Galvanic isolation, TVS diodes, PTC fuses, transient suppression on all field I/O |

**Architecture summary:**  
- **MCU Board:** manages logic, USB, Modbus, and power regulation  
- **Field Board:** contains LED drivers, relay circuit, and isolated input section  
This modular, two-board design ensures clean signal separation between logic and 24 V field wiring, improving reliability in mixed-voltage installations.

---

## 1.3 System Role & Communication

The **RGB-621-R1** operates as a **Modbus RTU slave** on an **RS-485 differential bus**, typically polled by a **HomeMaster controller** (MicroPLC / MiniPLC) or other Modbus master.  
Each module is assigned a unique Modbus address via WebConfig, supporting up to 32 devices per bus.

**Default communication parameters:**  
- **Address:** 1  
- **Baud rate:** 19200 bps  
- **Format:** 8 data bits, no parity, 1 stop bit (8N1)  
- **Termination:** 120 Ω enabled at end of bus  
- **Fail-safe:** retains last valid PWM and relay state if communication is lost  

The controller periodically polls holding registers to:  
- Write PWM duty values for R, G, B, CW, WW channels  
- Control the relay output  
- Read digital input and status bits  

WebConfig enables users to modify address, baud rate, test I/O, calibrate channels, and perform real-time diagnostics — simplifying setup and commissioning.

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

