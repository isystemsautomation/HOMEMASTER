HomeMaster® OpenTherm Gateway is a DIN-rail interface between OpenTherm-compatible boilers and an ESPHome-based automation system. The ESP32 controller reads boiler diagnostics—flame status, fault codes, return temperature, modulation level—and writes setpoints, domestic hot water demand and related commands on the OpenTherm bus through an opto-isolated transceiver.

Mains or 24 V DC can power the unit. A single SPDT relay with a 3 A @ 250 VAC system rating provides a dry contact for external interlocks or safety chains; loads above the module rating require an external contactor and dedicated protection. Two 1-Wire channels accept DS18B20 temperature probes for monitoring or control logic in ESPHome.

The product is listed under Made for ESPHome and integrates with Home Assistant via ESPHome climate and sensor entities. USB-C supports firmware maintenance. Wi-Fi uses the pre-certified ESP32 radio module on 2.4 GHz. OpenTherm wiring must follow the boiler manufacturer's instructions; the gateway does not replace licensed heating safety equipment.

Commissioning follows the ESPHome adoption flow on the local network. Keep the OpenTherm pair twisted and routed away from mains conductors. If the boiler shares a metal cabinet with other drives, bond shields at one end only. Document the chosen heating curve in your ESPHome configuration rather than relying on boiler defaults alone.

Mount inside a control cabinet on 35 mm DIN rail. Installation and mains wiring are for qualified personnel. The module is CE marked under EMC, LVD and RED with published documentation. Do not use “Works with Home Assistant” branding; integration is through ESPHome only. UL, CSA and FCC are not claimed. User manual included where supplied.
