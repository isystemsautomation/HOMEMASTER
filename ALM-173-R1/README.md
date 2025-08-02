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
| Power Supply             | 24 V DC                               |

---

## 🧠 Use Cases

- Security system integration (motion sensors, door/window contacts)  
- Smart siren or light control based on sensor triggers  
- Panic or emergency button handling  
- Sensor power distribution in automation enclosures  
- Seamless integration with Home Assistant via HomeMaster PLCs  

---

## 🌐 Web Config Tool

Configure the ALM-173-R1 directly from your browser—no software install required.

**➡ https://www.home-master.eu/configtool-alm-173-r1**

- Works with **Google Chrome**, **Microsoft Edge**, **Opera**, and **Firefox** (recent versions).  
- Connect the module via **USB Type-C**, click **Connect**, choose the serial port, and configure.

> The tool uses Web Serial to communicate with the device firmware and provides live status with instant configuration updates.

---

## 📘 User Manual – Default Firmware (Web Config Tool)

This section explains how to use the Web Config Tool and how the **default firmware** behaves. No Modbus knowledge is required.

### 1) Connect & Live Status
1. Open the **Web Config Tool** link above in a supported browser.  
2. Plug the ALM-173-R1 via **USB Type-C**.  
3. Click **Connect** and select the device port.  
4. The **Active Modbus Configuration** panel shows live **Address** and **Baud Rate** (used for RS-485 integration).

> **Reset Device** reboots the module safely. The serial connection will briefly disconnect and can then be re-opened.

### 2) Modbus Settings
- Set **Address** (1–255) and **Baud** (9600–115200).  
- Changes apply immediately and are **saved to flash**.  
- The device reconfigures the serial port after a baud change.

### 3) Digital Inputs (IN1–IN17)
For each input:
- **Enabled** — include/exclude from processing and alarm computation.  
- **Inverted** — flips the logic (useful for normally-closed contacts).  
- **Alarm Group** — assign to **Group 1**, **Group 2**, **Group 3**, or **None**.  
- The round **state dot** shows the **processed** state (after Invert and Enable).

**Processed state** = (raw input) XOR (Inverted), considered only if **Enabled**.

### 4) Alarm Groups & Modes
Each group (G1–G3) has a **Mode**:
- **None** — group never alarms.  
- **Non-latched** — group is **active only while** any assigned input is active.  
- **Latched** — once triggered, the group **stays active** until it is **acknowledged**.

**Any Alarm** lights when any group is active (latched or non-latched).

**Acknowledgement (for Latched mode):**
- Press a configured **Button** (Ack All / Ack G1 / G2 / G3).  
- Or acknowledge from a PLC/HMI (see Modbus section below).

### 5) Relays (R1–R3)
For each relay:
- **Enabled** — allows the output to turn ON.  
- **Inverted** — reverses the drive polarity (logical ON drives opposite level).  
- **Group** — the relay **follows the selected group’s active state**, unless manually overridden.

**Behavior:**  
If **Enabled** and its **Group** is active, the relay turns **ON** (respecting Inverted).  
Manual override (via Buttons or PLC) can impose ON/OFF regardless of the group.

> Hardware is **active-low** internally; the UI shows **logical** ON/OFF.

### 6) Buttons (4)
Assign one **Action** per button:
- **None**  
- **All alarm acknowledge**  
- **Alarm group 1/2/3 acknowledge**  
- **Relay 1/2/3 override (manual toggle)**

Buttons are **active-low** and trigger on the **press edge** (rising edge internally).  
Relay override toggles an internal **override flag** used by LEDs and control logic.

### 7) User LEDs (4)
Per LED choose:
- **Mode** — **Steady** or **Blink** when active.  
- **Source** — **Any alarm**, **Group 1/2/3**, **Relay 1/2/3 overridden**, or **None**.

Blink period is handled by firmware (~400 ms by default).  
Hardware is **active-low**; the UI shows **logical** ON/OFF.

### 8) Save / Load / Factory / Reset
- **Save** — persist current configuration to flash.  
- **Load** — reload last saved configuration.  
- **Factory** — restore defaults and save them.  
- **Reset Device** — reboot the module safely.

> The firmware also **auto-saves** shortly after changes (inactivity timeout).

### 9) Persistence & Boot
- Configuration is stored in internal flash and restored at boot.  
- If no valid configuration exists, factory defaults are applied and saved.

### 10) Troubleshooting
- **Connect button does nothing** → Replug USB; ensure no other app uses the port; try another supported browser.  
- **After reset** → Wait a few seconds, click **Connect** again.  
- **Windows** → Confirm the COM port appears in Device Manager.

---

## 🔌 Modbus RTU – Default Firmware Map (for Integrators)

> These addresses are provided by the **default firmware** for PLC/HMI integration.  
> **States** are exposed as **Discrete Inputs** (FC = 02).  
> **Commands** are **Coils** (FC = 05/15) and are treated as **pulses**: write `1`, device acts, coil auto-clears to `0`.

### Discrete Inputs — States (FC=02)

| Addr | Name       | Description                                        |
|-----:|------------|----------------------------------------------------|
| 0–16 | DI1–DI17   | Processed input states (after per-input invert)    |
| 32–35| LED1–LED4  | User LED logical states                            |
| 40–42| RELAY1–3   | Relay logical states                               |
| 48–50| ALARM_Gx   | Alarm group active flags                           |
| 51   | ALARM_ANY  | Any alarm active (G1 ∪ G2 ∪ G3)                    |
| 60–76| IN_ENx     | Digital input **Enabled** flags (read-only mirror) |

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
