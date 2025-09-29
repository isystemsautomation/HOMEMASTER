**Firmware Version:** 2025-09 snapshot

# WLD-521-R1 – Water Meter & Leak Detection Module

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="left" width="220" alt="WLD-521-R1 module photo">

The **WLD-521-R1** is a smart and reliable input/control module designed for **leak detection** and **water flow monitoring** in residential, commercial, and industrial environments. It connects easily to **MicroPLC** or **MiniPLC** systems via **RS-485 (Modbus RTU)** and offers seamless integration with **ESPHome** and **Home Assistant** for advanced automation.

<br clear="left"/>

## 📑 Table of Contents

### 1. [Introduction]
- [1.1 Overview of the WLD-521-R1 Module]
- [1.2 Supported Modules & Controllers]
- [1.3 Use Cases]  

### 2. [Safety Information](#2-safety-information)
- [2.1 General Electrical Safety] 
- [2.2 Handling & Installation] 
- [2.3 Device-Specific Warnings]

### 3. [System Overview]
- [3.1 Architecture & Modular Design]
- [3.2 MicroPLC vs MiniPLC]
- [3.3 Integration with Home Assistant]
- [3.4 Diagrams & Pinouts] 
- [3.5 WLD-521-R1 — Technical Specification]

### 4. [Getting Started](#4-getting-started)
- [4.1 What You Need](#41-what-you-need)  
- [4.2 Quick Setup Checklist](#42-quick-setup-checklist)  

### 5. [Powering the Devices](#5-powering-the-devices)
- [5.1 Power Supply Types](#51-power-supply-types)  
- [5.2 Current Consumption](#52-current-consumption)  
- [5.3 Power Safety Tips](#53-power-safety-tips)  

### 6. [Networking & Communication](#6-networking--communication)
- [6.1 RS-485 Modbus](#61-rs-485-modbus)  
- [6.2 USB-C Configuration](#62-usb-c-configuration)  

### 7. [Installation & Wiring](#7-installation--wiring)
- [7.1 ENM-223-R1 Wiring](#71-enm-223-r1-wiring)  

### 8. [Software & UI Configuration](#8-software--ui-configuration)
- [8.1 Web Config Tool (USB Web Serial)](#81-web-config-tool-usb-web-serial)  
- [8.2 ESPHome / Home Assistant](#82-esphome--home-assistant)  
- [8.3 Meter Options & Calibration](#83-meter-options--calibration)  
- [8.4 Alarms](#84-alarms)  
- [8.5 Relays & Overrides](#85-relays--overrides)  
- [8.6 Buttons](#86-buttons)  
- [8.7 User LEDs, Energies & Live Meter](#87-user-leds-energies--live-meter)  

### 9. [Modbus RTU Communication](#9-modbus-rtu-communication)
- [9.1 Input Registers (Read-Only)](#91-input-registers-read-only)  
- [9.2 Holding Registers (Read/Write)](#92-holding-registers-readwrite)  
- [9.3 Discrete Inputs & Coils](#93-discrete-inputs--coils)  
- [9.4 Scaling Summary](#scaling-summary)  
- [9.5 Basics & Function Codes](#basics--function-codes)  
- [9.6 Register Map (Summary)](#register-map-summary)  
- [9.7 Override Priority](#override-priority)  

### 10. [Programming & Customization](#10-programming--customization)
- [10.1 Supported Languages](#101-supported-languages)  
- [10.2 Flashing via USB-C](#102-flashing-via-usb-c)  
- [10.3 PlatformIO & Arduino](#103-platformio--arduino)  

### 11. [Diagrams & Pinouts](#11-diagrams--pinouts)  
### 12. [Maintenance & Troubleshooting](#12-maintenance--troubleshooting)  
### 13. [Technical Specifications](#13-technical-specifications-electrical--external)  
### 14. [Open Source & Licensing](#14-open-source--licensing)  
### 15. [Downloads](#15-downloads)  
### 16. [Support](#16-support)

### 17. [ESPHome Integration Guide (MicroPLC/MiniPLC + ENM)](#17-esphome-integration-guide-microplcminiplc--enm)

---
# 1. [Introduction]
## 1.1 Overview of the WLD-521-R1 Module 💧

The WLD-521-R1 is a highly specialized I/O module designed primarily for **Water Flow, Heat Energy (Calorimetry), and Irrigation/Leak Detection** applications. It functions as an intelligent Modbus slave device that processes local sensor data and executes commands from a master controller.

| Component | Quantity | Key Functionality |
| :--- | :--- | :--- |
| **Digital Inputs (DI)** | 5 (DI1-DI5) | **Opto-isolated** inputs used for pulse counting (Flow Meters) or reading dry-contact status (Leak Sensors, Buttons, General Status). |
| **Relays (RLY)** | 2 (R1, R2) | **High-performance industrial relays** for switching external loads, most commonly used for **pumps** or **shut-off valves**. |
| **1-Wire Bus (OW)** | 1 | Dedicated bus supporting up to 32 **DS18B20 temperature sensors** for accurate temperature monitoring and heat energy calculation. |
| **Local Interface** | 4 LEDs, 4 Buttons | Provides local status indication, manual control, and **override functions**. |

---

## 1.2 WLD-521-R1 — Supported Controllers 🔌


The **WLD-521-R1** is an intelligent expansion module for the **HomeMaster MiniPLC / MicroPLC Modular Smart Control System**.

### MiniPLC / MicroPLC (via Modbus RTU)

| Feature          | Specification                                                                                                                                         |
|------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------|
| **Controller Role** | Operates as a **Modbus Slave**. The MiniPLC/MicroPLC acts as the **Modbus Master** for network and system logic management.                          |
| **Communication**   | **Modbus RTU** (Serial).                                                                                                                             |
| **Physical Interface** | **RS-485** bus (using dedicated **UART2** pins).                                                                                                    |
| **Function**         | Master can **read** all I/O data (Flow, Heat, Leak Status) and **write** commands to actuate **Relays R1, R2** and control **irrigation zones**.     |
| **Modular Design**   | RS-485 architecture supports **daisy-chaining** multiple WLD-521-R1 modules and other expansion units for scalable I/O.                               |
| **Default ID**       | Factory-set **Modbus Slave ID: 3**.                                                                                                                  |

> **Note:** If multiple WLD-521-R1 modules share the same RS-485 segment, change the slave ID on each unit to avoid address conflicts.


---

## 1.3 Use Cases 🛠️

Below are practical ways to deploy the **WLD-521-R1** with the HomeMaster Mini/Micro PLC.

### 1) Basement leak detection + automatic water shut-off
- **Goal:** When water is detected, automatically close the main water valve and notify the PLC.
- **How:** Set **DI1** to **Water sensor**, enable it, and set its **Action** to *Toggle* with **Control target** → *Relay 1*. Wire a **motorized shut-off valve** (e.g., NC solenoid/actuated ball valve) to **R1** so the valve **closes on leak**. Optionally map an LED to **DI1** or **R1** for visual status. The PLC can read the leak status over Modbus for alerts or interlocks.

### 2) Garden irrigation with flow supervision & pump dry-run protection (merged)
- **Goal:** Safe, autonomous watering with pump protection and sensor interlocks.
- **How:** Configure **Zone 1/2** with a valve on **Relay 1/2** and select a **Flow DI (1..5)** tied to a pulse flowmeter. Enable **Use flow supervision** and set **Min rate (L/min)**, **Grace (s)**, **Timeout**, and optional **Target liters**.  
  Add protection and interlocks: set **R_pump** to the pump relay; require **DI_tank (OK when ON)**; use **DI_moist** (needs water when OFF) and **DI_rain** (block when ON)**.** The zone will **block or stop** when sensors are not OK or when flow drops below thresholds. Use **Irrigation Window** and **Auto-start** for time-based runs.

### 3) Water consumption metering
- **Goal:** Count liters for one or more branches (e.g., apartment/tenant billing).
- **How:** Set selected inputs to **Water counter**. Enter **Pulses per liter** and (optionally) calibrate **Total ×** and **Rate ×**. Use **Reset total** to move the accumulation baseline and **Calc from external** to align with an external meter after service. View **Rate (L/min)** and **Total (L)** live in the UI.

### 4) Hydronic loop heat-energy (ΔT) monitoring
- **Goal:** Track instantaneous heat power and energy per loop.
- **How:** For a DI configured as **Water counter**, open its **Heat** panel and enable **Heat**. Assign **Sensor A (supply)** and **Sensor B (return)** from stored **1-Wire** devices, then set **cp (J/kg·°C)**, **ρ (kg/L)**, and **Calibration (×)** as needed. The UI shows **TA**, **TB**, **ΔT**, **Power (W)**, **Energy (J / kWh)**. Use **Reset energy** to zero totals; manage sensors from **1-Wire** (scan, name, view live temperatures).

# 2. Safety Information

This section outlines essential safety guidelines. Failure to adhere to these warnings may result in equipment damage, system failure, or personal injury.

## 2.1 General Electrical Safety

| Warning | Requirement |
| :--- | :--- |
| **Professional Service** | Installation and maintenance must be performed exclusively by **qualified personnel** familiar with electrical codes. |
| **Power Isolation** | **ALWAYS** disconnect the primary **24VDC** power supply and all connected loads before performing any wiring, installation, or maintenance. |
| **Voltage Verification** | Ensure the power source strictly adheres to the **24VDC** primary supply voltage. |
| **Grounding** | Ensure all field wiring and system components are properly grounded. |

## 2.2 Handling & Installation

| Requirement | Detail |
| :--- | :--- |
| **ESD Protection** | Handle the module by its casing or edges and observe **Electrostatic Discharge (ESD)** precautions when handling the bare printed circuit boards (PCBs). |
| **Wiring** | Use insulated wires of the appropriate gauge for the connected loads. **Secure all wires firmly** to the screw terminals; loose connections pose a fire and arcing risk. |
| **Mounting** | The module is designed for **DIN-rail mounting** . It must be secured within a protective enclosure to shield it from moisture, dust, and mechanical damage. |

## 2.3 Device-Specific Warnings

| Component | Warning |
| :--- | :--- |
| **Relays (R1, R2)** | Do not exceed the **maximum voltage and current ratings** of the dry-contact relay outputs. Overloading the relays will cause permanent device damage and is a fire hazard. |
| **Digital Inputs (DI1-DI5)** | Inputs are opto-isolated. Only connect **dry-contact** or **low-voltage, isolated** signals. Connecting high AC or DC voltages will compromise the internal isolation barrier. |
| **Power Outputs (+5V / +12V)** | Do not exceed the specified power budget for the isolated +5VDC and +12VDC sensor supply outputs. |
| **1-Wire Bus (OW)** | Use this interface only for low-voltage digital sensors (e.g., DS18B20). |


# 3. System Overview
## 3.1 Architecture & Modular Design
## 3.2 MicroPLC vs MiniPLC
## 3.3 Integration with Home Assistant
## 3.4 Diagrams & Pinouts
## 3.5 Technical Specifications

## 3.5 WLD-521-R1 — Technical Specification

### Overview

- **Function**: Water leak detection, pulse water metering, irrigation control, and optional heat (ΔT) calculations  
- **System role**: RS-485 **Modbus RTU** slave; integrates with MicroPLC/MiniPLC and home automation stacks  
- **Form factor**: DIN-rail module, ~3M wide (approx. “3-gang” footprint).

---

### I/O Summary

| Interface | Qty | Electrical / Notes |
|---|---:|---|
| **Digital Inputs (DI1..DI5)** | 5 | **Opto-isolated**; supports dry-contact and pulse meters. Typical trigger ≈ **5 V**, ≈ **6 mA** input current; up to **~9 Hz** (firmware supports higher with configuration).|
| **Relays (R1, R2)** | 2 | SPDT (NO/NC/COM) dry contacts for valves/pumps; max **3 A @ 250 VAC**. Hardware uses **HF115F/005-1ZS3** relays with opto-isolated drivers.|
| **1-Wire bus** | 1 | 3-pin header: **+5 V / DATA / GND**. Protected by **DS9503** and MOSFET level shifting. Designed for DS18B20-class sensors.|
| **User buttons** | 4 | Panel buttons (SW1..SW4) routed to MCU GPIO; used for local control/overrides.|
| **User LEDs** | 4 + status | Front LEDs for status/override indication; driven via transistor stages from MCU GPIO.]{index=5} |

---

### Terminals & Pinout (field side)

<img src="Images/photo1.png" align="center" width="220" alt="WLD-521-R1 module photo">

---

- **Power**: `24VDC` and `0V` (primary supply). 
- **Inputs**: `I1..I5` and **GND_ISO** (return for input side). Each DI has opto-isolation and input conditioning.  
- **Relays**:  
  - **Relay 1**: `R1_NO`, `R1_C`, `R1_NC`  
  - **Relay 2**: `R2_NO`, `R2_C`, `R2_NC`
- **RS-485**: `A`, `B`, `COM` (shield/earth reference). 
- **1-Wire**: `+5V`, `D`, `GND` (isolated 5 V sourced).
- **Aux sensor power**: **+5 V** and **+12 V** isolated rails available for external sensors (fuse-protected).

> The front-label silkscreen in the product photo aligns with the terminals above (Inputs, RS-485, Relays, +5/12 V sensor supply).

---

### Electrical

#### Power & Regulation
- **Primary input**: **24 VDC** nominal. On-board protection includes input fuse, reverse protection diode, and surge suppression. A synchronous buck (**AP64501**) generates +5 V, followed by **AMS1117-3.3** for +3.3 V logic. :contentReference[oaicite:12]{index=12}
- **Isolated sensor rails**:  
  - **+12 V iso**: **B2412S-2WR3** DC-DC  
  - **+5 V iso**: **B2405S-2WR3** DC-DC  
  Both rails are LC-filtered and fuse-limited for field use. :contentReference[oaicite:13]{index=13}

#### Digital Inputs
- Opto-couplers **SFH6156-3** with series resistors and MOSFET front-ends for pulse handling and noise immunity. Pull-downs and Schmitt-style shaping provided per channel. :contentReference[oaicite:14]{index=14}

#### Relay Outputs
- **HF115F/005-1ZS3** SPDT relays with transistor drivers and opto-isolated control; RC snubbers/EMI parts on contacts for suppression. :contentReference[oaicite:15]{index=15}

#### 1-Wire Interface
- **DS9503** ESD/short protection and **BSS138** level translation between 3V3 MCU and 5 V field bus. Schottky clamp on +5 V. :contentReference[oaicite:16]{index=16}

#### RS-485 (Modbus RTU)
- Transceiver **MAX485** with DE/RE control, series/TVS protection (**SMAJ6.8CA**), polyfuses on A/B, and bias/termination network. Level-shifted between 3V3 MCU UART and 5 V transceiver. TX/RX activity LEDs present. :contentReference[oaicite:17]{index=17}

#### USB-C (service/config)
- **Type-C** receptacle with ESD array (**PRTR5V0U2X**), CC pull-downs, series resistors on D±, and reverse-polarity protection to +5 V rail (Schottky). Used for firmware and the Web Config Tool. :contentReference[oaicite:18]{index=18}

---

### MCU & Storage

- **MCU**: **Raspberry Pi RP2350A** (dual-core; RP2 family) with external QSPI flash. :contentReference[oaicite:19]{index=19}  
- **Boot/Flash**: **W25Q32JV** 32-Mbit QSPI NOR. :contentReference[oaicite:20]{index=20}  
- **Clocks**: 12 MHz crystal with load network; SWD header exposed for debug. :contentReference[oaicite:21]{index=21}

---

### Protections & Compliance-Oriented Features

- Input surge/ESD: TVS on RS-485 lines, ESD arrays on USB, series resistors and RC on data lines. Polyfuses on field buses. :contentReference[oaicite:22]{index=22}  
- Galvanic isolation: Separate **GND_ISO** domain for field inputs/power; isolated +5 V/+12 V DC-DC modules for sensors. :contentReference[oaicite:23]{index=23}  
- Relay contact suppression and snubbers to reduce EMI and contact wear. :contentReference[oaicite:24]{index=24}

---

### Firmware / Function (high-level)

- **Leak & irrigation logic**, pulse-based **flow** and **totalization**, optional **heat power/energy** using dual 1-Wire temps and pulse-derived flow.  
- USB-based **Config UI**, **Modbus RTU** register map, and override priority for local/manual control. :contentReference[oaicite:25]{index=25}

---

## Key Ratings (from prior release)

| Parameter | Typical / Max |
|---|---|
| Supply voltage | **24 VDC** |
| DI trigger level / current | ≈ **14 V** / ≈ **6 mA** |
| DI frequency | **~9 Hz** default (higher configurable) |
| Relay contacts | **3 A @ 250 VAC** (per relay) |
| Sensor power | **+5 V / +12 V** isolated, up to **~50 mA** combined budget (guideline) |
| Firmware update | **USB-C** (Web Serial / DFU) |
| Mounting | **DIN-rail**, ~3-module width | :contentReference[oaicite:26]{index=26}

---

## Connector Map (front label reference)

- **Top**: `V+`, `0V` (Power) • `I1..I5`, `GND` (Inputs) • `+5V`, `D`, `GND` (1-Wire) • `A`, `B`, `COM` (RS-485)  
- **Bottom**: `R1: NO, C, NC` • `R2: NO, C, NC` • `5/12 Vdc` sensor supply outputs. :contentReference[oaicite:27]{index=27}

---

### Notes

- Actual enclosure dimensions and environmental limits (temperature, humidity, IP rating) should be confirmed with the latest mechanical drawing—values are not specified in the current schematics.  
- For multi-module RS-485 networks, assign unique Modbus addresses and observe proper termination and biasing per segment. :contentReference[oaicite:28]{index=28}





## 🔧 Key Features

- **5 Opto-isolated digital inputs**
  - Supports dry contact leak sensors and pulse water meters
  - Configurable for up to 1 kHz input frequency
- **2 Relay outputs (NO/NC)**
  - For valves, pumps, or alarm systems
- **1-Wire interface**
  - Compatible with DS18B20 temperature sensors
- **Galvanically isolated power outputs**
  - 12 V and 5 V for powering external sensors
- **Front panel controls**
  - 4 user buttons and status LEDs
- **USB Type-C**
  - Easy firmware updates and configuration


---

## How heat is computed

1. **Temperatures**
   - Two 1‑Wire sensors: **A = supply (TA)** and **B = return (TB)**.
   - $\Delta T = T_A - T_B\ \text{(°C)}$.

2. **Flow from pulses** *(DI set to “Water counter”)*
   - The input counts pulses from a flow meter.
   - With **PPL = pulses per liter**:
     
     $$
     \text{raw flow}_{\mathrm{L/s}} = \frac{\text{pulses per second}}{\mathrm{PPL}}
     $$
     
   - Two separate calibrations exist in your UI:
     - **Rate calibration** *(Rate ×)*: scales the instantaneous flow *(used for power)*:
       
       $$
       \text{flow}_{\mathrm{L/s}} = \text{raw flow}_{\mathrm{L/s}} \times \mathrm{calRate}
       $$
     
     - **Total calibration** *(Total ×)*: applied to the accumulated liters counter *(for the displayed total volume)*.  
       *(You’ll see both in the Flow box: “Rate” and “Total”.)*

3. **Power (heat rate)**
   - With $\rho$ *(kg/L)* and $c_p$ *(J/kg·°C)*, convert flow to mass flow and multiply by $\Delta T$:
     
     $$
     \dot m\ \text{(kg/s)} = \text{flow}_{\mathrm{L/s}} \times \rho
     $$
     
     $$
     \text{Power (W)} = \dot m \times c_p \times \Delta T
     $$
     
   - Then your **Heat calibration** *(Calibration ×, shown in the Heat box)* is applied to the computed power/energy:
     
     $$
     \text{Power}_{\text{final}} = \text{Power} \times \mathrm{calib}
     $$

4. **Energy**
   - Energy integrates power over time:
     
     $$
     E_J(t+\Delta t) = E_J(t) + \text{Power}_{\text{final}} \times \Delta t
     $$
     
   - The UI shows both:
     - **Energy (J)** — raw joules.
     - **Energy (kWh)** — converted as:
       
       $$
       \mathrm{kWh} = \frac{E_J}{3{,}600{,}000}
       $$

---

### Reset behavior
- **Reset energy** sets the energy accumulator baseline to zero *(doesn’t affect flow totals)*.
- **Reset total** in the *Flow* box moves the volume baseline *(pulses are preserved)*.
- **Calc from external** lets you enter a reference volume since the last total reset; the module derives a new **Total** calibration so the accumulated liters match your reference.

### Defaults & signs
- Defaults shown: $c_p = 4186\ \text{J/kg·°C}$, $\rho = 1.000\ \text{kg/L}$, $\mathrm{Calibration}=1.0000$.
- $\Delta T$ is $A - B$. If return gets hotter than supply, $\Delta T$ goes negative; then power becomes negative. *(If you prefer to clamp to zero, that’s a simple firmware change.)*

### Tiny worked example
- **Flow:** $6.0\ \text{L/min} \rightarrow \frac{6}{60} = 0.1\ \text{L/s}$  
- $\rho = 1.0\ \text{kg/L} \Rightarrow \dot m = 0.1\ \text{kg/s}$  
- $c_p = 4186\ \text{J/kg·°C}$, $\Delta T = 5.0^\circ\text{C}$
  
  $$
  \text{Power} = 0.1 \times 4186 \times 5 = 2093\ \text{W} \approx 2.09\ \text{kW}
  $$
  
- **Over 10 seconds:**  
  
  $$
  \text{Energy} \approx 2093 \times 10 = 20{,}930\ \text{J} \approx 0.0058\ \text{kWh}
  $$
  
- Apply **Calibration ×** at the end if it’s not 1.0.


## 🏠 Example Use Cases

- Automatic valve shutoff on leak detection
- Water consumption monitoring with pulse meters
- Temperature-aware pipe protection logic
- Manual override buttons for maintenance

## What this does (in super simple terms)

- You have **2 irrigation zones** (Zone 1 & Zone 2).  
- Each zone opens a **valve relay** (Relay 1 or Relay 2).  
- A zone can **watch a flow sensor** on one input (**DI1..DI5**) to be sure water actually moves and to **count liters**.  
- Optional **sensors** can **block watering**:
  - **Moisture**: wet = block
  - **Rain**: raining = block
  - **Tank level**: empty = block
- Optional **pump** can run automatically while a zone is watering.
- You can limit watering to a daily **time window** and even **auto‑start** at the window start.
- You can set a **target liters** to stop automatically when that much water has passed.

> This README focuses only on irrigation (the firmware also supports flow, heat energy, 1‑Wire etc.).

---

### Before you start (1 minute)

1. **Wire things**
   - Valve(s) → **Relay 1/2**  
   - Flow sensor → one of **DI1..DI5**  
   - Optional: **moisture / rain / tank** sensors → free DI pins  
   - Optional: **pump** → one of the relays
2. **Power on** the module.
3. Open the **Configuration UI** (the web page for the device) and click **Connect**.

---

### Step 1 — Set the module clock (so windows & auto‑start make sense)

- Go to **“Module Time & Modbus Sync”**.  
- Click **Set from browser time**. Done.

*(Advanced: Home Assistant can send a “midnight pulse” via Modbus; totally optional.)*

---

### Step 2 — Tell the device which input is your flow sensor

- In **“Digital Inputs (5)”**, on the DI that has your **flow meter**, set **Type = Water counter**.  
- Leave regular sensors (moisture/rain/tank) as **Water sensor** (default).

*(Calibration can wait; defaults work.)*

---

### Step 3 — Configure a zone (repeat for both zones if needed)

Open **Irrigation → Zone 1**:

1. **Enable** the zone.
2. **Valve relay** → choose **Relay 1** (or 2), matching your wiring.
3. **Flow DI (1..5)** → pick the DI where your **flow sensor** is connected.
4. Keep **Use flow supervision** **ON** (recommended).  
   - **Min rate (L/min)**: start with **0.20**  
   - **Grace (s)**: **8** (lets pipes pressurize)  
   - **Target liters**: set a number (e.g. **50**). **0** = unlimited  
   - **Timeout (s)**: **3600** (1 hour safety)
5. **Sensors & Pump (optional)**
   - **DI_moist (needs water when OFF)** → pick your soil sensor DI  
     - *Dry (OFF) = watering allowed; Wet (ON) = block*
   - **DI_rain (block when ON)** → pick your rain sensor DI  
     - *Raining (ON) = block*
   - **DI_tank (OK when ON)** → pick your tank level DI  
     - *Tank OK (ON) = allowed; OFF = block*
   - **R_pump** → pick the relay that powers your pump (**(none)** if you don’t use a pump).  
     *The pump runs automatically only while the zone runs.*
6. **Irrigation Window (optional)**
   - **Enforce window** → ON if you only want watering during certain hours
   - **Start** / **End** → e.g. **06:00 → 09:00** (*cross‑midnight works, e.g. 22:00 → 06:00*)
   - **Auto‑start at window open** → ON if you want **daily automatic start** at the **start** time
7. Click **Save Zone 1**. Repeat for Zone 2 if used.

---

### Step 4 — Water!

- **Manual**: Press **Start** on the zone. It will run until you **Stop**, or:
  - **Target liters** is reached, or
  - **Timeout** hits, or
  - a **sensor blocks** it, or
  - the **window closes** (if enforced).
- **Automatic**: If **Auto‑start** is ON, the zone starts **once per day** at the **window start** time.

---

### What “Live Status” tells you

Each zone shows:
- **State**: `idle` / `run` / `pause` / `done` / `fault`  
- **Rate**: L/min (from your flow DI)  
- **Accum**: liters done so far  
- **Elapsed**: seconds since start  
- **Target** & **Timeout** (if set)  
- **Time now**: module clock (HH:MM)  
- **Window**: `OPEN` or `CLOSED`  
- **Sensors**: `OK` or `BLOCK`

If a start is refused, check **Window** and **Sensors** (they reveal the reason).

---

### Suggested starter settings

- **Use flow supervision**: ON  
- **Min rate**: `0.20 L/min`  
- **Grace**: `8 s`  
- **Timeout**: `3600 s`  
- **Target liters**: set what you want (or `0` for unlimited)  
- **Window**: `06:00–09:00` with **Auto‑start** ON (if you want daily watering)

---

### Common actions

- **Start / Stop / Reset** → buttons on each **Zone** card and in **Live Status**  
- **Shared pump for multiple zones** → set **R_pump** to the same relay in each zone; the firmware handles it (pump runs if any zone needs it)  
- **Only water when soil is dry** → set **DI_moist**; Dry (OFF) = allowed, Wet (ON) = block  
- **Skip when raining** → set **DI_rain**; ON = block  
- **Block if tank empty** → set **DI_tank**; ON = ok, OFF = block

---

### Troubleshooting (fast)

- **Won’t start** → check the zone is **Enabled**, **Window = OPEN**, **Sensors = OK**  
- **Stops with “low flow”** → increase **Grace**, lower **Min rate** a little, or check flow wiring  
- **Stops immediately** → **Target liters** is tiny, or a **sensor** is blocking  
- **Pump doesn’t run** → set **R_pump** to your pump’s relay and make sure that relay is **Enabled**  
- **Time looks wrong** → click **Set from browser time** again

---

### Optional: Midnight sync from Home Assistant (Modbus)

If you want the module clock to reset at midnight (00:00) via Modbus:

- Write `TRUE` then immediately `FALSE` to **coil 360** at 00:00 (daily).  
  The firmware will set minute‑of‑day to `0` and increment day‑index.

> This is optional. Most users can ignore it and just use “Set from browser time”.

---

### That’s it ✅

Use **Save** after changes, **Start** to test, watch **Rate** and **Accum** increase, and adjust **Target liters** and **Window** to suit your garden.


## ⚙️ Specifications

| Parameter                         | Value                              |
|----------------------------------|------------------------------------|
| Power Supply                     | 24 V DC                            |
| Digital Inputs                   | 5 opto-isolated, dry contact       |
| Input Voltage (trigger)          | ~14 V                              |
| Input Current                    | ~6 mA                              |
| Input Frequency                  | Up to 9 Hz (1 kHz configurable)    |
| Relay Outputs                    | 2 (NO/NC, max 3 A @ 250 V AC)      |
| 1-Wire Bus                       | Yes                                |
| Sensor Power Output              | 12 V / 5 V isolated (up to 50 mA)  |
| Communication Interface          | RS-485 (Modbus RTU)                |
| Dimensions                       | DIN-rail mount, 3 modules wide     |
| Firmware Update                  | USB Type-C                         |

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
