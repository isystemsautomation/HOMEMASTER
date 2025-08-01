#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>   // for JSONVar

// ===== Hardware pins (adjust for your board) =====
#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5

// ===== Modbus / RS-485 =====
const int TxenPin = -1;  // -1 if not using TXEN
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// Modbus coil indexes
const int Lamp1Coil = 0, Lamp2Coil = 1, Lamp3Coil = 2, Lamp4Coil = 3;
const int Relay1Coil = 4, Relay2Coil = 5, Relay3Coil = 6;
const int SwitchIsts = 11;

// ===== I2C expanders =====
PCF8574 pcf20(0x20, &Wire1);
PCF8574 pcf21(0x21, &Wire1);
PCF8574 pcf23(0x23, &Wire1);
PCF8574 pcf27(0x27, &Wire1);

// ===== Config structures =====
struct Config3 {
  bool enabled;
  bool inverted;
  uint8_t group;
};

struct ButtonCfg {
  uint8_t action;  // 0..7 (None, Ack All, Ack G1..3, Override R1..3)
};

// ===== Runtime state =====
Config3 digitalInputs[17];
Config3 relayConfigs[3];
ButtonCfg buttonCfg[4];

bool buttonState[4] = {false};  // live readings (active-low HW)

// ===== Web Serial =====
SimpleWebSerial WebSerial;

// status JSON
JSONVar modbusStatus;

// ===== Timers =====
unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;

// ====== Forward decls ======
void handleSetValues(JSONVar values);
void handleInputEnableList(JSONVar list);
void handleInputInvertList(JSONVar list);
void handleInputGroupList(JSONVar list);
void handleRelayConfigList(JSONVar list);
void handleButtonConfigList(JSONVar list);

void setup() {
  Serial.begin(57600);

  // WebSerial event handlers
  WebSerial.on("values", handleSetValues);
  WebSerial.on("inputEnableList", handleInputEnableList);
  WebSerial.on("inputInvertList", handleInputInvertList);
  WebSerial.on("inputGroupList", handleInputGroupList);
  WebSerial.on("relayConfigList", handleRelayConfigList);   // single array of objects
  WebSerial.on("ButtonConfigList", handleButtonConfigList); // single array of objects

  // Defaults
  for (int i = 0; i < 17; i++) digitalInputs[i] = { true, false, 0 };
  for (int i = 0; i < 3;  i++) relayConfigs[i]   = { true, false, 0 };
  for (int i = 0; i < 4;  i++) buttonCfg[i]      = { 0 };

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
  modbusStatus["baud"] = 19200;
  modbusStatus["state"] = 0;
}

// ===== Handlers from Web =====
void handleSetValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);

  bool reconfig = false;
  modbusStatus["address"] = addr;
  modbusStatus["baud"] = baud;

  if (reconfig) {
    // You could add additional actions here if needed
  }
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
  // list: [ {enabled, inverted, group}, ... ] (3 items)
  for (int i = 0; i < 3 && i < list.length(); i++) {
    relayConfigs[i].enabled  = (bool)list[i]["enabled"];
    relayConfigs[i].inverted = (bool)list[i]["inverted"];
    relayConfigs[i].group    = (int) list[i]["group"];
  }
}

void handleButtonConfigList(JSONVar list) {
  // list: [ {action}, ... ] (4 items)
  for (int i = 0; i < 4 && i < list.length(); i++) {
    int action = (int)list[i]["action"]; // 0..7
    buttonCfg[i].action = (uint8_t)constrain(action, 0, 7);
  }
}

// ===== Main loop =====
void loop() {
  unsigned long now = millis();
  if (now - lastSend >= sendInterval) {
    lastSend = now;

    // Modbus tasks
    mb.task();

    // Example: map one digital input into a coil (if you need it)
    mb.setCoil(SwitchIsts, pcf20.read(1));

    // --------- RELAYS (state from Modbus coils only) ----------
    JSONVar relayStateList;
    for (int i = 0; i < 3; i++) {
      bool coilState = mb.Coil(Relay1Coil + i);   // raw state from module
      relayStateList[i] = coilState;

      // Apply config mask to physical outputs
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

    // --------- BUTTONS (read active-low) ----------
    for (int i = 0; i < 4; i++) {
      // adjust pins as needed; here assume pcf27 P0..P3 are buttons
      buttonState[i] = !pcf27.read(i);
    }

    // --------- INPUTS (17 channels) ----------
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
  }
}
