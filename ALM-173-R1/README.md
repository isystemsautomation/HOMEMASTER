# 📘 ALM-173-R1 – Alarm Input & Relay Output Module  

The **ALM-173-R1** is a alarm expansion module for the **HomeMaster MicroPLC** and **MiniPLC** ecosystem.  
It integrates **17 digital alarm inputs**, **3 relay outputs**, configurable **buttons**, and **status LEDs** into a compact DIN-rail form factor.  

Designed for **security, automation, and smart-building systems**, it connects via **RS-485 Modbus RTU** and is managed by an intuitive **Web Config Tool**.  

---

## 📎 Useful Links
- 🌐 **Web Config Tool (online):** [configtool-alm-173-r1](https://www.home-master.eu/configtool-alm-173-r1)  
- 💾 **Firmware & Source:**  
  - [Arduino Sketch (default_alm_173_r1.ino)](https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Firmware/default_alm_173_r1/default_alm_173_r1.ino)  
  - [Precompiled UF2/BIN](https://github.com/isystemsautomation/HOMEMASTER/tree/main/ALM-173-R1/Firmware/default_alm_173_r1/build/rp2040.rp2040.generic_rp2350)  
  - [ConfigToolPage.html](https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Firmware/ConfigToolPage.html) – Web Tool source code  
- 📐 **Hardware Schematics:**  
  - [Field Board PDF](https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Schematics/ALM-173-R1-FieldBoard.pdf)  
  - [MCU Board PDF](https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Schematics/ALM-173-R1-MCUBoard.pdf)  

---

## 📑 Table of Contents
1. [Key Features](#-key-features)  
2. [Technical Specifications](#-technical-specifications)  
3. [Quick Start Guide](#-quick-start-guide)  
4. [Module Diagrams](#-module-diagrams)  
5. [Web Config Tool](#-web-config-tool)  
6. [Firmware & Module Guide](#-firmware--module-guide)  
   - [Overview](#overview)  
   - [Digital Inputs](#digital-inputs)  
   - [Alarm Groups](#alarm-groups)  
   - [Relays](#relays)  
   - [Buttons](#buttons)  
   - [User LEDs](#user-leds)  
   - [Save/Load/Factory/Reset](#saveloadfactoryreset)  
   - [Alarm Logic](#alarm-logic)  
   - [Modbus RTU Mapping](#modbus-rtu-mapping)  
   - [Button Combinations](#-button-combinations)  
   - [Quick Recipes](#quick-recipes)  
   - [Troubleshooting](#troubleshooting)  
   - [Build & Flash Notes](#build--flash-notes)  
7. [Open Source & Re-Programming](#-open-source--re-programming)  
8. [License](#-license)  

---

## ⚙️ Key Features
- **17 Opto-isolated Digital Inputs** (5 V logic) – connect PIR, door/window, tamper, panic, or dry-contact sensors  
- **3 SPDT Relays (NO/NC)** – drive sirens, strobes, locks, beacons  
- **Auxiliary Power Rails** – isolated 12 V and 5 V outputs for sensors  
- **4 User Buttons** – configurable actions (acknowledge, relay override)  
- **4 User LEDs** – steady/blink status, configurable source  
- **USB-C Port** – firmware updates & Web Serial configuration  
- **RS-485 Modbus RTU** – address 1–255, baud 9600–115200  
- **RP2350A MCU** – Arduino IDE, PlatformIO, MicroPython compatible  
- **Persistent Storage** – settings saved to internal flash  

---

## 🧰 Technical Specifications

| Parameter               | Value                            |
|--------------------------|----------------------------------|
| Digital Inputs           | 17 opto-isolated (5 V logic)     |
| Relay Outputs            | 3 × SPDT (NO/NC), isolated       |
| Aux Power                | 12 V & 5 V isolated rails        |
| Microcontroller          | RP2350A (RP2040 core)            |
| Communication            | RS-485 Modbus RTU slave          |
| Programming Interface    | USB Type-C                       |
| Mounting                 | DIN rail / surface               |
| Power Supply             | 24 V DC                          |

---

## 🚀 Quick Start Guide

### 1. Power Up  
- Supply **24 V DC** to the module.  
- Status LEDs blink briefly on startup.  

### 2. Connect via USB-C  
- Plug into your PC.  
- Open [Web Config Tool](https://www.home-master.eu/configtool-alm-173-r1) in Chrome/Edge/Firefox.  
- Click **Connect** → select the COM port of ALM-173-R1 device.  

### 3. Configure  
- Set Modbus address & baud rate.  
- Assign inputs to **Alarm Groups**.  
- Set relay behavior and LED indicators.  
- Configure button actions (acknowledge alarms, override relays).  

### 4. Integrate  
- Wire **RS-485 A/B** into your MicroPLC/MiniPLC network.  
- Integrate via MiniPLC/MicroPLC ESPHome → Home Assistant.  

---

## 📐 Module Diagrams  

### Field Board  
![Field Board](Images/FieldBoard-Diagram.png)  

### MCU Board  
![MCU Board](Images/MCUBoard-Diagram.png)  

### MCU Pinout (RP2350A)  
![MCU Pinout](Images/ALM_MCU_Pinouts.png)  

### System Block Diagram  
![System Block](Images/SystemBlockDiagram.png)  

---

## 🌐 Web Config Tool  

The **Web Config Tool** provides browser-based configuration via Web Serial.  

**Features:**  
- Change Modbus **address** and **baud rate**  
- Configure **17 inputs** (enable, invert, group assignment)  
- Set **Alarm Group modes** (None / Non-latched / Latched)  
- Map **Relays** to groups or override  
- Configure **Buttons** (Ack alarms / Relay override)  
- Configure **LEDs** (steady or blink, sources: Any Alarm / Groups / Relay overrides)  
- Live status indicators for inputs, relays, buttons, LEDs  
- Reset device safely with confirmation  

📎 [Open Web Config Tool](https://www.home-master.eu/configtool-alm-173-r1)  

---

# 🔍 Firmware & Module Guide  

## Overview
The ALM-173-R1 firmware continuously:  
- Reads **17 inputs**  
- Evaluates **3 alarm groups**  
- Drives **3 relays** and **4 LEDs**  
- Processes **button actions** (acknowledge / override)  
- Exposes all states and controls via **RS-485 Modbus**  

---

## Digital Inputs
- **IN1–IN17** are 5 V opto-isolated channels  
- Config per input: **Enable, Invert, Group**  
- Assigned group determines alarm contribution  

---

## Alarm Groups
- Each input can belong to **Group 1, 2, or 3**  
- Group modes:  
  - **None** → group ignored  
  - **Non-latched** → active only while input(s) active  
  - **Latched** → stays active until **acknowledged**  

---

## Relays
- **R1–R3 SPDT relays (16 A)**  
- Config per relay: **Enable, Invert, Group follow**  
- May be **overridden manually** via buttons  

---

## Buttons
- **4 buttons, configurable actions**:  
  - None  
  - Ack All  
  - Ack Group 1/2/3  
  - Relay 1/2/3 Override  

---

## User LEDs
- **4 configurable LEDs**  
- Modes: **Steady / Blink**  
- Sources: **Any Alarm, Group 1–3, Relay override**  

---

## Save/Load/Factory/Reset
- Config auto-saves after idle period  
- Actions:  
  - **Save** — persist immediately  
  - **Load** — reload last saved  
  - **Factory** — restore defaults  
  - **Reset** — reboot module safely  

---

## 🔔 Alarm Logic  

The ALM-173-R1 continuously evaluates inputs, groups, relays, LEDs, and button actions in a deterministic cycle.  

### Step-by-step process:  

1. **Read Raw Inputs**  
   - All 17 inputs are sampled.  

2. **Process Each Input**  
   - If **Disabled** → ignored.  
   - If **Enabled** → apply inversion setting.  
   - Result = **Processed State** (true/false).  

3. **Evaluate Groups (G1–G3)**  
   - Each input can belong to **Group 1, Group 2, Group 3, or None**.  
   - For each group:  
     - **None** → group always inactive.  
     - **Non-latched** → group is active **only while any assigned input is active**.  
     - **Latched** → group becomes active when any assigned input goes active and **stays active until acknowledged** (even if the input clears).  

   - **Any Alarm** = OR of all active groups.  

4. **Relays (R1–R3)**  
   - Each relay can follow the state of a selected group.  
   - If assigned group is active → relay turns ON.  
   - If **manual override** (via button or Modbus) is active → relay follows the override instead of the group.  
   - **Inverted** setting flips ON/OFF logic.  

5. **LEDs (L1–L4)**  
   - Each LED has a **mode** (Steady or Blink).  
   - Each LED has a **source** (Any Alarm, Group, or Relay override).  
   - LED turns on (steady or blinking) when its source is active.  

6. **Acknowledgements (via Button or Modbus)**  
   - In **Latched** mode, alarms remain active until acknowledged.  
   - **Ack All** → clears all groups.  
   - **Ack G1/G2/G3** → clears only the selected group.  

7. **Cycle Repeats**  
   - The loop runs continuously, updating states in real time.  

---

## 🔌 Modbus RTU Mapping

**Bus defaults**  
- **Slave ID (address):** `3`  
- **Baud / framing:** `19200, 8N1`  
- **Stack:** Modbus RTU over RS-485 (slave)  

> These defaults match the example PLC config (ESPHome) you’ll find in this repo.

---

### Function Codes Used
- **FC=02 – Discrete Inputs** (read-only bits; telemetry/state)
- **FC=05 / FC=15 – Coils** (write-only *pulse* commands; auto-clear)

> Coils behave like **momentary buttons**: write `1`, the module performs the action, and the coil **self-resets** to `0`.{index=1}

---

### Discrete Inputs (FC=02)

Read these to get live state from the ALM module. All addresses are **1-based** as shown.

| Addr | Name                | Description                                                                 |
|----:|---------------------|-----------------------------------------------------------------------------|
| 1   | IN1                 | Input 1 (after **Enable** + **Invert** processing)                          |
| 2   | IN2                 | Input 2 (processed)                                                          |
| 3   | IN3                 | Input 3 (processed)                                                          |
| 4   | IN4                 | Input 4 (processed)                                                          |
| 5   | IN5                 | Input 5 (processed)                                                          |
| 6   | IN6                 | Input 6 (processed)                                                          |
| 7   | IN7                 | Input 7 (processed)                                                          |
| 8   | IN8                 | Input 8 (processed)                                                          |
| 9   | IN9                 | Input 9 (processed)                                                          |
| 10  | IN10                | Input 10 (processed)                                                         |
| 11  | IN11                | Input 11 (processed)                                                         |
| 12  | IN12                | Input 12 (processed)                                                         |
| 13  | IN13                | Input 13 (processed)                                                         |
| 14  | IN14                | Input 14 (processed)                                                         |
| 15  | IN15                | Input 15 (processed)                                                         |
| 16  | IN16                | Input 16 (processed)                                                         |
| 17  | IN17                | Input 17 (processed)                                                         |
| 50  | ANY_ALARM           | True if Group 1 **or** 2 **or** 3 is active                                  |
| 51  | ALARM_G1            | Alarm **Group 1** active (non-latched or latched, per config)               |
| 52  | ALARM_G2            | Alarm **Group 2** active                                                     |
| 53  | ALARM_G3            | Alarm **Group 3** active                                                     |
| 60  | RELAY1_STATE        | **Effective** output of Relay 1 (after enable/invert/override resolution)   |
| 61  | RELAY2_STATE        | Effective output of Relay 2                                                  |
| 62  | RELAY3_STATE        | Effective output of Relay 3                                                  |
| 90  | LED1_STATE          | Physical LED1 line (reflects configured LED source/mode)                    |
| 91  | LED2_STATE          | Physical LED2 line                                                           |
| 92  | LED3_STATE          | Physical LED3 line                                                           |
| 93  | LED4_STATE          | Physical LED4 line                                                           |

> The PLC example exposes **1–17**, **50–53**, **60–62**, and **90–93** as binary_sensors.

---

### Coils (FC=05/15) — **Auto-Pulse Commands**

Write `1` to trigger the action; the device executes it and clears the coil.  
Use these from PLC / ESPHome **output.turn_on** actions.

#### Acknowledge Alarms
| Addr | Action        | Notes                                  |
|----:|----------------|----------------------------------------|
| 500 | ACK_ALL        | Clears all **latched** groups          |
| 501 | ACK_G1         | Clear Group 1 latch                    |
| 502 | ACK_G2         | Clear Group 2 latch                    |
| 503 | ACK_G3         | Clear Group 3 latch                    |

> Exposed in the PLC YAML as `alm_ack_*` and used by “Ack …” buttons.

#### Manual Relay Override (request ON/OFF)
| Addr | Action          | Target |
|----:|------------------|--------|
| 400 | RELAY1_ON        | R1     |
| 401 | RELAY2_ON        | R2     |
| 402 | RELAY3_ON        | R3     |
| 420 | RELAY1_OFF       | R1     |
| 421 | RELAY2_OFF       | R2     |
| 422 | RELAY3_OFF       | R3     |

- These requests set a **manual Modbus override** in firmware.  
- If a **Button-override** is currently active on that relay, the request is **ignored** until the button-override exits.

#### Enable / Disable Inputs (IN1…IN17)
| Range     | Action      |
|----------:|-------------|
| 200–216   | ENABLE_INx  |
| 300–316   | DISABLE_INx |

> Each coil is a pulse that flips the stored enable flag for the corresponding input. Exposed as `alm_en_in*` and `alm_dis_in*` in the PLC YAML. 

#### (Optional) Alarm Group **Pulse** Inject
| Addr | Action         | Purpose                                                     |
|----:|-----------------|-------------------------------------------------------------|
| 510 | PULSE_G1        | Inject a Group-1 alarm pulse from PLC                      |
| 511 | PULSE_G2        | Inject a Group-2 alarm pulse                                |
| 512 | PULSE_G3        | Inject a Group-3 alarm pulse                                |

> These coils appear in the provided PLC config. Use them  they allow the PLC to stimulate group activity (module will still apply latched/non-latched logic).

---

### Priority & Behavior Notes

- **Relay drive priority:**  
  `Button-override` ➜ `Modbus manual override` ➜ `Assigned Alarm Group` (then **Enable/Invert**).  
  Effective relay state is reported at discrete inputs **60–62**.
- **Pulse semantics:** All coils listed above **auto-clear** after the module consumes the command.  
- **LED reporting:** **90–93** are the **physical** LED lines (already reflect LED source & blinking chosen in the module config).

---

### Example (ESPHome / PLC)

- Read telemetry: map **discrete_input** addresses shown above to `binary_sensor:`.  
- Fire a command: call `output.turn_on: alm_rly1_on` (writes coil **400 = 1**) and the module clears it automatically.  
  See the full example PLC config file in the repo for a ready-to-use mapping.

---

## 🔘 Button Combinations  

The ALM-173-R1 also supports **hardware button combos** for maintenance:  

- **Button1 + Button2** → Enter **bootloader mode** (firmware flashing)  
- **Button3 + Button4** → **Reset** the module  

---

## ⚡ Quick Recipes
- **Door sensor → Group1** → Relay1 drives siren  
- **Panic button → Group2 (Latched)** → requires manual acknowledge  
- **Relay override** → Button toggles light output  
- **LED blink on Any Alarm** → map LED1 to Any Alarm, Blink mode  

---

## 🧩 Troubleshooting
- **Device not detected** → ensure USB-C cable supports data, use Chrome/Edge/Firefox  
- **No Modbus comms** → check address/baud, A/B wiring, common GND  
- **Relays not switching** → verify enabled, check inversion  
- **Inputs not reacting** → ensure enabled + correct group assignment  

---

## 🛠 Build & Flash Notes
- MCU: RP2350A (RP2040 core)  
- IDE: Arduino IDE or PlatformIO  
- Flash via USB-C (UF2 drag & drop)  
- Required libraries: `ModbusSerial`, `SimpleWebSerial`, `Arduino_JSON`, `LittleFS`  

---

## 🔓 Open Source & Re-Programming
- Ships with default firmware (ready to use)  
- Fully open source hardware + firmware  
- Reprogrammable with:  
  - Arduino IDE (C++)  
  - MicroPython  
  - Pico SDK  

---

## 📄 License
- **Hardware:** CERN-OHL-W 2.0  
- **Firmware:** GPLv3  

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
