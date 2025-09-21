# ENM-223-R1 — 3‑Phase Power Quality & Energy Metering Module

> **HOMEMASTER – Modular control. Custom logic.**

The **ENM‑223‑R1** is a high‑precision, compact metering module designed for seamless integration with **HomeMaster MicroPLC** and **MiniPLC** systems. It provides real‑time monitoring of 3‑phase electrical parameters, supports load control with **2× SPDT relays**, and is ideal for energy management, automation, and smart‑building applications.

---


## 📑 Table of Contents

1. Introduction  
   1.1. Overview of the HOMEMASTER Ecosystem  
   1.2. Supported Modules & Controllers  
   1.3. Use Cases  

2. Safety Information  
   2.1. General Electrical Safety  
   2.2. Handling & Installation  
   2.3. Device-Specific Warnings  

3. System Overview  
   3.1. Architecture & Modular Design  
   3.2. MicroPLC vs MiniPLC  
   3.3. Integration with Home Assistant  

4. Getting Started  
   4.1. What You Need  
   4.2. Quick Setup Checklist  

5. Powering the Devices  
   5.1. Power Supply Types  
   5.2. Current Consumption  
   5.3. Power Safety Tips  

6. Networking & Communication  
   6.1. RS-485 Modbus  
   6.2. USB-C Configuration  

7. Installation & Wiring  
   7.1. ENM-223-R1 Wiring  

8. Software Configuration  
   8.1. Web Config Tool (USB Web Serial)  
   8.2. ESPHome / Home Assistant  

9. Modbus RTU Communication  
   9.1. Basics & Function Codes  
   9.2. Register Map (Summary)  
   9.3. Override Priority  

9.5. Detailed Configuration (UI & Firmware)  
   9.5.1. Meter Options & Calibration  
   9.5.2. Alarms  
   9.5.3. Relays & Overrides  
   9.5.4. Buttons  
   9.5.5. User LEDs  
   9.5.6. Energies  
   9.5.7. Live Meter  

10. Programming & Customization  
    10.1. Supported Languages  
    10.2. Flashing via USB-C  
    10.3. PlatformIO & Arduino  

11. Diagrams & Pinouts  

12. Maintenance & Troubleshooting  

13. Technical Specifications  

14. Open Source & Licensing  

15. Downloads  

16. Support & Contact

---


## 1. Introduction

The **ENM‑223‑R1** is a compact, high‑precision **3‑phase power quality & energy metering module** built around the **ATM90E32AS** metering IC. It integrates neatly with **HOMEMASTER MicroPLC** and **MiniPLC** controllers for real‑time monitoring, automation, and energy optimization.

### Overview of the HOMEMASTER Ecosystem
HOMEMASTER provides modular DIN‑rail controllers and I/O modules that interconnect via RS‑485 and 24 V DC. The ENM‑223‑R1 adds detailed power analytics and two relays for on‑board control.

### Supported Modules & Controllers
- **Controllers:** MicroPLC, MiniPLC
- **Companion I/O:** DIO, DIM, AIO, ALM series

### Use Cases
- Sub‑metering for tenants/circuits
- Power monitoring in data centers/industrial panels
- Load shedding & demand response (via relays)
- Preventive maintenance via power‑quality KPIs

---

## 2. Safety Information

### General Electrical Safety
- Installation and service by qualified personnel only.
- De‑energize equipment and follow lockout/tagout procedures.
- Treat all terminals as **live** until verified safe.

### Handling & Installation
- Mount on **35 mm DIN rail** in a suitable enclosure.
- Separate low‑voltage wiring from mains where possible.
- Use ferrules; torque terminals to spec.

### Device-Specific Warnings
- Connect **PE** (protective earth) and **N** (neutral) per wiring diagrams. Proper PE bonding improves safety and measurement stability.
- Use **split‑core CTs** with **1 V or 333 mV RMS** secondary (or intermediate CTs). **Do not** connect 5 A secondary CTs directly.
- Ensure **CT orientation** (arrow → **load**). If reversed, either flip the CT or enable **Invert** in configuration.

---

## 3. System Overview

### Architecture & Modular Design
- **Metering IC:** ATM90E32AS (3×U, 3×I)
- **Measurements:** Urms/Upeak, Irms/Ipeak, P/Q/S/N, PF, phase angle, frequency (per phase & totals)
- **Control:** 2 × SPDT relays (NO/NC)
- **Indicators:** Status LED(s); CF pulse LED (1000 pulses = 1 kWh)

### MicroPLC vs MiniPLC
- **MicroPLC:** Higher I/O density, RS‑485 master, rule engine—ideal for multi‑module racks.  
- **MiniPLC:** Compact controller—suitable for smaller panels.

### Integration with Home Assistant
Connect ENM‑223‑R1 to a MicroPLC/MiniPLC that exposes data to Home Assistant (Modbus/ESPHome). Create automations using real‑time energy metrics and events.

---

## 4. Getting Started

### What You Need
- ENM‑223‑R1 module
- 24 V DC power supply
- RS‑485 wiring (A/B; shared **GND** recommended if separate PSUs)
- 3 split‑core CTs (1 V or 333 mV)
- DIN enclosure, **PE** and **N** available
- USB‑C cable (for config/firmware)

### Quick Setup Checklist
1. Mount ENM‑223‑R1 on DIN rail.  
2. Wire **PE** and **N** (high‑voltage block).  
3. Wire **L1/L2/L3** (through appropriate fuses/breakers).  
4. Install CTs on phase conductors (arrows → load) and land secondaries on CT inputs (+/−).  
5. Power interface side with **24 V DC** (**V+**, **GND**).  
6. Connect **RS‑485 A/B**; tie **GNDs** if devices use different supplies.  
7. Set Modbus address & baud (USB‑C config).  
8. Verify readings and **phase mapping**; correct CT inversion if needed.

---

## 5. Powering the Devices

### Power Supply Types
- **Interface power:** 24 V DC to **V+/GND**.  
- **Measurement side:** derived from phase inputs; energy counters are retained across power cycles.

### Current Consumption
- Typical interface power < **1 W** (planning figure).

### Power Safety Tips
- Always connect **PE**. Without PE, fault conditions can induce dangerous potentials on CT wiring.  
- Use separate breakers for measurement voltage taps.

---

## 6. Networking & Communication

### RS-485 Modbus
- **Default:** `9600 8N2` (configurable).  
- **Address range:** 1–247.  
- Standard Modbus RTU function codes supported for input/holding registers; relays via coils/registers.

### USB-C Configuration
Use USB‑C for initial configuration, firmware updates, and diagnostics via the **Web Serial Config Tool**.

---

## 7. Installation & Wiring

### ENM-223-R1 Wiring
- **Phases:** Connect **L1, L2, L3, N, PE** in the correct order.  
- **Single‑phase use:** Populate **L1** only; tie unused **Lx** to **N** to avoid induced phantom readings.  
- **CT leads:** Keep short or use shielded cable; observe CT loop resistance limits.  
- **Verification:** With ~100 W per phase, check PF/angles (≈ −40°…+40° for resistive loads). If angles are large or **P < 0**, remap phases or invert CTs.

---

## 8. Software Configuration

### Web Config Tool (USB Web Serial)
The included **Config Tool** (HTML) communicates over Web Serial in Chromium‑based browsers. It lets you:
- Read live measurements  
- Set **Modbus address/baud**  
- Configure **phase mapping** (assign Ux to Ix)  
- Enter **CT parameters** (turns/ratio, phase shift), **Invert** flags  
- Test **relay outputs** and LEDs

> Open `tools/ConfigToolPage.html` (or serve it locally), connect via USB‑C, choose the ENM serial port, and follow the on‑screen steps.

### ESPHome/Home Assistant
Expose ENM registers via your controller (ESPHome/Modbus). Create sensors for **Urms, Irms, P, PF, Frequency, Energy** and switches for **Relay 1/2**.

---

## 9. Modbus RTU Communication

### Basics & Function Codes
- **Physical:** RS‑485 half‑duplex, multi‑drop, termination at both ends.  
- **Function codes:** `0x03` Read Holding, `0x04` Read Input, `0x06` Write Single, `0x10` Write Multiple, `0x01/0x05/0x0F` for coils (if exposed).

### Register Map (Summary)
> Exact addresses depend on firmware build and will be published in `/docs/registers_enm_223_r1.md`.

- **Identification:** Model, FW version  
- **Comms:** Modbus address, baud, parity/stop  
- **Per Phase:** Urms, Upeak, Irms, Ipeak, P, Q, S, N, PF, PhaseAngle  
- **Totals:** P_total, Q_total, S_total, PF_total, Frequency  
- **Energies:** Active/Reactive/Apparent/Non‑active — Import/Export  
- **CT Config:** Turns_Lx, Phi_Lx, Invert_Lx  
- **Phase Mapping:** ActualPhase_Lx (map Ix ↔ Ux)  
- **Relays:** R1/R2 state, override, pulse width  
- **Diagnostics:** Status, alarms, counters

### Override Priority
1. Safety lock (if enabled)  
2. Manual override (front button)  
3. Modbus command  
4. PLC/HA automations

---


---

## 9.5. Detailed Configuration (UI & Firmware)

> This section is generated for the **ENM‑223‑R1 (2025‑09 firmware snapshot)** and documents what you see in the included **Web Serial Config UI** and what the firmware actually does behind the scenes.

### Meter Options & Calibration
**Where in UI:** *Meter Options* and *Calibration (Phase A/B/C)* cards.  
**Firmware:** pushed every 1 s via `MeterOptions`/`CalibCfg` messages; persisted to **LittleFS** with CRC32.

#### Meter Options
- **Line Frequency (Hz)** — `50` or `60` → applies to ATM90E32 `MMode0` and protection thresholds.  
- **Sum Mode** *(0=algorithmic, 1=absolute)* — affects totalization inside the metering IC.  
- **Ucal (gain)** — base voltage gain used for sag threshold/voltage scaling.  
- **Sample Interval (ms)** — main sampling/publish period (10…5000 ms).  
**How to change:** Edit a field and press **Enter**. The field shows an "editing…" badge and is *locked* until you press Enter or Esc.  
**Persistence:** Changes are auto‑saved ~1.2 s after the last touch. Re‑initialization of the chip occurs automatically when needed (line freq / sum mode changes).

#### Calibration (per phase A/B/C)
- **Ugain/Igain** — 16‑bit unsigned (0…65535).  
- **Uoffset/Ioffset** — 16‑bit signed (−32768…32767).  
**Workflow:** Enter values for each phase and press **Enter** → firmware writes to the ATM90E32 (`UgainX/IgainX/UoffsetX/IoffsetX`) under config‑access, then exits config mode and saves to flash.

> Tip: Perform calibration with stable loads. Verify PF and angles after applying calibration.

### Alarms
**Where in UI:** *Alarms (L1, L2, L3, Totals)* grid. Each channel has **Alarm**, **Warning**, **Event** rows and an **Ack required** toggle.  
**Firmware:** `AlarmsCfg` (config), `AlarmsState` (runtime). Evaluated against integer engineering units, not raw counts.

#### Metrics & Units
Choose a **Metric** per rule (drop‑down), and enter **min/max** thresholds in the indicated units:
- `Voltage (Urms)` — **0.01 V** units (e.g., 230.00 V → enter 23000)  
- `Current (Irms)` — **0.001 A** units (e.g., 5.000 A → 5000)  
- `Active power P` — **W**  
- `Reactive power Q` — **var** (sign follows phase angle)  
- `Apparent power S` — **VA**  
- `Frequency` — **0.01 Hz** units (e.g., 50.00 Hz → 5000; channel selection is ignored)

**Enable** a rule to make it effective. When **Ack required** is enabled for a channel, an out‑of‑band condition **latches** until acknowledged; otherwise it auto‑clears when back in range.

**Acknowledge:** Click **Ack [channel]** or use **Ack All** in the UI. Over Modbus, coils `610..613` acknowledge channels L1/L2/L3/Totals.

### Relays & Overrides
**Where in UI:** *Relays (2)*.  
**Firmware:** `RelaysCfg` (config) and `RelaysSet` (actions). Two logical layers protect the outputs: **Mode** and **Button Override**.

- **Enabled at power‑on** (per relay) — default state after boot.  
- **Inverted (active‑low)** — single polarity setting that applies to **both relays** in hardware.  
- **Mode:**  
  - **None** — firmware ignores external writes; relay stays at its internal state/defaults.  
  - **Modbus Controlled** — coil writes (`600` for R1, `601` for R2) are honored **unless** an override is active.  
  - **Alarm Controlled** — relay follows selected **Channel** (L1/L2/L3/Totals) and **Kinds** (Alarm/Warning/Event bit‑mask). Direct writes are **blocked** in this mode.
- **Toggle button** in UI sends `RelaysSet { idx, toggle:true }` (honored only if Mode ≠ Alarm and no override).

**Override with Button (per‑relay):**  
Assign a button action **“Override Relay 1 (hold 3s)”** or **“… Relay 2 (hold 3s)”**.  
- **Hold 3 s** → enters/leaves override mode for that relay (captures current state).  
- While in override, **short‑press** of the same button toggles the relay state.  
- Exiting override hands control back to **Modbus** / **Alarm** according to the configured **Mode**.

### Buttons
**Where in UI:** *Buttons (4)*.  
**Hardware:** GPIO22…25, debounced in firmware; long‑press threshold = **3 s**.  
**Actions:**  
`0 None` • `1 Toggle R1` • `2 Toggle R2` • `3 Toggle LED1` • `4 Toggle LED2` • `5 Toggle LED3` • `6 Toggle LED4` • `7 Override R1 (hold 3s)` • `8 Override R2 (hold 3s)`

> **Boot/Reset combinations:** Not implemented in this firmware snapshot. Factory‑reset and boot‑key combos will be documented once available. Use firmware flashing to restore defaults if required.

### User LEDs
**Where in UI:** *User LEDs (4)*.  
- **Mode:** `Steady` or `Blink` *(when active)*.  
- **Source:** Select what drives each LED — override state indicators and alarm sources are available:  
  - `Override R1`, `Override R2`  
  - `Alarm/Warning/Event` for L1/L2/L3/Totals  
  - `Any (A|W|E)` for L1/L2/L3/Totals  
When the chosen **Source** is active, the LED is ON (or blinks if **Mode=Blink**). You can also toggle LEDs manually via **button actions 3–6**; manual toggles layer on top of source logic.

### Energies
**UI:** *Energies* cards show **k‑units** from the metering IC: per‑phase (A/B/C) and **Totals**:  
- **Active + (kWh)** import, **Active − (kWh)** export  
- **Reactive + (kvarh)** inductive, **Reactive − (kvarh)** capacitive  
- **Apparent (kVAh)**

**Modbus:** 32‑bit **Wh/varh/VAh** pairs (Hi/Lo words) for A/B/C/Totals. Values are derived from ATM90E32 counters using the firmware’s internal scaling factors and published at the main sampling rate.

### Live Meter
**UI:** *Live Meter* shows at 1 Hz (while connected):  
- **L1/L2/L3 cards:** `U (V)`, `I (A)`, `P (W)`, `Q (var)`, `S (VA)`, `PF`, `Angle (°)`  
- **Totals:** `P/Q/S`, `PF (tot)`, `Freq (Hz)`, `Temp (°C)`  
While you are typing in a field elsewhere, that field pauses auto‑refresh (field lock). The **Serial Log** captures every command and echo for traceability.


## 10. Programming & Customization

### Supported Languages
- **MicroPython** (pre‑installed)  
- **C/C++**  
- **Arduino IDE**

### Flashing via USB-C
1. Connect USB‑C.  
2. Enter boot/flash mode if required.  
3. Upload the provided firmware/source via your preferred toolchain.

### PlatformIO & Arduino
- Select the appropriate board profile.  
- Add ATM90E32 and Modbus libraries.  
- Match serial/RS‑485 settings to your network.

---

## 11. Diagrams & Pinouts

```
L1/L2/L3 → Attenuation → ATM90E32AS ← CT1/2/3
                          │
                          ├─ PLC/MCU ↔ RS‑485 (A/B/GND)
                          ├─ Relays (NO/COM/NC ×2)
                          └─ LEDs/Buttons
```

**Terminals**  
- Voltage: **PE, N, L1, L2, L3**  
- Interface: **V+, GND, A, B (RS‑485)**  
- CT: **CT1+/−, CT2+/−, CT3+/−** (white/red = **+**, black = **−**)

---

## 12. Maintenance & Troubleshooting

- **Negative active power:** CT reversed → flip or set *Invert*.  
- **Large phase angle (>100°):** CT on wrong phase → fix mapping.  
- **Phantom voltage on unused phases:** Tie unused **Lx** to **N** (single‑phase).  
- **RS‑485 timeouts/noise:** Check termination, biasing, common GND reference.

Reset & Factory Restore: write the reset register or use the button sequence (TBD). Energy counters are retained across power cycles.

---

## 13. Technical Specifications

| Parameter             | Value                              |
|----------------------|------------------------------------|
| Voltage Inputs       | 3‑phase, **85–265 VAC**            |
| Current Inputs       | External CTs (**1 V** or **333 mV** RMS) |
| Relay Outputs        | **2× SPDT (NO/NC)**, up to **5 A** |
| Communication        | **RS‑485 (Modbus RTU)**            |
| Programming          | MicroPython / C++ / Arduino IDE    |
| Power Supply         | **24 V DC** (interface)            |
| Mounting             | **DIN rail**                       |
| Accuracy             | Active **0.5S**, Reactive **1** (typ.) |
| Indicators           | Status, **CF** (1000 pulses = 1 kWh) |

> Measurement metrics include RMS & peak U/I; P, Q, S, N; PF; phase angles; frequency; per‑phase and totals.

---

## 14. Open Source & Licensing

- **Hardware:** **CERN‑OHL‑W 2.0**  
- **Firmware & code samples:** **GPLv3** (unless otherwise noted)

---

## 15. Downloads

- **Config Tool (HTML):** `tools/ConfigToolPage.html`  
- **Default Arduino/firmware example:** `firmware/default_enm_223_r1.ino`  
- **(Coming soon) Register table:** `docs/registers_enm_223_r1.md`

> In this repository snapshot, the two core files are included for convenience:
> - `/tools/ConfigToolPage.html`  
> - `/firmware/default_enm_223_r1.ino`

---

## 16. Support & Contact

**ISYSTEMS AUTOMATION SRL**  
• GitHub Issues: use the Issues tab of this repo  
• Website: *TBD*

---

### Attribution
Parts of the best‑practice wiring, CT orientation, and phase mapping guidance are inspired by established industry practice for three‑phase sub‑metering devices.

