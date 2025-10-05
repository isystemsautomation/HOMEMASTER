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

These safety guidelines apply to the **ENM‑223‑R1 3‑phase metering and I/O module**. Ignoring them may result in **equipment damage, system failure, or personal injury**.

> ⚠️ **Mixed Voltage Domains** — This device contains both **SELV (e.g., 24 V DC, RS‑485, USB)** and **non-SELV mains inputs (85–265 V AC)**. Proper isolation, wiring, and grounding are required. Never connect SELV and mains GND together.

---

## 3.1 General Requirements

| Requirement           | Detail |
|-----------------------|--------|
| Qualified Personnel   | Installation and servicing must be done by qualified personnel familiar with high-voltage and SELV control systems. |
| Power Isolation       | Disconnect both **24 V DC** and **voltage inputs (Lx/N)** before servicing. Use lockout/tagout where applicable. |
| Environmental Limits  | Mount in a clean, sealed enclosure. Avoid condensation, conductive dust, or vibration. |
| Grounding             | Bond the panel to PE. Wire **PE and N** to the module. Never bridge **GND_ISO** to logic GND. |
| Voltage Compliance    | CT inputs: 1 V or 333 mV RMS only. Voltage inputs: 85–265 V AC. Use upstream fusing and surge protection. |

---

## 3.2 Installation Practices

| Task                | Guidance |
|---------------------|----------|
| ESD Protection       | Handle only by the case. Use antistatic wrist strap and surface when the board is exposed. |
| DIN Rail Mounting    | Mount securely on **35 mm DIN rail** inside an IP-rated cabinet. Allow cable slack for strain relief. |
| Wiring               | Use correct gauge wire and torque terminal screws. Separate relay, CT, and RS‑485 wiring. |
| Isolation Domains    | Respect isolation: **Do not bridge GND_ISO to GND**. Keep analog and logic grounds isolated. |
| Commissioning        | Before power-up, verify voltage wiring, CT polarity, RS‑485 A/B orientation, and relay COM/NO/NC routing. |

---

## 3.3 I/O & Interface Warnings

### ⚡ Power

| Area             | Warning |
|------------------|---------|
| **24 V DC Input** | Use a clean, fused SELV power source. Reverse polarity is protected but may disable the module. |
| **Voltage Input** | Connect **L1/L2/L3/N/PE** only within rated range (85–265 V AC). Use circuit protection upstream. |
| **Sensor Domain** | Use **CTs with 1 V or 333 mV RMS** output. Never apply 5 A directly. Observe polarity and shielding. |

### 🧲 Inputs & Relays

| Area              | Warning |
|-------------------|---------|
| **CT Inputs**      | Accept only voltage-output CTs. Reversing polarity may affect power sign. Use GND_ISO reference. |
| **Relay Outputs**  | Dry contacts only. Rated: **5 A @ 250 VAC or 30 VDC**. Use snubber (RC/TVS) for inductive loads. |

### 🖧 Communication & USB

| Area            | Warning |
|-----------------|---------|
| **RS‑485 Bus**   | Use **twisted pair**. Terminate at both ends. Match A/B polarity. Share GND if powered from different PSUs. |
| **USB-C (Front)**| For **setup only**. Not for permanent field connection. Disconnect during storms or long idle periods. |

### 🎛 Front Panel

| Area               | Warning |
|--------------------|---------|
| **Buttons & LEDs** | Can override relays or trigger alarms. Use firmware settings or lockout for safety-critical installs. |

### 🛡 Shielding & EMC

| Area             | Recommendation |
|------------------|----------------|
| **Cable Shields** | Terminate at one side only (preferably PLC/controller). Route away from VFDs and high-voltage cabling. |

---

### ✅ Pre‑Power Checklist

- [x] All wiring is torqued, labeled, and strain-relieved  
- [x] **No bridge between logic GND and GND_ISO**  
- [x] PE and N are wired to terminals  
- [x] RS‑485 A/B polarity and 120 Ω termination confirmed  
- [x] Relay loads do **not** exceed 5 A or contact voltage rating  
- [x] CTs installed with correct polarity and securely landed  
- [x] Voltage inputs fused, protected, and within spec (85–265 V AC)

> 🧷 **Tip:** In single-phase installations, energize **L1** and tie **L2/L3 → N** to prevent phantom voltages.



<a id="4-installation-quick-start"></a>

# 4. Installation & Quick Start

The **ENM‑223‑R1** connects to your system over **RS‑485 (Modbus RTU)** and supports configuration via **USB‑C WebConfig**. Setup involves:  
**1) Physical wiring**, **2) Digital setup** (WebConfig → Modbus or PLC/ESPHome control).

---

## 4.1 What You Need

| Category     | Item / Notes |
|--------------|--------------|
| **Hardware** | ENM‑223‑R1 module: DIN-rail, 3 voltage channels, 3 CTs, 2 relays, 4 buttons, 4 LEDs, RS‑485, USB‑C |
| **Controller** | MicroPLC, MiniPLC, or any Modbus RTU master |
| **24 VDC Power (SELV)** | Regulated 24 V DC @ ~100–200 mA |
| **RS‑485 Cable** | Twisted pair for A/B + COM/GND; external 120 Ω end-termination |
| **USB‑C Cable** | For WebConfig setup via Chromium browser (Chrome/Edge) |
| **Software** | Web browser (Web Serial enabled), ConfigToolPage.html |
| **Field Wiring** | L1/L2/L3/N/PE → voltage inputs, CT1/2/3 → external CTs |
| **Load Wiring** | Relay outputs (NO/COM/NC); observe relay max rating and use snubbers on inductive loads |
| **Isolation Domains** | GND (logic) ≠ GND_ISO (metering); never bond these directly |
| **Tools** | Torque screwdriver, ferrules, USB-capable PC, 120 Ω terminators if needed |

---

> **Quick Path**  
> ① Mount → ② wire **24 VDC + RS‑485 (A/B/COM)** → ③ connect **USB‑C** → ④ launch WebConfig →  
> Set **Address/Baud** → assign **Inputs/Relays/LEDs** → confirm data → ⑤ disconnect USB → hand control to Modbus master.

---

## 4.2 Power

The ENM‑223‑R1 uses **24 V DC** input for its interface domain and internally isolates metering circuits.

- **Power Terminals:** Top left: `V+` and `0V`
- **Voltage Range:** 22–28 V DC (nominal 24 V)
- **Typical Current:** 50–150 mA (relays off/on)
- **Protection:** Internally fused, reverse-polarity protected
- **Logic domain:** Powers MCU, RS‑485, LEDs, buttons, relays

### 4.2.1 Sensor Isolation

- **Metering IC** (ATM90E32AS) is powered from an isolated 5 V rail
- Analog domain uses **GND_ISO**, fully isolated from GND
- Do not connect **GND_ISO ↔ GND**; isolation via **B0505S + ISO7761**

> Only voltage inputs (Lx-N) and CTs connect to the isolated domain.

---

### 4.2.2 Power Tips

- **Do not power relays or outputs** from metering-side inputs
- Use separate fusing on L1–L3
- Tie **L2, L3 → N** if using single-phase only (prevents phantom voltage)
- Confirm PE is wired — improves stability & safety

---

## 4.3 Networking & Communication

### 4.3.1 RS‑485 (Modbus RTU)

#### Physical

| Terminals  | Description            |
|------------|------------------------|
| `A`, `B`   | Differential signal pair (twisted/shielded) |
| `COM`/`GND` | Logic reference (tie GNDs if on separate supplies) |

#### Cable & Topology

- Twisted pair (with or without shield)
- Terminate with **120 Ω** at each bus end (not inside module)
- Biasing resistors (pull-up/down) should be on the master

#### Defaults

| Setting       | Value        |
|---------------|--------------|
| Modbus Address | `3` |
| Baud Rate      | `19200` |
| Format         | `8N1` |
| Address Range  | 1–247 |

> 🧷 Reversed A/B will cause CRC errors — check if no response.

---

### 4.3.2 USB‑C (WebConfig)

**Purpose:** Web-based configuration tool over native USB Serial. Supports:
- Live readings
- Address/baudrate config
- Phase mapping
- Relay/button/LED logic
- Alarm setup
- Calibration (gains/offsets)

#### Steps

1. Connect USB‑C to PC (Chrome/Edge)
2. Open `tools/ConfigToolPage.html`  
3. Click **Connect**, select ENM serial port  
4. Configure settings: address, relays, LEDs, alarms, calibration  
5. Click **Save & Disconnect** when finished

> ⚠️ If **Connect** is greyed out: check browser support, OS permissions, and close any other apps using the port.


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

