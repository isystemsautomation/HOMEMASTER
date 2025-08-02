# ALM-173-R1 – Alarm Input & Relay Output Module

The **ALM-173-R1** is a high-performance alarm expansion module designed for modular integration with **HomeMaster MicroPLC** and **MiniPLC** systems via RS-485 Modbus RTU. It enables seamless connection of alarm sensors and output devices to PLC systems for automation, security, and smart building control.

---

## ⚙️ Features

- 17 opto-isolated digital inputs for dry-contact sensors (motion, door/window, tamper, panic).
- 3 relay outputs (NO/NC) for sirens, lights, electric locks, or other external devices.
- Isolated 12V and 5V power outputs for powering connected sensors directly.
- Status LEDs for all inputs and outputs, with surge and ESD protection.
- USB Type-C interface for firmware configuration and diagnostics.
- Ships with pre-installed MicroPython/Arduino-compatible firmware and predefined Modbus registers.
- Built on the RP2350A microcontroller; programmable via MicroPython or Arduino IDE.
- Fully integrable with **Home Assistant** via MiniPLC or MicroPLC for automation logic.

---

## 🧰 Technical Specifications

| Specification              | Value                                |
|---------------------------|--------------------------------------|
| Digital Inputs            | 17 opto-isolated (5V logic level)    |
| Relay Outputs             | 3 (NO/NC, industrial-grade, isolated)|
| Auxiliary Power Outputs   | 12V and 5V (isolated)                |
| Microcontroller           | RP2350A                              |
| Communication             | RS-485 (Modbus RTU slave)            |
| Programming Interface     | USB Type-C                           |
| Mounting                  | DIN rail or surface mount            |
| Power Supply              | 24V DC                               |

---

## 🧠 Use Cases

- Security system integration (motion sensors, door/window contacts)  
- Smart siren or light control based on sensor triggers  
- Panic or emergency button handling  
- Sensor power distribution in automation enclosures  
- Seamless integration with Home Assistant via HomeMaster PLCs  

---

## 🔌 Modbus RTU Overview

- **Interface:** RS-485 (Modbus RTU slave) on UART (8N1).
- **Default slave address:** `3` (configurable in firmware).
- **Default baud rate:** `19200` (configurable in firmware).
- **Function codes used:**
  - **FC=02** – Read **Discrete Inputs** (read-only **states**)
  - **FC=05/15** – Write **Coils** (write-only **commands**, **pulse 0→1→0** handled/auto-cleared by firmware)

> **Design rule:**  
> **States** (inputs, relays, LEDs, enabled flags, alarms) live in **Discrete Inputs**.  
> **Commands** (relay on/off, input enable/disable, acknowledges) are **Coils** with **separate addresses** per action and are consumed as **pulses**.

---

## 📟 Modbus Register Map

### Discrete Inputs — **States** (FC=02)

| Addr | Name          | Description                                               |
|-----:|---------------|-----------------------------------------------------------|
| 0    | DI1           | Digital Input 1 (processed state, after per-input invert)|
| 1    | DI2           | Digital Input 2                                           |
| …    | …             | …                                                         |
| 16   | DI17          | Digital Input 17                                          |
| 32   | LED1          | User LED1 logical state (true = ON)                       |
| 33   | LED2          | User LED2 logical state                                   |
| 34   | LED3          | User LED3 logical state                                   |
| 35   | LED4          | User LED4 logical state                                   |
| 40   | RELAY1        | Relay 1 logical state (true = ON)                         |
| 41   | RELAY2        | Relay 2 logical state                                     |
| 42   | RELAY3        | Relay 3 logical state                                     |
| 48   | ALARM_G1      | Alarm Group 1 active                                      |
| 49   | ALARM_G2      | Alarm Group 2 active                                      |
| 50   | ALARM_G3      | Alarm Group 3 active                                      |
| 51   | ALARM_ANY     | Any alarm active (G1 ∪ G2 ∪ G3)                           |
| 60   | IN_EN1        | Input 1 enabled flag (true = enabled)                     |
| 61   | IN_EN2        | Input 2 enabled flag                                      |
| …    | …             | …                                                         |
| 76   | IN_EN17       | Input 17 enabled flag                                     |

> The **enabled flags** mirror the current enable/disable configuration as **read-only state** for SCADA/PLC diagnostics.

---

### Coils — **Commands** (FC=05/15, **pulse 1 then auto-clear**)

**Relays – separate addresses for ON/OFF**

| Addr | Command       | Effect            |
|-----:|---------------|-------------------|
| 400  | CMD_R1_ON     | Turn Relay 1 ON   |
| 401  | CMD_R2_ON     | Turn Relay 2 ON   |
| 402  | CMD_R3_ON     | Turn Relay 3 ON   |
| 420  | CMD_R1_OFF    | Turn Relay 1 OFF  |
| 421  | CMD_R2_OFF    | Turn Relay 2 OFF  |
| 422  | CMD_R3_OFF    | Turn Relay 3 OFF  |

**Digital Inputs – separate addresses for Enable/Disable (affects state processing and `IN_ENx`)**

| Addr  | Command        | Effect              |
|------:|----------------|---------------------|
| 200   | CMD_EN_IN1     | Enable Input 1      |
| 201   | CMD_EN_IN2     | Enable Input 2      |
| …     | …              | …                   |
| 216   | CMD_EN_IN17    | Enable Input 17     |
| 300   | CMD_DIS_IN1    | Disable Input 1     |
| 301   | CMD_DIS_IN2    | Disable Input 2     |
| …     | …              | …                   |
| 316   | CMD_DIS_IN17   | Disable Input 17    |

**Alarms – acknowledge (for latched modes)**

| Addr | Command      | Effect                       |
|-----:|--------------|------------------------------|
| 500  | CMD_ACK_ALL  | Acknowledge **all** groups   |
| 501  | CMD_ACK_G1   | Acknowledge Group 1          |
| 502  | CMD_ACK_G2   | Acknowledge Group 2          |
| 503  | CMD_ACK_G3   | Acknowledge Group 3          |

> **Pulse semantics:** write `1` to the command coil; the module performs the action once and automatically clears the coil back to `0`. You do **not** need to write `0`.

---

## 🔓 Open Source & Programming

The ALM-173-R1 is fully open source. It is shipped with MicroPython/Arduino-compatible firmware and the Modbus address table above, making it plug-and-play with HomeMaster PLCs. Developers can customize behavior using:

- MicroPython  
- C/C++
- Arduino IDE  

---

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
