# 🚧 Project Status: Under Active Development & Testing

> **Important Notice:** This documentation, hardware designs, and firmware are for the **pre-release version** of the HomeMaster system. All information is preliminary and may contain errors or be subject to change.
>
> - **Hardware:** Modules are currently in the prototyping and testing phase. Final production versions may differ.
> - **Firmware:** Firmware is under active development and is considered **beta**. Features, configurations, and stability are being refined.
>
> Please use this information for evaluation and development purposes only. 

---

**Firmware Version:** 2025-10 snapshot

![Firmware Version](https://img.shields.io/badge/Firmware-2025--10-blue)
![Modbus](https://img.shields.io/badge/Protocol-Modbus%20RTU-brightgreen)
![License](https://img.shields.io/badge/License-GPLv3%20%2F%20CERN--OHL--W-blue)

# DIO-430-R1 — Module for Smart I/O Control

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="MODULE photo">

### Module Description

The **DIO-430-R1** is a configurable smart I/O module designed for **digital input monitoring and relay-based output control**.  
It includes **4 isolated digital inputs**, **3 SPDT relays**, and **optional 3 user buttons and 3 user LEDs**, with configuration via **WebConfig** using **USB-C (Web Serial)**.  
It connects over **RS-485 (Modbus RTU)** to a **MicroPLC/MiniPLC**, enabling use in **building automation, lighting systems, access control, HVAC, and alarms**.

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

## 1.1 Overview of the DIO-430-R1

The **DIO-430-R1** is a smart digital I/O module designed for integration into modular automation systems. It offers **4 opto-isolated digital inputs**, **3 high-current SPDT relays**, **3 user buttons**, and **3 configurable user LEDs**. All I/O channels are individually configurable, enabling flexible behavior such as toggle, pulse, manual override, and alarm indication.

It integrates seamlessly with **HomeMaster MicroPLC and MiniPLC**, as well as **Home Assistant via ESPHome**, **PLC/SCADA systems** via **Modbus RTU (RS-485)**, and supports standalone operation with local logic. Configuration is handled through a driverless **Web Serial interface via USB‑C**, using a browser-based **WebConfig Tool**.  
Its primary role is to provide reliable relay control and digital input monitoring for building automation, alarms, lighting, and other general-purpose control systems.

## 1.2 Features & Architecture

| Subsystem         | Qty | Description |
|------------------|-----|-------------|
| Digital Inputs    | 4   | Opto-isolated, dry contact compatible, noise-protected |
| Relays            | 3   | SPDT (NO/NC), 16 A rated, dry contacts |
| LEDs              | 3   | Configurable: Steady or Blink modes, linked to relays |
| Buttons           | 3   | User-configurable for override or reset |
| Modbus RTU        | Yes | RS-485 interface (Configurable: Addr 1–255, 9600–115200 baud) |
| USB-C             | Yes | WebConfig tool access via Web Serial (Chrome/Edge) |
| Power             | 24 VDC | Fused input, reverse-polarity and surge protected |
| MCU               | RP2350 | Dual-core, with QSPI flash, USB, UART, LittleFS |
| Protection        | TVS, PTC | ESD, surge, and short-circuit protection on I/O and power |

## 1.3 System Role & Communication

The DIO-430-R1 connects to the **RS-485 Modbus RTU bus** using A/B differential lines and a shared COM/GND terminal. It supports **poll-based communication** where a master controller (like a PLC or MicroPLC) reads input states and writes output commands.  
The module can function in both **master-controlled** and **standalone local logic** modes, depending on its configuration.

The factory default settings are:
- **Modbus Address:** `3`  
- **Baud Rate:** `19200`  
- **Parity:** `None`  
- **Stop Bits:** `1`  

The module’s logic (input→relay mapping, LED modes, button behavior) is stored persistently in internal flash via **LittleFS**, and settings can be changed live using **USB-C + WebConfig**.

---


<a id="2-use-cases"></a>

<a id="2-use-cases"></a>

# 2. Use Cases

The **DIO‑430‑R1** supports both **lighting** and **motor/pump control** — making it ideal for mixed automation tasks in smart homes, greenhouses, HVAC, and industrial setups.  
Below are 3 versatile examples combining both types of loads.

---

## 2.1 Staircase Light with Motion Sensor + Circulation Pump

Automatically turns ON a staircase light and a circulation pump when motion is detected.

**Setup Instructions:**
- Set **IN1** to `Action = Pulse`, `Target = Relay 1` (light).
- Set **IN2** to `Action = Pulse`, `Target = Relay 2` (pump).
- Enable **Relay 1** for the staircase lighting.
- Enable **Relay 2** for the circulation pump.
- Set **LED 1** = `Blink`, source = `Relay 1`.
- Set **LED 2** = `Steady`, source = `Relay 2`.

---

## 2.2 Manual Light + Fan Override (Wall Panel)

Wall-mounted buttons allow users to toggle lights and exhaust fans independently.

**Setup Instructions:**
- Assign **Button 1** → `Relay 1 override (toggle)` → Room Light  
- Assign **Button 2** → `Relay 2 override (toggle)` → Ventilation Fan  
- Enable both **Relay 1** and **Relay 2**.  
- Set **LED 1** and **LED 2** to `Steady`, following respective relays.  
- Optionally use Modbus Coils `200–201` for remote control.

---

## 2.3 Greenhouse Light + Irrigation Pump Automation

Lights and irrigation are controlled via digital inputs or remotely from a PLC.

**Setup Instructions:**
- **IN3** → `Toggle`, Target = `Relay 1` → Grow Light  
- **IN4** → `Pulse`,  Target = `Relay 2` → Irrigation Pump  
- Enable **Relay 1** and **Relay 2**.  
- Assign **Button 3** to `Relay 2 override (toggle)` for manual watering.  
- Set **LED 1** = `Steady` (light status), **LED 2** = `Blink` (pump running).

---


<a id="3-safety-information"></a>

# 3. Safety Information

## 3.1 General Requirements

| Requirement          | Detail |
|---------------------|--------|
| Qualified Personnel | Required for all installation tasks |
| Power Isolation     | Disconnect **24 VDC** supply before wiring terminals |
| Rated Voltages Only | SELV/PELV only; **no mains** on any terminal |
| Grounding           | Proper panel PE bonding; keep RS‑485 **COM/GND** common with controller |
| Enclosure           | Mount in a clean, dry DIN cabinet; avoid dust, moisture, and corrosives |

> Notes: The module includes front‑end protection (fuses/TVS) and isolated input stages, but these do **not** replace safe installation practices. See relay/DI/RS‑485 protection in the schematics.

---

## 3.2 Installation Practices

- **DIN mounting**
  - Mount on 35 mm DIN rail inside an electrical cabinet.
  - Route **power**, **I/O**, and **RS‑485** as separate harnesses to reduce noise coupling.
- **Isolation domains**
  - Respect isolated input domains: the DI front‑end uses isolation (e.g., ISO1212 with **GND1/FGND2** rails). Do **not** bond isolated grounds together unless the application specifically requires it. Keep sensor field grounds on the **field side** of the isolator.
- **Relay wiring**
  - Use relays to drive **low‑voltage loads or contactor coils**. For inductive loads (motors, valves, sirens), provide external snubbers (RC or MOV) at the load. Observe NO/COM/NC polarity and keep wiring short.
- **Sensor power connection**
  - Power sensors from a properly fused SELV rail. Follow sensor datasheets for polarity. Use twisted pairs and shielded cable for long runs; land shields at a single point to avoid ground loops.

---

## 3.3 Interface Warnings

### Power

| Item             | Guidance |
|------------------|----------|
| Nominal Input    | **24 VDC SELV/PELV**, panel‑fused upstream |
| Protection Onboard | Series fuse, reverse‑polarity MOSFET, input TVS, bulk and decoupling capacitors |
| Wiring           | Keep 24 V and GND pairs twisted; star‑route returns; avoid sharing returns with high‑current loads |
| Inrush/Surge     | Use a stable 24 VDC PSU; add upstream surge protection in noisy sites |

### Inputs (Digital)

| Item          | Guidance |
|---------------|----------|
| Electrical    | Isolated digital inputs via opto‑isolated receivers; field domain separate from logic domain |
| Type          | Dry contact or compatible 24 V signaling (follow your wiring standard) |
| Polarity/Invert | Inputs are configurable (enable/invert/action) in WebConfig |
| Noise         | Use shielded cable for long runs; debounce is handled in firmware, but avoid very noisy sources |
| Isolation     | Do **not** tie field ground to logic ground; keep per‑domain returns intact |

### Relays (Outputs)

| Item              | Guidance |
|-------------------|----------|
| Contact Type      | SPDT (NO/NC/COM) |
| Load Type         | Resistive or inductive; **use external snubbers** (RC/MOV) for inductive loads |
| Application       | Prefer driving **interposing relays/ contactors** for motors/pumps or mains circuits |
| Wiring            | Keep load and control wiring separated; use appropriately rated conductors and terminals |
| Safety            | De‑energize before servicing; confirm contact state after wiring and at commissioning |

### RS‑485 (Modbus RTU)

| Item             | Guidance |
|------------------|----------|
| Bus Termination  | 120 Ω at **both** physical ends of the RS‑485 trunk |
| Polarity         | Maintain **A/B** polarity consistently; connect **COM/GND** reference between nodes |
| Cabling          | Use twisted pair (shielded in noisy environments); daisy‑chain topology (no stubs) |
| Protection       | The port includes fusing and TVS; still route away from high‑power cabling and contactors |

### USB‑C

| Item        | Guidance |
|-------------|----------|
| Purpose     | **Setup/maintenance only** (WebConfig / firmware); not for powering field devices |
| ESD         | Protected on‑board; still avoid hot‑plugging in high‑EMI environments |
| Host        | Use a PC with Chrome/Edge; ensure a reliable chassis ground while connected |

---


<a id="4-installation-quick-start"></a>

# 4. Installation & Quick Start

The DIO-430-R1 joins your system over **RS-485 (Modbus RTU)**. Setup has two parts:  
1) **Physical wiring**, 2) **Digital configuration** (WebConfig → optional PLC/ESPHome).

---

## 4.1 What You Need

| Category          | Item / Notes |
|-------------------|--------------|
| **Hardware**      | **DIO-430-R1** — DIN-rail module with **4× DI**, **3× SPDT relays**, **3× buttons**, **3× LEDs**, **USB-C**, **RS-485**.  |
| **Controller (master)** | HomeMaster **MiniPLC/MicroPLC** or any **Modbus RTU** master. |
| **24 VDC PSU (SELV)** | Regulated **24 VDC**; size for logic + relay coils + sensors; inline panel fuse/breaker. Power input stage includes fuse/TVS/reverse-polarity protection.  |
| **RS-485 cable**  | Twisted pair for **A/B** + **COM/GND** reference, 120 Ω termination at both ends of the trunk.  |
| **USB-C cable**   | For WebConfig via a Chromium browser (service/commissioning).  |
| **Software**      | **Chromium-based browser** with Web Serial (Chrome/Edge). Web page exposes **Address/Baud** + I/O mapping.  |
| **Field I/O**     | **Dry contacts** to DI1…DI4 (isolated front-end per channel). **Relays** (NO/NC/COM) drive LV loads or interposing contactors; add RC/MOV snubbers for inductive loads.  |

> **Quick path:** mount → wire **24 VDC** + **RS-485 A/B/COM** → connect **USB-C** → WebConfig: set **Address/Baud** + map **inputs → relays/LEDs** → disconnect USB → hand over to controller. 

---

## 4.2 Power

The module uses **24 VDC** primary. Onboard regulation provides **5 V → 3.3 V** for logic; DI front-end is isolated.

### 4.2.1 Supply Types
- **24 VDC DIN-rail PSU** → **24Vdc(+) / 0V(–)** power terminals (top row: POWER).   
- **Sensor side (DI)** — isolated input receivers accept field signals; feed your sensors from the 24 V field rail and return to the **DI GND** pins (per-channel). Do **not** back-power logic from sensor rails. 

### 4.2.2 Sizing (rule of thumb)
Account for:
- Base electronics + LEDs  
- **Relay coils** (up to **3** simultaneously)  
- **Sensor rails** (DI field side, if powered from the same 24 V source)

> Size PSU for **worst-case relays + sensors**, then add **≥30 % headroom**.

### 4.2.3 Power Safety
- Correct polarity; keep logic **GND** and DI field ground **separate** (respect isolation domains).   
- Keep upstream **fusing/breaker** in place; the board also has input fuse/TVS/reverse-polarity MOSFET.   
- Use **snubbers** on inductive loads; prefer **interposing contactors** for motors/pumps.   
- **De-energize** before wiring; check shorts before power-up.

---

## 4.3 Networking & Communication

Runtime control is via **RS-485 (Modbus RTU)**. **USB-C** is for local setup/diagnostics (Web Serial).

### 4.3.1 RS-485 (Modbus RTU)

**Physical**
- **Terminals (lower front row):** **B**, **A**, **COM/GND** → then DI and DI-GNDs. Maintain A/B polarity, share the **COM/GND** reference with the controller.   
- **Cable:** Twisted pair (preferably shielded) for A/B + reference.  
- **Termination:** **120 Ω** at both physical ends of the trunk; avoid stubs. 

**Protocol**
- Role: **RTU slave**; controller is **master**.  
- **Address:** 1–255. **Factory default**: **Address 3**, **19200 8N1**.   
- Required: Dedicated **24 VDC** power (bus is data-only).

**Checklist**
- A→A, B→B, **COM→COM** (GND ref).  
- Two end terminations only; daisy-chain topology.  
- Consistent A/B polarity end-to-end.

### 4.3.2 USB-C (WebConfig)

**Purpose:** Chromium (Chrome/Edge) Web Serial setup/diagnostics page. 

**Steps**
1. Connect **USB-C** to the module.  
2. Open the **DIO-430-R1 WebConfig** page and click **Connect**.   
3. Set **Modbus Address & Baud** (header shows **Active Modbus Configuration**).   
4. Configure **Inputs / Relays / LEDs / Buttons**; changes apply live and are saved to flash.   
5. Use **Reset Device** from the page if needed (dialog confirms). 

> If **Connect** is disabled: ensure Chromium + serial permission; close other apps that might hold the port.

---

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

