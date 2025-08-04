#include <Arduino.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include "hardware/watchdog.h"

// ================== UART2 (RS-485 / Modbus) ==================
#define TX2 4
#define RX2 5
const int TxenPin = -1;          // -1 if RS-485 TXEN not used
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== GPIO MAP (direct) ==================
static const uint8_t DI_PINS[4]    = {7, 12, 11, 6};   // DI1..DI4
static const uint8_t RELAY_PINS[3] = {10, 9, 8};       // R1..R3 (active-HIGH)
static const uint8_t LED_PINS[3]   = {13, 14, 15};     // LED1..LED3 (active-HIGH)
static const uint8_t BTN_PINS[3]   = {1, 2, 3};        // Button1..3 (active-LOW, pullups)

// ================== Sizes ==================
static const uint8_t NUM_DI  = 4;
static const uint8_t NUM_RLY = 3;
static const uint8_t NUM_LED = 3;
static const uint8_t NUM_BTN = 3;

// ================== Config & runtime ==================
struct InCfg  { bool enabled; bool inverted; uint8_t action; /*0=None,1=Toggle,2=Pulse*/ uint8_t target; /*4=None,0=All,1..3=R1..R3*/ };
struct RlyCfg { bool enabled; bool inverted; };
struct LedCfg { uint8_t mode;   /*0=steady,1=blink*/ 
                uint8_t source; /*0=None, 5..7=Overridden relay 1..3*/ };
struct BtnCfg { uint8_t action; /*0=None, 5..7=Relay1..3 override toggle*/ };

InCfg   diCfg[NUM_DI];
RlyCfg  rlyCfg[NUM_RLY];
LedCfg  ledCfg[NUM_LED];
BtnCfg  btnCfg[NUM_BTN];

bool buttonState[NUM_BTN]   = {false,false,false};
bool buttonPrev[NUM_BTN]    = {false,false,false};
bool diState[NUM_DI]        = {false,false,false,false};
bool diPrev[NUM_DI]         = {false,false,false,false};

// Desired relay state (PLC/command, DI actions, or buttons set this)
bool desiredRelay[NUM_RLY] = {false,false,false};

// Pulse handling for relays (when DI action=Pulse)
uint32_t rlyPulseUntil[NUM_RLY] = {0,0,0};
const uint32_t PULSE_MS = 500; // default pulse width

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 250;
unsigned long lastBlinkToggle = 0;
const unsigned long blinkPeriodMs = 400;
bool blinkPhase = false;

// ================== Persisted Modbus settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
struct PersistConfig {
  uint32_t magic;  uint16_t version;  uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  LedCfg  ledCfg[NUM_LED];
  BtnCfg  btnCfg[NUM_BTN];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address;
  uint32_t mb_baud;
  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314D4C41UL; // 'ALM1'
static const uint16_t CFG_VERSION = 0x0007;       // bumped: LED source added
static const char*    CFG_PATH    = "/cfg.bin";

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
const uint32_t  CFG_AUTOSAVE_MS = 1500;

// ================== Utils ==================
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *data++;
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}
inline bool timeAfter32(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

// ================== Defaults / persist ==================
void setDefaults() {
  for (int i = 0; i < NUM_DI;  i++) diCfg[i]  = { true, false, 0 /*None*/, 0 /*All*/ };
  for (int i = 0; i < NUM_RLY; i++) rlyCfg[i] = { true, false };
  for (int i = 0; i < NUM_LED; i++) ledCfg[i] = { 0 /*steady*/, 0 /*source: None*/ };
  for (int i = 0; i < NUM_BTN; i++) btnCfg[i] = { 0 };
  for (int i = 0; i < NUM_RLY; i++) { desiredRelay[i] = false; rlyPulseUntil[i] = 0; }
  g_mb_address = 3; g_mb_baud = 19200;
}

void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC; pc.version = CFG_VERSION; pc.size = sizeof(PersistConfig);
  memcpy(pc.diCfg,        diCfg,        sizeof(diCfg));
  memcpy(pc.rlyCfg,       rlyCfg,       sizeof(rlyCfg));
  memcpy(pc.ledCfg,       ledCfg,       sizeof(ledCfg));
  memcpy(pc.btnCfg,       btnCfg,       sizeof(btnCfg));
  memcpy(pc.desiredRelay, desiredRelay, sizeof(desiredRelay));
  pc.mb_address = g_mb_address; pc.mb_baud = g_mb_baud;
  pc.crc32 = 0; pc.crc32 = crc32_update(0, (const uint8_t*)&pc, sizeof(PersistConfig));
}

bool applyFromPersist(const PersistConfig &pc) {
  if (pc.magic != CFG_MAGIC || pc.size != sizeof(PersistConfig)) return false;
  PersistConfig tmp = pc; uint32_t crc = tmp.crc32; tmp.crc32 = 0;
  if (crc32_update(0, (const uint8_t*)&tmp, sizeof(PersistConfig)) != crc) return false;
  if (pc.version != CFG_VERSION) return false;

  memcpy(diCfg,        pc.diCfg,        sizeof(diCfg));
  memcpy(rlyCfg,       pc.rlyCfg,       sizeof(rlyCfg));
  memcpy(ledCfg,       pc.ledCfg,       sizeof(ledCfg));
  memcpy(btnCfg,       pc.btnCfg,       sizeof(btnCfg));
  memcpy(desiredRelay, pc.desiredRelay, sizeof(desiredRelay));
  g_mb_address = pc.mb_address; g_mb_baud = pc.mb_baud;
  return true;
}

bool saveConfigFS() {
  PersistConfig pc{}; captureToPersist(pc);
  File f = LittleFS.open(CFG_PATH, "w"); if (!f) return false;
  size_t n = f.write((const uint8_t*)&pc, sizeof(pc)); f.close();
  return n == sizeof(pc);
}
bool loadConfigFS() {
  File f = LittleFS.open(CFG_PATH, "r"); if (!f) return false;
  if (f.size() != sizeof(PersistConfig)) { f.close(); return false; }
  PersistConfig pc{}; size_t n = f.read((uint8_t*)&pc, sizeof(pc)); f.close();
  if (n != sizeof(pc)) return false; return applyFromPersist(pc);
}

// ================== SFINAE helper ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id)
  -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) { m.setSlaveId(id); }
inline void setSlaveIdIfAvailable(...) {}

// ================== Modbus state (FC=02) ==================
enum : uint16_t {
  ISTS_DI_BASE   = 1,   // 1..4 : IN1..IN4 (after enable+invert)
  ISTS_RLY_BASE  = 60,  // 60..62 : RELAY1..3 logical state
  ISTS_LED_BASE  = 90   // 90..92 : LED1..3 logical state
};

// ================== Modbus command coils (FC=05/15; pulse) ==================
enum : uint16_t {
  CMD_RLY_ON_BASE   = 200,  // 200..202 : pulse turn Relay1..3 ON
  CMD_RLY_OFF_BASE  = 210,  // 210..212 : pulse turn Relay1..3 OFF
  CMD_DI_EN_BASE    = 300,  // 300..303 : pulse ENABLE  IN1..IN4
  CMD_DI_DIS_BASE   = 320   // 320..323 : pulse DISABLE IN1..IN4
};

// ================== Fw decls ==================
void applyModbusSettings(uint8_t addr, uint32_t baud);
void handleValues(JSONVar values);
void handleUnifiedConfig(JSONVar obj);
void handleCommand(JSONVar obj);
void performReset();
JSONVar LedConfigListFromCfg();
void sendAllEchoesOnce();
void processModbusCommandPulses();
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now);

// ================== Setup ==================
void setup() {
  Serial.begin(57600);

  // GPIO directions
  for (uint8_t i=0;i<NUM_DI;i++)   pinMode(DI_PINS[i],   INPUT);           // change to INPUT_PULLUP if needed
  for (uint8_t i=0;i<NUM_RLY;i++)  { pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], LOW); } // OFF
  for (uint8_t i=0;i<NUM_LED;i++)  { pinMode(LED_PINS[i],   OUTPUT);  digitalWrite(LED_PINS[i],   LOW); } // OFF
  for (uint8_t i=0;i<NUM_BTN;i++)  pinMode(BTN_PINS[i],   INPUT_PULLUP);   // active-LOW

  setDefaults();

  // LittleFS
  if (!LittleFS.begin()) { WebSerial.send("message", "LittleFS mount failed. Formatting…"); LittleFS.format(); LittleFS.begin(); }
  if (loadConfigFS()) WebSerial.send("message", "Config loaded from flash");
  else { WebSerial.send("message", "No valid config. Using defaults."); saveConfigFS(); }

  // Serial2 / Modbus
  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud); mb.config(g_mb_baud); setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("DIO430-DIDO");

  // ==== Modbus states (discrete inputs) ====
  for (uint16_t i=0;i<NUM_DI;i++)  mb.addIsts(ISTS_DI_BASE + i);
  for (uint16_t i=0;i<NUM_RLY;i++) mb.addIsts(ISTS_RLY_BASE + i);
  for (uint16_t i=0;i<NUM_LED;i++) mb.addIsts(ISTS_LED_BASE + i);

  // ==== Modbus command pulses (coils) ====
  for (uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_ON_BASE  + i);  mb.setCoil(CMD_RLY_ON_BASE  + i, false); }
  for (uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_OFF_BASE + i);  mb.setCoil(CMD_RLY_OFF_BASE + i, false); }
  for (uint16_t i=0;i<NUM_DI;i++)  { mb.addCoil(CMD_DI_EN_BASE   + i);  mb.setCoil(CMD_DI_EN_BASE   + i, false); }
  for (uint16_t i=0;i<NUM_DI;i++)  { mb.addCoil(CMD_DI_DIS_BASE  + i);  mb.setCoil(CMD_DI_DIS_BASE  + i, false); }

  // Status for UI
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);

  WebSerial.send("message", "Boot OK (DI actions: None/Toggle/Pulse; targets: None/All/R1/R2/R3; LED source: None/Overridden R1..R3)");
  sendAllEchoesOnce();
}

// ================== Command handler / reset ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) { WebSerial.send("message", "command: missing 'action'"); return; }
  String act = String(actC); act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    saveConfigFS();
    WebSerial.send("message", "Rebooting now…");
    delay(120); performReset();
  } else if (act == "save") {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved"); else WebSerial.send("message", "ERROR: Save failed");
  } else if (act == "load") {
    if (loadConfigFS()) { WebSerial.send("message", "Configuration loaded"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address, g_mb_baud); }
    else WebSerial.send("message", "ERROR: Load failed/invalid");
  } else if (act == "factory") {
    setDefaults(); if (saveConfigFS()) { WebSerial.send("message", "Factory defaults restored & saved"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address, g_mb_baud); }
    else WebSerial.send("message", "ERROR: Save after factory reset failed");
  } else {
    WebSerial.send("message", String("Unknown command: ") + actC);
  }
}

void performReset() {
  if (Serial) Serial.flush(); delay(50);
  watchdog_reboot(0, 0, 0);
  while (true) { __asm__("wfi"); }
}

void applyModbusSettings(uint8_t addr, uint32_t baud) {
  if ((uint32_t)modbusStatus["baud"] != baud) { Serial2.end(); Serial2.begin(baud); mb.config(baud); }
  setSlaveIdIfAvailable(mb, addr);
  g_mb_address = addr; g_mb_baud = baud;
  modbusStatus["address"] = g_mb_address; modbusStatus["baud"] = g_mb_baud;
}

// ================== WebSerial config handlers ==================
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255); baud = constrain(baud, 9600, 115200);
  applyModbusSettings((uint8_t)addr, (uint32_t)baud);
  WebSerial.send("message", "Modbus configuration updated");
  cfgDirty = true; lastCfgTouchMs = millis();
}

// Supported types: inputEnable, inputInvert, inputAction, inputTarget, relays, buttons, leds
void handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"]; JSONVar list = obj["list"]; if (!t) return;
  String type = String(t); bool changed = false;

  if (type == "inputEnable") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].enabled = (bool)list[i];
    WebSerial.send("message", "Input Enabled list updated"); changed = true;

  } else if (type == "inputInvert") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].inverted = (bool)list[i];
    WebSerial.send("message", "Input Invert list updated"); changed = true;

  } else if (type == "inputAction") {
    for (int i=0;i<NUM_DI && i<list.length();i++) {
      int a = (int)list[i];
      diCfg[i].action = (uint8_t)constrain(a, 0, 2); // 0=None,1=Toggle,2=Pulse
    }
    WebSerial.send("message", "Input Action list updated"); changed = true;

  } else if (type == "inputTarget") {
    for (int i=0;i<NUM_DI && i<list.length();i++) {
      int tgt = (int)list[i];
      // 4=None, 0=All, 1..3 = Relay 1..3
      diCfg[i].target = (uint8_t)((tgt==4 || tgt==0 || (tgt>=1 && tgt<=3)) ? tgt : 0);
    }
    WebSerial.send("message", "Input Control Target list updated"); changed = true;

  } else if (type == "relays") {
    for (int i = 0; i < NUM_RLY && i < list.length(); i++) {
      rlyCfg[i].enabled  = (bool)list[i]["enabled"];
      rlyCfg[i].inverted = (bool)list[i]["inverted"];
    }
    WebSerial.send("message", "Relay Configuration updated"); changed = true;

  } else if (type == "buttons") {
    for (int i = 0; i < NUM_BTN && i < list.length(); i++) {
      int a = (int)list[i]["action"]; btnCfg[i].action = (uint8_t)constrain(a, 0, 7);
    }
    WebSerial.send("message", "Buttons Configuration updated"); changed = true;

  } else if (type == "leds") {
    for (int i = 0; i < NUM_LED && i < list.length(); i++) {
      ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);        // 0 steady, 1 blink
      int src          = (int)list[i]["source"];
      ledCfg[i].source = (uint8_t)((src==0 || src==5 || src==6 || src==7) ? src : 0); // 0=None, 5..7=R1..R3
    }
    WebSerial.send("message", "LEDs Configuration updated"); changed = true;

  } else {
    WebSerial.send("message", "Unknown Config type");
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); }
}

// ================== Modbus command pulses ==================
void processModbusCommandPulses() {
  // Relay ON/OFF
  for (int r=0; r<NUM_RLY; r++) {
    if (mb.Coil(CMD_RLY_ON_BASE + r))  { mb.setCoil(CMD_RLY_ON_BASE + r,  false); desiredRelay[r] = true;  rlyPulseUntil[r] = 0; cfgDirty = true; lastCfgTouchMs = millis(); }
    if (mb.Coil(CMD_RLY_OFF_BASE + r)) { mb.setCoil(CMD_RLY_OFF_BASE + r, false); desiredRelay[r] = false; rlyPulseUntil[r] = 0; cfgDirty = true; lastCfgTouchMs = millis(); }
  }
  // DI enable/disable
  for (int i=0; i<NUM_DI; i++) {
    if (mb.Coil(CMD_DI_EN_BASE + i))  { mb.setCoil(CMD_DI_EN_BASE + i,  false); if (!diCfg[i].enabled)  { diCfg[i].enabled  = true;  cfgDirty = true; lastCfgTouchMs = millis(); } }
    if (mb.Coil(CMD_DI_DIS_BASE + i)) { mb.setCoil(CMD_DI_DIS_BASE + i, false); if ( diCfg[i].enabled)  { diCfg[i].enabled  = false; cfgDirty = true; lastCfgTouchMs = millis(); } }
  }
}

// ================== Apply DI action to a target ==================
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now) {
  auto doRelay = [&](int rIdx) {
    if (rIdx < 0 || rIdx >= NUM_RLY) return;
    if (action == 1) { // Toggle
      desiredRelay[rIdx] = !desiredRelay[rIdx];
      rlyPulseUntil[rIdx] = 0;
    } else if (action == 2) { // Pulse
      desiredRelay[rIdx] = true;
      rlyPulseUntil[rIdx] = now + PULSE_MS;
    }
  };

  if (action == 0) return;           // None action
  if (target == 4) return;           // None target

  if (target == 0) {                 // All relays
    for (int r=0; r<NUM_RLY; r++) doRelay(r);
  } else if (target >= 1 && target <= 3) {
    doRelay(target - 1);
  }
  cfgDirty = true; lastCfgTouchMs = now;
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  mb.task();                     // Modbus polling
  processModbusCommandPulses();  // consume pulses

  // Blink phase (for LED blink mode)
  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  // Auto-save after quiet period
  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  // -------- Buttons: read (ACTIVE-LOW), rising edge ----------
  for (int i = 0; i < NUM_BTN; i++) {
    bool pressed = (digitalRead(BTN_PINS[i]) == LOW);
    buttonPrev[i] = buttonState[i];
    buttonState[i] = pressed;

    if (!buttonPrev[i] && buttonState[i]) {
      // Only override toggles supported: 5..7 map to Relay1..3
      uint8_t act = btnCfg[i].action;
      if (act >= 5 && act <= 7) {
        int r = act - 5; if (r >= 0 && r < NUM_RLY) {
          desiredRelay[r] = !desiredRelay[r];
          rlyPulseUntil[r] = 0; // cancel any pending pulse
          cfgDirty = true; lastCfgTouchMs = millis();
        }
      }
    }
  }

  // -------- Inputs (4) with Actions & Targets ----------
  JSONVar inputs;
  for (int i = 0; i < NUM_DI; i++) {
    bool val = false;
    if (diCfg[i].enabled) {
      val = (digitalRead(DI_PINS[i]) == HIGH);
      if (diCfg[i].inverted) val = !val;
    }
    diPrev[i] = diState[i];
    diState[i] = val;
    inputs[i] = val;
    mb.setIsts(ISTS_DI_BASE + i, val);

    // Rising edge -> perform action to selected target
    if (!diPrev[i] && diState[i]) {
      applyActionToTarget(diCfg[i].target, diCfg[i].action, now);
    }
  }

  // End pulse windows
  for (int r=0; r<NUM_RLY; r++) {
    if (rlyPulseUntil[r] && timeAfter32(now, rlyPulseUntil[r])) {
      desiredRelay[r] = false;
      rlyPulseUntil[r] = 0;
      cfgDirty = true; lastCfgTouchMs = millis();
    }
  }

  // -------- Relays: drive outputs from desiredRelay + relay config ----------
  JSONVar relayStateList;
  for (int i = 0; i < NUM_RLY; i++) {
    bool outVal = desiredRelay[i];
    if (!rlyCfg[i].enabled) outVal = false;
    if (rlyCfg[i].inverted) outVal = !outVal;

    digitalWrite(RELAY_PINS[i], outVal ? HIGH : LOW);

    relayStateList[i] = outVal;
    mb.setIsts(ISTS_RLY_BASE + i, outVal);
  }

  // -------- LEDs: follow selected source; blink if mode=1 ----------
  JSONVar LedStateList;
  for (int i = 0; i < NUM_LED; i++) {
    // Determine "source active"
    bool srcActive = false;
    uint8_t src = ledCfg[i].source;            // 0=None, 5..7 -> relays 1..3
    if (src >= 5 && src <= 7) {
      int r = src - 5;                         // 0..2
      bool relLogical = (r >=0 && r < NUM_RLY) ? (bool)relayStateList[r] : false; // logical relay (after cfg)
      srcActive = relLogical;
    }

    bool phys = (ledCfg[i].mode == 0) ? srcActive : (srcActive && blinkPhase);
    LedStateList[i] = phys;
    digitalWrite(LED_PINS[i], phys ? HIGH : LOW);
    mb.setIsts(ISTS_LED_BASE + i, phys);
  }

  // -------- WebSerial UI updates --------
  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();
    WebSerial.check();
    WebSerial.send("status", modbusStatus);

    JSONVar invertList, enableList, actionList, targetList;
    for (int i = 0; i < NUM_DI; i++) {
      invertList[i] = diCfg[i].inverted;
      enableList[i] = diCfg[i].enabled;
      actionList[i] = diCfg[i].action;     // 0=None,1=Toggle,2=Pulse
      targetList[i] = diCfg[i].target;     // 4=None,0=All,1..3=R1..R3
    }

    JSONVar relayEnableList, relayInvertList;
    for (int i = 0; i < NUM_RLY; i++) { relayEnableList[i] = rlyCfg[i].enabled; relayInvertList[i] = rlyCfg[i].inverted; }

    JSONVar ButtonStateList, ButtonGroupList;
    for (int i = 0; i < NUM_BTN; i++) { ButtonStateList[i] = buttonState[i]; ButtonGroupList[i] = btnCfg[i].action; }

    JSONVar LedConfigList = LedConfigListFromCfg();

    WebSerial.send("inputs", inputs);
    WebSerial.send("invertList", invertList);
    WebSerial.send("enableList", enableList);
    WebSerial.send("inputActionList", actionList);
    WebSerial.send("inputTargetList", targetList);

    WebSerial.send("relayStateList", relayStateList);
    WebSerial.send("relayEnableList", relayEnableList);
    WebSerial.send("relayInvertList", relayInvertList);

    WebSerial.send("ButtonStateList", ButtonStateList);
    WebSerial.send("ButtonGroupList", ButtonGroupList);

    WebSerial.send("LedConfigList", LedConfigList);
    WebSerial.send("LedStateList", LedStateList);
  }
}

// ================== helpers ==================
JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < NUM_LED; i++) {
    JSONVar o;
    o["mode"]   = ledCfg[i].mode;                     // 0 steady, 1 blink
    o["source"] = ledCfg[i].source;                   // 0=None, 5..7=Overridden relay 1..3
    arr[i] = o;
  }
  return arr;
}

void sendAllEchoesOnce() {
  JSONVar enableList, invertList, actionList, targetList;
  for (int i = 0; i < NUM_DI; i++) {
    enableList[i] = diCfg[i].enabled;
    invertList[i] = diCfg[i].inverted;
    actionList[i] = diCfg[i].action;
    targetList[i] = diCfg[i].target; // 4=None,0=All,1..3
  }
  WebSerial.send("enableList", enableList);
  WebSerial.send("invertList", invertList);
  WebSerial.send("inputActionList", actionList);
  WebSerial.send("inputTargetList", targetList);

  JSONVar relayEnableList, relayInvertList;
  for (int i = 0; i < NUM_RLY; i++) { relayEnableList[i] = rlyCfg[i].enabled; relayInvertList[i] = rlyCfg[i].inverted; }
  WebSerial.send("relayEnableList", relayEnableList);
  WebSerial.send("relayInvertList", relayInvertList);

  JSONVar ButtonGroupList; for (int i = 0; i < NUM_BTN; i++) ButtonGroupList[i] = btnCfg[i].action;
  WebSerial.send("ButtonGroupList", ButtonGroupList);

  WebSerial.send("LedConfigList", LedConfigListFromCfg());

  modbusStatus["address"] = g_mb_address; modbusStatus["baud"] = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}
