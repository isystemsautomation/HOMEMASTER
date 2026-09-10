# Public claims notice (partner resellers)

Extract from `business/marketing/claims.md`. When in doubt, the module README in
this repository is the authoritative product description.

## Allowed

- **CE marking** with reference to the supplied EU Declaration of Conformity.
- **Made for ESPHome** — only MiniPLC and OpenTherm Gateway (official ESPHome registry).
- **Integrates with Home Assistant via ESPHome** for ESPHome-based controllers and gateways.
- **Open hardware / open firmware** — specify CERN-OHL-W v2 for hardware and MIT for
  published firmware; do not say “fully open source” without both licences.
- **Distributed local logic** — DIO, DIM, WLD, ENM and similar modules can execute
  configured rules on-board; do not claim that all automation runs only in modules.
- **MiniPLC protections** — TVS, PTC, EMI filtering on inputs; RS-485 transient protection
  (not galvanic isolation); relay system limit 3 A @ 250 VAC with external fusing required.
- **WLD-521 heat calculation** — indicative energy from flow × ΔT; **not** MID-certified
  or suitable for billing.

## Not allowed

- **“Works with Home Assistant”** in any wording (programme declined 2026-08-03).
- **UL, CSA, FCC** certification — not held.
- **Galvanically isolated RS-485** — protection only, no isolation.
- **RTC keeps time without network** on MiniPLC/MicroPLC — factory battery not fitted;
  time syncs from the automation platform.
- **Component ratings as module limits** — e.g. relay chip 16 A is not the module rating.
- **Billing-grade metering** for WLD or indicative ENM displays without MID disclaimer.

## HomeMaster® trade mark

Registered EU trade mark EUTM 019082911 (ISYSTEMS AUTOMATION S.R.L.). Use only on
product listings and packaging for genuine modules — not as your own brand name.
