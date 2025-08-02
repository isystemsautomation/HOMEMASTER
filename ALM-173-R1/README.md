# ALM-173-R1 – Alarm Input & Relay Output Module

The **ALM-173-R1** is a high-performance alarm expansion module designed for modular integration with **HomeMaster MicroPLC** and **MiniPLC** systems via RS-485 Modbus RTU. It enables seamless connection of alarm sensors and output devices to PLC systems for automation, security, and smart building control.

---

## ⚙️ Features

- 17 opto-isolated digital inputs for dry-contact sensors (motion, door/window, tamper, panic)  
- 3 relay outputs (NO/NC) for sirens, lights, electric locks, or other external devices  
- Isolated 12 V and 5 V auxiliary power outputs for powering sensors directly  
- Status LEDs for all inputs and outputs, with surge and ESD protection  
- USB Type-C interface for firmware configuration and diagnostics  
- Ships with **pre-installed Arduino-compatible firmware** and predefined Modbus registers  
- Built on the RP2350A microcontroller; programmable via **MicroPython**, **Arduino IDE**, or **C++**  
- Seamless integration with **Home Assistant** via HomeMaster MiniPLC/MicroPLC  

---

## 🧰 Technical Specifications

| Specification            | Value                                 |
|--------------------------|---------------------------------------|
| Digital Inputs           | 17 opto-isolated (5 V logic level)    |
| Relay Outputs            | 3 (NO/NC, industrial-grade, isolated) |
| Aux Power Outputs        | 12 V and 5 V (isolated)               |
| Microcontroller          | RP2350A                               |
| Communication            | RS-485 (Modbus RTU slave)             |
| Programming Interface    | USB Type-C                            |
| Mounting                 | DIN rail or surface mount             |
| Power Supply             | 24 V DC                                |

---

## 🧠 Use Cases

- Security system integration (motion sensors, door/window contacts)  
- Smart siren or light control based on sensor triggers  
- Panic or emergency button handling  
- Sensor power distribution in automation enclosures  
- Seamless integration with Home Assistant via HomeMaster PLCs  

---

## 📦 Default Firmware – User Manual

The ALM-173-R1 ships with **ready-to-use firmware**. You can configure the module over **USB** using the **Web Config Tool** (runs in Chrome/Edge) and use it immediately with a HomeMaster PLC. This section explains the module’s internal logic and how to configure it.

### 🔔 Alarm Groups & Modes

- Each input may be assigned to **Group 1**, **Group 2**, **Group 3**, or **None**.  
- Each group has a **Mode**:
  - **None** – The group is ignored.  
  - **Non-latched** – Group is **active only while** any assigned input is active.  
  - **Latched** – Group becomes active when any assigned input triggers and **stays active** until **acknowledged**.

**Any Alarm** is on when any group (G1/G2/G3) is active.

**Acknowledgement paths** (only relevant for *Latched* groups):
- Press an assigned **button** (Ack All / Ack G1 / G2 / G3)
- Use a **PLC command** (via Modbus, see table below)
- Use the **Web Config Tool** (change mode or force an ack action depending on your UI setup)

### ↕️ Input Processing

Per input you can set:
- **Enabled** – Exclude/include input from processing (and from alarm computation)
- **Inverted** – Reverse active logic (useful for normally-closed contacts)
- **Group** – Select None/1/2/3

The **processed input state** = (raw input) XOR (inverted flag), considered only if **Enabled**.

### ⚡ Relays

Per relay you can set:
- **Enabled** – Output is allowed to turn on  
- **Inverted** – Reverse hardware polarity (logical ON drives the opposite level)  
- **Group** – Relay **follows the chosen group’s active state** unless you manually override it (e.g., via button or PLC)

**Relay behavior:**
- If **Enabled** and its **Group** is active, the relay turns **ON** (respecting Inverted).
- Manual/PLC override can force ON/OFF regardless of group logic.

### 🖲️ Buttons

Each of the 4 push buttons can perform one action:
- **None**  
- **All alarm acknowledge**  
- **Alarm group 1/2/3 acknowledge**  
- **Relay 1/2/3 override (manual)** – toggles a manual override flag

When a button is pressed (active-low), the action triggers on the **rising edge** (press event).

### 💡 User LEDs

Each of the 4 user LEDs has:
- **Mode** – **Steady** or **Blink** when active  
- **Source** – Trigger from:
  - **Any alarm**, **Group 1**, **Group 2**, **Group 3**
  - **Relay 1/2/3 overridden** (manual override indicator)
  - **None**

Blink timing is handled by the firmware (default ~400 ms period).

### 🔧 Web Config Tool (over USB)

- **Live Modbus status** (address/baud) is displayed.
- Configure:
  - **Alarm modes** (G1–G3)
  - **Inputs** (Enable, Invert, Group for IN1–IN17)
  - **Relays** (Enable, Invert, Group for R1–R3)
  - **Buttons** (action)
  - **User LEDs** (mode + source)
- Settings are applied instantly; most are persisted in flash (depending on firmware build).
- A **Reset Device** command is available (reboots the module safely).

> The module continuously evaluates inputs, updates alarm states, latches alarms where configured, drives relays/LEDs, and exposes states/commands to the PLC via Modbus.

---

## 🔌 Modbus RTU – Default Firmware Map

> These addresses are provided by the **default firmware** for PLC integration.  
> **States** are exposed as **Discrete Inputs** (FC=02).  
> **Commands** are **Coils** (FC=05/15) and are treated as **pulses**: write `1`, firmware acts, coil auto-clears to `0`.

### Discrete Inputs — States (FC=02)

| Addr | Name       | Description                                        |
|-----:|------------|----------------------------------------------------|
| 0–16 | DI1–DI17   | Processed input states (after per-input invert)    |
| 32–35| LED1–LED4  | User LED logical states                            |
| 40–42| RELAY1–3   | Relay logical states                               |
| 48–50| ALARM_Gx   | Alarm group active flags                           |
| 51   | ALARM_ANY  | Any alarm active (G1 ∪ G2 ∪ G3)                    |
| 60–76| IN_ENx     | Digital input Enabled flags (read-only mirror)     |

### Coils — Commands (FC=05/15, pulse 1 → auto-clear)

**Relays (separate ON/OFF addresses)**

| Addr | Command     | Effect           |
|-----:|-------------|------------------|
| 400  | CMD_R1_ON   | Turn Relay 1 ON  |
| 401  | CMD_R2_ON   | Turn Relay 2 ON  |
| 402  | CMD_R3_ON   | Turn Relay 3 ON  |
| 420  | CMD_R1_OFF  | Turn Relay 1 OFF |
| 421  | CMD_R2_OFF  | Turn Relay 2 OFF |
| 422  | CMD_R3_OFF  | Turn Relay 3 OFF |

**Digital Inputs (separate Enable/Disable addresses)**

| Addr | Command       | Effect            |
|-----:|---------------|-------------------|
| 200–216 | CMD_EN_INx | Enable input x     |
| 300–316 | CMD_DIS_INx| Disable input x    |

**Alarms – Acknowledge**

| Addr | Command      | Effect                          |
|-----:|--------------|---------------------------------|
| 500  | CMD_ACK_ALL  | Acknowledge **all** groups      |
| 501  | CMD_ACK_G1   | Acknowledge Group 1             |
| 502  | CMD_ACK_G2   | Acknowledge Group 2             |
| 503  | CMD_ACK_G3   | Acknowledge Group 3             |

**Defaults**  
- UART: 8N1  
- Address: `3`  
- Baud: `19200`

---

## 🔓 Open Source & Re-Programming

The module ships with the **default firmware** described above and works out-of-the-box.  
If you prefer, you can **replace the firmware** with your own using:
- **MicroPython**
- **Arduino IDE**
- **C++** (Pico SDK / Arduino Core)

---

## 📄 License

- Hardware design files and documentation: **CERN-OHL-W 2.0**  
- Firmware and code samples: **GPLv3** unless otherwise noted

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
