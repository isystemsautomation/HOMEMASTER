# WLD-521-R1 Modbus Register Table

## Function Codes
- **FC02** - Read Input Status (Discrete Inputs)
- **FC03** - Read Holding Registers
- **FC05** - Write Single Coil
- **FC01** - Read Coils

---

## Input Status (FC02) - Read Only

| Address | Name | Type | Description |
|---------|------|------|-------------|
| 1-5 | DI1-DI5 | Boolean | Digital Input states (0=OFF, 1=ON) |
| 60-61 | RLY1-RLY2 | Boolean | Relay output states (0=OFF, 1=ON) |
| 90-93 | LED1-LED4 | Boolean | User LED states (0=OFF, 1=ON) |
| 100-103 | BTN1-BTN4 | Boolean | Button pressed states (0=Released, 1=Pressed) |

---

## Coils (FC01/FC05) - Read/Write

### Maintained Coils (Switched - ESPHome can set ON/OFF directly)

| Address | Name | Type | Description |
|---------|------|------|-------------|
| 200 | Relay 1 State | Boolean | Relay 1 ON/OFF state (maintained) |
| 201 | Relay 2 State | Boolean | Relay 2 ON/OFF state (maintained) |
| 220-224 | DI1-DI5 Enable | Boolean | Digital Input enable state (maintained) |

### Pulse Coils (Cleared after use)

| Address | Name | Type | Description |
|---------|------|------|-------------|
| 340-344 | DI1-DI5 Counter Reset | Boolean | Reset counter for DI (pulse, auto-cleared) |

---

## Holding Registers (FC03) - Read Only

All data accessible via FC03 Read Holding Registers.

### Status Registers (UINT16: 0 or 1)

| Address | Name | Type | Description |
|---------|------|------|-------------|
| 1-5 | DI1-DI5 State | UINT16 | Digital Input states (mirror of ISTS) |
| 60-61 | RLY1-RLY2 State | UINT16 | Relay output states (mirror of ISTS) |
| 90-93 | LED1-LED4 State | UINT16 | User LED states (mirror of ISTS) |
| 100-103 | BTN1-BTN4 State | UINT16 | Button pressed states (mirror of ISTS) |

### Flow Meter Data (UINT32 - 2 registers each, Little Endian)

| Address | Name | Type | Unit | Description |
|---------|------|------|------|-------------|
| 104-105 | DI1 Flow Rate | UINT32 | L/min × 1000 | Flow rate for DI1 |
| 106-107 | DI2 Flow Rate | UINT32 | L/min × 1000 | Flow rate for DI2 |
| 108-109 | DI3 Flow Rate | UINT32 | L/min × 1000 | Flow rate for DI3 |
| 110-111 | DI4 Flow Rate | UINT32 | L/min × 1000 | Flow rate for DI4 |
| 112-113 | DI5 Flow Rate | UINT32 | L/min × 1000 | Flow rate for DI5 |
| 114-115 | DI1 Flow Accumulated | UINT32 | L × 1000 | Total flow for DI1 |
| 116-117 | DI2 Flow Accumulated | UINT32 | L × 1000 | Total flow for DI2 |
| 118-119 | DI3 Flow Accumulated | UINT32 | L × 1000 | Total flow for DI3 |
| 120-121 | DI4 Flow Accumulated | UINT32 | L × 1000 | Total flow for DI4 |
| 122-123 | DI5 Flow Accumulated | UINT32 | L × 1000 | Total flow for DI5 |

### Heat Energy Data (S32/U32 - 2 registers each, Little Endian)

| Address | Name | Type | Unit | Description |
|---------|------|------|------|-------------|
| 124-125 | DI1 Heat Power | SINT32 | W | Heat power for DI1 |
| 126-127 | DI2 Heat Power | SINT32 | W | Heat power for DI2 |
| 128-129 | DI3 Heat Power | SINT32 | W | Heat power for DI3 |
| 130-131 | DI4 Heat Power | SINT32 | W | Heat power for DI4 |
| 132-133 | DI5 Heat Power | SINT32 | W | Heat power for DI5 |
| 134-135 | DI1 Heat Energy | UINT32 | Wh × 1000 | Total heat energy for DI1 |
| 136-137 | DI2 Heat Energy | UINT32 | Wh × 1000 | Total heat energy for DI2 |
| 138-139 | DI3 Heat Energy | UINT32 | Wh × 1000 | Total heat energy for DI3 |
| 140-141 | DI4 Heat Energy | UINT32 | Wh × 1000 | Total heat energy for DI4 |
| 142-143 | DI5 Heat Energy | UINT32 | Wh × 1000 | Total heat energy for DI5 |
| 144-145 | DI1 Heat ΔT | SINT32 | °C × 1000 | Temperature difference for DI1 |
| 146-147 | DI2 Heat ΔT | SINT32 | °C × 1000 | Temperature difference for DI2 |
| 148-149 | DI3 Heat ΔT | SINT32 | °C × 1000 | Temperature difference for DI3 |
| 150-151 | DI4 Heat ΔT | SINT32 | °C × 1000 | Temperature difference for DI4 |
| 152-153 | DI5 Heat ΔT | SINT32 | °C × 1000 | Temperature difference for DI5 |

### 1-Wire Temperature Data (SINT32 - 2 registers each, Little Endian)

| Address | Name | Type | Unit | Description |
|---------|------|------|------|-------------|
| 154-155 | OW Sensor 1 Temp | SINT32 | °C × 1000 | 1-Wire sensor #1 temperature |
| 156-157 | OW Sensor 2 Temp | SINT32 | °C × 1000 | 1-Wire sensor #2 temperature |
| 158-159 | OW Sensor 3 Temp | SINT32 | °C × 1000 | 1-Wire sensor #3 temperature |
| 160-161 | OW Sensor 4 Temp | SINT32 | °C × 1000 | 1-Wire sensor #4 temperature |
| 162-163 | OW Sensor 5 Temp | SINT32 | °C × 1000 | 1-Wire sensor #5 temperature |
| 164-165 | OW Sensor 6 Temp | SINT32 | °C × 1000 | 1-Wire sensor #6 temperature |
| 166-167 | OW Sensor 7 Temp | SINT32 | °C × 1000 | 1-Wire sensor #7 temperature |
| 168-169 | OW Sensor 8 Temp | SINT32 | °C × 1000 | 1-Wire sensor #8 temperature |
| 170-171 | OW Sensor 9 Temp | SINT32 | °C × 1000 | 1-Wire sensor #9 temperature |
| 172-173 | OW Sensor 10 Temp | SINT32 | °C × 1000 | 1-Wire sensor #10 temperature |

---

## Notes

1. **Maintained Coils (200-224)**: These coils maintain their state. ESPHome can read/write them directly as switches.
2. **Pulse Coils (340-344)**: These coils are automatically cleared after being read. Write `1` to reset the corresponding counter.
3. **Holding Registers**: All 32-bit values (UINT32/SINT32) are stored as two consecutive 16-bit registers in Little Endian format (low word first).
4. **Scaling**: 
   - Flow Rate: Multiply by 0.001 to get L/min
   - Flow Accumulated: Multiply by 0.001 to get L
   - Heat Energy: Multiply by 0.001 to get Wh
   - Temperatures: Multiply by 0.001 to get °C
5. **Total Register Range**: 1-173 (continuous address space for FC03)

---

## Example Usage

### Read Digital Input 1 State
- **FC03**, Address: **1** → Returns: `0` (OFF) or `1` (ON)

### Control Relay 1 ON
- **FC05**, Address: **200**, Value: `1` (ON) or `0` (OFF)

### Read Flow Rate for DI1
- **FC03**, Addresses: **104-105** → Read as UINT32, divide by 1000 → Result in L/min

### Reset Counter for DI1
- **FC05**, Address: **340**, Value: `1` (will be auto-cleared by firmware)

