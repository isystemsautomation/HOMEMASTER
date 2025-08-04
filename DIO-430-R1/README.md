# DIO-430-R1

**4× Digital Inputs · 3× Relays · 3× Buttons · 3× User LEDs**  
**RS-485 / Modbus-RTU · Web Serial Page Config Tool · LittleFS persistence**

---

This repository contains the firmware and a browser-based Page Config Tool for the **DIO-430-R1** module.  
You can configure Modbus parameters, digital input behavior, relay options, button overrides, and user LED behavior directly from your browser using **Web Serial**—no extra apps required.

> If you’re familiar with our **ALM-173-R1** project page, this README follows the same structure and “click-to-configure” approach.

---

## Table of contents

- [What’s in the box](#whats-in-the-box)
- [Hardware at a glance](#hardware-at-a-glance)
- [Page Config Tool (Web Serial)](#page-config-tool-web-serial)
- [Features](#features)
- [Digital Inputs: Actions & Targets](#digital-inputs-actions--targets)
- [Buttons: Relay override](#buttons-relay-override)
- [User LEDs: Mode & Source](#user-leds-mode--source)
- [Modbus map](#modbus-map)
- [Build & flash](#build--flash)
- [Persistent configuration](#persistent-configuration)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## What’s in the box

- `firmware/` – Arduino sketch for DIO-430-R1 (Modbus + Web Serial + LittleFS)
- `tools/page-config/` – a single-file **Page Config Tool** (HTML+JS) you can open locally or host with GitHub Pages
- `docs/` – wiring, register map, and quick guides (optional)

---

## Hardware at a glance

- **Digital Inputs (4)** — GPIO mapped: `7, 12, 11, 6`
- **Relays (3)** — active-HIGH outputs: `10, 9, 8`
- **Buttons (3)** — active-LOW with pull-ups: `1, 2, 3`  
- **User LEDs (3)** — active-HIGH outputs: `13, 14, 15`
- **RS-485 / Modbus-RTU** on UART2 (TX2=4, RX2=5), optional TXEN
- **MCU**: RP2040-class / Arduino core (as used in examples)

> Pin numbers match the default sketch; adjust for your hardware if needed.

---

## Page Config Tool (Web Serial)

The Page Config Tool lets you **configure the module from Chrome/Edge** over Web Serial.

**Capabilities**

- Set **Modbus address** (1–255) and **baud rate** (9600–115200)
- Configure **Digital Inputs**:
  - Enabled / Inverted
  - **Action**: `None · Toggle · Pulse`
  - **Control Target**: `None · Control all · Control relay 1/2/3`
- Configure **Relays**: enable & invert
- Configure **Buttons**: relay override (`None`, `Relay1/2/3 toggle`)
- Configure **User LEDs**:
  - **Mode**: `Steady · Blink`
  - **Source**: `None · Overridden relay 1/2/3`
- View live states (inputs, relays, buttons, LEDs) in real-time
- Save / Load / Factory reset

**How to use**

1. Open the HTML tool in a compatible browser (Chrome/Edge on desktop).
2. Click **Connect**, select the USB/serial device.
3. Adjust settings; changes are sent live to the device.
4. Use **Save** (or the firmware’s auto-save) to persist to flash.

> You can host the tool via GitHub Pages or open it directly as a local file.

---

## Features

- **Modbus-RTU slave** (address & baud configurable from the page)
- **Web Serial** JSON protocol for simple, human-readable control & telemetry
- **LittleFS** persistence with CRC-protected binary config blob
- **DI actions**: Toggle or timed Pulse applied to selected targets
- **Buttons** for **manual relay override** (on press: toggles selected relay)
- **LEDs** can reflect the logical state of an overridden relay, with steady/blink modes
- **Modbus coils** for one-shot “command pulses” (relay ON/OFF, DI enable/disable)

---

## Digital Inputs: Actions & Targets

Each DI has:

- **Enabled / Inverted**
- **Action** (`action`):
  - `0 = None`
  - `1 = Toggle` – toggles target relay(s) on **rising edge**
  - `2 = Pulse` – sets target relay(s) **ON for PULSE_MS (default 500 ms)**, then back **OFF**
- **Control Target** (`target`):
  - `4 = None` (no output is controlled)
  - `0 = Control all` (apply action to all relays)
  - `1..3 = Control relay 1..3`

Actions apply on the **debounced rising edge** after enable/invert logic.

---

## Buttons: Relay override

Each button can be configured with:

- `0 = None`
- `5 = Relay 1 override (toggle)`
- `6 = Relay 2 override (toggle)`
- `7 = Relay 3 override (toggle)`

On a **button press** (rising edge), the mapped relay **toggles** its desired state.

---

## User LEDs: Mode & Source

Each LED has:

- **Mode** (`mode`): `0 = Steady`, `1 = Blink`
- **Source** (`source`):  
  `0 = None`, `5 = Overridden relay 1`, `6 = Overridden relay 2`, `7 = Overridden relay 3`

When **Source ≠ None**, the LED follows the **logical relay state** (after enable/invert).  
In **Blink** mode, the LED blinks when the source relay is active.

---

## Modbus map

**Discrete Inputs (FC=02):**

| Address range | Meaning                         |
|---------------|---------------------------------|
| 1..4          | IN1..IN4 logical state (after enable+invert) |
| 60..62        | RELAY1..RELAY3 logical state              |
| 90..92        | LED1..LED3 logical state                  |

**Coils (FC=05/15) – command pulses:**

| Address range | Command (write `1` to pulse; device resets it to `0`) |
|---------------|--------------------------------------------------------|
| 200..202      | Relay 1..3 ON (set desired state = ON)                |
| 210..212      | Relay 1..3 OFF (set desired state = OFF)              |
| 300..303      | Enable DI 1..4                                        |
| 320..323      | Disable DI 1..4                                       |

> Coils are **momentary**: the firmware consumes and clears them.

---

## Web Serial JSON topics

The page and firmware exchange small JSON messages:

**From device → page** (periodic):
- `"status"` → `{ address, baud }`
- `"inputs"` → `[bool × 4]`
- `"enableList"` → `[bool × 4]`
- `"invertList"` → `[bool × 4]`
- `"inputActionList"` → `[0/1/2 × 4]`
- `"inputTargetList"` → `[4/0/1/2/3 × 4]`
- `"relayStateList"` → `[bool × 3]`
- `"relayEnableList"` → `[bool × 3]`
- `"relayInvertList"` → `[bool × 3]`
- `"ButtonStateList"` → `[bool × 3]`
- `"ButtonGroupList"` → `[0/5/6/7 × 3]`
- `"LedConfigList"` → `[{ mode, source } × 3]`
- `"LedStateList"` → `[bool × 3]`
- `"message"` → string diagnostics

**From page → device** (on change):
- `"values"` → `{ mb_address, mb_baud }`
- `"Config"` with `t` and `list`, where `t` ∈  
  `inputEnable | inputInvert | inputAction | inputTarget | relays | buttons | leds`
- `"command"` → `{ action: "save" | "load" | "factory" | "reset" }`

---

## Build & flash

1. **Arduino IDE / CLI**
   - Install board support (RP2040 or your MCU target).
   - Install libs:  
     `ModbusSerial`, `SimpleWebSerial`, `Arduino_JSON`, `LittleFS` (for your core).
2. **Open** the firmware sketch from `firmware/`.
3. **Adjust pins/defines** as needed (TX/RX, pins, TXEN if used).
4. **Build & Upload**.
5. (Optional) **Format LittleFS** on first use; the firmware will auto-format if mount fails.

---

## Persistent configuration

- Settings are stored in **LittleFS** as a binary structure with **CRC-32**.
- Auto-save triggers **~1.5 s** after the last change.
- You can also trigger:
  - `"command": { "action": "save" }`
  - `"command": { "action": "load" }`
  - `"command": { "action": "factory" }` (reset to defaults + save)
  - `"command": { "action": "reset" }` (reboot)

> The config blob includes DI/Relay/Button/LED settings and last desired relay states, plus Modbus address/baud.

---

## Troubleshooting

- **Can’t connect from the browser?**  
  Use Chrome/Edge (desktop), enable the **Web Serial API**, and make sure no other app is holding the COM port.
- **No Modbus responses?**  
  Verify **address** and **baud** from the Page Config Tool; ensure wiring and biasing/termination on RS-485.
- **Settings not sticking?**  
  Check the console messages; the device **auto-saves** after a short delay or on explicit **Save**.

---

## License

Unless otherwise noted, source code in this repository is released under the **MIT License**.  
See `LICENSE` for details.

---

## Credits

- Built on top of Arduino, ModbusSerial, SimpleWebSerial, Arduino_JSON, and LittleFS.
- © iSystems Automation – **DIO-430-R1**
