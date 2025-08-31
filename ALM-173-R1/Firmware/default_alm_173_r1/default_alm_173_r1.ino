// ==== ALM173-R1 with command pulse coils ===================================
// Adds write-only Modbus coils for "auto-pulse" commands.
// Coils auto-clear after the action is executed.
// ===========================================================================

#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include "hardware/watchdog.h"

// ================== Hardware pins ==================
#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5

// ================== Modbus / RS-485 ==================
const int TxenPin = -1;  // -1 if not using TXEN
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== I2C expanders ==================
PCF8574 pcf20(0x20, &Wire1); // IN1..IN8
PCF8574 pcf21(0x21, &Wire1); // IN9..IN16
PCF8574 pcf23(0x23, &Wire1); // IN17 + Relays (active-low)
PCF8574 pcf27(0x27, &Wire1); // Buttons + User LEDs (active-low)

// ================== Config structures ==================
struct ThreeCfg { bool enabled; bool inverted; uint8_t group; };
struct LedCfg   { uint8_t mode; uint8_t source; };
struct ButtonCfg{ uint8_t action; };

// ================== Runtime state ==================
ThreeCfg  digitalInputs[17];
ThreeCfg  relayConfigs[3];
LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool buttonState[4]   = {false,false,false,false};
bool buttonPrev[4]    = {false,false,false,false};
bool relayOverride[3] = {false,false,false};  // used by LED sources 5..7

// Manual relay override (from Modbus command coils)
bool relayManualActive[3] = {false,false,false};
bool relayManualValue[3]  = {false,false,false};

// Alarm modes: 0=None, 1=non-latched, 2=latched
uint8_t alarmModeList[3] = {0,0,0};
bool    latchedGroup[4]  = {false,false,false,false}; // 1..3 used

// ================== Pin maps ==================
const uint8_t PCF20_INPUT_PINS[8] = {0,1,2,3,7,6,5,4};
const uint8_t PCF21_INPUT_PINS[8] = {0,1,2,3,7,6,5,4};
const uint8_t PCF23_IN17_PIN = 0;
const uint8_t RELAY_PINS[3] = {3, 7, 6};    // ACTIVE-LOW
const uint8_t LED_PINS[4]   = {7, 6, 5, 4}; // ACTIVE-LOW

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 1000;
unsigned long lastBlinkToggle = 0;
const unsigned long blinkPeriodMs = 400;
bool blinkPhase = false;

// ================== Persisted Modbus settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
struct PersistConfig {
  uint32_t magic;       // 'ALM1'
  uint16_t version;
  uint16_t size;
  ThreeCfg  digitalInputs[17];
  ThreeCfg  relayConfigs[3];
  LedCfg    ledCfg[4];
  ButtonCfg buttonCfg[4];
  uint8_t   alarmModeList[3];
  uint8_t   mb_address;
  uint32_t  mb_baud;
  uint32_t  crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314D4C41UL; // 'ALM1'
static const uint16_t CFG_VERSION = 0x0002;
static const char*    CFG_PATH    = "/cfg.bin";

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
const uint32_t  CFG_AUTOSAVE_MS = 1500;

// ================== CRC32 ==================
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *data++;
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}

void setDefaults() {
  for (int i = 0; i < 17; i++) digitalInputs[i] = { true, false, 0 };
  for (int i = 0; i < 3;  i++) relayConfigs[i]   = { true, false, 0 }; // group 0 = no follow
  for (int i = 0; i < 4;  i++) { ledCfg[i] = { 0, 0 }; buttonCfg[i] = { 0 }; }
  for (int i = 0; i < 3;  i++) alarmModeList[i]  = 0;
  g_mb_address = 3;
  g_mb_baud    = 19200;
}

void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);
  memcpy(pc.digitalInputs, digitalInputs, sizeof(digitalInputs));
  memcpy(pc.relayConfigs,  relayConfigs,  sizeof(relayConfigs));
  memcpy(pc.ledCfg,        ledCfg,        sizeof(ledCfg));
  memcpy(pc.buttonCfg,     buttonCfg,     sizeof(buttonCfg));
  memcpy(pc.alarmModeList, alarmModeList, sizeof(alarmModeList));
  pc.mb_address = g_mb_address;
  pc.mb_baud    = g_mb_baud;
  pc.crc32 = 0;
  pc.crc32 = crc32_update(0, reinterpret_cast<const uint8_t*>(&pc), sizeof(PersistConfig));
}

bool applyFromPersist(const PersistConfig &pc) {
  if (pc.magic != CFG_MAGIC) return false;
  if (pc.version != CFG_VERSION) return false;
  if (pc.size != sizeof(PersistConfig)) return false;
  PersistConfig tmp = pc;
  uint32_t crc = tmp.crc32;
  tmp.crc32 = 0;
  if (crc32_update(0, reinterpret_cast<const uint8_t*>(&tmp), sizeof(PersistConfig)) != crc)
    return false;
  memcpy(digitalInputs, pc.digitalInputs, sizeof(digitalInputs));
  memcpy(relayConfigs,  pc.relayConfigs,  sizeof(relayConfigs));
  memcpy(ledCfg,        pc.ledCfg,        sizeof(ledCfg));
  memcpy(buttonCfg,     pc.buttonCfg,     sizeof(buttonCfg));
  memcpy(alarmModeList, pc.alarmModeList, sizeof(alarmModeList));
  g_mb_address = pc.mb_address;
  g_mb_baud    = pc.mb_baud;
  return true;
}

bool saveConfigFS() {
  PersistConfig pc{};
  captureToPersist(pc);
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  size_t n = f.write(reinterpret_cast<const uint8_t*>(&pc), sizeof(pc));
  f.close();
  return n == sizeof(pc);
}

bool loadConfigFS() {
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;
  if (f.size() != sizeof(PersistConfig)) { f.close(); return false; }
  PersistConfig pc{};
  size_t n = f.read(reinterpret_cast<uint8_t*>(&pc), sizeof(pc));
  f.close();
  if (n != sizeof(pc)) return false;
  return applyFromPersist(pc);
}

// ================== SFINAE: setSlaveId if present ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id)
  -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) {
  m.setSlaveId(id);
}
inline void setSlaveIdIfAvailable(...) {}

// ================== Modbus maps ==================
enum : uint16_t {
  // Discrete inputs (telemetry)
  ISTS_DI_BASE   = 1,   // 1..17 : IN1..IN17 (after enable+invert)
  ISTS_AL_ANY    = 50,  // any alarm
  ISTS_AL_G1     = 51,
  ISTS_AL_G2     = 52,
  ISTS_AL_G3     = 53,
  ISTS_RLY_BASE  = 60,  // 60..62 : Relay1..3 effective
  ISTS_LED_BASE  = 90,  // 90..93 : LED1..4 physical

  // Command coils (write 1 -> action -> auto-clear)
  CMD_EN_IN_BASE  = 200, // 200..216 enable IN1..IN17
  CMD_DIS_IN_BASE = 300, // 300..316 disable IN1..IN17
  CMD_RLY_ON_BASE = 400, // 400..402 relay 1..3 ON (manual override)
  CMD_RLY_OFF_BASE= 420, // 420..422 relay 1..3 OFF (manual override)
  CMD_ACK_ALL     = 500, // 500 acknowledge ALL
  CMD_ACK_G1      = 501, // 501..503 acknowledge G1..G3
  CMD_ACK_G2      = 502,
  CMD_ACK_G3      = 503
};

// ================== Forward decls ==================
void applyModbusSettings(uint8_t addr, uint32_t baud);
void handleValues(JSONVar values);
void handleUnifiedConfig(JSONVar obj);
void handleCommand(JSONVar obj);
void performReset();
void processCommandPulses();

bool evalLedSource(uint8_t source, bool anyAlarm, const bool grpActive[4]);
JSONVar LedConfigListFromCfg();
void sendAllEchoesOnce();
void ackAll();
void ackGroup(uint8_t g);

// ================== Handlers (IMPLEMENTATIONS) ==================
// values: { "mb_address": <1..255>, "mb_baud": <9600..115200> }
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);

  applyModbusSettings((uint8_t)addr, (uint32_t)baud);
  WebSerial.send("message", "Modbus configuration updated");

  // mark dirty so autosave persists new address/baud
  cfgDirty = true;
  lastCfgTouchMs = millis();
}

// Config router: expects { t:"...", list:[...] }
void handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"];
  JSONVar list  = obj["list"];
  if (!t) { WebSerial.send("message", "Config: missing 't'"); return; }

  String type = String(t);
  bool changed = false;

  if (type == "inputEnable") {
    for (int i=0;i<17 && i<list.length();i++) digitalInputs[i].enabled = (bool)list[i];
    WebSerial.send("message", "Input Enabled list updated");
    changed = true;

  } else if (type == "inputInvert") {
    for (int i=0;i<17 && i<list.length();i++) digitalInputs[i].inverted = (bool)list[i];
    WebSerial.send("message", "Input Invert list updated");
    changed = true;

  } else if (type == "inputGroup") {
    for (int i=0;i<17 && i<list.length();i++) digitalInputs[i].group = (uint8_t)(int)list[i];
    WebSerial.send("message", "Input Alarm Group list updated");
    changed = true;

  } else if (type == "relays") {
    for (int i = 0; i < 3 && i < list.length(); i++) {
      relayConfigs[i].enabled  = (bool)list[i]["enabled"];
      relayConfigs[i].inverted = (bool)list[i]["inverted"];
      relayConfigs[i].group    = (uint8_t)(int)list[i]["group"];
    }
    WebSerial.send("message", "Relay Configuration updated");
    changed = true;

  } else if (type == "buttons") {
    for (int i = 0; i < 4 && i < list.length(); i++) {
      buttonCfg[i].action = (uint8_t)constrain((int)list[i]["action"], 0, 7);
    }
    WebSerial.send("message", "Buttons Configuration updated");
    changed = true;

  } else if (type == "leds") {
    for (int i = 0; i < 4 && i < list.length(); i++) {
      ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
      ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 7);
    }
    WebSerial.send("message", "LEDs Configuration updated");
    changed = true;

  } else if (type == "alarms") {
    for (int g = 0; g < 3 && g < list.length(); g++) {
      uint8_t m = (uint8_t)constrain((int)list[g], 0, 2);
      if (alarmModeList[g] != m && m != 2) latchedGroup[g+1] = false; // leaving latched -> clear
      alarmModeList[g] = m;
    }
    WebSerial.send("message", "Alarms Configuration updated");
    changed = true;

  } else {
    WebSerial.send("message", String("Unknown Config type: ") + t);
  }

  if (changed) {
    cfgDirty = true;
    lastCfgTouchMs = millis();
  }
}

// ================== Setup ==================
void setup() {
  Serial.begin(57600);

  // I2C init
  Wire1.setSDA(SDA);
  Wire1.setSCL(SCL);
  Wire1.begin();
  pcf20.begin(); pcf21.begin(); pcf23.begin(); pcf27.begin();

  // Outputs OFF (ACTIVE-LOW -> HIGH=OFF)
  for (uint8_t i = 0; i < 3; i++) pcf23.write(RELAY_PINS[i], HIGH);
  for (uint8_t i = 0; i < 4; i++) pcf27.write(LED_PINS[i], HIGH);

  setDefaults();

  // LittleFS
  if (!LittleFS.begin()) {
    WebSerial.send("message", "LittleFS mount failed. Formatting…");
    LittleFS.format();
    LittleFS.begin();
  }
  if (loadConfigFS()) WebSerial.send("message", "Config loaded from flash");
  else { WebSerial.send("message", "No valid config. Using defaults."); saveConfigFS(); }

  // Serial2 / Modbus RTU using persisted baud/address
  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("ALM173");  // comment out if your Modbus lib lacks this

  // ---- Register telemetry (discrete inputs) ----
  for (uint16_t i=0;i<17;i++) mb.addIsts(ISTS_DI_BASE + i);
  mb.addIsts(ISTS_AL_ANY); mb.addIsts(ISTS_AL_G1); mb.addIsts(ISTS_AL_G2); mb.addIsts(ISTS_AL_G3);
  for (uint16_t i=0;i<3;i++) mb.addIsts(ISTS_RLY_BASE + i);
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_LED_BASE + i);

  // ---- Register command coils ----
  for (uint16_t i=0;i<17;i++) { mb.addCoil(CMD_EN_IN_BASE  + i); mb.addCoil(CMD_DIS_IN_BASE + i); }
  for (uint16_t r=0;r<3;r++)  { mb.addCoil(CMD_RLY_ON_BASE + r); mb.addCoil(CMD_RLY_OFF_BASE+ r); }
  mb.addCoil(CMD_ACK_ALL); mb.addCoil(CMD_ACK_G1); mb.addCoil(CMD_ACK_G2); mb.addCoil(CMD_ACK_G3);

  // Status defaults for UI
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  // WebSerial handlers
  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);

  WebSerial.send("message", "Boot OK");
  sendAllEchoesOnce();
}

// ================== Command handler (reset + persistence ops) ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) { WebSerial.send("message", "command: missing 'action'"); return; }
  String act = String(actC); act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    saveConfigFS();
    WebSerial.send("message", "Rebooting now…");
    delay(120);
    performReset(); // no return
  } else if (act == "save") {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else WebSerial.send("message", "ERROR: Save failed");
  } else if (act == "load") {
    if (loadConfigFS()) {
      WebSerial.send("message", "Configuration loaded");
      sendAllEchoesOnce();
      applyModbusSettings(g_mb_address, g_mb_baud);
    } else WebSerial.send("message", "ERROR: Load failed/invalid");
  } else if (act == "factory") {
    setDefaults();
    if (saveConfigFS()) {
      WebSerial.send("message", "Factory defaults restored & saved");
      sendAllEchoesOnce();
      applyModbusSettings(g_mb_address, g_mb_baud);
    } else WebSerial.send("message", "ERROR: Save after factory reset failed");
  } else {
    WebSerial.send("message", String("Unknown command: ") + actC);
  }
}

// ================== RP2350 reset helper ==================
void performReset() {
  if (Serial) Serial.flush();
  delay(50);
  watchdog_reboot(0, 0, 0);
  while (true) { __asm__("wfi"); }
}

// ================== Apply Modbus settings ==================
void applyModbusSettings(uint8_t addr, uint32_t baud) {
  if ((uint32_t)modbusStatus["baud"] != baud) {
    Serial2.end();
    Serial2.begin(baud);
    mb.config(baud);
  }
  setSlaveIdIfAvailable(mb, addr);
  g_mb_address = addr;
  g_mb_baud    = baud;
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
}

// ================== Ack helpers ==================
void ackAll() { latchedGroup[1] = latchedGroup[2] = latchedGroup[3] = false; }
void ackGroup(uint8_t g) { if (g >= 1 && g <= 3) latchedGroup[g] = false; }

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  // Blink phase
  if (now - lastBlinkToggle >= blinkPeriodMs) {
    lastBlinkToggle = now;
    blinkPhase = !blinkPhase;
  }

  // Auto-save after quiet period
  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  // Modbus stack
  mb.task();

  // Handle incoming command pulses
  processCommandPulses();

  // -------- Buttons: read (ACTIVE-LOW), rising edge ----------
  for (int i = 0; i < 4; i++) {
    bool pressed = !pcf27.read(i); // ACTIVE-LOW
    buttonPrev[i] = buttonState[i];
    buttonState[i] = pressed;

    if (!buttonPrev[i] && buttonState[i]) {
      uint8_t act = buttonCfg[i].action;
      if (act == 1)       ackAll();
      else if (act >= 2 && act <= 4) ackGroup(act - 1);
      else if (act >= 5 && act <= 7) {
        int r = act - 5;
        if (r >= 0 && r < 3) relayOverride[r] = !relayOverride[r];
      }
    }
  }

  // -------- Inputs (17) + Alarm evaluation ----------
  bool grpCondition[4] = {false,false,false,false};
  JSONVar inputs;

  for (int i = 0; i < 17; i++) {
    bool val = false;
    if (digitalInputs[i].enabled) {
      if (i < 8)       val = pcf20.read(PCF20_INPUT_PINS[i]);
      else if (i < 16) val = pcf21.read(PCF21_INPUT_PINS[i - 8]);
      else             val = pcf23.read(PCF23_IN17_PIN);
      if (digitalInputs[i].inverted) val = !val;
    }
    inputs[i] = val;
    uint8_t g = digitalInputs[i].group;
    if (val && g >= 1 && g <= 3) grpCondition[g] = true;
  }

  // Alarm groups (0=None, 1=non-latched, 2=latched)
  bool grpAlarmActive[4] = {false,false,false,false};
  for (int g = 1; g <= 3; g++) {
    switch (alarmModeList[g-1]) {
      case 0: grpAlarmActive[g] = false;           latchedGroup[g] = false; break;
      case 1: grpAlarmActive[g] = grpCondition[g]; latchedGroup[g] = false; break;
      case 2:
        if (grpCondition[g]) latchedGroup[g] = true;
        grpAlarmActive[g] = grpCondition[g] || latchedGroup[g];
        break;
    }
  }
  bool anyAlarmActive = grpAlarmActive[1] || grpAlarmActive[2] || grpAlarmActive[3];

  // -------- Relays: manual override OR follow group ----------
  JSONVar relayStateList;
  for (int r = 0; r < 3; r++) {
    bool desired = false;
    if (relayManualActive[r]) {
      desired = relayManualValue[r];
    } else {
      uint8_t g = relayConfigs[r].group; // 0..3
      if (g >= 1 && g <= 3) desired = grpAlarmActive[g];
    }
    // Apply enable + inversion
    bool outVal = desired;
    if (!relayConfigs[r].enabled) outVal = false;
    if (relayConfigs[r].inverted) outVal = !outVal;
    relayStateList[r] = outVal;
    pcf23.write(RELAY_PINS[r], outVal ? LOW : HIGH); // ACTIVE-LOW
  }

  // -------- User LEDs (ACTIVE-LOW) ----------
  JSONVar LedStateList;
  for (int i = 0; i < 4; i++) {
    bool active = evalLedSource(ledCfg[i].source, anyAlarmActive, grpAlarmActive);
    bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase);
    LedStateList[i] = phys;
    pcf27.write(LED_PINS[i], phys ? LOW : HIGH); // ACTIVE-LOW
  }

  // ---- Publish telemetry to Modbus discrete inputs ----
  for (int i = 0; i < 17; i++) mb.setIsts(ISTS_DI_BASE + i, (bool)inputs[i]);
  mb.setIsts(ISTS_AL_ANY, anyAlarmActive);
  mb.setIsts(ISTS_AL_G1 , grpAlarmActive[1]);
  mb.setIsts(ISTS_AL_G2 , grpAlarmActive[2]);
  mb.setIsts(ISTS_AL_G3 , grpAlarmActive[3]);
  for (int r=0; r<3; r++) mb.setIsts(ISTS_RLY_BASE + r, (bool)relayStateList[r]);
  for (int l=0; l<4; l++) mb.setIsts(ISTS_LED_BASE + l, (bool)LedStateList[l]);

  // ---- Web echo ----
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
  JSONVar LedConfigList = LedConfigListFromCfg();

  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();
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
    WebSerial.send("LedConfigList", LedConfigList);
    WebSerial.send("LedStateList", LedStateList);
    JSONVar am; for (int g = 0; g < 3; g++) am[g] = alarmModeList[g];
    WebSerial.send("AlarmModeList", am);
    JSONVar AlarmState; AlarmState["any"] = anyAlarmActive;
    { JSONVar groups; for (int g=1; g<=3; g++) groups[g-1] = grpAlarmActive[g]; AlarmState["groups"] = groups; }
    WebSerial.send("AlarmState", AlarmState);
  }
}

// ================== helpers ==================
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
    case 0: return false;
    case 1: return anyAlarm;
    case 2: return grpActive[1];
    case 3: return grpActive[2];
    case 4: return grpActive[3];
    case 5: return relayOverride[0];
    case 6: return relayOverride[1];
    case 7: return relayOverride[2];
    default: return false;
  }
}

void sendAllEchoesOnce() {
  JSONVar am; for (int g = 0; g < 3; g++) am[g] = alarmModeList[g];
  WebSerial.send("AlarmModeList", am);

  JSONVar enableList, invertList, groupList;
  for (int i = 0; i < 17; i++) {
    enableList[i] = digitalInputs[i].enabled;
    invertList[i] = digitalInputs[i].inverted;
    groupList[i]  = digitalInputs[i].group;
  }
  WebSerial.send("enableList", enableList);
  WebSerial.send("invertList", invertList);
  WebSerial.send("groupList",  groupList);

  JSONVar relayEnableList, relayInvertList, relayGroupList;
  for (int i = 0; i < 3; i++) {
    relayEnableList[i] = relayConfigs[i].enabled;
    relayInvertList[i] = relayConfigs[i].inverted;
    relayGroupList[i]  = relayConfigs[i].group;
  }
  WebSerial.send("relayEnableList", relayEnableList);
  WebSerial.send("relayInvertList", relayInvertList);
  WebSerial.send("relayGroupList",  relayGroupList);

  JSONVar ButtonGroupList;
  for (int i = 0; i < 4; i++) ButtonGroupList[i] = buttonCfg[i].action;
  WebSerial.send("ButtonGroupList", ButtonGroupList);

  WebSerial.send("LedConfigList", LedConfigListFromCfg());

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}

// ---- Process "auto-pulse" command coils and clear them ----
void processCommandPulses() {
  // Inputs enable/disable
  for (int i = 0; i < 17; i++) {
    uint16_t enAddr  = CMD_EN_IN_BASE  + i;
    uint16_t disAddr = CMD_DIS_IN_BASE + i;
    if (mb.Coil(enAddr)) {
      digitalInputs[i].enabled = true;
      cfgDirty = true; lastCfgTouchMs = millis();
      mb.Coil(enAddr, false); // auto-clear
    }
    if (mb.Coil(disAddr)) {
      digitalInputs[i].enabled = false;
      cfgDirty = true; lastCfgTouchMs = millis();
      mb.Coil(disAddr, false); // auto-clear
    }
  }

  // Relay overrides
  for (int r = 0; r < 3; r++) {
    uint16_t onAddr  = CMD_RLY_ON_BASE  + r;
    uint16_t offAddr = CMD_RLY_OFF_BASE + r;
    if (mb.Coil(onAddr)) {
      relayManualActive[r] = true;
      relayManualValue[r]  = true;
      mb.Coil(onAddr, false);
    }
    if (mb.Coil(offAddr)) {
      relayManualActive[r] = true;
      relayManualValue[r]  = false;
      mb.Coil(offAddr, false);
    }
  }

  // Acknowledge alarms
  if (mb.Coil(CMD_ACK_ALL)) { ackAll(); mb.Coil(CMD_ACK_ALL, false); }
  if (mb.Coil(CMD_ACK_G1 )) { ackGroup(1); mb.Coil(CMD_ACK_G1 , false); }
  if (mb.Coil(CMD_ACK_G2 )) { ackGroup(2); mb.Coil(CMD_ACK_G2 , false); }
  if (mb.Coil(CMD_ACK_G3 )) { ackGroup(3); mb.Coil(CMD_ACK_G3 , false); }
}
