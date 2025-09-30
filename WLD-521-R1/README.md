**Firmware Version:** 2025-09 snapshot

# WLD-521-R1 – Water Meter & Leak Detection Module

**HOMEMASTER – Modular control. Custom logic.**

<img src="Images/photo1.png" align="right" width="440" alt="WLD-521-R1 module photo">

### Module Description

The **WLD-521-R1** is a smart, fully configurable input/control module for **leak detection**, **water flow**, **heat energy**, and **irrigation**. It connects to a **MicroPLC/MiniPLC** over **RS-485 (Modbus RTU)** and is configured via a USB-C **WebConfig** UI that lets you set Modbus params, choose per-input types (sensor/counter), map inputs to relay actions, calibrate flow, compute heat energy from paired 1-Wire sensors, run **two local irrigation zones** (flow supervision, interlocks, time windows, pump), and assign **LEDs/Buttons** with overrides—all with live status and optional clock sync for HA. Integration with **ESPHome + Home Assistant** is straightforward through the controller, which exposes the module’s **sensors** and **relays** as entities.

<br clear="left"/>

## 📑 Table of Contents

### 1. [Introduction]
- [1.1 Overview of the WLD-521-R1 Module]
- [1.2 Supported Modules & Controllers]
- [1.3 Use Cases]  

### 2. [Safety Information]
- [2.1 General Electrical Safety] 
- [2.2 Handling & Installation] 
- [2.3 Device-Specific Warnings]

### 3. [System Overview]
- [3.1 Architecture & Modular Design]
- [3.2 Integration with Home Assistant]
- [3.3 Diagrams & Pinouts] 
- [3.4 WLD-521-R1 — Technical Specification]

### 4. [Getting Started]
- [4.1 What You Need]
- [4.2 Powering the Devices]
  - [4.2.1 Power Supply Types]
  - [4.2.2 Current Consumption]
  - [4.2.3 Power Safety Tips]
- [4.3 Networking & Communication]
  - [4.3.1 RS-485 Modbus]
  - [4.3.2 USB-C Configuration]
- [4.4 Quick Setup]

### 5. [Installation & Wiring]
- [5.1 WLD-521-R1 Wiring]

### 6. [Software & UI Configuration]
- [6.1 Web Config Tool (USB Web Serial)] 
- [6.2 ESPHome / Home Assistant]
- [6.3 Meter Options & Calibration] 
- [6.4 Alarms]
- [6.5 Relays & Overrides]
- [6.6 Buttons] 
- [6.7 User LEDs, Energies & Live Meter]

### 7. [Modbus RTU Communication]
- [7.1 Input Registers (Read-Only)] 
- [7.2 Holding Registers (Read/Write)]
- [7.3 Discrete Inputs & Coils]
- [7.4 Scaling Summary]
- [7.5 Basics & Function Codes] 
- [7.6 Register Map (Summary)]
- [7.7 Override Priority] 

### 8. [ESPHome Integration Guide (MicroPLC/MiniPLC + WLD-521-R1)]

### 9. [Programming & Customization]
- [9.1 Supported Languages] 
- [9.2 Flashing via USB-C] 
- [9.3 PlatformIO & Arduino]

### 10. [Maintenance & Troubleshooting] 
### 11. [Open Source & Licensing]
### 12. [Downloads] 
### 13. [Support]

---
# 1. [Introduction]
## 1.1 Overview of the WLD-521-R1 Module 💧

The WLD-521-R1 is a specialized I/O module for water flow, heat energy (calorimetry), and irrigation/leak detection. It operates as an intelligent Modbus RTU slave that processes local sensor data and runs a fully configurable internal logic (e.g., input→relay actions, irrigation windows, flow supervision, safety interlocks) autonomously. Automation scenarios can then be extended and orchestrated by a central PLC/controller to add system-wide coordination, monitoring, history, and advanced rules.

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

The **WLD-521-R1 (Water Leak Detection, 5 Inputs, 2 Relays)** is a dedicated Extension Module designed for reliable, distributed automation within the HomeMaster system. It’s built on an **RP2350** microcontroller and concentrates water-safety I/O into a single DIN-rail Modbus device.

**Core I/O & Sensors**
- **5× Digital Inputs (DI):** configurable for flow meters and water-related sensors (leak/moisture/rain).
- **2× Relays (R1, R2):** for valves, pumps, sirens, or other actuators.
- **1-Wire bus (GPIO16):** for temperature sensors (e.g., DS18B20). Optional heat-energy on a counter DI using two 1-Wire sensors (A/B).

**Communication & Setup**
- **RS-485 Modbus RTU** to the central MiniPLC/MicroPLC (typ. **19200 8N1**).
- **WebConfig (USB-C):** set unique **Modbus Address (1–255)** and **Baud**, configure DI modes, relays, 1-Wire, and irrigation.

**Local Resilience**
- On-device logic executes critical actions without the controller: immediate **leak → relay action** and **two irrigation zones** with flow supervision, time windows, and interlocks. Core safety keep running even if the controller or Wi-Fi is down.

## 3.2 Integration with Home Assistant

Integration of the WLD-521-R1 into Home Assistant (HA) is achieved via the **ESPHome firmware** running on the HomeMaster controller (MiniPLC/MicroPLC). ESPHome handles Modbus polling/writes and exposes entities to HA.

**Home Assistant Communication**
- **ESPHome abstraction:** the controller periodically reads/writes Modbus **coils** and **holding registers** and publishes:
  - **binary_sensors** (DI / leak),
  - **sensors** (flow rate/total, temperatures / ΔT / power / energy),
  - **switches** (relays / valves),
  - plus irrigation state (via sensors/switches or template entities).

**Add in ESPHome**
- In the controller’s ESPHome YAML, add a `modbus_controller:` and the WLD package/templates for this module. ESPHome then exposes entities to Home Assistant (binary_sensors for DI/leaks, sensors for flow rate/total and temperatures/ΔT/power/energy, switches for relays; irrigation state can be mapped via sensors/switches or template entities).

**Timekeeping & schedules in HA**
- If you rely on the module’s **local irrigation window** or **daily counters**, schedule an HA automation at **00:00** to **pulse coil 360** and write **HREG 1100/1101** for minute/day—this keeps the module clock aligned with HA.

## 3.3 Diagrams & Pinouts
<div align="center">
  <table>
    <tr>
      <td align="center">
        <strong>ENM System Diagram</strong><br>
        <img src="Images/WLD_Diagram.png" alt="ENM System Diagram" width="360">
      </td>
      <td align="center">
        <strong>RP2350 MCU Pinout</strong><br>
        <img src="Images/WLD_MCU_Pinouts.png" alt="MCU Pinouts" width="360">
      </td>
    </tr>
    <tr>
      <td align="center">
        <strong>Field Board Layout</strong><br>
        <img src="Images/FieldBoard_Diagram.png" alt="Field Board Diagram" width="360">
      </td>
      <td align="center">
        <strong>MCU Board Layout</strong><br>
        <img src="Images/MCUBoard_Diagram.png" alt="MCU Board Diagram" width="360">
      </td>
    </tr>
  </table>
</div>

## 3.4 WLD-521-R1 — Technical Specification

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
| **User LEDs** | 4 + status | Front LEDs for status/override indication; driven via transistor stages from MCU GPIO.]|

---

### Terminals & Pinout (field side)

<img src="Images/photo1.png" align="left" width="660" alt="WLD-521-R1 module photo">

#### Connector Map (front label reference)

- **Top**: `V+`, `0V` (Power) • `I1..I5`, `GND` (Inputs) • `+5V`, `D`, `GND` (1-Wire) • `A`, `B`, `COM` (RS-485)  
- **Bottom**: `R1: NO, C, NC` • `R2: NO, C, NC` • `5/12 Vdc` sensor supply outputs. :contentReference
<br clear="left"/>
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
- **Primary input**: **24 VDC** nominal. On-board protection includes input fuse, reverse protection diode, and surge suppression. A synchronous buck (**AP64501**) generates +5 V, followed by **AMS1117-3.3** for +3.3 V logic.
- **Isolated sensor rails**:  
  - **+12 V iso**: **B2412S-2WR3** DC-DC  
  - **+5 V iso**: **B2405S-2WR3** DC-DC  
  Both rails are LC-filtered and fuse-limited for field use. :contentReference

#### Digital Inputs
- Opto-couplers **SFH6156-3** with series resistors and MOSFET front-ends for pulse handling and noise immunity. Pull-downs and Schmitt-style shaping provided per channel. :contentReference

#### Relay Outputs
- **HF115F/005-1ZS3** SPDT relays with transistor drivers and opto-isolated control; RC snubbers/EMI parts on contacts for suppression.

#### 1-Wire Interface
- **DS9503** ESD/short protection and **BSS138** level translation between 3V3 MCU and 5 V field bus. Schottky clamp on +5 V.

#### RS-485 (Modbus RTU)
- Transceiver **MAX485** with DE/RE control, series/TVS protection (**SMAJ6.8CA**), polyfuses on A/B, and bias/termination network. Level-shifted between 3V3 MCU UART and 5 V transceiver. TX/RX activity LEDs present.

#### USB-C (service/config)
- **Type-C** receptacle with ESD array (**PRTR5V0U2X**), CC pull-downs, series resistors on D±, and reverse-polarity protection to +5 V rail (Schottky). Used for firmware and the Web Config Tool.

---

### MCU & Storage

- **MCU**: **Raspberry Pi RP2350A** (dual-core; RP2 family) with external QSPI flash.
- **Boot/Flash**: **W25Q32JV** 32-Mbit QSPI NOR.
- **Clocks**: 12 MHz crystal with load network; SWD header exposed for debug.

---

### Protections & Compliance-Oriented Features

- Input surge/ESD: TVS on RS-485 lines, ESD arrays on USB, series resistors and RC on data lines. Polyfuses on field buses.
- Galvanic isolation: Separate **GND_ISO** domain for field inputs/power; isolated +5 V/+12 V DC-DC modules for sensors.
- Relay contact suppression and snubbers to reduce EMI and contact wear.

---

### Firmware / Function (high-level)

- **Leak & irrigation logic**, pulse-based **flow** and **totalization**, optional **heat power/energy** using dual 1-Wire temps and pulse-derived flow.  
- USB-based **Config UI**, **Modbus RTU** register map, and override priority for local/manual control.

---

# 4. Getting Started

The **WLD-521-R1** integrates into the HomeMaster system over the **RS-485 Modbus** bus. Initial setup has two parts: **physical wiring** and **digital configuration** (WebConfig + ESPHome).

---

## 4.1 What You Need

| Category   | Item                    | Details                                                                                         |
|------------|-------------------------|-------------------------------------------------------------------------------------------------|
| **Hardware** | **WLD-521-R1 Module**   | DIN-rail I/O module with **5× DI**, **2× relays**, and **1-Wire** (DS18B20, etc.).              |
|            | **HomeMaster Controller** | MiniPLC/MicroPLC acting as **Modbus master** and network gateway; ESPHome uses `wld_address: "3"`. |
|            | **Power Supply**          | **24 VDC** to module power terminals (on-board 5 V/3.3 V rails derived).                        |
|            | **RS-485 Cabling**        | Two-wire **A/B** plus **COM/GND**; use **120 Ω** termination at bus ends.                       |
|            | **USB-C Cable**           | Connects the module to your computer to run WebConfig via Web Serial.                            |
| **Software** | **WebConfig Tool**       | Browser page to set **Modbus Address & Baud**, view **Active Modbus Configuration**, reset, and configure I/O/irrigation. |
|            | **ESPHome YAML**          | Controller config declaring the WLD on RS-485 (`modbus_controller:` + entities).                |
| **I/O**     | **Sensors / Actuators**   | Leak/moisture/rain probes & **pulse flow meters** on DIs; **DS18B20** on **1-Wire (GPIO16)**; valves/pumps on **R1/R2**. |
---

## 4.2 Powering the Devices

The WLD-521-R1 is powered from a **24 VDC primary input** on the field board. On-board regulators generate the internal rails for logic and provide **isolated 5 V / 12 V auxiliary rails** intended for low-power sensors.

> Relays are **dry contacts** (SPDT). Do **not** power valves/pumps from the module’s internal rails; use a dedicated external supply and switch it via the relay contacts.

---

### 4.2.1 Power Supply Types

- **Regulated 24 VDC DIN-rail PSU:** Connect to the module’s **+V / 0V** power terminals. Size the PSU for the module plus any externally powered devices.
- **No power over RS-485:** The RS-485 bus carries signals only. Always provide local 24 VDC power to the module.
- **Sensor power from module:** Use the **isolated** rails — **+5 V_ISO** and **+12 V_ISO** — **together with GND_ISO (DI ground)** to power **low-power field sensors** (leak probes, flow-meter heads) connected to the DI terminals.  
  **Note:** The **1-Wire bus uses the module’s non-isolated +5 V (logic domain)**. Do **not** power DI sensors from the 1-Wire **+5 V/GND**, and do **not** tie the 1-Wire ground to the DI ground. Keep the **isolated (DI)** and **logic (1-Wire)** domains separate to preserve isolation and avoid noise/ground loops.


---


### 4.2.2 Current Consumption

Actual current depends on configuration and what’s attached. Budget for:
- **Base electronics (logic/MCU/LEDs).**
- **Relays:** add coil current for each energized relay.
- **Sensor rails:** total draw of any devices on the isolated **+5 V / +12 V** rails.

**Sizing tip:** choose a 24 V supply that covers base load + worst-case **both relays ON** + all sensor current, with at least **30% headroom** for startup and temperature.

---

### 4.2.3 Power Safety Tips

- **Polarity & grounds:** Observe **24 V polarity** and keep logic ground and isolated sensor ground separate as designed.
- **Fusing & protection:** Keep upstream over-current protection (fuse/breaker). Do not bypass on-board protective elements.
- **Relay contact ratings:** Treat relay outputs as isolated contacts; follow the contact rating from the relay datasheet and local electrical codes.
- **Use sensor rails only for sensors:** Do **not** power valves, pumps, or sirens from the module’s **+5 V / +12 V** sensor rails.
- **De-energize before wiring:** Power down the 24 V supply before changing wiring. Double-check for shorts before re-energizing.


---

## 4.4 Quick Setup

**Phase 1 — Physical Wiring**

- **Power:** connect **24 VDC** to **V+ / 0 V** on the module.
- **RS-485:** wire **A → A**, **B → B**, and **COM/GND** between the WLD-521-R1 and the controller; keep polarity consistent. Terminate the two bus ends with **120 Ω**.
- **I/O:**
  - **Relays:** wire your valves/pumps to **R1/R2 (NO/NC/COM)**.
  - **Digital inputs:** connect leak sensors / flow meters / buttons to **DI1…DI5**.
  - **1-Wire:** connect **DS18B20** sensors to **1WIRE_5V / 1WIRE_DATA / 1WIRE_GND** (GPIO16).

_For diagrams, terminal pinouts, cable gauges, and safety notes, see **[Section 5 — Installation & Wiring]**._


**Phase 2 — Module Configuration (WebConfig)**

- **Connect:** plug **USB-C** into the module; open [ConfigToolPage.html](https://www.home-master.eu/configtool-wld-521-r1) in a Chromium browser and click **Connect**.
- **Set Modbus:** choose a unique **Modbus Address (1–255)** and **Baud** (default list includes **19200**). The header shows the **Active Modbus Configuration**.
- **Configure I/O:**  
  - **Digital Inputs:** set role per DI (e.g., flow meter vs. leak sensor); counters show **Rate (L/min)** / **Total (L)**, with **Pulses-per-liter** and **Rate×/Total×** calibration, plus **Reset** tools.
  - **Irrigation (2 zones, optional):** map **Valve relay** and **Flow DI**; set **Min rate**, **Grace**, **Timeout**, **Target liters**; add interlocks (**DI_moist / DI_rain / DI_tank / R_pump**) and an **Irrigation Window** (Start/End, Enforce, Auto-start).
  - **1-Wire:** scan, store, and name sensors for temperature/heat features.
- (Optional) **Reset Device** from the dialog if you need to restart; the serial link will reconnect.

_For more details about WebConfig cards and fields, see **[Software & UI Configuration](#software--ui-configuration)**._

**Phase 3 — Controller Integration (ESPHome)**

- **Update YAML:** In the ESPHome YAML configuration file for your MiniPLC/MicroPLC (using the provided template, **`default_wld_521_r1_plc.yaml`**):  
  - Verify the **`uart`** settings match your controller’s **RS-485 pins**.  
  - Add a new **`modbus_controller:`** entry, ensuring the **`wld_address`** substitution matches the Modbus Address set in Step 3 of Phase 2 (e.g., `wld_address: "3"`).
- **Compile & Upload:** Build and upload the updated ESPHome config to the controller. After reboot, the controller will poll the WLD-521-R1 and expose **DI states/counters**, **1-Wire temperatures**, **relay controls**, and **irrigation status** as HA entities.

_For protocol details and end-to-end examples, see **[Modbus RTU Communication](#modbus-rtu-communication)** and **[ESPHome Integration Guide (MicroPLC/MiniPLC + WLD-521-R1)](#esphome-integration-guide-microplcminiplc--wld-521-r1)**._
  

**Timekeeping (recommended for local schedules)**  
If you use the module’s **Irrigation Window** or daily counters, schedule a Home Assistant automation at **00:00** to pulse the **time-sync coil** and keep the module’s minute/day registers aligned with HA. (See the **Module Time & Modbus Sync** area in the UI and your controller automations.)

**Verify**  
Use WebConfig’s **Serial Log** and live status panels to confirm DI changes, flow **rate/total**, relay actions, and irrigation state.

## Key Ratings (from prior release)

| Parameter | Typical / Max |
|---|---|
| Supply voltage | **24 VDC** |
| DI trigger level / current | ≈ **14 V** / ≈ **6 mA** |
| DI frequency | **~9 Hz** default (higher configurable) |
| Relay contacts | **3 A @ 250 VAC** (per relay) |
| Sensor power | **+5 V / +12 V** isolated, up to **~50 mA** combined budget (guideline) |
| Firmware update | **USB-C** (Web Serial / DFU) |
| Mounting | **DIN-rail**, ~3-module width | 

---

### Notes

- Actual enclosure dimensions and environmental limits (temperature, humidity, IP rating) should be confirmed with the latest mechanical drawing—values are not specified in the current schematics.  
- For multi-module RS-485 networks, assign unique Modbus addresses and observe proper termination and biasing per segment.





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
