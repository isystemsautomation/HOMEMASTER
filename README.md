# HOMEMASTER – Modular, Resilient Smart Automation System

**Version: 2025‑09** — Fully open-source hardware, firmware, and configuration tools.

---

## 1. Introduction

### 1.1 Overview of the HOMEMASTER Ecosystem
HomeMaster is an industrial-grade, modular automation system for smart homes, labs, and professional installations. It features:

- ESP32‑based PLC controllers (MiniPLC & MicroPLC)
- A family of smart I/O modules (energy monitoring, lighting, alarms, analog I/O, etc.)
- RS‑485 Modbus RTU communication
- ESPHome compatibility for Home Assistant
- USB-C & WebConfig UI for driverless configuration

Modules include local logic and continue functioning even without a network.

### 1.2 Modules & Controllers

<table>
<tr>
<td align="center" width="50%">
  <strong>🔵 MicroPLC</strong><br>
  <a href="./MicroPLC/Images/MicroPLC.png">
    <img src="./MicroPLC/Images/MicroPLC.png" alt="MicroPLC" height="220">
  </a><br><sub>Click to view full size</sub>
</td>
<td align="center" width="50%">
  <strong>🟢 MiniPLC</strong><br>
  <a href="./MiniPLC/Images/MiniPLC2.png">
    <img src="./MiniPLC/Images/MiniPLC2.png" alt="MiniPLC" height="220">
  </a><br><sub>Click to view full size</sub>
</td>
</tr>
</table>

#### Controllers

| Controller  | Description |
|------------|-------------|
| **MiniPLC** | Advanced DIN‑rail controller with Ethernet, relays, analog I/O, RTD, display, SD logging |
| **MicroPLC** | Compact controller with RS‑485, relay, input, 1‑Wire, RTC, USB‑C |

### 1.3 Use Cases
- Smart energy monitoring and control
- Smart lighting and climate control
- Leak detection and safety automation
- Modbus-connected distributed systems
- Industrial and home lab control

---

## 2. Safety Information

### 2.1 General Electrical Safety
- Only trained personnel should install or service modules.
- Disconnect all power sources before wiring or reconfiguring.
- Always follow local electrical codes and standards.

### 2.2 Handling & Installation
- Mount on 35 mm DIN rails inside protective enclosures.
- Separate low-voltage and high-voltage wiring paths.
- Avoid exposure to moisture, chemicals, or extreme temperatures.

### 2.3 Device-Specific Warnings
- Connect PE/N properly for metering modules.
- Use correct CTs (1 V or 333 mV) — never connect 5 A CTs directly.
- Avoid reverse polarity on RS-485 lines.

---

## 3. System Overview

### 3.1 Architecture & Modular Design
- Controllers connect to extension modules via RS-485 Modbus RTU.
- Each module operates independently using onboard logic.
- USB‑C and WebConfig allow local driverless setup and diagnostics.

### 3.2 MicroPLC vs MiniPLC

| Feature      | MiniPLC        | MicroPLC      |
|--------------|----------------|---------------|
| Size         | Full DIN       | Compact DIN   |
| I/O          | Rich onboard   | Basic onboard |
| Connectivity| Ethernet + USB | USB only      |
| Expansion    | Modbus RTU     | Modbus RTU    |
| Target Use   | Large systems  | Small systems |

### 3.3 Integration with Home Assistant
- ESPHome integration: modules appear as devices with sensors, switches, and alarms.
- Home Assistant can use entities for dashboards, automations, and energy monitoring.
- Use YAML package files to add ENM, ALM, DIM, etc. easily.

---

## 4. Networking & Communication

### 4.1 RS-485 Modbus
- All modules use Modbus RTU (slave) over RS‑485.
- Baud rate defaults: `19200 8N1` (configurable).
- Star or bus topology supported; use 120 Ω termination at ends.

### 4.2 USB-C Configuration
- Use `ConfigToolPage.html` (no drivers needed) in Chrome or Edge.
- Enables calibration, phase mapping, relay control, alarm config, etc.
- Available for each module type.

### 4.3 Wi-Fi and Bluetooth
- Wi-Fi is available on MiniPLC and MicroPLC.
- Improv Wi-Fi onboarding via BLE supported (MicroPLC only).
- Once connected, modules communicate over Modbus RS-485; controllers expose them wirelessly.

### 4.4 Ethernet
- Available on MiniPLC only.
- Enables fast and stable connection to Home Assistant or MQTT brokers.

---

## 5. Software & UI Configuration

### 5.1 Web Config Tool (USB Web Serial)
- HTML file that runs locally in browser (no install needed)
- Features per module:
  - Modbus address & baud rate
  - Relay control
  - Alarm rules
  - Input mappings
  - LED behavior
  - Calibration / phase mapping

### 5.2 ESPHome Wi-Fi Setup (via controller)
- MiniPLC/MicroPLC expose connected modules using `modbus_controller:` in ESPHome.
- Use `packages:` with variable overrides for each ENM or DIM module.
- Add ESPHome device to Home Assistant and select energy sensors or switches.

---

## 6. Programming & Customization

### 6.1 Supported Languages
- **Arduino IDE**
- **PlatformIO**
- **MicroPython** (via Thonny)
- **ESPHome YAML** (default config for most users)

### 6.2 Flashing via USB-C
- All controllers and modules support auto-reset via USB‑C.
- No need to press buttons — supports drag-and-drop UF2 (RP2040) or ESPHome Web Flasher.

### 6.3 PlatformIO & Arduino
- Clone firmware repository
- Use `default_xxx.ino` sketches for each module
- Add libraries: `ModbusSerial`, `LittleFS`, `Arduino_JSON`, `SimpleWebSerial`

---

## 7. Open Source & Licensing

- **Hardware:** CERN-OHL-W v2.0 (can be modified, commercial use permitted with open-source derivative)
- **Firmware:** GPLv3 (contributions welcome)
- **Web UI:** MIT (ConfigToolPage.html files for each module)

---

## 8. Downloads

- 📥 [Firmware (INO files)](https://github.com/isystemsautomation/HOMEMASTER/tree/main/Firmware)
- 🛠 [Config Tools (HTML)](https://github.com/isystemsautomation/HOMEMASTER/tree/main/tools)
- 📷 [Images & Diagrams](https://github.com/isystemsautomation/HOMEMASTER/tree/main/Images)
- 📐 [Schematics](https://github.com/isystemsautomation/HOMEMASTER/tree/main/Schematics)
- 📖 [Manuals (PDF)](https://github.com/isystemsautomation/HOMEMASTER/tree/main/Manuals)

---

## 9. Support

- 🌐 [Official Support Portal](https://www.home-master.eu/support)
- 🧠 [Hackster.io Projects](https://www.hackster.io/homemaster)
- 🎥 [YouTube Channel](https://www.youtube.com/channel/UCD_T5wsJrXib3Rd21JPU1dg)
- 💬 [Reddit /r/HomeMaster](https://www.reddit.com/r/HomeMaster)
- 📷 [Instagram](https://www.instagram.com/home_master.eu)

---

## 🔌 MiniPLC Power Supply and Protection

The **MiniPLC** can be powered in two ways:

- Via **external 24 VDC** to the V+/0V terminals  
- Via the **internal MYRRA 47156 AC/DC power supply**, which accepts:
  - **85–265 VAC** or **120–370 VDC**
  - Output: 24 V / 220 mA (≈5 W), 79% efficiency

> ⚠️ **Do not connect 24 VDC to the terminal block if internal PSU is active.**

### 🔧 Internal Fuse & TVS Protection
- TVS diodes on input/output terminals
- Resettable fuses on relay outputs and RS‑485 lines

---

## ⚙️ I/O Circuitry Details

### 🟥 Relay Outputs (6x)
- Relays: **HF115F/005-1ZS3** (SPDT, dry contact)
- Rated for:
  - 250 VAC 16 A (resistive)
  - 250 VAC 9 A (inductive, cosφ=0.4)
  - 30 VDC 10 A
- Internal varistors and optocouplers provide isolation

### 🟩 Digital Inputs (4x)
- Isolated via **ISO1212** surge-protected input ICs
- Voltage ranges:
  - Logic 0: 0–9.2 VDC
  - Undefined: 9.2–15.8 VDC
  - Logic 1: 15.8–24 VDC

### 🟦 RTD Inputs (2x)
- **MAX31865-based**
- Supports PT100 / PT1000 (2-, 3-, 4-wire)
- Jumper-configurable:
  - J1–J8 for RTD type and wire count
  - Factory default: RTD1 = PT100 2-wire, RTD2 = PT1000 2-wire

```text
Jumper Setup:
  PT100:  J2=ON, J6=ON
  PT1000: J3=ON, J7=ON
  2-Wire: J1=ON, J5=ON, J8=ON
  3-Wire: J4=ON, J6=ON, J8=ON
  4-Wire: J4=ON, J5=ON
```

### ESPHome RTD YAML Example:

```yaml
sensor:
  - platform: max31865
    name: "RTD Channel 1"
    cs_pin: GPIO1
    reference_resistance: 400.0
    rtd_nominal_resistance: 100.0
    update_interval: 60s

  - platform: max31865
    name: "RTD Channel 2"
    cs_pin: GPIO3
    reference_resistance: 4000.0
    rtd_nominal_resistance: 1000.0
    update_interval: 60s
```

### 🟨 Analog I/O
- Inputs: **ADS1115** (4 channels, 16-bit, 0–10 V)
- Output: **MCP4725** DAC (12-bit, 0–10 V via op-amp)
- ESD and EMI-protected

---

## 🌐 Web Server and OTA Updates

The MiniPLC includes a built-in **ESPHome OTA-capable web server**.

### WebConfig Access (ESPHome Improv)
1. Connect to the fallback Wi-Fi hotspot: `MiniPLC Fallback`
2. Password: `12345678`
3. Visit: `http://192.168.4.1`
4. Join your home Wi-Fi

### ESPHome OTA Update Flow
- Upload firmware via ESPHome Dashboard or OTA
- Use USB-C to flash manually if needed

---

