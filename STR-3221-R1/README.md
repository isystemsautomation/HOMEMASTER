# 🚧 Project Status: Under Active Development & Testing

> **Important Notice:** This documentation, hardware designs, and firmware are for the **pre-release version** of the HomeMaster system. All information is preliminary and may contain errors or be subject to change.
>
> - **Hardware:** Modules are currently in the prototyping and testing phase. Final production versions may differ.
> - **Firmware:** Firmware is under active development and is considered **beta**. Features, configurations, and stability are being refined.
>
> Please use this information for evaluation and development purposes only
# STR-3222-R1 – Smart Staircase Lighting Module

The **STR-3222-R1** is a programmable LED lighting controller specifically designed for **staircase illumination effects**. Built for integration with **MiniPLC** and **MicroPLC** platforms via **RS‑485 Modbus RTU**, it delivers smooth, animated step lighting sequences triggered by **motion sensors** or **manual switches**.

Powered by the **RP2350** microcontroller, it supports advanced automation logic, making it ideal for smart homes, hotels, commercial stairwells, and architectural lighting.

---

## ✨ Key Features

- **32 LED Output Channels**
  - Controlled via 4× TLC59208F constant-current I²C LED drivers
  - MOSFET-switched outputs (SI2307A) for 12 V / 24 V LED strips
  - Smooth step-by-step fade and dimming effects

- **3 Opto-Isolated Digital Inputs**
  - 2 for motion detectors (top/bottom stairs)
  - 1 for manual switch or override control

- **Onboard Interface**
  - USB Type‑C port for configuration and firmware updates
  - Status LEDs and buttons for manual test/control

- **Robust Power Design**
  - Protected 9–28 VDC input with reverse polarity protection
  - Onboard voltage regulation

- **Firmware**
  - Preloaded with **MicroPython**
  - Fully programmable via **MicroPython**, **Arduino (C/C++)**, or custom protocols

---

## 🏠 Home Assistant Integration

When paired with a MiniPLC or MicroPLC running **ESPHome**, the STR-3222-R1 integrates into **Home Assistant**, allowing:

- Automation of animated step lighting
- Triggering via occupancy or motion events
- Manual override through switches or UI
- Synchronized smart lighting scenes


---

## 🧰 Technical Specifications

| Parameter                | Value                                 |
|-------------------------|---------------------------------------|
| Microcontroller         | RP2350                                 |
| LED Channels            | 32 (via 4× TLC59208F, constant current)|
| Output Voltage Range    | 12 V / 24 V DC                         |
| Switch Type             | SI2307A MOSFET per channel            |
| Digital Inputs          | 3× isolated                           |
| Interface               | RS‑485 (Modbus RTU)                   |
| Local Control           | 2× buttons, status LEDs               |
| Configuration           | USB Type‑C                            |
| Power Supply            | 24 VDC (reverse polarity protected) |
| Mounting                | DIN-rail compatible                   |

---

## 🔧 Example Applications

- Step-by-step staircase LED animation
- Motion-triggered stairwell lighting
- Manual override of ambient lighting modes
- Synchronized hallway and staircase effects

---

## 💻 Open Source & Customization

The STR-3222-R1 is fully open for development. Use **MicroPython**, **C/C++**, or the **Arduino IDE** to build custom step animations, timing logic, and smart integrations. Firmware updates and serial debugging are available via USB-C.

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
