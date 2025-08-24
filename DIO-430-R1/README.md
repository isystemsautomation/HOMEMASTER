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
| [`DIM-430-R1_Default_Firmware.uf2`](https://github.com/isystemsautomation/HOMEMASTER/tree/main/DIO-430-R1/Firmware/default_DIO_430_R1/build/rp2040.rp2040.generic_rp2350) | Default preloaded firmware with Web Config Tool support |
| [`default_DIO_430_R1.ino`](https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIO-430-R1/Firmware/default_DIO_430_R1/default_DIO_430_R1.ino) | Arduino IDE source code |
| [`ConfigToolPage.html`](https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIO-430-R1/Firmware/ConfigToolPage.html)) | Code of Web Config Tool HTML/JS |

---

# DIO-430-R1 — Firmware & Module Guide

**MCU:** RP2350A | **I/O:** 4× DI, 3× Relays, 3× Buttons, 3× LEDs  
**Interfaces:** RS-485/Modbus RTU (UART2), USB Web Serial  
**Persistence:** LittleFS (`/cfg.bin`) with guarded format + CRC

---

## Contents
- [Overview](#overview)
- [Features](#features)
- [Hardware & Pinout](#hardware--pinout)
- [Power-Up & Defaults](#power-up--defaults)
- [Connect to the Module (Web Serial)](#connect-to-the-module-web-serial)
- [Web Config UI — Panels & Fields](#web-config-ui--panels--fields)
- [I/O Logic & Modes](#io-logic--modes)
- [Modbus RTU Mapping](#modbus-rtu-mapping)
- [Save/Load/Factory/Reset](#saveloadfactoryreset)
- [Persistence (LittleFS) Details](#persistence-littlefs-details)
- [Quick Recipes](#quick-recipes)
- [Troubleshooting](#troubleshooting)
- [Build & Flash Notes](#build--flash-notes)
- [Appendix — Persisted Struct & CRC](#appendix--persisted-struct--crc)
- [License](#license)

---

## Overview
DIO-430-R1 is a compact Modbus RTU I/O module with 4 digital inputs, 3 relay outputs, 3 local buttons, and 3 user LEDs.  
It can be configured over **USB/Web Serial** (Chrome/Edge) and controlled/monitored via **Modbus RTU** over RS-485.

---

## Features
- 4× **Digital Inputs** (enable/invert per-channel; action + target mapping)
- 3× **Relays** (enable/invert; latched logic)
- 3× **Buttons** (per-button “override toggle” of relays)
- 3× **User LEDs** (steady/blink; source follows overridden relay state)
- **Modbus RTU** (address/baud persisted)
- **LittleFS** persistence with guarded format & CRC verification
- **Auto-save** after 1.5 s of inactivity
- **Web Serial** UI for configuration and status

---

## Hardware & Pinout

| Block | Count | MCU Pins (GPIO) | Polarity / Notes |
|---|---:|---|---|
| Digital Inputs | 4 | IN1..IN4 = `6, 11, 12, 7` | **Active-HIGH** at pin. “Inverted” flips logic after read. |
| Relays | 3 | R1..R3 = `10, 9, 8` | **Active-HIGH** drive. “Inverted” flips physical output. |
| Buttons | 3 | B1..B3 = `2, 3, 1` | **Active-LOW** (internal pull-ups). Rising edge = press. |
| User LEDs | 3 | LED1..LED3 = `13, 14, 15` | **Active-HIGH**. |
| RS-485 (UART2) | — | TX2=`4`, RX2=`5` | TXEN not used (`-1`). |

> RS-485: ensure correct A/B wiring, proper termination, and common GND as required.

---

## Power-Up & Defaults
On boot the firmware:
1. Initializes GPIOs (relays/LEDs OFF).
2. Mounts **LittleFS** and attempts to **load `/cfg.bin`**.  
   - If invalid/missing → **factory defaults** → attempt **save**.  
   - If save fails → **format FS once** → retry save (guarded format).
3. Starts **Modbus RTU** using persisted address/baud.

**Factory defaults**
- Modbus: Address **3**, Baud **19200**
- DIs: Enabled, not inverted, **Action=None**, **Target=All**
- Relays: Enabled, not inverted, desired OFF
- Buttons: **Action=None**
- LEDs: **Mode=Steady**, **Source=None**

---

## Connect to the Module (Web Serial)

### Requirements
- **Chrome** or **Edge** (desktop) — Web Serial API
- The provided **Web Config** HTML (includes `simple-web-serial`)

### Procedure
1. Open the Web Config page in Chrome/Edge.  
2. Click **Connect** and select the module’s serial port.  
3. Watch **Serial Log** messages (“Boot OK”, “Config loaded…”, etc.).  
4. The **Active Modbus Configuration** banner shows current Address/Baud.  
5. Adjust settings in the panels; changes apply immediately.

**Reset from Web:** Click **Reset Device** → confirm. The device saves config, reboots (watchdog), and the port reconnects.

---

## Web Config UI — Panels & Fields

### Modbus Configuration
- **Address:** 1…255
- **Baud:** 9600 / 19200 / 38400 / 57600 / 115200

### Digital Inputs (4)
- **Enabled:** include DI in logic & telemetry  
- **Inverted:** invert logic after read  
- **Action:**  
  - **None (0)** — no effect  
  - **Toggle (1)** — **toggle** target relay(s) on **any edge** (rising _or_ falling)  
  - **Pulse (2)** — **toggle** target relay(s) on **rising edge only**  
- **Control target:** **None (4)**, **All (0)**, or **Relay 1..3 (1..3)**

### Relays (3)
- **Enabled:** if false, output forced OFF  
- **Inverted:** invert physical drive

### Buttons (3)
- **Action:**  
  - **None (0)**  
  - **Relay 1/2/3 override (toggle)** → values **5/6/7**  
    (On **rising edge** of a button press, toggles that relay)

### User LEDs (3)
- **Mode:** **Steady** or **Blink** (blink only when source is active)  
- **Source:** **None (0)** or **Overridden Relay 1..3 (5..7)**  
  (LED follows the **logical** relay state; in Blink mode it flashes when active)

---

## I/O Logic & Modes

### Digital Inputs → Relays
- **Toggle (1):** toggle target relay(s) on **any edge** (rising or falling)  
  _Use for maintained/rocker switches; every change toggles._
- **Pulse (2):** toggle target relay(s) on **rising edge only**  
  _Use for momentary pulses; each pulse toggles once._

### Buttons → Relays
- **Action 5/6/7:** on **rising edge** (press), toggle the selected relay  
- Buttons act directly; they do not use DI action/target mappings

### Relays → Physical Outputs
- Internal **desired state** derives from DI, Button, and Modbus commands  
- If **Enabled=false** → output forced OFF  
- If **Inverted=true** → physical drive inverted

### LEDs
- **Steady:** LED = source active  
- **Blink:** LED = source active **AND** blink phase

---

## Modbus RTU Mapping

**Serial:** UART2 (TX=4, RX=5), default 8N1. Baud/address are persisted.

### Discrete Inputs (FC=02)
| Address | Meaning |
|---:|---|
| **1..4** | DI1..DI4 **logical state** (after enable+invert) |
| **60..62** | Relay1..3 **logical output state** |
| **90..92** | LED1..3 **physical state** |

### Coils (FC=05/15)
| Address | Action |
|---:|---|
| **200..202** | **Relay1..3 ON** (latched set) |
| **210..212** | **Relay1..3 OFF** (latched reset) |
| **300..303** | **Enable DI1..DI4** |
| **320..323** | **Disable DI1..DI4** |

> Coils act like **pulses**: firmware consumes them, applies the change, then clears the coil.

---

## Save/Load/Factory/Reset
- Config changes mark state **dirty**; after **1.5 s** idle it **auto-saves** to `/cfg.bin`
- **Commands** via Web Serial:  
  - `save` — force save now  
  - `load` — reload `/cfg.bin` (re-echo state, apply Modbus)  
  - `factory` — restore defaults **and save**  
  - `reset` / `reboot` — save (with verify log) then reboot via watchdog

---

## Persistence (LittleFS) Details
- File: **`/cfg.bin`** — header (magic, version, size), payload, **CRC32**  
- Boot sequence:  
  1. `LittleFS.begin()`; if fail → **format** → retry  
  2. Try **load**; if invalid → **defaults** → **save**  
  3. If save fails → **format once** → retry save (**guarded format**)  
- All steps are logged to Web Serial

> If you see **“save: open failed”** or **“ERROR: Save failed”**: ensure your board build has a **LittleFS partition** (Arduino IDE → **Tools → Flash Size / Filesystem**).

---

## Quick Recipes

**Maintained switch toggles Relay 1 on each flip**  
- INx: **Action = Toggle (1)**, **Target = Relay 1**

**Push-button pulse toggles Relay 2**  
- INx: **Action = Pulse (2)**, **Target = Relay 2**

**Front-panel button toggles Relay 3**  
- Button 3: **Action = Relay 3 override (toggle)** (value **7**)

**LED blinks when Relay 2 is ON**  
- LEDk: **Mode = Blink**, **Source = Overridden Relay 2** (value **6**)

**Force Relay 1 ON via Modbus**  
- Write coil **200** = TRUE (auto-clears); verify at discrete input **60**

---

## Troubleshooting

**Cannot save config (“save: open failed” / “ERROR: Save failed”)**  
- Ensure a **LittleFS partition** is selected (Arduino IDE → Tools → Flash Size / Filesystem)  
- Try **factory** then **save** from Web UI  
- Power-cycle; the firmware will **format** and retry once (see Serial Log)  
- Hardware: verify external flash **WP# / HOLD#** pull-ups if applicable

**Web page doesn’t see the device**  
- Use **Chrome/Edge** (desktop), click **Connect** and pick the right port  
- After reset, wait a few seconds for USB re-enumeration

**No Modbus comms**  
- Verify **Address** & **Baud** on master and device  
- Check RS-485 A/B wiring and termination  
- UART2 is used: **TX=4**, **RX=5**, no TXEN

**Relays not switching**  
- Ensure **Relay Enabled** is ON  
- If **Inverted**, physical ON/OFF appears reversed  
- Read relay state at 60..62 to confirm logic

**DI/Buttons not reacting**  
- DI must be **Enabled**; set **Inverted** if wiring needs it  
- Remember: **Toggle** reacts on **any edge**; **Pulse** on **rising edge** only  
- Buttons (5/6/7) toggle on **rising edge**

---

## Build & Flash Notes
- **Board core:** RP22350A (e.g., Earle Philhower or vendor core)  
- **Arduino IDE → Tools → Flash Size / Filesystem:** choose a layout **with LittleFS** (e.g., “Sketch 1 MB / LittleFS 1 MB”)  
- **Required Libraries:**
  - `ModbusSerial`
  - `SimpleWebSerial`
  - `Arduino_JSON`
  - `LittleFS` (provided by the RP2040 core)
- **Serial (Modbus):** UART2, default 8N1  
- **Tips:**  
  - Use `FILE_READ` / `FILE_WRITE` modes with `LittleFS.open()`  
  - If you change the persisted struct, **bump `CFG_VERSION`** and handle migration

---

## Appendix — Persisted Struct & CRC

- **Magic:** `0x314D4C41` (`'ALM1'`)  
- **Version:** `0x0007` (includes LED `source`)  
- **Path:** `/cfg.bin`  
- **CRC32 polynomial:** `0xEDB88320` over the full packed struct with `crc32=0` during calculation

```c
typedef struct __attribute__((packed)) {
  uint32_t magic;       // 0x314D4C41 ('ALM1')
  uint16_t version;     // 0x0007
  uint16_t size;        // sizeof(PersistConfig)
  InCfg    diCfg[4];
  RlyCfg   rlyCfg[3];
  LedCfg   ledCfg[3];
  BtnCfg   btnCfg[3];
  bool     desiredRelay[3];
  uint8_t  mb_address;
  uint32_t mb_baud;
  uint32_t crc32;       // CRC32 of the whole struct with this field set to 0
} PersistConfig;


## 📐 Hardware Schematics

- 📎 **[DIO-430-R1-FieldBoard.pdf)](https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIO-430-R1/Schematics/DIO-430-R1-FieldBoard.pdf)**
- 📎 **[DIO-430-R1-MCUBoard.pdf](https://github.com/isystemsautomation/HOMEMASTER/blob/main/DIO-430-R1/Schematics/DIO-430-R1-MCUBoard.pdf)**

---

## 🛠 Open Source & Re-Programming

The **DIM-430-R1** firmware and hardware design are fully open source.  
You can reprogram the **RP2350** MCU using:

- **Arduino IDE (C/C++)**
- **PlatformIO**
- **MicroPython**


> Tip: For Arduino/PlatformIO, select the appropriate RP2350/RP2040-compatible board profile.  
> Flash via USB-C (UF2 drag-and-drop) or serial bootloader.

---

## 📄 License

- **Hardware:** [CERN-OHL-W 2.0](https://ohwr.org/cern_ohl_w_v2.txt)
- **Firmware:** [GNU GPLv3](https://www.gnu.org/licenses/gpl-3.0.en.html)

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
