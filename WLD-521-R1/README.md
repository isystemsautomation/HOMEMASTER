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

---



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
