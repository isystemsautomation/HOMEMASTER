# DIM-430-R1 – 3-Relay, 4-Digital Input, Configurable I/O Module

The **DIM-430-R1** is a smart RS-485 Modbus RTU I/O module with **3 relays**, **4 isolated digital inputs**, **3 user buttons**, and **3 user LEDs**.  
It features a **Web Config Tool** for setting Modbus parameters, I/O behavior, LED and logic without extra programming.  
Fully compatible with **HomeMaster MicroPLC** and **MiniPLC** controllers, as well as **Home Assistant** via ESPHome.

---

## ⚙️ Key Features

- **3 × SPDT Relays (NO/NC)** – up to 16 A load, manual or remote control, pulse/toggle operation
- **4 × Isolated Digital Inputs** – 24 VDC, configurable action & target (None, All, Relay 1–3)
- **3 × User Buttons** – assignable to relay overrides
- **3 × User LEDs** – configurable mode (steady/blink) and activation source (None / Overridden Relay 1–3)
- **RS-485 Modbus RTU** – address 1–255, baud rate 9600–115200
- **USB-C Port** – firmware updates & Web Serial configuration
- **Persistent Storage** – saves settings to internal flash
- **Open Firmware** – Arduino & MicroPython compatible

---

## 🌐 Web Config Tool

The **Web Config Tool** allows you to configure the DIM-430-R1 directly from your browser using the **Web Serial API**.  
No drivers or special software required.

**Features in Web Config Tool:**
- Modbus address & baud rate settings
- Digital Inputs:
  - Enable/disable, invert logic
  - Action: None, Toggle, Pulse
  - Control target: None, All, Relay 1–3
- Relays:
  - Enable/disable
  - Invert logic
- Buttons:
  - Action: None, Relay 1 Override, Relay 2 Override, Relay 3 Override
- User LEDs:
  - Mode: Steady/Blink
  - Activation Source: None / Overridden Relay 1–3
- Live status updates
- Serial log viewer

📎 **[Open Web Config Tool](https://www.home-master.eu/configtool-dio-430-r1)**
📎
---

## 💾 Firmware Downloads & Source

| File | Description |
|------|-------------|
| [`DIM-430-R1_Default_Firmware.uf2`](./firmware/DIM-430-R1_Default_Firmware.uf2) | Default preloaded firmware with Web Config Tool support |
| [`default_DIO_430_R1.ino`](https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIO-430-R1/Firmware/sketch_DIO-430_R1/default_DIO_430_R1/default_DIO_430_R1.ino)) | Arduino IDE source code |
| [`web-config-DIM-430-R1.html`](DIO-430-R1/Firmware/ConfigToolPage.html) | Code of Web Config Tool HTML/JS |

---

## 📖 User Manual – Default Firmware & Web Config Tool

### Digital Inputs (DI1–DI4)
- Isolated, 24 VDC
- Configurable **Enable**, **Invert**, **Action**, **Target**
- Actions:
  - **None** – ignore changes
  - **Toggle** – On/off target relay(s)
  - **Pulse** – turn target relay(s) on/off for pulse time
- Targets:
  - **None** – no relay control
  - **All** – all relays
  - **Relay 1–3** – single relay

### Relays (R1–R3)
- SPDT, 16 A
- Enable/disable control
- Invert output

### Buttons (B1–B3)
- Assignable to:
  - None
  - Relay override toggle (R1, R2, or R3)

### User LEDs (LED1–LED3)
- **Mode:**
  - Steady – on while activation source is active
  - Blink – blink while activation source is active
- **Activation Source:**
  - None
  - Overridden Relay 1–3

---

## 🔄 Internal Working Logic

1. **Inputs** read and processed according to enable/invert/action/target settings.
2. **Button events** toggle relays if assigned.
3. **Relays** updated from inputs, buttons, or Modbus commands.
4. **LEDs** update based on their mode and activation source.


---

## 📡 Modbus RTU Map – Connecting to MiniPLC/MicroPLC with ESPHome & Home Assistant

**Discrete Inputs (FC=02)**  
| Address | Description |
|---------|-------------|
| 1–4     | Digital Inputs DI1–DI4 (processed) |
| 60–62   | Relay 1–3 state |
| 90–92   | LED 1–3 state |

**Coils (FC=05/15)**  
| Address | Description |
|---------|-------------|
| 200–202 | Pulse Relay ON (R1–R3) |
| 210–212 | Pulse Relay OFF (R1–R3) |
| 300–303 | Enable DI1–DI4 |
| 320–323 | Disable DI1–DI4 |

## 📐 Hardware Schematics

- 📎 **[View Hardware Schematic (PDF)](./hardware/DIM-430-R1_Schematic.pdf)**
- 📎 **[PCB Layout (Gerber/PCB Source)](./hardware/)**

---

## 🛠 Open Source & Re-Programming

The **DIM-430-R1** firmware and hardware design are fully open source.  
You can reprogram the **RP2350** MCU using:

- **Arduino IDE (C/C++)**
- **PlatformIO**
- **MicroPython**

Useful links:

- 📎 **[Arduino Firmware Source](./firmware/DIM-430-R1_Arduino_Source.zip)**
- 📎 **[Default Firmware (.uf2)](./firmware/DIM-430-R1_Default_Firmware.uf2)**
- 📎 **[Web Config Tool (HTML)](./tools/web-config-DIM-430-R1.html)**

> Tip: For Arduino/PlatformIO, select the appropriate RP2350/RP2040-compatible board profile.  
> Flash via USB-C (UF2 drag-and-drop) or serial bootloader.

---

## 📄 License

- **Hardware:** [CERN-OHL-W 2.0](https://ohwr.org/cern_ohl_w_v2.txt)
- **Firmware:** [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html)

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
