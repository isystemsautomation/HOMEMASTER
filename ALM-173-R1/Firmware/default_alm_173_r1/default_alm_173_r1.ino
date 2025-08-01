#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>   // JSONVar

// ===== Hardware pins (adjust for your board) =====
#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5

// ===== Modbus / RS-485 =====
const int TxenPin = -1;  // -1 if not using TXEN
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// Modbus coil indices
const int Lamp1Coil = 0, Lamp2Coil = 1, Lamp3Coil = 2, Lamp4Coil = 3;
const int Relay1Coil = 4, Relay2Coil = 5, Relay3Coil = 6;
const int SwitchIsts = 11;

// ===== I2C expanders =====
// IN1..IN8
PCF8574 pcf20(0x20, &Wire1);
// IN9..IN16
PCF8574 pcf21(0x21, &Wire1);
// Relays + IN17 + user LEDs
PCF8574 pcf23(0x23, &Wire1);
// Lamps (no buttons used here)
PCF8574 pcf27(0x27, &Wire1);

// ===== Config structures =====
struct ThreeCfg {
  bool     enabled;
  bool     inverted;
  uint8_t  group;      // 0..3 (0=None)
};

struct LedCfg {
  uint8_t  mode;       // 0=steady, 1=blink
  uint8_t  source;     // 0=None,1=Any,2=G1,3=G2,4=G3,5=R1 Overr.,6=R2 Overr.,7=R3 Overr.
};

// ===== Runtime state =====
ThreeCfg  digitalInputs[17];
ThreeCfg  relayConfigs[3];
LedCfg    ledCfg[4];

// If you still want LED sources 5..7 available later, keep these flags.
// With no button support they will remain false unless you set them elsewhere.
bool relayOverride[3] = {false, false, false};

// ===== LED pin mapping on pcf23 (adjust as needed) =====
// pcf23 P0 = IN17, P3/P6/P7 used for relays; use P1,P2,P4,P5 for User LED1..4
const uint8_t LED_PINS[4] = {1, 2, 4, 5};

// ===== Web Serial =====
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ===== Timing =====
unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;

unsigned long lastBlinkToggle = 0;
const unsigned long blinkPeriodMs = 400;
bool blinkPhase = false;

// ===== Prototypes =====
void handleSetValues(JSONVar values);
void handleInputEnableList(JSONVar list);
void handleInputInvertList(JSONVar list);
void handleInputGroupList(JSONVar list);
void handleRelayConfigList(JSONVar list);
void handleLedConfigList(JSONVar list);
bool evalLedSource(uint8_t source, bool anyAlarm, const bool grpActive[4]);

void setup() {
  Serial.begin(57600);

  // Default configs
  for (int i = 0; i < 17; i++) digitalInputs[i] = { true, false, 0 };
  for (int i = 0; i < 3;  i++) relayConfigs[i]   = { true, false, 0 };
  for (int i = 0; i < 4;  i++) ledCfg[i] = { 0, 0 }; // steady, None

  // WebSerial event handlers
  WebSerial.on("values",            handleSetValues);
  WebSerial.on("inputEnableList",   handleInputEnableList);
  WebSerial.on("inputInvertList",   handleInputInvertList);
  WebSerial.on("inputGroupList",    handleInputGroupList);
  WebSerial.on("relayConfigList",   handleRelayConfigList);   // [{enabled,inverted,group} x3]
  WebSerial.on("LedConfigList",     handleLedConfigList);     // [{mode,source} x4]

  // I2C init
  Wire1.setSDA(SDA);
  Wire1.setSCL(SCL);
  Wire1.begin();
  pcf20.begin(); pcf21.begin(); pcf23.begin(); pcf27.begin();

  // Serial2 / Modbus RTU
  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(19200);
  mb.config(19200);
  mb.setAdditionalServerData("LAMP");

  // Declare coils
  mb.addCoil(Lamp1Coil); mb.addCoil(Lamp2Coil); mb.addCoil(Lamp3Coil); mb.addCoil(Lamp4Coil);
  mb.addCoil(Relay1Coil); mb.addCoil(Relay2Coil); mb.addCoil(Relay3Coil);
  mb.addCoil(SwitchIsts);

  // Status defaults
  modbusStatus["address"] = 3;
  modbusStatus["baud"]    = 19200;
  modbusStatus["state"]   = 0;

  WebSerial.send("message", "Boot OK (no button support)");
}

// ===== Handlers from Web =====
void handleSetValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);

#if defined(ARDUINO_ARCH_ESP32)
  if ((int)modbusStatus["baud"] != baud) {
    Serial2.updateBaudRate(baud);
    mb.config(baud);
  }
#else
  if ((int)modbusStatus["baud"] != baud) {
    Serial2.end();
    Serial2.begin(baud);
    mb.config(baud);
  }
#endif
  modbusStatus["address"] = addr;
  modbusStatus["baud"]    = baud;
}

void handleInputEnableList(JSONVar list) {
  for (int i = 0; i < 17 && i < list.length(); i++)
    digitalInputs[i].enabled = (bool)list[i];
}
void handleInputInvertList(JSONVar list) {
  for (int i = 0; i < 17 && i < list.length(); i++)
    digitalInputs[i].inverted = (bool)list[i];
}
void handleInputGroupList(JSONVar list) {
  for (int i = 0; i < 17 && i < list.length(); i++)
    digitalInputs[i].group = (int)list[i];
}

void handleRelayConfigList(JSONVar list) {
  for (int i = 0; i < 3 && i < list.length(); i++) {
    relayConfigs[i].enabled  = (bool)list[i]["enabled"];
    relayConfigs[i].inverted = (bool)list[i]["inverted"];
    relayConfigs[i].group    = (int) list[i]["group"];
  }
}

void handleLedConfigList(JSONVar list) {
  for (int i = 0; i < 4 && i < list.length(); i++) {
    int mode   = (int)list[i]["mode"];   // 0 or 1
    int source = (int)list[i]["source"]; // 0..7
    ledCfg[i].mode   = (uint8_t)constrain(mode, 0, 1);
    ledCfg[i].source = (uint8_t)constrain(source, 0, 7);
  }
  WebSerial.send("message", "LED config applied");
}

// ===== Main loop =====
void loop() {
  unsigned long now = millis();

  // LED blink phase
  if (now - lastBlinkToggle >= blinkPeriodMs) {
    lastBlinkToggle = now;
    blinkPhase = !blinkPhase;
  }

  if (now - lastSend >= sendInterval) {
    lastSend = now;

    // Modbus
    mb.task();

    // Example: mirror an input to a coil (adjust if not desired)
    mb.setCoil(SwitchIsts, pcf20.read(1));

    // --------- Relays: state from Modbus coils ----------
    JSONVar relayStateList;
    for (int i = 0; i < 3; i++) {
      bool coilState = mb.Coil(Relay1Coil + i);
      relayStateList[i] = coilState;

      bool outVal = coilState;
      if (!relayConfigs[i].enabled) outVal = false;
      if (relayConfigs[i].inverted) outVal = !outVal;

      // pcf23 pins for relays
      switch (i) {
        case 0: pcf23.write(3, outVal); break;
        case 1: pcf23.write(7, outVal); break;
        case 2: pcf23.write(6, outVal); break;
      }
    }

    // --------- Lamps from Modbus coils ----------
    pcf27.write(7, mb.Coil(Lamp1Coil));
    pcf27.write(6, mb.Coil(Lamp2Coil));
    pcf27.write(5, mb.Coil(Lamp3Coil));
    pcf27.write(4, mb.Coil(Lamp4Coil));

    // --------- Inputs (17) & simple alarm derivation (any + by group) ----------
    bool grpActive[4] = {false,false,false,false}; // index 1..3 used
    bool anyAlarm = false;

    JSONVar inputs;
    for (int i = 0; i < 17; i++) {
      bool val = false;
      if (digitalInputs[i].enabled) {
        if (i < 8)       val = pcf20.read(i);        // IN1..IN8
        else if (i < 16) val = pcf21.read(i - 8);    // IN9..IN16
        else             val = pcf23.read(0);        // IN17 on pcf23 P0
        if (digitalInputs[i].inverted) val = !val;
      }
      inputs[i] = val;

      uint8_t g = digitalInputs[i].group; // 0..3
      if (val) {
        if (g >= 1 && g <= 3) grpActive[g] = true;
        if (g != 0) anyAlarm = true;
      }
    }

    // --------- User LEDs (4) ----------
    JSONVar LedStateList;
    for (int i = 0; i < 4; i++) {
      bool active = evalLedSource(ledCfg[i].source, anyAlarm, grpActive);
      bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase);
      LedStateList[i] = phys;
      // pcf23 LED pins (active-high assumed; invert here if needed)
      pcf23.write(LED_PINS[i], phys);
    }

    // --------- Echo config arrays for UI sync ----------
    JSONVar invertList, groupList, enableList;
    for (int i = 0; i < 17; i++) {
      invertList[i] = digitalInputs[i].inverted;
      groupList[i]  = digitalInputs[i].group;
      enableList[i] = digitalInputs[i].enabled;
    }

    JSONVar relayEnableList, relayInvertList, relayGroupList;
    for (int i = 0; i < 3; i++) {
      relayEnableList[i] = relayConfigs[i].enabled;
      relayInvertList[i] = relayConfigs[i].inverted;
      relayGroupList[i]  = relayConfigs[i].group;
    }

    // --------- Send to Web ---------
    WebSerial.check();
    WebSerial.send("status", modbusStatus);

    WebSerial.send("inputs", inputs);
    WebSerial.send("invertList", invertList);
    WebSerial.send("groupList", groupList);
    WebSerial.send("enableList", enableList);

    WebSerial.send("relayStateList", relayStateList);
    WebSerial.send("relayEnableList", relayEnableList);
    WebSerial.send("relayInvertList", relayInvertList);
    WebSerial.send("relayGroupList", relayGroupList);

    WebSerial.send("LedConfigList", LedConfigListFromCfg()); // helper packs cfg
    WebSerial.send("LedStateList", LedStateList);
  }
}

// Helper: pack current LED cfg to JSON array for UI sync
JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) {
    JSONVar o; o["mode"] = ledCfg[i].mode; o["source"] = ledCfg[i].source;
    arr[i] = o;
  }
  return arr;
}

// ===== helper: evaluate LED trigger source =====
bool evalLedSource(uint8_t source, bool anyAlarm, const bool grpActive[4]) {
  switch (source) {
    case 0: return false;            // None
    case 1: return anyAlarm;         // Any alarm
    case 2: return grpActive[1];     // Group 1
    case 3: return grpActive[2];     // Group 2
    case 4: return grpActive[3];     // Group 3
    case 5: return relayOverride[0]; // Relay 1 overridden (will be false without buttons)
    case 6: return relayOverride[1]; // Relay 2 overridden
    case 7: return relayOverride[2]; // Relay 3 overridden
    default: return false;
  }
}
