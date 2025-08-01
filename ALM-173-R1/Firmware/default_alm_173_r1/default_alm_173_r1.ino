#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h> // JSONVar

// ===== Hardware pins (adjust for your board) =====
#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5

// ===== Modbus / RS-485 =====
const int TxenPin = -1;  // -1 if not using TXEN
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// Modbus coil indices (as used previously)
const int Lamp1Coil = 0, Lamp2Coil = 1, Lamp3Coil = 2, Lamp4Coil = 3;
const int Relay1Coil = 4, Relay2Coil = 5, Relay3Coil = 6;
const int SwitchIsts = 11;

// ===== I2C expanders =====
PCF8574 pcf20(0x20, &Wire1); // inputs IN1..IN8
PCF8574 pcf21(0x21, &Wire1); // inputs IN9..IN16
PCF8574 pcf23(0x23, &Wire1); // relays, IN17, user LEDs
PCF8574 pcf27(0x27, &Wire1); // lamps, buttons

// ===== Config structures =====
struct ThreeCfg {
  bool enabled;
  bool inverted;
  uint8_t group; // 0..3 (0=None)
};

struct ButtonCfg {
  uint8_t action; // 0..7 (None, Ack All, Ack G1..3, Override R1..3)
};

struct LedCfg {
  uint8_t mode;   // 0=steady, 1=blink
  uint8_t source; // 0=None,1=Any,2=G1,3=G2,4=G3,5=R1 Override,6=R2 Override,7=R3 Override
};

// ===== Runtime state =====
ThreeCfg digitalInputs[17];
ThreeCfg relayConfigs[3];
ButtonCfg buttonCfg[4];
LedCfg    ledCfg[4];

bool buttonState[4]      = {false, false, false, false};
bool buttonPrev[4]       = {false, false, false, false};
bool relayOverride[3]    = {false, false, false}; // manual override flag (for LED trigger only)

// ===== Web Serial =====
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ===== Timers =====
unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;

unsigned long lastBlinkToggle = 0;
const unsigned long blinkPeriodMs = 400;
bool blinkPhase = false; // toggles at blinkPeriodMs

// ===== LED pin mapping on pcf23 (adjust as needed) =====
// pcf23 P0 = IN17, P3/P6/P7 used for relays -> choose P1,P2,P4,P5 for LEDs
const uint8_t LED_PINS[4] = {1, 2, 4, 5}; // LED1..LED4 -> P1,P2,P4,P5

// ===== Forwards =====
void handleSetValues(JSONVar values);
void handleInputEnableList(JSONVar list);
void handleInputInvertList(JSONVar list);
void handleInputGroupList(JSONVar list);
void handleRelayConfigList(JSONVar list);
void handleButtonConfigList(JSONVar list);
void handleLedConfigList(JSONVar list);

bool evalLedSource(uint8_t source,
                   const bool anyAlarm,
                   const bool grpActive[4]); // grpActive[1..3] used

void setup() {
  Serial.begin(57600);

  // WebSerial event handlers
  WebSerial.on("values", handleSetValues);
  WebSerial.on("inputEnableList", handleInputEnableList);
  WebSerial.on("inputInvertList", handleInputInvertList);
  WebSerial.on("inputGroupList", handleInputGroupList);
  WebSerial.on("relayConfigList", handleRelayConfigList);     // [{enabled,inverted,group} x3]
  WebSerial.on("ButtonConfigList", handleButtonConfigList);   // [{action} x4]
  WebSerial.on("LedConfigList", handleLedConfigList);         // [{mode,source} x4]

  // Defaults
  for (int i = 0; i < 17; i++) digitalInputs[i] = { true, false, 0 };
  for (int i = 0; i < 3;  i++) relayConfigs[i]   = { true, false, 0 };
  for (int i = 0; i < 4;  i++) {
    buttonCfg[i] = { 0 };
    ledCfg[i]    = { 0, 0 }; // steady, None
  }

  // I2C
  Wire1.setSDA(SDA);
  Wire1.setSCL(SCL);
  Wire1.begin();
  pcf20.begin();
  pcf21.begin();
  pcf23.begin();
  pcf27.begin();

  // Serial2 / Modbus RTU
  Serial2.setTX(TX2);
  Serial2.setRX(RX2);
  Serial2.begin(19200);
  mb.config(19200);
  mb.setAdditionalServerData("LAMP");

  // Declare coils
  mb.addCoil(Lamp1Coil);
  mb.addCoil(Lamp2Coil);
  mb.addCoil(Lamp3Coil);
  mb.addCoil(Lamp4Coil);
  mb.addCoil(Relay1Coil);
  mb.addCoil(Relay2Coil);
  mb.addCoil(Relay3Coil);
  mb.addCoil(SwitchIsts);

  // Status defaults
  modbusStatus["address"] = 3;
  modbusStatus["baud"]    = 19200;
  modbusStatus["state"]   = 0;
}

// ===== Handlers from Web =====
void handleSetValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);

  // If your MCU does not support updateBaudRate, replace with Serial2.end()/begin.
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

void handleButtonConfigList(JSONVar list) {
  for (int i = 0; i < 4 && i < list.length(); i++) {
    int action = (int)list[i]["action"]; // 0..7
    buttonCfg[i].action = (uint8_t)constrain(action, 0, 7);
  }
}

void handleLedConfigList(JSONVar list) {
  // list: [ {mode, source}, ... ] (4 items)
  for (int i = 0; i < 4 && i < list.length(); i++) {
    int mode   = (int)list[i]["mode"];   // 0 or 1
    int source = (int)list[i]["source"]; // 0..7
    ledCfg[i].mode   = (uint8_t)constrain(mode, 0, 1);
    ledCfg[i].source = (uint8_t)constrain(source, 0, 7);
  }
}

// ===== Main loop =====
void loop() {
  unsigned long now = millis();

  // Blink phase for LED blinking
  if (now - lastBlinkToggle >= blinkPeriodMs) {
    lastBlinkToggle = now;
    blinkPhase = !blinkPhase;
  }

  if (now - lastSend >= sendInterval) {
    lastSend = now;

    // Modbus tasks
    mb.task();

    // Optional: map one digital input into a coil
    mb.setCoil(SwitchIsts, pcf20.read(1));

    // --------- BUTTONS: read active-low + detect rising edge for override toggles ----------
    for (int i = 0; i < 4; i++) {
      bool raw = !pcf27.read(i);             // assume P0..P3 are buttons (active-low)
      buttonPrev[i] = buttonState[i];
      buttonState[i] = raw;

      // On rising edge, if action is override R1..R3 (5..7), toggle the corresponding flag
      if (!buttonPrev[i] && buttonState[i]) {
        uint8_t act = buttonCfg[i].action;
        if (act >= 5 && act <= 7) {
          int r = act - 5; // 0..2
          relayOverride[r] = !relayOverride[r];
        }
        // You can add behaviors for Ack All / Ack Group 1..3 here if needed.
      }
    }

    // --------- RELAYS (state from Modbus coils only) ----------
    JSONVar relayStateList;
    for (int i = 0; i < 3; i++) {
      bool coilState = mb.Coil(Relay1Coil + i); // raw state from module
      relayStateList[i] = coilState;

      // Mask to physical outputs based on config
      bool outVal = coilState;
      if (!relayConfigs[i].enabled) outVal = false;
      if (relayConfigs[i].inverted) outVal = !outVal;

      // Write to PCF8574 pins as per your wiring
      switch (i) {
        case 0: pcf23.write(3, outVal); break;
        case 1: pcf23.write(7, outVal); break;
        case 2: pcf23.write(6, outVal); break;
      }
    }

    // --------- LAMPS FROM MODBUS COILS ----------
    pcf27.write(7, mb.Coil(Lamp1Coil));
    pcf27.write(6, mb.Coil(Lamp2Coil));
    pcf27.write(5, mb.Coil(Lamp3Coil));
    pcf27.write(4, mb.Coil(Lamp4Coil));

    // --------- INPUTS (17 channels) ----------
    bool grpActive[4] = {false,false,false,false}; // idx 1..3 used
    bool anyAlarm = false;

    JSONVar inputs;
    for (int i = 0; i < 17; i++) {
      bool val = false;
      if (digitalInputs[i].enabled) {
        if (i < 8)       val = pcf20.read(i);
        else if (i < 16) val = pcf21.read(i - 8);
        else             val = pcf23.read(0);   // IN17 on pcf23 P0 (adjust if needed)
        if (digitalInputs[i].inverted) val = !val;
      }
      inputs[i] = val;

      // derive group/any alarm activity
      uint8_t g = digitalInputs[i].group; // 0..3
      if (val) {
        if (g >= 1 && g <= 3) grpActive[g] = true;
        if (g != 0) anyAlarm = true; // treat grouped inputs as alarms
      }
    }

    // --------- USER LEDs (4) ----------
    JSONVar LedStateList;
    for (int i = 0; i < 4; i++) {
      bool active = evalLedSource(ledCfg[i].source, anyAlarm, grpActive);
      bool phys = false;
      if (ledCfg[i].mode == 0) {
        // steady when active
        phys = active;
      } else {
        // blinking when active
        phys = active && blinkPhase;
      }
      LedStateList[i] = phys;
      // Write LED to PCF (active-high assumed; invert if needed)
      pcf23.write(LED_PINS[i], phys);
    }

    // --------- Echo config arrays (for UI sync) ----------
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

    JSONVar ButtonStateList, ButtonGroupList;
    for (int i = 0; i < 4; i++) {
      ButtonStateList[i] = buttonState[i];
      ButtonGroupList[i] = buttonCfg[i].action;
    }

    JSONVar LedConfigList;
    for (int i = 0; i < 4; i++) {
      JSONVar o;
      o["mode"] = ledCfg[i].mode;
      o["source"] = ledCfg[i].source;
      LedConfigList[i] = o;
    }

    // --------- Send to Web ---------
    WebSerial.check();
    WebSerial.send("status", modbusStatus);
    WebSerial.send("inputs", inputs);
    WebSerial.send("invertList", invertList);
    WebSerial.send("groupList", groupList);
    WebSerial.send("enableList", enableList);

    WebSerial.send("relayStateList", relayStateList);  // states for display
    WebSerial.send("relayEnableList", relayEnableList);
    WebSerial.send("relayInvertList", relayInvertList);
    WebSerial.send("relayGroupList", relayGroupList);

    WebSerial.send("ButtonStateList", ButtonStateList);
    WebSerial.send("ButtonGroupList", ButtonGroupList);

    WebSerial.send("LedConfigList", LedConfigList); // echo current LED configs
    WebSerial.send("LedStateList", LedStateList);   // actual LED on/off
  }
}

// ===== helper: evaluate LED trigger source =====
bool evalLedSource(uint8_t source,
                   const bool anyAlarm,
                   const bool grpActive[4]) {
  switch (source) {
    case 0: return false;           // None
    case 1: return anyAlarm;        // Any alarm
    case 2: return grpActive[1];    // Group 1 alarm
    case 3: return grpActive[2];    // Group 2 alarm
    case 4: return grpActive[3];    // Group 3 alarm
    case 5: return relayOverride[0];// Relay 1 overridden (manual)
    case 6: return relayOverride[1];// Relay 2 overridden
    case 7: return relayOverride[2];// Relay 3 overridden
    default: return false;
  }
}
