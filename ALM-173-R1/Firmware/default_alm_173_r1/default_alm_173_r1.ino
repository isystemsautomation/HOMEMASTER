#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>               // persistence
#include <utility>                  // std::declval for SFINAE
#include "hardware/watchdog.h"      // RP2350 reset

// ================== Hardware pins ==================
#define SDA 6
#define SCL 7
#define TX2 4
#define RX2 5

// ================== Modbus / RS-485 ==================
const int TxenPin = -1;  // -1 if not using TXEN
int SlaveId = 1;         // compile-time default; may be overridden at runtime
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// Modbus coil indices (protocol compatibility)
const int Lamp1Coil = 0, Lamp2Coil = 1, Lamp3Coil = 2, Lamp4Coil = 3;
const int Relay1Coil = 4, Relay2Coil = 5, Relay3Coil = 6;
const int SwitchIsts = 11;

// ================== I2C expanders ==================
PCF8574 pcf20(0x20, &Wire1); // IN1..IN8
PCF8574 pcf21(0x21, &Wire1); // IN9..IN16
PCF8574 pcf23(0x23, &Wire1); // IN17 + Relays (active-low)
PCF8574 pcf27(0x27, &Wire1); // Buttons P0..P3 (active-low) + User LEDs (active-low)

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
bool relayOverride[3] = {false,false,false};       // used by LED sources 5..7

// Alarm modes: 0=None, 1=non-latched, 2=latched
uint8_t alarmModeList[3] = {0,0,0};
bool    latchedGroup[4]  = {false,false,false,false}; // use indexes 1..3

// ================== Pin maps (your wiring) ==================
// IN1..IN8 -> pcf20 pins
const uint8_t PCF20_INPUT_PINS[8] = {0,1,2,3,7,6,5,4};
// IN9..IN16 -> pcf21 pins
const uint8_t PCF21_INPUT_PINS[8] = {0,1,2,3,7,6,5,4};
// IN17 -> pcf23 P0
const uint8_t PCF23_IN17_PIN = 0;
// Relays on pcf23 (ACTIVE-LOW)
const uint8_t RELAY_PINS[3] = {3, 7, 6};  // R1=P3, R2=P7, R3=P6
// User LEDs on pcf27 (ACTIVE-LOW)
const uint8_t LED_PINS[4] = {7, 6, 4, 3}; // LED1=P7, LED2=P6, LED3=P4, LED4=P3

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
  uint16_t version;     // bump when structure changes
  uint16_t size;        // sizeof(PersistConfig)
  ThreeCfg  digitalInputs[17];
  ThreeCfg  relayConfigs[3];
  LedCfg    ledCfg[4];
  ButtonCfg buttonCfg[4];
  uint8_t   alarmModeList[3];
  uint8_t   mb_address; // NEW
  uint32_t  mb_baud;    // NEW
  uint32_t  crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314D4C41UL; // 'ALM1'
static const uint16_t CFG_VERSION = 0x0002;       // bumped because we added Modbus fields
static const char*    CFG_PATH    = "/cfg.bin";

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
const uint32_t  CFG_AUTOSAVE_MS = 1500;

// CRC32 helper
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
  for (int i = 0; i < 3;  i++) relayConfigs[i]   = { true, false, 0 };
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

// ================== SFINAE helper: setSlaveId if present ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id)
  -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) {
  m.setSlaveId(id);
}
inline void setSlaveIdIfAvailable(...) {}

// ================== Fixed Modbus **state** map (discrete inputs) ==================
enum : uint16_t {
  ISTS_DI_BASE   = 1,   // 1..17  : IN1..IN17 state (after enable+invert)
  ISTS_AL_ANY    = 50,  // 50     : any alarm
  ISTS_AL_G1     = 51,  // 51     : group 1
  ISTS_AL_G2     = 52,  // 52     : group 2
  ISTS_AL_G3     = 53,  // 53     : group 3
  ISTS_RLY_BASE  = 60,  // 60..62 : Relay1..3 state (1=ON)
  ISTS_LED_BASE  = 90   // 90..93 : LED1..4 state (1=ON)
};

// ================== Forward decls ==================
void applyModbusSettings(uint8_t addr, uint32_t baud);
void handleValues(JSONVar values);
void handleUnifiedConfig(JSONVar obj);
void handleCommand(JSONVar obj);
void performReset();

bool evalLedSource(uint8_t source, bool anyAlarm, const bool grpActive[4]);
JSONVar LedConfigListFromCfg();
void sendAllEchoesOnce();

void ackAll();
void ackGroup(uint8_t g);

// ================== Setup ==================
void setup() {
  Serial.begin(57600);

  // I2C init
  Wire1.setSDA(SDA);
  Wire1.setSCL(SCL);
  Wire1.begin();
  pcf20.begin(); pcf21.begin(); pcf23.begin(); pcf27.begin();

  // Initialize outputs to OFF (ACTIVE-LOW -> drive HIGH to turn OFF)
  for (uint8_t i = 0; i < 3; i++) pcf23.write(RELAY_PINS[i], HIGH); // relays OFF
  for (uint8_t i = 0; i < 4; i++) pcf27.write(LED_PINS[i], HIGH);   // user LEDs OFF

  // Defaults before trying to load
  setDefaults();

  // ---- LittleFS mount + load persisted config ----
  if (!LittleFS.begin()) {
    WebSerial.send("message", "LittleFS mount failed. Formatting…");
    LittleFS.format();
    LittleFS.begin();
  }
  if (loadConfigFS()) {
    WebSerial.send("message", "Config loaded from flash");
  } else {
    WebSerial.send("message", "No valid config. Using defaults.");
    saveConfigFS(); // create an initial file
  }

  // Serial2 / Modbus RTU using persisted baud/address
  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("ALM173");

  // Legacy demo coils (keep existing)
  mb.addCoil(Lamp1Coil); mb.addCoil(Lamp2Coil); mb.addCoil(Lamp3Coil); mb.addCoil(Lamp4Coil);
  mb.addCoil(Relay1Coil); mb.addCoil(Relay2Coil); mb.addCoil(Relay3Coil);
  mb.addCoil(SwitchIsts);

  // ==== Modbus states BEGIN: register discrete-input addresses ====
  for (uint16_t i=0;i<17;i++) mb.addIsts(ISTS_DI_BASE + i);   // inputs 1..17
  mb.addIsts(ISTS_AL_ANY);  mb.addIsts(ISTS_AL_G1); mb.addIsts(ISTS_AL_G2); mb.addIsts(ISTS_AL_G3);
  for (uint16_t i=0;i<3;i++)  mb.addIsts(ISTS_RLY_BASE + i);  // relays 1..3
  for (uint16_t i=0;i<4;i++)  mb.addIsts(ISTS_LED_BASE + i);  // leds 1..4
  // ==== Modbus states END ====

  // Status defaults for UI
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  // WebSerial handlers
  WebSerial.on("values",  handleValues);        // { mb_address, mb_baud }
  WebSerial.on("Config",  handleUnifiedConfig); // { t: "...", list: [...] }
  WebSerial.on("command", handleCommand);       // { action: "reset"/"save"/"load"/"factory" }

  WebSerial.send("message", "Boot OK");
  sendAllEchoesOnce();
}

// ================== Command handler (reset + persistence ops) ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) { WebSerial.send("message", "command: missing 'action'"); return; }
  String act = String(actC); act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    // Save before reboot (best-effort)
    saveConfigFS();
    WebSerial.send("message", "Rebooting now… Communication will briefly disconnect.");
    delay(120);
    performReset(); // does not return
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
  watchdog_reboot(0, 0, 0);   // immediate reboot (RP2350/Pico SDK)
  while (true) { __asm__("wfi"); }
}

// ================== Apply Modbus settings (baud + optional slave id) ==================
void applyModbusSettings(uint8_t addr, uint32_t baud) {
  // Reconfigure UART + Modbus stack if baud changed
  if ((uint32_t)modbusStatus["baud"] != baud) {
    Serial2.end();
    Serial2.begin(baud);
    mb.config(baud);
  }
  // Set slave id if the library exposes it
  setSlaveIdIfAvailable(mb, addr);

  g_mb_address = addr;
  g_mb_baud    = baud;

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
}

// ================== Handlers ==================
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

// Unified Config router: expects { t:"...", list:[...] }
void handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"];
  JSONVar list  = obj["list"];
  if (!t) return;

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
    WebSerial.send("message", "Unknown command received");
  }

  // Mark config dirty for auto-save
  if (changed) {
    cfgDirty = true;
    lastCfgTouchMs = millis();
  }
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

  // Modbus poller
  mb.task();

  // -------- Buttons: read (ACTIVE-LOW on pcf27 P0..P3), rising edge ----------
  for (int i = 0; i < 4; i++) {
    bool pressed = !pcf27.read(i); // ACTIVE-LOW
    buttonPrev[i] = buttonState[i];
    buttonState[i] = pressed;

    if (!buttonPrev[i] && buttonState[i]) {
      uint8_t act = buttonCfg[i].action;
      if (act == 1) {               // Ack All
        ackAll();
      } else if (act >= 2 && act <= 4) { // Ack Group 1..3
        ackGroup(act - 1);
      } else if (act >= 5 && act <= 7) { // Toggle relay override flags
        int r = act - 5; // 0..2
        if (r >= 0 && r < 3) relayOverride[r] = !relayOverride[r];
      }
    }
  }

  // -------- Relays from Modbus coils (ACTIVE-LOW on pcf23) ----------
  JSONVar relayStateList;
  for (int i = 0; i < 3; i++) {
    bool coilState = mb.Coil(Relay1Coil + i);   // logical desired state
    relayStateList[i] = coilState;

    // Apply UI config
    bool outVal = coilState;
    if (!relayConfigs[i].enabled) outVal = false;
    if (relayConfigs[i].inverted) outVal = !outVal;

    // Hardware is ACTIVE-LOW: LOW=ON, HIGH=OFF
    pcf23.write(RELAY_PINS[i], outVal ? LOW : HIGH);
  }

  // -------- Inputs (17) + Alarm evaluation ----------
  bool grpCondition[4] = {false,false,false,false}; // per-group condition (1..3)
  JSONVar inputs;

  for (int i = 0; i < 17; i++) {
    bool val = false;
    if (digitalInputs[i].enabled) {
      if (i < 8) {
        val = pcf20.read(PCF20_INPUT_PINS[i]);
      } else if (i < 16) {
        val = pcf21.read(PCF21_INPUT_PINS[i - 8]);
      } else {
        val = pcf23.read(PCF23_IN17_PIN);
      }
      if (digitalInputs[i].inverted) val = !val;
    }
    inputs[i] = val;

    uint8_t g = digitalInputs[i].group; // 0..3
    if (val && g >= 1 && g <= 3) grpCondition[g] = true;
  }

  // Compute alarm active per group based on mode + latches
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

  // -------- User LEDs on pcf27 (ACTIVE-LOW) ----------
  JSONVar LedStateList;
  for (int i = 0; i < 4; i++) {
    bool active = evalLedSource(ledCfg[i].source, anyAlarmActive, grpAlarmActive);
    bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase); // desired logical ON
    LedStateList[i] = phys;
    // Hardware ACTIVE-LOW: LOW=ON, HIGH=OFF
    pcf27.write(LED_PINS[i], phys ? LOW : HIGH);
  }

  // ==== Modbus states BEGIN: publish all states to discrete inputs ====
  // Digital inputs 1..17 (after enable+invert)
  for (int i = 0; i < 17; i++) {
    bool di = (bool)inputs[i];
    mb.setIsts(ISTS_DI_BASE + i, di);
  }
  // Alarms
  mb.setIsts(ISTS_AL_ANY, anyAlarmActive);
  mb.setIsts(ISTS_AL_G1 , grpAlarmActive[1]);
  mb.setIsts(ISTS_AL_G2 , grpAlarmActive[2]);
  mb.setIsts(ISTS_AL_G3 , grpAlarmActive[3]);
  // Relays (as actually driven)
  for (int r=0; r<3; r++) {
    bool coilState = (bool)relayStateList[r];
    // reflect the *effective* output after enable/invert
    bool outVal = coilState;
    if (!relayConfigs[r].enabled) outVal = false;
    if (relayConfigs[r].inverted) outVal = !outVal;
    mb.setIsts(ISTS_RLY_BASE + r, outVal);
  }
  // LEDs (physical)
  for (int l=0; l<4; l++) {
    bool ledOn = (bool)LedStateList[l];
    mb.setIsts(ISTS_LED_BASE + l, ledOn);
  }
  // ==== Modbus states END ====

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

  JSONVar ButtonStateList, ButtonGroupList;
  for (int i = 0; i < 4; i++) {
    ButtonStateList[i] = buttonState[i];
    ButtonGroupList[i] = buttonCfg[i].action;
  }

  JSONVar LedConfigList = LedConfigListFromCfg();

  // Alarm Mode echo
  JSONVar AlarmModeList;
  for (int g = 0; g < 3; g++) AlarmModeList[g] = alarmModeList[g];

  // Alarm State { any, groups:[g1,g2,g3] }
  JSONVar AlarmState;
  AlarmState["any"] = anyAlarmActive;
  {
    JSONVar groups;
    for (int g = 1; g <= 3; g++) groups[g-1] = grpAlarmActive[g];
    AlarmState["groups"] = groups;
  }

  // -------- Send to Web -------- (unchanged)
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
    WebSerial.send("AlarmModeList", AlarmModeList);
    WebSerial.send("AlarmState",   AlarmState);
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
    case 0: return false;            // None
    case 1: return anyAlarm;         // Any alarm active
    case 2: return grpActive[1];     // Group 1
    case 3: return grpActive[2];     // Group 2
    case 4: return grpActive[3];     // Group 3
    case 5: return relayOverride[0]; // Relay 1 overridden
    case 6: return relayOverride[1]; // Relay 2 overridden
    case 7: return relayOverride[2]; // Relay 3 overridden
    default: return false;
  }
}

// Send initial states/config so UI shows defaults after connect
void sendAllEchoesOnce() {
  // Send current alarm modes so UI selects are set
  JSONVar am;
  for (int g = 0; g < 3; g++) am[g] = alarmModeList[g];
  WebSerial.send("AlarmModeList", am);

  // Echo inputs config
  JSONVar enableList, invertList, groupList;
  for (int i = 0; i < 17; i++) {
    enableList[i] = digitalInputs[i].enabled;
    invertList[i] = digitalInputs[i].inverted;
    groupList[i]  = digitalInputs[i].group;
  }
  WebSerial.send("enableList", enableList);
  WebSerial.send("invertList", invertList);
  WebSerial.send("groupList",  groupList);

  // Echo relays config
  JSONVar relayEnableList, relayInvertList, relayGroupList;
  for (int i = 0; i < 3; i++) {
    relayEnableList[i] = relayConfigs[i].enabled;
    relayInvertList[i] = relayConfigs[i].inverted;
    relayGroupList[i]  = relayConfigs[i].group;
  }
  WebSerial.send("relayEnableList", relayEnableList);
  WebSerial.send("relayInvertList", relayInvertList);
  WebSerial.send("relayGroupList",  relayGroupList);

  // Echo buttons config
  JSONVar ButtonGroupList;
  for (int i = 0; i < 4; i++) ButtonGroupList[i] = buttonCfg[i].action;
  WebSerial.send("ButtonGroupList", ButtonGroupList);

  // Echo LED config
  WebSerial.send("LedConfigList", LedConfigListFromCfg());

  // Status
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}
