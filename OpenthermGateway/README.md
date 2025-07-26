# 🔥 Opentherm Gateway – DIN-Rail Smart Heating Interface for Home Assistant

The **Opentherm Gateway** is a DIN-rail mountable module designed to connect OpenTherm-compatible boilers to **Home Assistant** via **ESPHome**. Powered by the **ESP32-WROOM-32U** microcontroller, it enables wireless integration of heating systems into modern smart homes, providing real-time control, automation, and diagnostics over Wi-Fi.

---

## 🌡️ Description

The Opentherm Gateway enables full bidirectional OpenTherm communication for intelligent climate control. It supports monitoring and control of key heating parameters such as burner status, flame modulation, setpoint temperatures, and system diagnostics.

A built-in high-voltage relay allows local control of zone valves or backup heaters, while two independent **1-Wire interfaces** support digital temperature sensors (e.g., DS18B20) for detailed room or system temperature monitoring.

Designed for seamless integration with **ESPHome** and **Home Assistant**, the gateway allows OTA updates, local LED indicators, and configuration via USB Type-C.

---

## ⚙️ Key Features

- **OpenTherm Interface**: Full OpenTherm communication with compatible boilers for temperature control and diagnostics
- **ESP32-WROOM-32U**: Wi-Fi/Bluetooth-enabled microcontroller with ESPHome pre-installed
- **Relay Output**: One high-voltage relay for local switching (e.g., heaters, zone valves)
- **Dual 1-Wire Interfaces**: Two isolated 1-Wire buses for temperature sensors like DS18B20
- **Power Options**: Operates on 24 VDC or 220 VAC for flexible installation
- **USB Type-C**: For firmware updates, serial configuration, and power
- **OTA Updates**: Supported via ESPHome for wireless firmware management
- **DIN-Rail Mountable**: Standardized enclosure for electrical cabinets
- **Status LEDs**: Visual indicators for power, relay, OpenTherm, and Wi-Fi status
- **Open Source**: Both hardware and firmware are open for community contribution

---

## 🏠 Integration with Home Assistant

When flashed with ESPHome, the Opentherm Gateway exposes the following entities in Home Assistant:

- Boiler on/off
- Burner status
- Flame modulation level (%)
- CH/DHW setpoint temperatures
- Boiler water temperature
- System pressure (if supported)
- Relay output status
- Temperature readings from connected 1-Wire sensors
- etc.

These enable complex automations like zone heating, backup switching, or temperature-based schedules—all managed within Home Assistant.



