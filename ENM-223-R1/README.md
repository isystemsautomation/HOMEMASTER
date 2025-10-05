**Firmware Version:** 2025-09 snapshot

![Firmware Version](https://img.shields.io/badge/Firmware-2025--09-blue)
![Modbus](https://img.shields.io/badge/Protocol-Modbus%20RTU-brightgreen)
![License](https://img.shields.io/badge/License-GPLv3%20%2F%20CERN--OHL--W-blue)

# ENM-223-R1 — 3-Phase Power Metering & I/O Module

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="MODULE photo">

### Module Description

The **ENM-223-R1** is a configurable smart I/O module designed for **3-phase power quality and energy metering**.  
It includes **3 voltage inputs, 3 current channels**, **2 relays**, and optional **4 buttons / 4 LEDs**, with configuration via **WebConfig** using **USB-C (Web Serial)**.  
It connects over **RS-485 (Modbus RTU)** to a **MicroPLC/MiniPLC**, enabling use in **energy monitoring, automation, and smart building applications**.

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

## 1.1 Overview of the ENM‑223‑R1 Module ⚡

The **ENM‑223‑R1** is a modular **3‑phase energy metering + I/O** device for power monitoring, automation, and local control. It features **3 voltage channels (L1/L2/L3‑N)**, **3 current channels (external CTs)**, **2 SPDT relays**, **4 user LEDs**, and **4 buttons**—all driven by an **RP2350** MCU with QSPI flash and a dedicated **ATM90E32AS** metering IC.

It integrates with **MiniPLC/MicroPLC** controllers or any **Modbus RTU** master over **RS‑485**, and it’s configured in‑browser via **USB‑C Web Serial** (no drivers). Typical uses include **energy dashboards, demand response, alarm‑driven relay control, and building automation**. Defaults ship as **Modbus address 3 @ 19200 8N1** (changeable in WebConfig).

> Quick device flow:  
> **Wire Lx/N/PE + CTs → set address/baud in WebConfig → calibrate gains/offsets → define alarms per L1/L2/L3/Totals → map relays/LEDs/buttons → connect RS‑485 A/B (and GND if separate PSUs) → poll via Modbus.**

---

## 1.2 Features & Architecture

### Core Capabilities

| Subsystem       | Qty | Description |
|-----------------|-----|-------------|
| Voltage Inputs  | 3   | L1/L2/L3‑N measurement (divider network on FieldBoard) feeding ATM90E32AS |
| Current Inputs  | 3   | Differential CT inputs (IAP/IAN, IBP/IBN, ICP/ICN) with filtering/burdens |
| Relays          | 2   | **SPDT** dry contacts (NO/NC); opto‑driven; alarm‑ or Modbus‑controlled |
| LEDs            | 4   | User LEDs; sources: overrides/alarms/warnings/events; steady or blink |
| Buttons         | 4   | User actions (toggle relays/LEDs, overrides, acks) with live state feedback |
| Metering & Energy | — | ATM90E32AS: Urms/Irms, **P/Q/S**, PF, angle, freq; energy kWh/kvarh/kVAh (phase & totals) |
| Config UI       | Web Serial | In‑browser **WebConfig** over **USB‑C** (Chrome/Edge); live meter, calibration, alarms, relays, LEDs, buttons |
| Modbus RTU      | RS‑485 | Multi‑drop slave; address 1…255; baud 9600–115200 (default **19200 8N1**) |
| MCU             | RP2350 + QSPI | Dual‑core MCU, native USB, external W25Q32 flash; RS‑485 via MAX485 transceiver |
| Power           | 24 VDC | Buck to 5 V → 3.3 V LDO; **isolated analog domain** via B0505S DC‑DC + ISO7761 |
| Protection      | TVS, PTC, fuses | Surge/ESD on USB & RS‑485; resettable fuses on field I/O; reverse‑polarity protection |

---

## 1.3 System Role & Communication 🍰

The **ENM‑223‑R1** is a **smart Modbus RTU slave**. It executes local alarm logic (thresholds & acks) and mirrors states/values to a PLC or SCADA via registers/coils. Configuration (meter options, calibration, relay/LED logic, button actions, Modbus address/baud) is done via **USB‑C WebConfig**, stored to non‑volatile memory.

| Role                 | Description |
|----------------------|-------------|
| System Position      | Expansion meter+I/O on the **RS‑485** trunk (A/B/GND) |
| Master Controller    | MiniPLC / MicroPLC or any third‑party Modbus RTU **master** (polling) |
| Address / Baud       | Configurable 1…255 / **9600–115200**; **factory default: ID 3 @ 19200 8N1** |
| Bus Type             | RS‑485 half‑duplex; termination/bias per bus rules; share **GND** if separate PSUs |
| USB‑C Port           | Setup/diagnostics via Chromium browser (Web Serial); native USB D+/D− to MCU |
| Default Modbus ID    | **3** (change in WebConfig) |
| Daisy‑Chaining       | Multi‑drop on shared A/B; ensure unique IDs and end‑of‑line termination |

> **Note:** The UI exposes per‑channel **Alarm / Warning / Event** with min/max thresholds and **Ack required** option; relays can follow selected alarm kinds or be **Modbus‑controlled**. Buttons can toggle/override relays; LEDs reflect overrides or alarm states.


<a id="2-use-cases"></a>

# 2. Use Cases

This section outlines practical application examples for the **ENM‑223‑R1** module. Each use case includes a functional goal and a clear configuration procedure using the WebConfig tool and/or Modbus RTU integration.

These templates are applicable in energy management, automation, industrial control, and building infrastructure deployments.

---

## 2.1 Overcurrent Alarm with Manual Reset

**Purpose:** Activate **Relay 1** when current exceeds a configured threshold and hold it until manually acknowledged.

### Configuration:
- **Alarms** → Channel: `Totals`  
  - Enable **Alarm**  
  - Metric: `Current (Irms)`  
  - Max threshold: e.g. `> 5000` (for 5 A)  
  - Enable **Ack required**
- **Relays** → Relay 1  
  - Mode: `Alarm Controlled`  
  - Channel: `Totals`, Kinds: `Alarm`
- **LEDs** → LED 1  
  - Source: `Alarm Totals`, Mode: `Steady`
- **Acknowledge**: via Web UI, Modbus coils `610–613`, or front panel button (if assigned)

---

## 2.2 Manual Override for Load Control

**Purpose:** Allow field operators to override **Relay 2** using a button, regardless of Modbus or automation control.

### Configuration:
- **Relays** → Relay 2  
  - Mode: `Modbus Controlled`  
  - Enabled at power-on
- **Buttons** → Button 2  
  - Action: `Override Relay 2 (hold 3s)`
- **LEDs** → LED 2  
  - Source: `Override R2`, Mode: `Blink` or `Steady`

> Holding the button for 3 seconds enters override mode. A short press toggles the relay. Holding again exits override mode.

---

## 2.3 Environmental Voltage/Frequency Alarm with Auto-Clear

**Purpose:** Detect power quality faults (sag/swell or freq drift), activate **Relay 1** as an output, and auto-reset when back in range.

### Configuration:
- **Alarms** → Channel: `L1`  
  - Enable **Alarm**  
  - Metric: `Voltage (Urms)`  
  - Min: `21000` (210 V), Max: `25000` (250 V)  
  - Leave **Ack required** unchecked
- **Relays** → Relay 1  
  - Mode: `Alarm Controlled`, Channel: `L1`, Kinds: `Alarm`
- **LEDs** → LED 1  
  - Source: `Alarm L1`

---

## 2.4 Staged Load Shedding via Modbus Scenes

**Purpose:** Use a controller to shed non-critical loads as power consumption increases.

### Configuration:
- **Relays** → Relay 1 and Relay 2  
  - Mode: `Modbus Controlled`
- In PLC logic:
  - Monitor `Totals S (VA)` via Input Register
  - If `S > 8000`, write coil `600 = OFF` (Relay 1)
  - If `S > 10000`, write coil `601 = OFF` (Relay 2)
  - Restore relays when values drop below defined hysteresis limits

> Ideal for HVAC or lighting where priority-based power shedding is needed.

---

### Summary Table

| Use Case                               | Feature Used                | Reset Method         | Relay Mode         |
|----------------------------------------|-----------------------------|----------------------|--------------------|
| Overcurrent Alarm + Ack                | Alarms, Ack, Relay 1        | Manual (Ack)         | Alarm Controlled   |
| Manual Override via Button             | Button override, LED        | Button toggle        | Modbus Controlled  |
| Voltage/Frequency Fault Auto-Reset     | Alarm (no ack), Relay       | Auto (value returns) | Alarm Controlled   |
| Load Shedding (Staged Scenes)          | PLC Modbus, Relay 1 & 2     | PLC-controlled       | Modbus Controlled  |

> 🛠 All parameters are configurable via USB‑C WebConfig. Modbus control assumes master-side logic (PLC, SCADA, or MicroPLC/MiniPLC).

---


<a id="3-safety-information"></a>

# 3. Safety Information

The ENM‑223‑R1 is a compact metering and I/O module with multiple electrical domains, including high-voltage mains inputs, low-voltage 24 V DC control, and galvanically isolated metering circuits. To prevent equipment damage, measurement inaccuracy, or personal injury, all wiring and installation must follow proper safety procedures.

---

## 3.1 General Requirements

| Requirement            | Detail |
|------------------------|--------|
| **Qualified Personnel**  | Required for all installation, wiring, and servicing tasks |
| **Power Isolation**      | De-energize **all terminals** before working on the device |
| **Rated Inputs Only**    | Use only supported voltages: CTs (1 V / 333 mV), AC inputs (85–265 V AC) |
| **Grounding (PE)**       | PE terminal must be connected to protective earth |
| **Enclosure Environment**| Use in clean, dry cabinets; avoid conductive dust, moisture, or condensation |

---

## 3.2 Installation Practices

To ensure long-term reliability and safety, follow these best practices during mechanical and electrical installation:

### 🧩 DIN Mounting
- Mount vertically on **35 mm DIN rail**
- Allow at least **10 mm clearance** top and bottom for airflow

### 🔌 Respect Isolation Domains
- **Do not short** digital ground (**GND**) and analog/metrology ground (**GND_ISOLATED**)
- Respect signal isolation between RS-485, USB, relay outputs, and analog inputs
- Follow silkscreen and pinouts when connecting CTs and high-voltage lines

### ⚡ Voltage Wiring (L1/L2/L3/N/PE)
- Connect L1–L3 via protected AC feeds (fused or breaker)
- Tie **unused L2/L3 → N** for single-phase operation to avoid phantom voltages
- Always wire **PE** — this improves both safety and metering accuracy

### 🧲 Current Transformer Inputs
- Use **1 V RMS or 333 mV RMS** output CTs (split-core or intermediate type)
- Do **not** connect 5 A CTs directly
- Observe CT orientation (arrow → load)
- Shield CT wiring for long cable runs

### 🔁 Relay Outputs
- Rated for **5 A max** @ 250 V AC or 30 V DC
- Use **external snubber** (RC or diode) on inductive loads (motors, contactors)
- Relay contact wiring is **dry-contact only**; do not source current through the relay board itself

### 🔌 24 VDC Power Input
- Supply clean **24 V DC** to `V+ / GND` terminals (reverse-protected)
- The 24 V input powers the **interface logic**, relays, and RS‑485 transceiver

---

## 3.3 Interface Warnings

| Interface      | Rating & Notes |
|----------------|----------------|
| **Power Input** | 24 V DC ±10%, fused and reverse‑protected (input on FieldBoard) |
| **Relay Outputs** | 2× SPDT dry contact, 5 A max; inductive loads require external snubber |
| **Voltage Inputs (L1/L2/L3/N)** | 85–265 V AC; must be fused externally; PE and N required |
| **CT Inputs** | Accept 333 mV / 1 V RMS CTs only; high-current secondary CTs must use burden conversion |
| **RS‑485 (A/B/COM)** | Standard 3‑wire half-duplex; termination and fail-safe biasing required at bus ends |
| **GND / GND_ISO** | Do not bridge interface GND and analog GND_ISO |
| **USB‑C Port** | Use for configuration only (via Web Serial); not rated for field connection or long-term use |

---

> ⚠️ **Important**: Improper wiring (e.g., grounding violations, CT overvoltage, incorrect relay load) can cause equipment damage or hazardous conditions. Always review schematics and safety guidance before installation.


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

