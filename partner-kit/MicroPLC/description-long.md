HomeMaster® MicroPLC targets smaller installations that need a Modbus master on DIN rail without the full I/O matrix of the MiniPLC. The ESP32-based module provides one dry-contact digital input with IEC 61131-2 front-end conditioning, one relay output, a 1-Wire port for DS18B20 sensors, Wi-Fi, USB-C programming access and a non-isolated RS-485 Modbus RTU interface with surge protection and fail-safe biasing.

The relay contact is functionally normally closed when de-energised; external overcurrent protection is required. RS-485 is protected against transients but not galvanically isolated from logic ground. An external antenna connector improves Wi-Fi coverage inside metal cabinets. Real-time clock hardware is present; battery backup is not fitted at the factory, so time is synchronised from the automation platform.

The device ships with ESPHome firmware and is intended to integrate with Home Assistant via ESPHome. It acts as a Modbus master for HomeMaster expansion modules such as DIO, ENM, WLD and RGB while exposing its own entities to the bus supervisor. Configuration uses ESPHome YAML; USB-C provides firmware updates through the standard ESPHome workflow.

Typical panel layouts combine the MicroPLC on RS-485 with two or three expansion modules in one DIN enclosure. Terminate the bus at both ends when cable length exceeds a few metres and share a common reference conductor between nodes. Keep mains wiring segregated from the SELV fieldbus.

Install only inside an IP20 cabinet with qualified personnel. Use 24 V DC SELV on V+/0V. Do not claim UL, CSA or FCC certification. CE marking applies with a published EU Declaration of Conformity. Hardware sources are open under CERN-OHL-W v2; firmware under MIT. Datasheet and DoC PDFs are included in this pack.
