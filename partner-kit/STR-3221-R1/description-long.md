HomeMaster® STR-3221-R1 is a high-channel LED controller for sequential staircase and walkway lighting. Thirty-two low-side MOSFET outputs switch 12–24 V DC loads with a module total current limit of 18 A at 24 V and 1.5 A per channel. Two presence-sensor inputs and one IEC digital input trigger progressive on/off patterns configurable through WebConfig.

Logic power is 24 V DC on V+/0V with fuse, reverse-polarity and TVS protection. LED power uses a separate supply on dedicated terminals; do not mix logic and LED returns. RS-485 Modbus RTU (default address 21, 115200 baud) reports channel and sensor states; USB-C provides configuration and firmware update access.

Fused 24 V auxiliary rails supply external sensors. Output stages use gate RC networks and ferrites; inductive loads may need external snubbers at the fixture. The module is a Modbus slave for any compliant master, including HomeMaster PLCs with ESPHome modbus integration.

Plan step wiring so return currents share the LED PSU negative and not the logic 0V. Group channels per flight according to the configured scene table in WebConfig. Verify presence sensors before commissioning timed fade profiles.

Allow adequate airflow above the heat sink path of the LED returns. Commission each flight separately so presence direction matches physical traffic flow. Document Modbus register addresses in the integrator handover pack.

Mount on 35 mm DIN rail inside an IP20 enclosure. Size the LED PSU for strip length and wattage per metre. External protection is required on relay or mains circuits if used. CE marked; EU DoC and datasheet included. Open hardware (CERN-OHL-W v2) and published firmware sources.
