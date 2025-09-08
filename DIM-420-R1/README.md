# 📘 DIM-420-R1 — Dual-Channel AC Dimmer Module (MOSFET-Based)

The **DIM-420-R1** is a **two-channel, phase-cut AC dimmer** for the **HomeMaster MicroPLC / MiniPLC** ecosystem.  
It provides **2 dimming channels**, **4 digital inputs** (momentary or latching with rich press logic), **4 user buttons**, **4 LEDs**, and an **RS-485 Modbus RTU** interface. An in-browser **Web Config Tool (Web Serial over USB)** enables quick setup, logging, and live diagnostics.

---

## 📎 Useful Links

- **PLC Integration (Full ESPHome YAML)**  
  `https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIM-420-R1/Firmware/default_dim_420_r1_plc/default_dim_420_r1_plc.yaml`
- **Web Config Tool**  
  `DIM-420-R1/Firmware/ConfigToolPage.html` *(open in a Chromium-based browser → connect via Web Serial)*
- **Firmware (Arduino / RP2350A)**  
  `DIM-420-R1/Firmware/default_DIM_420_R1/default_DIM_420_R1.ino`  
  Build outputs: `DIM-420-R1/Firmware/build/`
- **(Add)** Hardware Schematics  
  *(Add links to Field Board & MCU Board PDFs when published)*

---

## Table of Contents

1. [Purpose](#1-purpose)  
2. [Technical Specifications](#2-technical-specifications)  
3. [General Operation Principle](#3-general-operation-principle)  
3.1 [Indication](#31-indication)  
3.2 [Safety](#32-safety)  
4. [Installation](#4-installation)  
4.1 [Installation Example](#41-installation-example)  
4.2 [Working with LED Lamps](#42-working-with-led-lamps)  
5. [Representation in the Controller Web Interface](#5-representation-in-the-controller-web-interface)  
5.1 [Template Selection](#51-template-selection)  
5.2 [Device Control and Value Monitoring](#52-device-control-and-value-monitoring)  
6. [Configuration](#6-configuration)  
6.1 [Configuration Methods](#61-configuration-methods)  
6.2 [Input Control Modes](#62-input-control-modes)  
6.2.1 [Pushbutton (Momentary)](#621-pushbutton-momentary)  
6.2.2 [Latching (Maintained) Switch](#622-latching-maintained-switch)  
6.3 [Load Type](#63-load-type)  
6.4 [Dimming Front (Cut Mode)](#64-dimming-front-cut-mode)  
6.5 [Lower Dimming Threshold](#65-lower-dimming-threshold)  
6.6 [Upper Dimming Threshold](#66-upper-dimming-threshold)  
6.7 [Dimming Smoothness](#67-dimming-smoothness)  
6.8 [Key Mode](#68-key-mode)  
6.9 [Safe Mode and Power-On Behavior](#69-safe-mode-and-power-on-behavior)  
7. [Working via Modbus](#7-working-via-modbus)  
7.1 [Default Port Parameters](#71-default-port-parameters)  
7.2 [Modbus Address](#72-modbus-address)  
7.3 [“Fast Modbus” Extension](#73-fast-modbus-extension)  
7.4 [Register Map](#74-register-map)  
7.5 [Channel and Brightness Control](#75-channel-and-brightness-control)  
8. [Firmware Update & Reset](#8-firmware-update--reset)  
9. [Known Issues](#9-known-issues)  
10. [Device Revisions](#10-device-revisions)  
11. [Images & Drawings](#11-images--drawings)

---

## 1. Purpose

**DIM-420-R1** controls dimmable AC loads with per-channel **Leading/Trailing edge** selection, **Lower/Upper thresholds**, and **press logic** designed for wall switches (short / long / double / short-then-long). The module integrates with MicroPLC / MiniPLC via **RS-485 Modbus RTU** and offers an **interactive Web Config Tool** for commissioning and diagnostics.

---

## 2. Technical Specifications

- **Dimming channels:** 2 (CH1, CH2), phase-cut per channel  
- **Digital inputs:** 4 (DI1–DI4), *Momentary* or *Latching*; press types: **Short**, **Long**, **Double**, **Short-then-Long**  
- **User buttons / LEDs:** 4 / 4 (LED mode: Steady / Blink; sources: CH / AC / DI)  
- **Mains sync:** dual zero-cross inputs; per-channel **mains frequency** reported as **Freq_x100** (e.g., 5000 = 50.00 Hz)  
- **Interface:** RS-485 Modbus RTU, 8N1  
- **Default comms:** Slave ID **3**, Baud **19200** (editable via Web Config)  
- **MCU:** RP2350A (Arduino / PlatformIO supported)  
- **Persistence:** LittleFS autosave (~1.5 s after last change)  

> *(Add power ratings, terminal specs, environmental limits, enclosure & mounting details.)*

---

## 3. General Operation Principle

The firmware:
- Samples **DI1–DI4** and resolves the selected **switch mode** and **press logic**  
- Monitors per-channel **zero-cross**, computes **Freq_x100**, and schedules MOSFET gating with safety blanking/guards  
- Mirrors live state to **Modbus** (discrete inputs & holding registers); consumes **coils** and **holding writes** for control  
- Streams a **live JSON snapshot + event log** via Web Serial once per second

### 3.1 Indication

- **LED1–LED4:** configurable **mode** (steady/blink) and **source** (CH1/CH2 ON, AC presence per channel, DI1–DI4)  
- **AC presence (ZC OK):** per-channel health indicator exposed via discrete inputs (see §7.4)

### 3.2 Safety

- Install by **qualified personnel**; comply with local electrical codes.  
- **Disconnect mains** before wiring/servicing.  
- Select the correct **Cut Mode** (Leading/Trailing) to match the load.  
- Provide proper **overcurrent protection**, cabinet rating, airflow, and EMC practices.  
- Ensure loads are within the module’s electrical and thermal limits.

---

## 4. Installation

1. **Power & mains:** wire dimmer outputs to loads; match load type and cut mode.  
2. **DI wall switches:** wire to DI1–DI4; choose *Momentary* or *Latching* in config.  
3. **RS-485 bus:** connect A/B; ensure termination and biasing.  
4. **USB-C:** for Web Serial configuration and UF2 updates.

> **Quick start:** USB-C → open `ConfigToolPage.html` → connect → set **Modbus ID/baud** → tune **Lower/Upper**, **Load Type**, **Cut Mode**, **Preset** → map **DI / Buttons / LEDs** → save.

### 4.1 Installation Example

*(Add a typical wiring sketch: mains input, two dimmed outputs, DI wall switches, RS-485 bus.)*

### 4.2 Working with LED Lamps

- Many LED drivers prefer **Trailing edge**; some require **Leading**.  
- Select per the **lamp/driver datasheet** to avoid flicker or stress.  
- If flicker persists, tune **Lower/Upper thresholds** per §§6.5–6.6.

---

## 5. Representation in the Controller Web Interface

Expose CH controls, DI states, ZC OK, and configuration parameters in your controller UI (MiniPLC / MicroPLC → ESPHome → Home Assistant).  
*(Add screenshots and controller-specific notes if needed.)*

### 5.1 Template Selection

Use the **full PLC YAML** (kept as a link only):  
`https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIM-420-R1/Firmware/default_dim_420_r1_plc/default_dim_420_r1_plc.yaml`

### 5.2 Device Control and Value Monitoring

Display **Level**, **Percent×10**, **Lower/Upper**, **Load Type**, **Cut Mode**, **Preset**, and binary indicators for **CH ON**, **ZC OK**, **LEDs**, **DIs**.  
To avoid dashboard spam, publish **only on change** (template number + hidden delta sensor pattern in ESPHome).

---

## 6. Configuration

### 6.1 Configuration Methods

- **Web Config Tool (Web Serial):**  
  edit Modbus address/baud; per-channel **Lower/Upper**, **Load Type**, **Cut Mode**, **Preset**, **Percent×10**; DI / Buttons / LEDs; view logs & live snapshot.  
- **Modbus via PLC:**  
  write holding registers / coils per §7.4.

**Example-only ESPHome snippet** *(keep your project scaffolding; use the full YAML link above)*:

```yaml
uart:
  tx_pin: 17
  rx_pin: 16
  baud_rate: 19200

modbus:
  id: bus

modbus_controller:
  - id: dim420
    address: 3
    modbus_id: bus
    update_interval: 1s

binary_sensor:
  - platform: modbus_controller
    modbus_controller_id: dim420
    name: "DIM CH1 On"
    address: 50
    register_type: discrete_input

switch:
  - platform: modbus_controller
    id: dim_ch1_on
    modbus_controller_id: dim420
    name: "DIM CH1 ON"
    address: 200
    register_type: coil
    on_turn_on:
      - delay: 200ms
      - switch.turn_off: dim_ch1_on

number:
  - platform: modbus_controller
    modbus_controller_id: dim420
    name: "DIM CH1 Level"
    address: 400
    register_type: holding
    value_type: U_WORD
    min_value: 0
    max_value: 255
    step: 1
```
*(Use the full YAML link for a complete integration.)*

### 6.2 Input Control Modes

Two DI control modes per input.

#### 6.2.1 Pushbutton (Momentary)

Each DI recognizes **Short**, **Long**, **Double**, **Short-then-Long** presses.  
Every event can run an **Action** on a **Target**: CH1, CH2, or ALL.

**Actions:**  
- Turn **ON** (to **Preset**)  
- Turn **OFF**  
- **Toggle** (0 ↔ last non-zero)  
- **Increase** level  
- **Decrease** level  
- **Increase/Decrease** (ping-pong) — Short = one step; Long = continuous ramp (~1 Hz)  
- **Go Max** (jump to **Upper** threshold)

> Some event/action combinations have no practical meaning and are ignored by firmware.

#### 6.2.2 Latching (Maintained) Switch

- **Toggle Preset/0** on each edge, or  
- **Continuous ping-pong** (1 Hz steps) until the next edge.

### 6.3 Load Type

Mapping from **Percent×10** to **Level (0..255)** per channel:
- **Lamp:** logarithmic curve (smooth visual steps)  
- **Heater:** linear power mapping  
- **Key:** non-dimmable; 0 = OFF, >0 = **Upper**.

### 6.4 Dimming Front (Cut Mode)

Per channel: **Leading** (RL) or **Trailing** (RC).  
Choose per load datasheet to avoid flicker/overstress. Default is **Leading**.

### 6.5 Lower Dimming Threshold

Set **Lower** so the lamp reliably lights at low levels (default ≈ **20**):

1. Set **Level = 0**, then slowly increase until the lamp first lights.  
2. Record that value as **Lower**.

### 6.6 Upper Dimming Threshold

Set **Upper** to the highest level that still changes brightness (default **255**):

1. Set **100%**, then reduce until brightness begins to fall.  
2. Record that value as **Upper** (ensure `Lower ≤ Upper`).

### 6.7 Dimming Smoothness

*(Not exposed as a parameter in current firmware; add here if/when implemented.)*

### 6.8 Key Mode

With **LoadType = Key**, the channel behaves as a non-dimmable output: **0 = OFF**, **>0 = ON** at **Upper**.

### 6.9 Safe Mode and Power-On Behavior

*(Document power-on defaults, brown-out behavior, and failsafes when finalized.)*

---

## 7. Working via Modbus

### 7.1 Default Port Parameters

- **Slave ID:** `3`  
- **Baud / Framing:** `19200, 8N1`  
- **Stack:** Modbus RTU over RS-485 (slave)

### 7.2 Modbus Address

Changeable via Web Config; persisted to flash (autosave ~1.5 s after last change).

### 7.3 “Fast Modbus” Extension

*(Not applicable.)*

### 7.4 Register Map

**Function codes:** FC02 (Discrete Inputs, RO), FC01/05 (Coils, momentary), FC03/06/16 (Holding Registers).  
Per-channel pairs use **CH1/CH2 = +0 / +1**.

#### Discrete Inputs — FC02 (read-only)

| Addr | Name           | Description                                   |
|----:|----------------|-----------------------------------------------|
| 1–4 | DI1..DI4       | DI states (after inversion)                   |
| 50–51 | CH1/CH2 ON   | Channel enabled **and** Level > 0             |
| 90–93 | LED1..LED4   | Physical LED states                            |
| 120–121 | ZC1/ZC2 OK | Zero-cross (AC presence) per channel          |

#### Coils — FC01/05 (write `1` to trigger; auto-clear)

| Addr | Action               |
|----:|----------------------|
| 200  | CH1 ON (to Preset)  |
| 201  | CH2 ON (to Preset)  |
| 210  | CH1 OFF             |
| 211  | CH2 OFF             |
| 300–303 | DI1..DI4 ENABLE  |
| 320–323 | DI1..DI4 DISABLE |

#### Holding Registers — FC03/06/16 (R/W unless noted)

| Addr (CH1/CH2) | Name       | Range / Units | Notes                                   |
|---------------:|------------|---------------|-----------------------------------------|
| 400 / 401      | Level      | 0..255        | Output level (0 = OFF; clamped)         |
| 410 / 411      | Lower      | 0..255        | Min useful level                        |
| 420 / 421      | Upper      | 0..255        | Max useful level                        |
| 430 / 431      | Freq_x100  | Hz×100        | **RO** per channel                      |
| 440 / 441      | Percent×10 | 0..1000       | Target %×10; maps to Level by LoadType  |
| 460 / 461      | LoadType   | 0..2          | 0=Lamp, 1=Heater, 2=Key                 |
| 470 / 471      | CutMode    | 0..1          | 0=Leading, 1=Trailing                   |
| 480 / 481      | Preset     | 0..255        | Used by CH ON coils                     |

**Behavior notes**

- Writing **Percent×10** recalculates and updates **Level** immediately (per LoadType + thresholds).  
- Changing **Lower/Upper** may clamp current **Level** and **Preset**.  
- **Coils** are **momentary**: write `1` to trigger; firmware auto-clears back to `0`.  
- Config changes mark “dirty” and auto-save ~1.5 s after the last change.

### 7.5 Channel and Brightness Control

- **Turn CH1 ON to Preset:** coil **200** ← `1`.  
- **Turn CH2 OFF:** coil **211** ← `1`.  
- **Set CH1 to 75% linear:** write **LoadType(460)=1**, then **Percent×10(440)=750**.  
- **Clamp CH2 to 30–220 and set Level 180:** **411=30**, **421=220**, **401=180**.

---

## 8. Firmware Update & Reset

- **Web Serial commands:** `save`, `load`, `factory` (restore defaults).  
- **Update method:** USB-C **UF2 drag-and-drop** (RP2350A bootloader).  
- **Build / Flash:** Arduino IDE / PlatformIO; libraries: `ModbusSerial`, `SimpleWebSerial`, `Arduino_JSON`, `LittleFS`.  
*(Add any button/boot-select combos if used.)*

---

## 9. Known Issues

*(Add current limitations or behaviors here.)*

---

## 10. Device Revisions

*(Track hardware/firmware revisions here.)*

---

## 11. Images & Drawings

*(Replace placeholders with actual images for consistency across modules.)*

- Field Board: `Images/FieldBoard-Diagram.png`  
- MCU Board: `Images/MCUBoard-Diagram.png`  
- MCU Pinout: `Images/DIM_MCU_Pinouts.png`  
- System Block: `Images/SystemBlockDiagram.png`  

---

### ⚠️ Safety (ALM-style)

- Install by **qualified personnel**; follow local electrical standards.  
- **Disconnect mains** before wiring/servicing.  
- Choose **correct cut mode** (Leading/Trailing) and **match load type**; wrong choice may damage lamps or the dimmer.  
- Provide proper **overcurrent protection**, cabinet rating, airflow, and EMC practices.

---

### 🧰 Mini Quick-Start (ALM-style)

1. Power the module → LEDs blink on startup.  
2. USB-C → open `ConfigToolPage.html` in a Chromium browser → **Connect (Web Serial)**.  
3. Configure: **Modbus ID/baud**, **Lower/Upper**, **Load Type**, **Cut Mode**, **Preset**, DI/Buttons/LEDs.  
4. Wire RS-485 A/B → import the **full PLC YAML** (link above) → expose controls in your dashboard.

---

### 📄 License / Openness

- **Hardware:** CERN-OHL-W 2.0  
- **Firmware:** GPLv3  
- Reprogrammable with **Arduino (C++)**, **PlatformIO**, or **Pico SDK**.
