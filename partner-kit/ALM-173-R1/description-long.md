HomeMaster® ALM-173-R1 concentrates security and status wiring into one Modbus RTU slave for DIN-rail panels. Seventeen opto-isolated digital inputs accept door contacts, window sensors, PIR detectors and similar dry closures. Three SPDT relay outputs provide a 3 A @ 250 VAC system rating per channel for sirens, indicators or automation interlocks; external fusing is required.

Each zone can be enabled, inverted and assigned to groups in the onboard configuration stored in flash. Auxiliary +12 V and +5 V sensor rails are fuse-protected for small external devices. RS-485 communication includes TVS diodes, PTC current limiters, a common-mode choke and fail-safe biasing; the transceiver is not galvanically isolated. USB-C WebConfig configures debounce, Modbus address and baud rate without a separate tool chain.

The module operates as a Modbus RTU slave (default address 3, 19200 baud). It pairs with HomeMaster MiniPLC or MicroPLC controllers—or any third-party Modbus master—and exposes zone and relay states on standard function codes. Integrators using Home Assistant typically map registers through ESPHome modbus_controller packages on the bus master.

Zone wiring uses the published GND reference on the input block; do not apply external voltage to inputs. Size sensor rail loads within the stated 150 mA and 200 mA limits. Label each zone in WebConfig before sealing the cabinet so field service matches the register map.

Install inside a ventilated cabinet. Use 24 V DC SELV on V+/0V. Qualified personnel only. CE marked with EU Declaration of Conformity supplied. Hardware is open-source under CERN-OHL-W v2; firmware under MIT for the published release.
