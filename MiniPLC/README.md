# HomeMaster MiniPLC (ESP32)

![HomeMaster MiniPLC](MiniPLC2.png)

## Overview

The **HomeMaster MiniPLC** is a professional, open-source DIN rail controller built around the ESP32 platform, designed for robust and scalable smart automation in residential, commercial, and light industrial environments. It combines extensive onboard I/O—including relays, digital and analog inputs, temperature sensors, and a user interface—with native Home Assistant integration via pre-installed ESPHome.

Engineered for reliability and flexibility, the MiniPLC supports multiple power input options (24V DC or wide-range AC/DC) and features an isolated RS-485 Modbus interface for expansion with a wide range of compatible I/O modules. Its local processing capability ensures continued operation even when network or cloud connectivity is lost, making it suitable for mission-critical applications.

This controller operates as a complete standalone automation system using its comprehensive onboard I/O, while also offering seamless expansion via the RS-485 bus to connect HomeMaster smart modules for energy metering, lighting control, security, and more.

## Quick Overview

- Pre-installed **ESPHome** for native Home Assistant integration
- Industrial **DIN-rail form factor** with 24 V DC supply
- Runs **locally and offline** without cloud or HA
- **Expandable** via RS-485 Modbus field devices

## Typical Applications

- Smart home central controller
- HVAC automation and plant control
- Solar and energy management systems
- Laboratory automation
- Industrial I/O gateway
- Building management systems (BMS)
- SCADA edge controller
- RS-485 Modbus master / field gateway

## Technical Specifications

| Specification | Details |
|--------------|---------|
| **Microcontroller** | ESP32-WROOM-32U (dual-core) |
| **Power Input** | 24 V DC nominal (V+ / 0V) OR 85–265 V AC (L / N) OR 120–370 V DC (L / N + / −) |
| **Digital Inputs** | 4 × isolated, 24 V DC compatible |
| **Relay Outputs** | 6 × SPDT — **3 A MAX continuous per output** (board/system limit). Relay component contacts rated up to 12 A @ 250 V AC (resistive). |
| **Analog Inputs** | 4 × 0–10 V, 16-bit (ADS1115) |
| **Analog Output** | 1 × 0–10 V, 12-bit (MCP4725) |
| **Temperature Inputs** | 2 × RTD (PT100/PT1000 via MAX31865), 2 × 1-Wire (DS18B20 compatible) |
| **Display** | 128 × 64 OLED (SH1106) |
| **User Interface** | 4 buttons, 3 user LEDs, 1 status LED, onboard buzzer (GPIO2, PWM) |
| **Wi-Fi** | Wi-Fi (ESP32) |
| **Ethernet** | Optional Ethernet (LAN8720 PHY) |
| **RS-485** | RS-485 Modbus RTU (MAX485, half-duplex) with TVS surge protection, PTC fuses, EMI choke, and fail-safe biasing |
| **Storage** | MicroSD card (SPI interface, power-switched 3.3 V rail) |
| **USB** | USB-C (ESD protected, CC detection, data to ESP32) |
| **RTC** | PCF8563 with battery backup |

> **Note:** Relay outputs are not internally fused. Use external overcurrent protection per local code and use an external contactor/relay for loads above 3 A or with high inrush/inductive characteristics.

## Installation & Environmental

- **Mounting:** 35 mm DIN rail
- **Dimensions:** 157.4 × 91 × 58.4 mm (6.2 × 3.58 × 2.3 in) (L × W × H)
- **DIN units:** 9 division units (≈ 90 mm DIN rail mounting width)
- **Weight:** 300 g (net), 450 g (gross)
- **Operating Temperature:** 0 °C to +40 °C
- **Storage Temperature:** -10 °C to 55 °C
- **Relative Humidity:** 0–90 % RH, non-condensing
- **Ingress Protection:** IP20 (inside cabinet)
- **Installation:** Indoor control cabinet only; not for outdoor or exposed installation

> **Safety:** All wiring terminals must be protected against accidental contact by an insulating front plate, wiring duct, or terminal cover. Exposed live terminals are not permitted.

## Home Assistant & ESPHome Integration

The MiniPLC comes with **ESPHome pre-installed** and can be integrated directly into Home Assistant without flashing custom firmware.

### Quick Setup Process (Improv Wi-Fi)

1. **Mount & Power** – Install on a 35mm DIN rail and connect power.
2. **Open Improv** – Go to [improv-wifi.com](https://improv-wifi.com).
3. **Connect** – Use USB-C (Serial) or Bluetooth LE.
4. **Enter Wi-Fi** – Input SSID and password, then press Connect.
5. **Auto-Discovery** – Device appears in Home Assistant & ESPHome Dashboard.

### One-Click Import (ESPHome Dashboard)

Once connected to Wi-Fi, the MiniPLC is automatically discovered in the ESPHome Dashboard. Click **"Take Control"** to import the official configuration directly from GitHub.

### USB Type-C Manual Flashing (Optional)

1. Connect the MiniPLC to your computer using USB Type-C.
2. Open ESPHome Dashboard and add the device.
3. Import the configuration from [miniplc.yaml](Firmware/miniplc.yaml).
4. Compile and flash the firmware.
5. The device reboots automatically and runs the new firmware.

## Documentation & Resources

### Hardware Design Files

- [Schematic (MCU Board)](Schematic/MCU_Board.pdf) - Main controller board schematic
- [Schematic (Relay Board)](Schematic/Relay_Board.pdf) - Relay and power section schematic
- [Schematic (USB Board)](Schematic/USB_Board.pdf) - USB-C interface and power management

### Firmware & Software

- [Default ESPHome Config](Firmware/miniplc.yaml) - Pre-configured YAML for Home Assistant
- [Firmware Source Code](Firmware/) - Latest firmware builds and source
- [ESPHome Integration Guide](README.md) - Complete setup instructions (this file)

### Manuals & Datasheets

- [Datasheet](Manuals/Datasheet.pdf) - Technical specifications and ratings
- [User Manual](Manuals/User_Manual.pdf) - Installation and configuration guide

All design files and documentation are available in the [HomeMaster GitHub repository](https://github.com/isystemsautomation/HOMEMASTER/tree/main/MiniPLC).

## Pin Mapping

### I2C Bus

| Signal | GPIO |
|--------|------|
| SDA    | GPIO32 |
| SCL    | GPIO33 |

### I2C Addresses

| Device | Address |
|--------|---------|
| PCF8574/2 | 0x38 |
| PCF8574/1 | 0x39 |
| ADS1115   | 0x48 |
| SH1106 128x64 | 0x3C |
| PCF8563   | 0x51 |

### SPI Bus

| Signal | GPIO |
|--------|------|
| MISO   | GPIO12 |
| MOSI   | GPIO13 |
| CLK    | GPIO14 |

### SPI Chip Select Pins

| Device | GPIO |
|--------|------|
| MAX31865 RTD1 | GPIO01 |
| MAX31865 RTD2 | GPIO03 |
| SD Card       | GPIO15 |

### Digital Inputs

| Input | GPIO |
|-------|------|
| DI1   | GPIO36 |
| DI2   | GPIO39 |
| DI3   | GPIO34 |
| DI4   | GPIO35 |

### RS-485 Modbus

| Signal | GPIO |
|--------|------|
| TX     | GPIO17 |
| RX     | GPIO16 |

### 1-Wire Temperature Sensors

| Bus | GPIO |
|-----|------|
| 1-Wire 1 | GPIO05 |
| 1-Wire 2 | GPIO04 |

## Basic ESPHome Configuration

See [Firmware/miniplc.yaml](Firmware/miniplc.yaml) for the complete ESPHome configuration file with all sensors, switches, and I/O properly configured.

## Compliance & Certifications

The HomeMaster MiniPLC is **CE marked** and designed to comply with the applicable European Union directives. The manufacturer, **ISYSTEMS AUTOMATION** (HomeMaster brand), maintains the technical documentation and a signed EU Declaration of Conformity (DoC).

### EU Directives

- **EMC** 2014/30/EU
- **LVD** 2014/35/EU
- **RED** 2014/53/EU
- **RoHS** 2011/65/EU

### Harmonised Standards

| Area | Standards |
|------|-----------|
| **EMC** | EN 61000-6-1 (Immunity) · EN 61000-6-3 (Emissions) |
| **Electrical Safety** | EN 62368-1 |
| **Radio** | EN 300 328 · EN 301 489-1 · EN 301 489-17 |
| **RoHS** | EN IEC 63000 |

### Radio

The product integrates a pre-certified ESP32 Wi-Fi radio module (2.4 GHz). Final product conformity with the Radio Equipment Directive is demonstrated by the maintained technical documentation and conformity assessment of the complete device.

### Safety Notice

**L / N:** hazardous voltage · **24 V DC:** SELV · Qualified personnel only

## Links

- **Product Page:** [home-master.eu/shop/esp32-miniplc-55](https://www.home-master.eu/shop/esp32-miniplc-55)
- **GitHub Repository:** [github.com/isystemsautomation/HOMEMASTER/tree/main/MiniPLC](https://github.com/isystemsautomation/HOMEMASTER/tree/main/MiniPLC)
- **Manufacturer:** [home-master.eu](https://www.home-master.eu/)

## License

This project is open-source. Please refer to the repository for license details.

---

**Manufacturer:** ISYSTEMS AUTOMATION (HomeMaster brand)
