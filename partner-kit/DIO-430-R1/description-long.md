HomeMaster® DIO-430-R1 is a relay and input expansion module for Modbus RTU installations. Three dry-contact relay outputs carry a 3 A @ 250 VAC system rating (750 VA max); contactors and external fuses are required for heavier or inductive loads. Four digital inputs use an ISO1212 IEC 61131-2 front-end for dry contacts with per-channel PTC, TVS and reverse-polarity protection.

Press, double-press, triple-press and hold gestures are counted on the module and remain available even when the bus master polls slowly—a practical advantage for wall switches mapped to automation scenes. Output interlock logic can be configured in WebConfig. RS-485 includes surge protection; isolation from remote ground is not provided.

The RP2350 firmware stores settings in LittleFS. USB-C WebConfig configures Modbus address, baud, input modes and relay behaviour without recompiling firmware. Default address is 3 at 19200 baud.

Front-panel buttons mirror selected input functions for bench testing. Wire dry contacts between each input terminal and the module GND reference; do not inject external 24 V into the input. Provide snubbers or RC networks on inductive relay loads according to your national wiring rules.

Test gesture counters with the actual wall switches before enabling automation scenes. Verify interlock masks if multiple relays share a load path.

Use 24 V DC SELV supply. Mount in a ventilated DIN-rail enclosure. Suitable for pumps, boilers, valves and lighting contactors when wired by qualified personnel. CE marked; EU DoC and module datasheet supplied. Integrate through any Modbus master, commonly HomeMaster PLCs with ESPHome. Open hardware files and published firmware are available from the manufacturer repository.
