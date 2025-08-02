# ALM-173-R1 – Alarm Input & Relay Output Module

The **ALM-173-R1** is a high-performance alarm expansion module designed for modular integration with **HomeMaster MicroPLC** and **MiniPLC** systems via RS-485 Modbus RTU. It enables seamless connection of alarm sensors and output devices for automation, security, and smart-building control.

---

## ⚙️ Features

- 17 opto-isolated digital inputs for dry-contact sensors (motion, door/window, tamper, panic)  
- 3 relay outputs (NO/NC) for sirens, lights, locks, beacons, etc.  
- Isolated 12 V & 5 V auxiliary power rails for sensors  
- Status LEDs for all I/O, surge & ESD protection  
- USB Type-C for configuration and diagnostics (Web Serial)  
- Ships with **pre-installed Arduino-compatible firmware** (Modbus RTU slave)  
- RP2350A microcontroller; programmable with **MicroPython**, **Arduino IDE**, or **C++**  
- Works out-of-the-box with HomeMaster MiniPLC/MicroPLC and **Home Assistant** integrations

---

## 🧰 Technical Specifications

| Specification            | Value                                  |
|--------------------------|----------------------------------------|
| Digital Inputs           | 17 opto-isolated (5 V logic)           |
| Relay Outputs            | 3 × SPDT (NO/NC), isolated             |
| Aux Power Outputs        | 12 V and 5 V (isolated)                |
| Microcontroller          | RP2350A                                 |
| Communication            | RS-485 (Modbus RTU slave)              |
| Programming Interface    | USB Type-C                              |
| Mounting                 | DIN rail / surface                      |
| Power Supply             | 24 V DC                                 |

---

## 🌐 Web Config Tool

Configure the ALM-173-R1 directly from your browser—no software install.

- **Online tool:** https://www.home-master.eu/configtool-alm-173-r1  
- **Supported browsers:** **Chrome**, **Edge**, **Opera**, **Firefox** (recent versions)

> The tool uses Web Serial to communicate with the module over USB-C and provides live status with instant updates.

**Source code of the tool (HTML/JS):**  
- Config Tool page (HTML with embedded JS):  
  https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Firmware/ConfigToolPage.html

---

## 📦 Firmware Downloads & Source

- **Default Firmware (Arduino sketch):**  
  https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Firmware/default_alm_173_r1/default_alm_173_r1.ino

- **Pre-compiled binaries (RP2350 / rp2040 core):**  
  https://github.com/isystemsautomation/HOMEMASTER/tree/main/ALM-173-R1/Firmware/default_alm_173_r1/build/rp2040.rp2040.generic_rp2350

> Flash the UF2 or BIN from the build folder if you prefer not to compile in the Arduino IDE.

---

## 📘 User Manual – Default Firmware & Web Config Tool

This section describes the **out-of-the-box firmware** and how to use the Web Config Tool. You can later replace the firmware with your own (MicroPython / Arduino / C++), but most users won’t need to.

### 1) Connect & Identify

1. Open the **Web Config Tool** in a supported browser.  
2. Connect the module via **USB-C**.  
3. Click **Connect** and select the device port.  
4. The **Active Modbus Configuration** panel shows **Address** and **Baud** (used for RS-485).

> **Reset Device** safely reboots the module; reconnect after a few seconds. Settings persist in flash (per firmware build).

### 2) Configure

**Digital Inputs (IN1–IN17)**  
- **Enabled** — include/exclude the input from processing and alarms  
- **Inverted** — flips logic (e.g., for normally-closed loops)  
- **Group** — assign to **Group 1**, **Group 2**, **Group 3**, or **None**  
- The colored dot shows the **processed** state (after Enable/Invert)

**Alarm Groups (G1–G3) – Modes**  
- **None** — group never raises an alarm  
- **Non-latched** — group is **active only while** any assigned input is active  
- **Latched** — once any assigned input becomes active, the group **stays active** until it’s **acknowledged**  
- **Any Alarm** indicator turns on when any group is active

**Relays (R1–R3)**  
- **Enabled** — allows the output to be driven  
- **Inverted** — reverses the electrical level for logical ON  
- **Group** — relay **follows the selected group’s active state**, unless manually overridden

**Buttons (4)**  
- One action per button:  
  - **None**  
  - **Acknowledge All**  
  - **Acknowledge Group 1 / 2 / 3**  
  - **Relay 1 / 2 / 3 override (manual toggle)**  
- Actions trigger on **rising edge** of button press (active-low input, debounced by edge detection)

**User LEDs (4)**  
- **Mode** — **Steady** or **Blink** when active  
- **Source** — **Any Alarm**, **Group 1/2/3**, or **Relay 1/2/3 Overridden**, or **None**  
- Blink timing is handled by firmware (~400 ms period)

**Save / Load / Factory / Reset**  
- **Save** — persist current settings to flash  
- **Load** — restore last saved settings  
- **Factory** — restore defaults and save them  
- **Reset Device** — reboot the module safely  
- Auto-save occurs after a short quiet period following changes

---

## 🧩 Alarm System – Internal Working Logic

The firmware continuously cycles through a deterministic evaluation loop. The order and rules below describe how alarms, LEDs, and relays behave.

### A) Input Processing
1. Read raw digital inputs from the PCF8574 expanders.  
2. For each input *i*:  
   - If **Enabled[i]** is `false` → its processed state is **false** (ignored).  
   - Else **Processed[i] = Raw[i] XOR Inverted[i]**.  
3. Each input contributes to the **group condition** of its assigned **Group[i]** (1..3).

> No additional dwell/debounce is applied in firmware; apply sensor-side or PLC-side filtering if required.

### B) Group Conditions → Group Active
For each group **g ∈ {1,2,3}**:
- **Cond[g]** = OR of **Processed[i]** for all inputs assigned to **g**.  
- **Mode = None** → **Active[g] = false**, **Latched[g] = false**.  
- **Mode = Non-latched** → **Active[g] = Cond[g]**, **Latched[g] = false**.  
- **Mode = Latched**:  
  - On **Cond[g]** transition **false → true**, set **Latched[g] = true**.  
  - **Active[g] = Cond[g] OR Latched[g]**.  
  - An **acknowledge** clears **Latched[g]** (see D).

**AnyAlarm** = **Active[1] OR Active[2] OR Active[3]**.

### C) Outputs – LEDs & Relays

**User LEDs (per LED k)**  
- **ActiveLED[k]** depends on **Source**:  
  - **None** → `false`  
  - **Any Alarm** → `AnyAlarm`  
  - **Group 1/2/3** → `Active[1/2/3]`  
  - **Relay 1/2/3 Overridden** → `Override[1/2/3]`  
- **ShownLED[k]** =  
  - **Steady** → `ActiveLED[k]`  
  - **Blink** → `ActiveLED[k] AND BlinkPhase` (BlinkPhase toggles ~400 ms)  
- Hardware is **active-low**; pins are driven accordingly to match the logical LED state.

**Relays (per relay r)**  
- A relay can be driven by **group logic** or **manual override**.  
- **Override[r]** toggles via a configured Button action (or PLC).  
- If **Override[r]** is **not** active:  
  - If **Enabled[r]** and **GroupSel[r]** is **Active**, the relay is **ON**; else **OFF**.  
- If **Override[r]** is active:  
  - The relay state is forced by the override (ON/OFF) regardless of group status.  
- **Inverted[r]** flips the electrical level to achieve logical ON/OFF at the terminals.

### D) Acknowledgement
- **Ack All** clears **Latched[1]**, **Latched[2]**, **Latched[3]**.  
- **Ack G1/G2/G3** clears only the selected **Latched[g]**.  
- Acknowledgement sources:  
  - **Buttons** (configured actions)  
  - **PLC/HMI** via Modbus in the default firmware

> In **Non-latched** mode, acknowledgements have no effect.

### E) Timing & Responsiveness
- LED blink period: ~**400 ms**  
- The main loop continuously services I/O and Modbus  
- Configuration changes apply immediately and persist shortly after changes

---
## 🔌 Modbus RTU Map – Connecting to MiniPLC/MicroPLC with ESPHome & Home Assistant

> The table below describes the **Modbus addresses** provided by the **default firmware** for use with a **HomeMaster MiniPLC** or **MicroPLC**.  
> These PLCs can then be integrated with **ESPHome** and **Home Assistant** to monitor inputs, alarms, LEDs, and control relays.  
> 
> **States** are exposed as **Discrete Inputs** (FC=02).  
> **Commands** are **Coils** (FC=05/15) and are treated as **pulses**: write `1`, device acts, coil auto-clears to `0`.

### Discrete Inputs — States (FC = 02)

| Addr | Name       | Description                                        |
|-----:|------------|----------------------------------------------------|
| 0–16 | DI1–DI17   | Processed input states (after per-input invert)    |
| 32–35| LED1–LED4  | User LED logical states                            |
| 40–42| RELAY1–3   | Relay logical states                               |
| 48–50| ALARM_Gx   | Alarm group active flags                           |
| 51   | ALARM_ANY  | Any alarm active (G1 ∪ G2 ∪ G3)                    |
| 60–76| IN_ENx     | Digital input **Enabled** flags (read-only mirror) |

### Coils — Commands (FC = 05/15, pulse 1 → auto-clear)

**Relays (separate ON/OFF addresses)**

| Addr | Command     | Effect           |
|-----:|-------------|------------------|
| 400  | CMD_R1_ON   | Turn Relay 1 ON  |
| 401  | CMD_R2_ON   | Turn Relay 2 ON  |
| 402  | CMD_R3_ON   | Turn Relay 3 ON  |
| 420  | CMD_R1_OFF  | Turn Relay 1 OFF |
| 421  | CMD_R2_OFF  | Turn Relay 2 OFF |
| 422  | CMD_R3_OFF  | Turn Relay 3 OFF |

**Digital Inputs (Enable/Disable; separate addresses)**

| Addr   | Command        | Effect           |
|------: |----------------|------------------|
| 200–216| CMD_EN_INx     | Enable input x   |
| 300–316| CMD_DIS_INx    | Disable input x  |

**Alarms – Acknowledge**

| Addr | Command      | Effect                      |
|-----:|--------------|-----------------------------|
| 500  | CMD_ACK_ALL  | Acknowledge **all** groups  |
| 501  | CMD_ACK_G1   | Acknowledge Group 1         |
| 502  | CMD_ACK_G2   | Acknowledge Group 2         |
| 503  | CMD_ACK_G3   | Acknowledge Group 3         |

**Defaults**  
- UART framing: **8N1**  
- Slave address: **3**  
- Baud rate: **19200**

---

## 🧩 Hardware Schematics

- **ALM-173-R1 Field Board schematic (PDF):**  
  https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Schematics/ALM-173-R1-FieldBoard.pdf

- **ALM-173-R1 MCU Board schematic (PDF):**  
  https://github.com/isystemsautomation/HOMEMASTER/blob/main/ALM-173-R1/Schematics/ALM-173-R1-MCUBoard.pdf

---

## 🔓 Open Source & Re-Programming

The module ships with the **default firmware** above and works out-of-the-box.  
If desired, you can **replace the firmware** with your own using:
- **MicroPython**
- **Arduino IDE**
- **C++** (Pico SDK / Arduino Core)

---

## 📄 License

- Hardware design files and documentation: **CERN-OHL-W 2.0**  
- Firmware and code samples: **GPLv3** unless otherwise noted

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
