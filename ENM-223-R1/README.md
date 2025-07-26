# ENM-223-R1 — 3-Phase Power Quality & Energy Metering Module

The **ENM-223-R1** is a high-precision, compact metering module designed for seamless integration with **HomeMaster MicroPLC** and **MiniPLC** systems. It provides real-time monitoring of 3-phase electrical parameters, supports load control, and is ideal for energy management, automation, and smart building applications.

---

## 🔧 Key Features

- **3-Phase Energy Monitoring** using the ATM90E32AS metering IC
- Measures:
  - RMS & peak voltages and currents
  - Active, reactive, and apparent power
  - Power factor, phase angles, and frequency
- **External CT support**: Works with 1 V or 333 mV RMS split-core transformers
- **2 Relay outputs** (NO/NC) for external load control
- **Status LEDs and tactile buttons** for diagnostics and local control
- **RS-485 communication** with Modbus RTU protocol
- **USB Type-C** for firmware updates and configuration
- **Built-in protections**: ESD and surge protection
- **DIN-rail mountable**, 24 V DC powered

---

## 📦 Firmware & Integration

- Comes with **pre-installed MicroPython firmware**
- Predefined **Modbus RTU address table** for plug-and-play operation  
- Fully programmable using:
  - MicroPython
  - C/C++
  - Arduino IDE

---

## 🏠 Home Assistant Integration

When connected to a **MicroPLC** or **MiniPLC**, the ENM-223-R1 can be integrated with **Home Assistant**, enabling advanced automation based on real-time energy data.


---

## 🛠️ Specifications (Summary)

| Parameter            | Value                            |
|---------------------|----------------------------------|
| Voltage Inputs       | 3-phase, 85–265 VAC              |
| Current Inputs       | External CTs (1 V or 333 mV RMS) |
| Relay Outputs        | 2x SPDT (NO/NC), up to 5 A       |
| Communication        | RS-485, Modbus RTU               |
| Programming          | MicroPython / C++/ Arduino IDE   |
| Power Supply         | 24 V DC                          |
| Mounting             | DIN rail                         |

---

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
