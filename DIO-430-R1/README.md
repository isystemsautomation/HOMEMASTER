# DIO-430-R1 – 3-Channel Relay & Digital Input Module

The **DIO-430-R1** is a compact, intelligent relay expansion module designed for use with **MicroPLC** and **MiniPLC** platforms via **RS‑485 Modbus RTU**. Built for reliable **load control** in smart home, building automation, and industrial systems, it integrates seamlessly with **Home Assistant** through **ESPHome**.

Powered by the **RP2350** microcontroller, it combines robust hardware with open-source flexibility, enabling advanced automation scenarios and custom logic development.

---

## ⚙️ Key Features

- **3× SPDT Relay Outputs (NO/NC)**
  - Up to **16 A** switching current
  - Manual push-button control for each relay
  - LED indicators for relay status

- **4× Opto-Isolated Digital Inputs**
  - 24 VDC compatible
  - Ideal for wall switches, sensors, or dry contact devices
  - LED input status indicators

- **User Interface & Diagnostics**
  - USB Type‑C port for firmware updates
  - RS‑485 TX/RX status LEDs
  - Power LED and comms feedback

- **Built-In Logic Support**
  - Preloaded MicroPython firmware
  - Customizable with Arduino IDE (C/C++) or MicroPython
  - Supports timers, conditional switching, automation hooks

---

## 🏠 Home Assistant Integration

Using a **MicroPLC or MiniPLC with ESPHome**, the DIO-430-R1 becomes fully compatible with **Home Assistant**, supporting:

- Relay control via UI, automation, or voice
- Input monitoring for switches and triggers
- Local/manual override via front-panel buttons
- Event-based logic for lighting, HVAC, alarms, and more


---

## 🧰 Technical Specifications

| Parameter                  | Value                                |
|---------------------------|--------------------------------------|
| Power Supply              | 24 V DC                              |
| Relay Outputs             | 3 × SPDT (NO/NC), max 16 A           |
| Digital Inputs            | 4 × Opto-isolated (24 VDC)           |
| Interface                 | RS‑485 (Modbus RTU)                  |
| Programming               | MicroPython, Arduino IDE (C/C++)     |
| USB Port                  | USB Type‑C                           |
| Indicators                | Relay status, input status, comms    |
| Mounting                  | DIN-rail or surface                  |
| Dimensions                | 3 DIN module width                   |

---

## 🔧 Example Use Cases

- Lighting circuits with local override
- HVAC contactor control with feedback
- Alarm input monitoring (door/window sensors)
- Energy-aware device control via Home Assistant

---

## 💻 Open Source

The DIO-430-R1 is fully open source. Schematics, firmware, and example configurations are available in this repository for complete transparency and user-driven development.


