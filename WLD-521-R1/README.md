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
- [2.1 General Electrical Safety](#21-general-electrical-safety)  
- [2.2 Handling & Installation](#22-handling--installation)  
- [2.3 Device-Specific Warnings](#23-device-specific-warnings)  

### 3. [System Overview](#3-system-overview)
- [3.1 Architecture & Modular Design](#31-architecture--modular-design)  
- [3.2 MicroPLC vs MiniPLC](#32-microplc-vs-miniplc)  
- [3.3 Integration with Home Assistant](#33-integration-with-home-assistant)  
- [3.4 Diagrams & Pinouts](#34-diagrams--pinouts)  
- [3.5 Technical Specifications](#35-technical-specifications-module-internals)  

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
### 1. [Introduction]
## 1.1 Overview of the WLD-521-R1 Module 💧

The WLD-521-R1 is a highly specialized Industrial I/O module designed primarily for **Water Flow, Heat Energy (Calorimetry), and Irrigation/Leak Detection** applications. It functions as an intelligent Modbus slave device that processes local sensor data and executes commands from a master controller.

| Component | Quantity | Key Functionality |
| :--- | :--- | :--- |
| **Digital Inputs (DI)** | 5 (DI1-DI5) | **Opto-isolated** inputs used for pulse counting (Flow Meters) or reading dry-contact status (Leak Sensors, Buttons, General Status). |
| **Relays (RLY)** | 2 (R1, R2) | **High-performance industrial relays** for switching external loads, most commonly used for **pumps** or **shut-off valves**. |
| **1-Wire Bus (OW)** | 1 | Dedicated bus supporting up to 32 **DS18B20 temperature sensors** for accurate temperature monitoring and heat energy calculation. |
| **Local Interface** | 4 LEDs, 4 Buttons | Provides local status indication, manual control, and **override functions**. |

---

## 1.2 Supported Controllers 🔌

The WLD-521-R1 is engineered as an **intelligent expansion module** for the **HomeMaster MicroPLC/MiniPLC Modular Smart Control System**. 
### MiniPLC / MicroPLC (via Modbus RTU)

| Feature | Specification |
| :--- | :--- |
| **Controller Role** | Operates as a **Modbus Slave**. The MiniPLC/MicroPLC acts as the **Modbus Master** for network and system logic management. |
| **Communication** | **Modbus RTU (Serial)** protocol. |
| **Physical Interface** | **RS-485 bus** (using dedicated UART2 pins). |
| **Function** | Enables the Master to **read all I/O data** (Flow, Heat, Leak Status) and **write commands** to actuate the two Relays (R1, R2) and control Irrigation zones. |
| **Modular Design** | The RS-485 architecture facilitates the **daisy-chaining of multiple WLD-521-R1 modules** and other expansion units to the central PLC, allowing for scalable I/O. |
| **Default ID** | Modbus Slave ID is factory-set to **3**. |

---

## 1.3 Use Cases 🛠️

### A. Water Flow & Leak Detection
* **Flow Metering:** Configures DIs as **pulse counters** (`IT_WFLOW`) to track cumulative volume and calculate instantaneous **flow rates** (`flowRateLmin`).
* **Leak Detection & Shut-off:** DIs can be configured for water sensing (`IT_WATER`). Upon detecting a leak, the module's logic can be configured to automatically trigger an **action** on a target relay (R1 or R2) to **close a main shut-off valve**.
* **General Sensing:** DIs can be used for general purpose counting (`IT_WCOUNTER`) or humidity sensing (`IT_HUMIDITY`).

### B. Heat Energy Monitoring (Calorimetry)
* The module performs specialized calculations when an input is set to **Heat Energy Mode** (`IT_WHEAT`).
* Requires a **Flow Meter** and two dedicated **1-Wire temperature sensors** (T_A and T_B) to measure the temperature differential ($\Delta$T).
* The module autonomously calculates and reports **heat power** (`heatPowerW`) and **accumulated heat energy** (`heatEnergyJ`).

### C. Irrigation Control
* The module provides native, dedicated logic for managing **two independent Irrigation Zones** (Zone 1 and Zone 2).
* Relays are mapped to the physical irrigation valves.
* The system allows for scheduling (not fully detailed in code snippets but inferred from time logic) and manual/remote commands to control **Start**, **Stop**, and **Reset** of the zones.
* Local buttons can be mapped to trigger specific irrigation actions (e.g., `BTN_IRR1_START`).


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
