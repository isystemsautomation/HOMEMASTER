#include <PCF8574.h>
#include "LittleFS.h"
#include <ModbusSerial.h>
#include <Wire.h>
#include <SimpleWebSerial.h>

SimpleWebSerial WebSerial;
JSONVar modbus;
JSONVar inputs;
JSONVar relayStates;
JSONVar ButtonStates;


#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5

unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;
bool heartbeat = false;

const int TxenPin = -1;
int SlaveId = 1;

const int Lamp1Coil = 0;
const int Lamp2Coil = 1;
const int Lamp3Coil = 2;
const int Lamp4Coil = 3;
const int Relay1Coil = 4;
const int Relay2Coil = 5;
const int Relay3Coil = 6;
const int SwitchIsts = 11;

ModbusSerial mb(Serial2, SlaveId, TxenPin);

PCF8574 pcf20(0x20, &Wire1);
PCF8574 pcf21(0x21, &Wire1);
PCF8574 pcf23(0x23, &Wire1);
PCF8574 pcf27(0x27, &Wire1);

struct Config {
  bool enabled;
  bool inverted;
  uint8_t group;
};

Config digitalInputs[17];
Config relayConfigs[3];
Config ButtonConfigs[4];

bool relayState[3] = {false, false, false};
bool ButtonState[4] = {false, false, false, false};


void setup() {
  Serial.begin(57600);
  WebSerial.on("values", setModbusValues);
  WebSerial.on("invertList", handleInvertList);
  WebSerial.on("groupList", handleGroupList);
  WebSerial.on("enableList", handleEnableList);

//  WebSerial.on("relayStateList", handleRelayStateList);
  WebSerial.on("relayInvertList", handleRelayInvertList);
  WebSerial.on("relayGroupList", handleRelayGroupList);
  WebSerial.on("relayEnableList", handleRelayEnableList);

//  WebSerial.on("ButtonStateList", handleButtonStateList);
  WebSerial.on("ButtonGroupList", handleButtonGroupList); 

  
  for (int i = 0; i < 17; i++) {
    digitalInputs[i] = {true, false, 0};
  }
  for (int i = 0; i < 3; i++) {
    relayConfigs[i] = {true, false, 0};
    relayState[i] = false;
  }


  Wire1.setSDA(SDA);
  Wire1.setSCL(SCL);
  Wire1.begin();
  pcf20.begin();
  pcf21.begin();
  pcf23.begin();
  pcf27.begin();

  Serial2.setTX(TX2);
  Serial2.setRX(RX2);
  Serial2.begin(19200);

  mb.config(19200);
  mb.setAdditionalServerData("LAMP");

  mb.addCoil(Lamp1Coil);
  mb.addCoil(Lamp2Coil);
  mb.addCoil(Lamp3Coil);
  mb.addCoil(Lamp4Coil);
  mb.addCoil(Relay1Coil);
  mb.addCoil(Relay2Coil);
  mb.addCoil(Relay3Coil);
  mb.addCoil(SwitchIsts);

  modbus["address"] = 3;
  modbus["baud"] = 19200;
  modbus["state"] = 5;
}

void setModbusValues(JSONVar values) {
  modbus["address"] = constrain((int)values["mb_address"], 1, 255);
  modbus["baud"] = constrain((int)values["mb_baud"], 9600, 115200);
}

void handleInvertList(JSONVar list) {
  for (int i = 0; i < 17 && i < list.length(); i++)
    digitalInputs[i].inverted = (bool)list[i];
}
void handleGroupList(JSONVar list) {
  for (int i = 0; i < 17 && i < list.length(); i++)
    digitalInputs[i].group = (int)list[i];
}
void handleEnableList(JSONVar list) {
  for (int i = 0; i < 17 && i < list.length(); i++)
    digitalInputs[i].enabled = (bool)list[i];
}

void handleRelayStateList(JSONVar list) {
  for (int i = 0; i < 3 && i < list.length(); i++)
    relayState[i] = (bool)list[i];
}
void handleRelayInvertList(JSONVar list) {
  for (int i = 0; i < 3 && i < list.length(); i++)
    relayConfigs[i].inverted = (bool)list[i];
}
void handleRelayGroupList(JSONVar list) {
  for (int i = 0; i < 3 && i < list.length(); i++)
    relayConfigs[i].group = (int)list[i];
}
void handleRelayEnableList(JSONVar list) {
  for (int i = 0; i < 3 && i < list.length(); i++)
    relayConfigs[i].enabled = (bool)list[i];
}
void handleButtonStateList(JSONVar list) {
  for (int i = 0; i < 4 && i < list.length(); i++)
    ButtonState[i] = (bool)list[i];
}
void handleButtonGroupList(JSONVar list) {
  for (int i = 0; i < 4 && i < list.length(); i++)
    ButtonConfigs[i].group = (int)list[i];
}

void loop() {
  unsigned long now = millis();
  if (now - lastSend >= sendInterval) {
    lastSend = now;
    heartbeat = !heartbeat;
  }

  mb.task();
  mb.setCoil(SwitchIsts, pcf20.read(1));
  for (int i = 0; i < 4; i++) {
    ButtonState[i] = !pcf27.read(i);  // Assume active-low buttons
  }
  // Handle relays with config
  for (int i = 0; i < 3; i++) {
    bool val = relayState[i];
    if (!relayConfigs[i].enabled) val = false;
    if (relayConfigs[i].inverted) val = !val;

    switch (i) {
      case 0: pcf23.write(3, val); break;
      case 1: pcf23.write(7, val); break;
      case 2: pcf23.write(6, val); break;
    }
  }

  // Lamps (static from coils)
  pcf27.write(7, mb.Coil(Lamp1Coil));
  pcf27.write(6, mb.Coil(Lamp2Coil));
  pcf27.write(5, mb.Coil(Lamp3Coil));
  pcf27.write(4, mb.Coil(Lamp4Coil));

  // Read input values
  inputs = JSONVar();
  for (int i = 0; i < 17; i++) {
    bool val = false;
    if (!digitalInputs[i].enabled) {
      inputs[i] = false;
      continue;
    }

    if (i < 8) val = false;
    else if (i < 16) val = pcf21.read(i - 8);
    else val = pcf23.read(0); // IN17

    if (digitalInputs[i].inverted) val = !val;
    inputs[i] = val;
  }

  // Prepare config arrays
  JSONVar invertList, groupList, enableList;
  JSONVar relayStateList, relayInvertList, relayGroupList, relayEnableList;
  JSONVar ButtonStateList, ButtonGroupList;

  for (int i = 0; i < 17; i++) {
    invertList[i] = digitalInputs[i].inverted;
    groupList[i] = digitalInputs[i].group;
    enableList[i] = digitalInputs[i].enabled;
  }

  for (int i = 0; i < 3; i++) {
    relayStateList[i]  = relayState[i];
    relayInvertList[i] = relayConfigs[i].inverted;
    relayGroupList[i]  = relayConfigs[i].group;
    relayEnableList[i] = relayConfigs[i].enabled;
  }

  for (int i = 0; i < 4; i++) {
    ButtonStateList[i]  = ButtonState[i];
    ButtonGroupList[i]  = ButtonConfigs[i].group;
  }

  // Send all data to UI
  WebSerial.check();
  WebSerial.send("status", modbus);
  WebSerial.send("inputs", inputs);
  WebSerial.send("invertList", invertList);
  WebSerial.send("groupList", groupList);
  WebSerial.send("enableList", enableList);

  WebSerial.send("relayStateList", relayStateList);
  WebSerial.send("relayInvertList", relayInvertList);
  WebSerial.send("relayGroupList", relayGroupList);
  WebSerial.send("relayEnableList", relayEnableList);

  WebSerial.send("ButtonStateList", ButtonStateList);
  WebSerial.send("ButtonGroupList", ButtonGroupList);

  delay(5);
}