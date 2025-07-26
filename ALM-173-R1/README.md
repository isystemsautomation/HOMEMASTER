# ALM-173-R1 – Alarm Input & Relay Output Module

The **ALM-173-R1** is a high-performance alarm expansion module designed for modular integration with **HomeMaster MicroPLC** and **MiniPLC** systems via RS‑485 Modbus RTU. It enables seamless connection of alarm sensors and output devices to PLC systems for automation, security, and smart building control.

---

## ⚙️ Features

- 17 opto-isolated digital inputs for dry-contact sensors (motion, door/window, tamper, panic).
- 3 relay outputs (NO/NC) for sirens, lights, electric locks, or other external devices.
- Isolated 12V and 5V power outputs for powering connected sensors directly.
- Status LEDs for all inputs and outputs, with surge and ESD protection.
- USB Type‑C interface for firmware configuration and diagnostics.
- Ships with pre-installed MicroPython firmware and predefined Modbus registers.
- Built on the RP2350A microcontroller; programmable via MicroPython or Arduino IDE.
- Fully integrable with **Home Assistant** via MiniPLC or MicroPLC for automation logic.

---

## 🧰 Technical Specifications

| Specification              | Value                                |
|---------------------------|--------------------------------------|
| Digital Inputs             | 17 opto-isolated (5V logic level)    |
| Relay Outputs              | 3 (NO/NC, industrial-grade, isolated) |
| Auxiliary Power Outputs    | 12V and 5V (isolated)                |
| Microcontroller            | RP2350A                              |
| Communication              | RS‑485 (Modbus RTU slave)            |
| Programming Interface      | USB Type‑C                           |
| Mounting                   | DIN rail or surface mount            |
| Power Supply               | 24V DC                               |

---

## 🧠 Use Cases

- Security system integration (motion sensors, door/window contacts)  
- Smart siren or light control based on sensor triggers  
- Panic or emergency button handling  
- Sensor power distribution in automation enclosures  
- Seamless integration with Home Assistant via HomeMaster PLCs  



---

## 🔓 Open Source & Programming

The ALM-173-R1 is fully open source. It is shipped with MicroPython firmware and a predefined Modbus address table, making it plug-and-play with HomeMaster PLCs. Developers can customize behavior using:

- MicroPython  
- C/C++
- Arduino IDE  


---
## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
