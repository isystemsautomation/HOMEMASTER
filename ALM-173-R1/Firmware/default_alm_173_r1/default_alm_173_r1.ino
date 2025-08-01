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
PCF8574 pcf20(0x20, &Wire1); // IN1..IN8
PCF8574 pcf21(0x21, &Wire1); // IN9..IN16
PCF8574 pcf23(0x23, &Wire1); // Relays + IN17 + user LEDs
PCF8574 pcf27(0x27, &Wire1); // Lamps + Buttons (P0..P3 buttons, P4..P7 lamps)

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

struct ButtonCfg {
  uint8_t  action;     // 0=None,1=AckAll,2..4=Ack G1..G3,5..7=Relay1..3 override toggle
};

// ===== Runtime state =====
ThreeCfg  digitalInputs[17];
ThreeCfg  relayConfigs[3];
LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool buttonState[4]   = {false,false,false,false}; // live read
bool buttonPrev[4]    = {false,false,false,false}; // for edge detect
bool relayOverride[3] = {false,false,false};       // used by LED sources 5..7

// ===== LED pin mapping on pcf23 =====
// pcf23 P0=IN17, P3/P6/P7=relays; use P1,P2,P4,P5 for user LEDs 1..4
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
void handleButtonConfigList(JSONVar list);

bool evalLedSource(uint8_t source, bool anyAlarm, const bool grpActive[4]);
JSONVar LedConfigListFromCfg();

// Optional stubs for acknowledgements (replace with your alarm logic)
void ackAll() { WebSerial.send("message", "AckAll"); }
void ackGroup(uint8_t g) { JSONVar m; m["ackGroup"]=g; WebSerial.send("message", m); }

void setup() {
  Serial.begin(57600);

  // Defaults
  for (int i = 0; i < 17; i++) digitalInputs[i] = { true, false, 0 };
  for (int i = 0; i < 3;  i++) relayConfigs[i]   = { true, false, 0 };
  for (int i = 0; i < 4;  i++) { ledCfg[i] = { 0, 0 }; buttonCfg[i] = { 0 }; }

  // WebSerial handlers
  WebSerial.on("values",            handleSetValues);
  WebSerial.on("inputEnableList",   handleInputEnableList);
  WebSerial.on("inputInvertList",   handleInputInvertList);
  WebSerial.on("inputGroupList",    handleInputGroupList);
  WebSerial.on("relayConfigList",   handleRelayConfigList);   // [{enabled,inverted,group} x3]
  WebSerial.on("LedConfigList",     handleLedConfigList);     // [{mode,source} x4]
  WebSerial.on("ButtonConfigList",  handleButtonConfigList);  // [{action} x4]

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

  // Coils
  mb.addCoil(Lamp1Coil); mb.addCoil(Lamp2Coil); mb.addCoil(Lamp3Coil); mb.addCoil(Lamp4Coil);
  mb.addCoil(Relay1Coil); mb.addCoil(Relay2Coil); mb.addCoil(Relay3Coil);
  mb.addCoil(SwitchIsts);

  // Status defaults
  modbusStatus["address"] = 3;
  modbusStatus["baud"]    = 19200;
  modbusStatus["state"]   = 0;

  WebSerial.send("message", "Boot OK (with button support)");
}

// ===== Handlers =====
void handleSetValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);

#if defined(ARDUINO_ARCH_ESP32)
  if ((int)modbusStatus["baud"] != baud) { Serial2.updateBaudRate(baud); mb.config(baud); }
#else
  if ((int)modbusStatus["baud"] != baud) { Serial2.end(); Serial2.begin(baud); mb.config(baud); }
#endif
  modbusStatus["address"] = addr;
  modbusStatus["baud"]    = baud;
}

void handleInputEnableList(JSONVar list) { for (int i=0;i<17 && i<list.length();i++) digitalInputs[i].enabled = (bool)list[i]; }
void handleInputInvertList(JSONVar list) { for (int i=0;i<17 && i<list.length();i++) digitalInputs[i].inverted = (bool)list[i]; }
void handleInputGroupList(JSONVar list)  { for (int i=0;i<17 && i<list.length();i++) digitalInputs[i].group   = (int) list[i]; }

void handleRelayConfigList(JSONVar list) {
  for (int i = 0; i < 3 && i < list.length(); i++) {
    relayConfigs[i].enabled  = (bool)list[i]["enabled"];
    relayConfigs[i].inverted = (bool)list[i]["inverted"];
    relayConfigs[i].group    = (int) list[i]["group"];
  }
}

void handleLedConfigList(JSONVar list) {
  for (int i = 0; i < 4 && i < list.length(); i++) {
    ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
    ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 7);
  }
  WebSerial.send("message", "LED config applied");
}

void handleButtonConfigList(JSONVar list) {
  for (int i = 0; i < 4 && i < list.length(); i++) {
    buttonCfg[i].action = (uint8_t)constrain((int)list[i]["action"], 0, 7);
  }
  WebSerial.send("message", "Button config applied");
}

// ===== Main loop =====
void loop() {
  unsigned long now = millis();

  // Blink phase
  if (now - lastBlinkToggle >= blinkPeriodMs) {
    lastBlinkToggle = now;
    blinkPhase = !blinkPhase;
  }

  if (now - lastSend >= sendInterval) {
    lastSend = now;

    // Modbus
    mb.task();

    // Example: map input to coil for demo
    mb.setCoil(SwitchIsts, pcf20.read(1));

    // -------- Buttons: read (active-low on pcf27 P0..P3), rising edge ----------
    for (int i = 0; i < 4; i++) {
      bool raw = !pcf27.read(i); // P0..P3
      buttonPrev[i] = buttonState[i];
      buttonState[i] = raw;

      if (!buttonPrev[i] && buttonState[i]) {
        uint8_t act = buttonCfg[i].action;
        if (act == 1) {               // Ack All
          ackAll();
        } else if (act >= 2 && act <= 4) { // Ack Group 1..3
          ackGroup(act - 1);
        } else if (act >= 5 && act <= 7) { // Toggle relay override flags
          int r = act - 5; // 0..2
          relayOverride[r] = !relayOverride[r];
        }
      }
    }

    // -------- Relays from Modbus coils ----------
    JSONVar relayStateList;
    for (int i = 0; i < 3; i++) {
      bool coilState = mb.Coil(Relay1Coil + i);
      relayStateList[i] = coilState;

      bool outVal = coilState;
      if (!relayConfigs[i].enabled) outVal = false;
      if (relayConfigs[i].inverted) outVal = !outVal;

      switch (i) {
        case 0: pcf23.write(3, outVal); break;
        case 1: pcf23.write(7, outVal); break;
        case 2: pcf23.write(6, outVal); break;
      }
    }

    // -------- Lamps from Modbus coils ----------
    pcf27.write(7, mb.Coil(Lamp1Coil));
    pcf27.write(6, mb.Coil(Lamp2Coil));
    pcf27.write(5, mb.Coil(Lamp3Coil));
    pcf27.write(4, mb.Coil(Lamp4Coil));

    // -------- Inputs + simple alarm derivation ----------
    bool grpActive[4] = {false,false,false,false}; // 1..3 used
    bool anyAlarm = false;

    JSONVar inputs;
    for (int i = 0; i < 17; i++) {
      bool val = false;
      if (digitalInputs[i].enabled) {
        if (i < 8)       val = pcf20.read(i);
        else if (i < 16) val = pcf21.read(i - 8);
        else             val = pcf23.read(0); // IN17 on pcf23 P0
        if (digitalInputs[i].inverted) val = !val;
      }
      inputs[i] = val;

      uint8_t g = digitalInputs[i].group; // 0..3
      if (val) {
        if (g >= 1 && g <= 3) grpActive[g] = true;
        if (g != 0) anyAlarm = true;
      }
    }

    // -------- User LEDs ----------
    JSONVar LedStateList;
    for (int i = 0; i < 4; i++) {
      bool active = evalLedSource(ledCfg[i].source, anyAlarm, grpActive);
      bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase);
      LedStateList[i] = phys;
      pcf23.write(LED_PINS[i], phys);
    }

    // -------- Echo config arrays for UI sync ----------
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

    // Buttons state + configured actions
    JSONVar ButtonStateList, ButtonGroupList;
    for (int i = 0; i < 4; i++) {
      ButtonStateList[i] = buttonState[i];
      ButtonGroupList[i] = buttonCfg[i].action;
    }

    // -------- Send to Web --------
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

    WebSerial.send("ButtonStateList", ButtonStateList);
    WebSerial.send("ButtonGroupList", ButtonGroupList);

    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    WebSerial.send("LedStateList", LedStateList);
  }
}

// ===== helpers =====
JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) {
    JSONVar o; o["mode"] = ledCfg[i].mode; o["source"] = ledCfg[i].source;
    arr[i] = o;
  }
  return arr;
}

bool evalLedSource(uint8_t source, bool anyAlarm, const bool grpActive[4]) {
  switch (source) {
    case 0: return false;            // None
    case 1: return anyAlarm;         // Any alarm
    case 2: return grpActive[1];     // Group 1
    case 3: return grpActive[2];     // Group 2
    case 4: return grpActive[3];     // Group 3
    case 5: return relayOverride[0]; // Relay 1 overridden
    case 6: return relayOverride[1]; // Relay 2 overridden
    case 7: return relayOverride[2]; // Relay 3 overridden
    default: return false;
  }
}
