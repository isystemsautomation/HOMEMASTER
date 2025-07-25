# RGB-621-R1 – RGBCCT LED Control Module

The **RGB-621-R1** is a smart RS‑485 expansion module designed for precise control of **RGB + Tunable White (CCT)** LED strips in **home automation**, **commercial lighting**, and **building control systems**. Powered by the **RP2350A** microcontroller, it communicates via **Modbus RTU** and integrates seamlessly with **MicroPLC** or **MiniPLC** controllers.

With **ESPHome** support, the module is fully compatible with **Home Assistant**, enabling advanced lighting scenes, automation, and responsive control.

---

## 🌈 Key Features

- **5× Independent PWM Channels**
  - Full-spectrum RGBCCT LED strip control
  - Smooth dimming and color/temperature transitions
  - Compatible with 12 V and 24 V LED strips

- **2× Isolated Digital Inputs**
  - For wall switches, buttons, or motion sensors
  - Enables local scene triggering and manual overrides

- **1× Relay Output (NO)**
  - For switching LED drivers, accent lights, or auxiliary loads

- **User Interface**
  - 2 front-panel buttons + status LEDs
  - USB Type-C for firmware updates and diagnostics

- **RS‑485 Modbus RTU**
  - Predefined register map
  - Seamless connection with MicroPLC / MiniPLC
  - Native support in ESPHome & Home Assistant

---

## 🧠 Smart Home Integration

Pair the RGB-621-R1 with **MicroPLC/MiniPLC controllers** to expose all LED channels and inputs directly in **Home Assistant**, allowing you to:

- Automate scenes (sunrise/sunset, motion, schedules)
- Control color, brightness, and temperature via UI or voice
- Link lighting to presence, security, or climate systems
- Enable advanced triggers using physical switches

---

## 📦 Firmware & Programming

- Preloaded with **MicroPython firmware**
- Programmable using:
  - **MicroPython**
  - **Arduino IDE**
  - **(C/C++)**
- Custom logic, animations, or sensor-based control possible



---

## ⚙️ Technical Specifications

| Parameter                  | Value                                  |
|---------------------------|----------------------------------------|
| Power Supply              | 24 V DC                                |
| PWM Outputs               | 5 × 12/24 V channels (0–100% duty)      |
| Digital Inputs            | 2 × opto-isolated (dry contact)        |
| Relay Output              | max 16 A                       |
| Interface                 | RS‑485 (Modbus RTU)                     |
| USB Port                  | USB Type‑C                              |
| Dimensions                | DIN-rail mount, 3 modules wide         |
| Compatibility             | MicroPLC, MiniPLC, ESPHome, Home Assistant |

---

## 💡 Example Applications

- Dynamic ambient lighting (living rooms, offices, hotels)
- Human-centric lighting with tunable white control
- Smart LED strip control with wall switch fallback
- Color-based status indicators in automation systems

