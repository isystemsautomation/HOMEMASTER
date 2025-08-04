# DIM-430-R1 – 3-Relay, 4-Digital Input, Configurable I/O Module

The **DIM-430-R1** is a smart RS-485 Modbus RTU I/O module with **3 relays**, **4 opto-isolated digital inputs**, **3 user buttons**, and **3 user LEDs**.  
It features a built-in **Web Config Tool** for setting Modbus parameters, I/O behavior, and LED logic without extra programming.  
Fully compatible with **HomeMaster MicroPLC** and **MiniPLC** controllers, as well as **Home Assistant** via ESPHome.

---

## ⚙️ Key Features

- **3 × SPDT Relays (NO/NC)** – up to 16 A load, manual or remote control, pulse/toggle operation
- **4 × Opto-Isolated Digital Inputs** – 24 VDC, configurable action & target (None, All, Relay 1–3)
- **3 × User Buttons** – assignable to relay overrides
- **3 × User LEDs** – configurable mode (steady/blink) and activation source (None / Overridden Relay 1–3)
- **RS-485 Modbus RTU** – address 1–255, baud rate 9600–115200
- **USB-C Port** – firmware updates & Web Serial configuration
- **Persistent Storage** – saves settings to internal flash
- **Open Firmware** – Arduino & MicroPython compatible

---

## 🌐 Web Config Tool

The **Web Config Tool** allows you to configure the DIM-430-R1 directly from your browser (Chrome/Edge) using the **Web Serial API**.  
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
- Save, Load, Factory Reset
- Live status updates
- Serial log viewer

📎 **[Open Web Config Tool](./tools/web-config-DIM-430-R1.html)**

---

## 💾 Firmware Downloads & Source

| File | Description |
|------|-------------|
| [`DIM-430-R1_Default_Firmware.uf2`](./firmware/DIM-430-R1_Default_Firmware.uf2) | Default preloaded firmware with Web Config Tool support |
| [`DIM-430-R1_Arduino_Source.zip`](./firmware/DIM-430-R1_Arduino_Source.zip) | Arduino IDE source code |
| [`web-config-DIM-430-R1.html`](./tools/web-config-DIM-430-R1.html) | Standalone Web Config Tool HTML/JS |

---

## 📖 User Manual – Default Firmware & Web Config Tool

### Digital Inputs (DI1–DI4)
- Opto-isolated, 24 VDC
- Configurable **Enable**, **Invert**, **Action**, **Target**
- Actions:
  - **None** – ignore changes
  - **Toggle** – toggle target relay(s)
  - **Pulse** – turn target relay(s) on for configured pulse time
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
3. **Desired relay state** updated from inputs, buttons, or Modbus commands.
4. **Relays** driven considering enable/invert settings.
5. **LEDs** update based on their mode and activation source.
6. **Configuration changes** saved to flash with CRC verification.

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

**Integration Example with ESPHome:**
```yaml
modbus_controller:
  - id: dim430
    address: 3
    modbus_id: modbus1
    update_interval: 500ms
