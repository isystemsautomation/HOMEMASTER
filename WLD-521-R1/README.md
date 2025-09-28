# WLD-521-R1 – Water Meter & Leak Detection Module

The **WLD-521-R1** is a smart and reliable input/control module designed for **leak detection** and **water flow monitoring** in residential, commercial, and industrial environments. It connects easily to **MicroPLC** or **MiniPLC** systems via **RS-485 (Modbus RTU)** and offers seamless integration with **ESPHome** and **Home Assistant** for advanced automation.

---

## 🔧 Key Features

- **5 Opto-isolated digital inputs**
  - Supports dry contact leak sensors and pulse water meters
  - Configurable for up to 1 kHz input frequency
- **2 Relay outputs (NO/NC)**
  - For valves, pumps, or alarm systems
- **1-Wire interface**
  - Compatible with DS18B20 temperature sensors
- **Galvanically isolated power outputs**
  - 12 V and 5 V for powering external sensors
- **Front panel controls**
  - 4 user buttons and status LEDs
- **USB Type-C**
  - Easy firmware updates and configuration


---

## How heat is computed

1. **Temperatures**
   - Two 1‑Wire sensors: **A = supply (TA)** and **B = return (TB)**.
   - $\Delta T = T_A - T_B\ \text{(°C)}$.

2. **Flow from pulses** *(DI set to “Water counter”)*
   - The input counts pulses from a flow meter.
   - With **PPL = pulses per liter**:
     
     $$
     \text{raw flow}_{\mathrm{L/s}} = \frac{\text{pulses per second}}{\mathrm{PPL}}
     $$
     
   - Two separate calibrations exist in your UI:
     - **Rate calibration** *(Rate ×)*: scales the instantaneous flow *(used for power)*:
       
       $$
       \text{flow}_{\mathrm{L/s}} = \text{raw flow}_{\mathrm{L/s}} \times \mathrm{calRate}
       $$
     
     - **Total calibration** *(Total ×)*: applied to the accumulated liters counter *(for the displayed total volume)*.  
       *(You’ll see both in the Flow box: “Rate” and “Total”.)*

3. **Power (heat rate)**
   - With $\rho$ *(kg/L)* and $c_p$ *(J/kg·°C)*, convert flow to mass flow and multiply by $\Delta T$:
     
     $$
     \dot m\ \text{(kg/s)} = \text{flow}_{\mathrm{L/s}} \times \rho
     $$
     
     $$
     \text{Power (W)} = \dot m \times c_p \times \Delta T
     $$
     
   - Then your **Heat calibration** *(Calibration ×, shown in the Heat box)* is applied to the computed power/energy:
     
     $$
     \text{Power}_{\text{final}} = \text{Power} \times \mathrm{calib}
     $$

4. **Energy**
   - Energy integrates power over time:
     
     $$
     E_J(t+\Delta t) = E_J(t) + \text{Power}_{\text{final}} \times \Delta t
     $$
     
   - The UI shows both:
     - **Energy (J)** — raw joules.
     - **Energy (kWh)** — converted as:
       
       $$
       \mathrm{kWh} = \frac{E_J}{3{,}600{,}000}
       $$

---

### Reset behavior
- **Reset energy** sets the energy accumulator baseline to zero *(doesn’t affect flow totals)*.
- **Reset total** in the *Flow* box moves the volume baseline *(pulses are preserved)*.
- **Calc from external** lets you enter a reference volume since the last total reset; the module derives a new **Total** calibration so the accumulated liters match your reference.

### Defaults & signs
- Defaults shown: $c_p = 4186\ \text{J/kg·°C}$, $\rho = 1.000\ \text{kg/L}$, $\mathrm{Calibration}=1.0000$.
- $\Delta T$ is $A - B$. If return gets hotter than supply, $\Delta T$ goes negative; then power becomes negative. *(If you prefer to clamp to zero, that’s a simple firmware change.)*

### Tiny worked example
- **Flow:** $6.0\ \text{L/min} \rightarrow \frac{6}{60} = 0.1\ \text{L/s}$  
- $\rho = 1.0\ \text{kg/L} \Rightarrow \dot m = 0.1\ \text{kg/s}$  
- $c_p = 4186\ \text{J/kg·°C}$, $\Delta T = 5.0^\circ\text{C}$
  
  $$
  \text{Power} = 0.1 \times 4186 \times 5 = 2093\ \text{W} \approx 2.09\ \text{kW}
  $$
  
- **Over 10 seconds:**  
  
  $$
  \text{Energy} \approx 2093 \times 10 = 20{,}930\ \text{J} \approx 0.0058\ \text{kWh}
  $$
  
- Apply **Calibration ×** at the end if it’s not 1.0.


## 🏠 Example Use Cases

- Automatic valve shutoff on leak detection
- Water consumption monitoring with pulse meters
- Temperature-aware pipe protection logic
- Manual override buttons for maintenance

## What this does (in super simple terms)

- You have **2 irrigation zones** (Zone 1 & Zone 2).  
- Each zone opens a **valve relay** (Relay 1 or Relay 2).  
- A zone can **watch a flow sensor** on one input (**DI1..DI5**) to be sure water actually moves and to **count liters**.  
- Optional **sensors** can **block watering**:
  - **Moisture**: wet = block
  - **Rain**: raining = block
  - **Tank level**: empty = block
- Optional **pump** can run automatically while a zone is watering.
- You can limit watering to a daily **time window** and even **auto‑start** at the window start.
- You can set a **target liters** to stop automatically when that much water has passed.

> This README focuses only on irrigation (the firmware also supports flow, heat energy, 1‑Wire etc.).

---

### Before you start (1 minute)

1. **Wire things**
   - Valve(s) → **Relay 1/2**  
   - Flow sensor → one of **DI1..DI5**  
   - Optional: **moisture / rain / tank** sensors → free DI pins  
   - Optional: **pump** → one of the relays
2. **Power on** the module.
3. Open the **Configuration UI** (the web page for the device) and click **Connect**.

---

### Step 1 — Set the module clock (so windows & auto‑start make sense)

- Go to **“Module Time & Modbus Sync”**.  
- Click **Set from browser time**. Done.

*(Advanced: Home Assistant can send a “midnight pulse” via Modbus; totally optional.)*

---

### Step 2 — Tell the device which input is your flow sensor

- In **“Digital Inputs (5)”**, on the DI that has your **flow meter**, set **Type = Water counter**.  
- Leave regular sensors (moisture/rain/tank) as **Water sensor** (default).

*(Calibration can wait; defaults work.)*

---

### Step 3 — Configure a zone (repeat for both zones if needed)

Open **Irrigation → Zone 1**:

1. **Enable** the zone.
2. **Valve relay** → choose **Relay 1** (or 2), matching your wiring.
3. **Flow DI (1..5)** → pick the DI where your **flow sensor** is connected.
4. Keep **Use flow supervision** **ON** (recommended).  
   - **Min rate (L/min)**: start with **0.20**  
   - **Grace (s)**: **8** (lets pipes pressurize)  
   - **Target liters**: set a number (e.g. **50**). **0** = unlimited  
   - **Timeout (s)**: **3600** (1 hour safety)
5. **Sensors & Pump (optional)**
   - **DI_moist (needs water when OFF)** → pick your soil sensor DI  
     - *Dry (OFF) = watering allowed; Wet (ON) = block*
   - **DI_rain (block when ON)** → pick your rain sensor DI  
     - *Raining (ON) = block*
   - **DI_tank (OK when ON)** → pick your tank level DI  
     - *Tank OK (ON) = allowed; OFF = block*
   - **R_pump** → pick the relay that powers your pump (**(none)** if you don’t use a pump).  
     *The pump runs automatically only while the zone runs.*
6. **Irrigation Window (optional)**
   - **Enforce window** → ON if you only want watering during certain hours
   - **Start** / **End** → e.g. **06:00 → 09:00** (*cross‑midnight works, e.g. 22:00 → 06:00*)
   - **Auto‑start at window open** → ON if you want **daily automatic start** at the **start** time
7. Click **Save Zone 1**. Repeat for Zone 2 if used.

---

### Step 4 — Water!

- **Manual**: Press **Start** on the zone. It will run until you **Stop**, or:
  - **Target liters** is reached, or
  - **Timeout** hits, or
  - a **sensor blocks** it, or
  - the **window closes** (if enforced).
- **Automatic**: If **Auto‑start** is ON, the zone starts **once per day** at the **window start** time.

---

### What “Live Status” tells you

Each zone shows:
- **State**: `idle` / `run` / `pause` / `done` / `fault`  
- **Rate**: L/min (from your flow DI)  
- **Accum**: liters done so far  
- **Elapsed**: seconds since start  
- **Target** & **Timeout** (if set)  
- **Time now**: module clock (HH:MM)  
- **Window**: `OPEN` or `CLOSED`  
- **Sensors**: `OK` or `BLOCK`

If a start is refused, check **Window** and **Sensors** (they reveal the reason).

---

### Suggested starter settings

- **Use flow supervision**: ON  
- **Min rate**: `0.20 L/min`  
- **Grace**: `8 s`  
- **Timeout**: `3600 s`  
- **Target liters**: set what you want (or `0` for unlimited)  
- **Window**: `06:00–09:00` with **Auto‑start** ON (if you want daily watering)

---

### Common actions

- **Start / Stop / Reset** → buttons on each **Zone** card and in **Live Status**  
- **Shared pump for multiple zones** → set **R_pump** to the same relay in each zone; the firmware handles it (pump runs if any zone needs it)  
- **Only water when soil is dry** → set **DI_moist**; Dry (OFF) = allowed, Wet (ON) = block  
- **Skip when raining** → set **DI_rain**; ON = block  
- **Block if tank empty** → set **DI_tank**; ON = ok, OFF = block

---

### Troubleshooting (fast)

- **Won’t start** → check the zone is **Enabled**, **Window = OPEN**, **Sensors = OK**  
- **Stops with “low flow”** → increase **Grace**, lower **Min rate** a little, or check flow wiring  
- **Stops immediately** → **Target liters** is tiny, or a **sensor** is blocking  
- **Pump doesn’t run** → set **R_pump** to your pump’s relay and make sure that relay is **Enabled**  
- **Time looks wrong** → click **Set from browser time** again

---

### Optional: Midnight sync from Home Assistant (Modbus)

If you want the module clock to reset at midnight (00:00) via Modbus:

- Write `TRUE` then immediately `FALSE` to **coil 360** at 00:00 (daily).  
  The firmware will set minute‑of‑day to `0` and increment day‑index.

> This is optional. Most users can ignore it and just use “Set from browser time”.

---

### That’s it ✅

Use **Save** after changes, **Start** to test, watch **Rate** and **Accum** increase, and adjust **Target liters** and **Window** to suit your garden.


## ⚙️ Specifications

| Parameter                         | Value                              |
|----------------------------------|------------------------------------|
| Power Supply                     | 24 V DC                            |
| Digital Inputs                   | 5 opto-isolated, dry contact       |
| Input Voltage (trigger)          | ~14 V                              |
| Input Current                    | ~6 mA                              |
| Input Frequency                  | Up to 9 Hz (1 kHz configurable)    |
| Relay Outputs                    | 2 (NO/NC, max 3 A @ 250 V AC)      |
| 1-Wire Bus                       | Yes                                |
| Sensor Power Output              | 12 V / 5 V isolated (up to 50 mA)  |
| Communication Interface          | RS-485 (Modbus RTU)                |
| Dimensions                       | DIN-rail mount, 3 modules wide     |
| Firmware Update                  | USB Type-C                         |

## 📄 License

All hardware design files and documentation are licensed under **CERN-OHL-W 2.0**.  
Firmware and code samples are released under the **GNU General Public License v3 (GPLv3)** unless otherwise noted.

---

> 🔧 **HOMEMASTER – Modular control. Custom logic.**
