/**************************************************************
 * DIM-420-R1 — RP2350A (Pico 2) firmware
 * - 4× DI:     GPIO8, GPIO9, GPIO15, GPIO16
 * - 2× DIM:    CH1 ZC=GPIO0, Gate=GPIO1; CH2 ZC=GPIO2, Gate=GPIO3
 * - 4× BTN:    GPIO22..25 (active-LOW)
 * - 4× LEDs:   GPIO18..21 (active-HIGH)
 * - Modbus/RS485 on Serial2: TX=GPIO4, RX=GPIO5
 **************************************************************/

#include <Arduino.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include "hardware/watchdog.h"  // Pico SDK (RP2350)
#include "pico/time.h"          // add_alarm_in_us(), alarm_id_t

// ================== UART2 (RS-485 / Modbus) ==================
#define TX2 4
#define RX2 5
const int TxenPin = -1; // -1 if RS-485 TXEN not used
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== GPIO MAP (RP2350A) ==================
// Digital Inputs
static const uint8_t DI_PINS[4] = {8, 9, 15, 16}; // IN1..IN4
// Dimming channels
static const uint8_t ZC_PINS[2]   = {0, 2};       // CH1/CH2 zero-cross
static const uint8_t GATE_PINS[2] = {1, 3};       // CH1/CH2 gate
// User LEDs (active-HIGH)
static const uint8_t LED_PINS[4]  = {18, 19, 20, 21};
// Buttons (active-LOW, internal pullups)
static const uint8_t BTN_PINS[4]  = {22, 23, 24, 25};

// ================== Sizes ==================
static const uint8_t NUM_DI   = 4;
static const uint8_t NUM_CH   = 2;  // dimmer channels
static const uint8_t NUM_LED  = 4;
static const uint8_t NUM_BTN  = 4;

// ================== AC timing ==================
constexpr uint32_t FREQ_US_50HZ_HALF = 10000u; // 50 Hz half-cycle (10,000 us)
// constexpr uint32_t FREQ_US_60HZ_HALF = 8333u; // ~60 Hz half-cycle
constexpr uint32_t ZC_EDGE_DELAY_US  = 100u;   // input blanking near ZC

// ---- State (volatile: accessed in ISR) ----
volatile uint8_t  chLevel[NUM_CH] = {0,0};           // 0..255 current target
volatile uint8_t  chLastNonZero[NUM_CH] = {200,200}; // remembered for toggles

// ================== Config & runtime ==================
struct InCfg {
  bool    enabled;
  bool    inverted;
  uint8_t action; /*0=None,1=Toggle,2=Pulse*/
  uint8_t target; /*4=None,0=All,1..2 = CH1..CH2*/
};

struct ChCfg { bool enabled; };

struct LedCfg {
  uint8_t mode;   /*0=steady,1=blink*/
  uint8_t source; /*0=None, 1..2=CH1..CH2 "on" source*/
};

struct BtnCfg {
  uint8_t action; /*0=None, 1=Toggle CH1, 2=Toggle CH2, 3=Up CH1, 4=Down CH1, 5=Up CH2, 6=Down CH2*/
};

InCfg  diCfg[NUM_DI];
ChCfg  chCfg[NUM_CH];
LedCfg ledCfg[NUM_LED];
BtnCfg btnCfg[NUM_BTN];

bool buttonState[NUM_BTN] = {false,false,false,false};
bool buttonPrev[NUM_BTN]  = {false,false,false,false};
bool diState[NUM_DI]      = {false,false,false,false};
bool diPrev[NUM_DI]       = {false,false,false,false};

// Pulse handling for channels (DI action=Pulse => full for a short time)
uint32_t chPulseUntil[NUM_CH] = {0,0};
const uint32_t PULSE_MS = 500;

// ================== Web Serial & Modbus status ==================
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
// *** IMPORTANT: This struct MUST be above any function that references it ***
struct PersistConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  InCfg  diCfg[NUM_DI];
  ChCfg  chCfg[NUM_CH];
  LedCfg ledCfg[NUM_LED];
  BtnCfg btnCfg[NUM_BTN];
  uint8_t chLevel[NUM_CH];
  uint8_t chLastNonZero[NUM_CH];
  uint8_t mb_address;
  uint32_t mb_baud;
  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314D4449UL; // 'IDM1'
static const uint16_t CFG_VERSION = 0x0001;
static const char*    CFG_PATH    = "/cfg.bin";

volatile bool   cfgDirty       = false;
uint32_t        lastCfgTouchMs = 0;
const uint32_t  CFG_AUTOSAVE_MS= 1500;

// ================== Utils ==================
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *data++;
    for (uint8_t k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}
inline bool timeAfter32(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

// ---- Gate pulse end callbacks ----
static int64_t gate_off_cb_ch0(alarm_id_t, void*) { digitalWrite(GATE_PINS[0], LOW); return 0; }
static int64_t gate_off_cb_ch1(alarm_id_t, void*) { digitalWrite(GATE_PINS[1], LOW); return 0; }

// ---- Zero-cross ISRs ----
void zc_isr_ch0() {
  delayMicroseconds(ZC_EDGE_DELAY_US);
  digitalWrite(GATE_PINS[0], HIGH);
  uint32_t delay_us = (uint32_t(chLevel[0]) * FREQ_US_50HZ_HALF) / 255u;
  add_alarm_in_us((int64_t)delay_us, gate_off_cb_ch0, nullptr, true);
}
void zc_isr_ch1() {
  delayMicroseconds(ZC_EDGE_DELAY_US);
  digitalWrite(GATE_PINS[1], HIGH);
  uint32_t delay_us = (uint32_t(chLevel[1]) * FREQ_US_50HZ_HALF) / 255u;
  add_alarm_in_us((int64_t)delay_us, gate_off_cb_ch1, nullptr, true);
}

// ================== Defaults / persist ==================
void setDefaults() {
  for (int i = 0; i < NUM_DI; i++)  diCfg[i] = { true, false, 0 /*None*/, 0 /*All*/ };
  for (int i = 0; i < NUM_CH; i++)  chCfg[i] = { true };
  for (int i = 0; i < NUM_LED; i++) ledCfg[i] = { 0 /*steady*/, 0 /*None*/ };
  for (int i = 0; i < NUM_BTN; i++) btnCfg[i] = { 0 };

  for (int i=0;i<NUM_CH;i++) {
    chLevel[i] = 0;
    chLastNonZero[i] = 200;
    chPulseUntil[i] = 0;
  }

  g_mb_address = 3;
  g_mb_baud    = 19200;
}

void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);

  memcpy(pc.diCfg, diCfg, sizeof(diCfg));
  memcpy(pc.chCfg, chCfg, sizeof(chCfg));
  memcpy(pc.ledCfg, ledCfg, sizeof(ledCfg));
  memcpy(pc.btnCfg, btnCfg, sizeof(btnCfg));

  // Copy from volatile via loop (avoid const-qualifier issues)
  for (int i = 0; i < NUM_CH; ++i) {
    pc.chLevel[i] = chLevel[i];
    pc.chLastNonZero[i] = chLastNonZero[i];
  }

  pc.mb_address = g_mb_address;
  pc.mb_baud    = g_mb_baud;

  pc.crc32 = 0;
  pc.crc32 = crc32_update(0, (const uint8_t*)&pc, sizeof(PersistConfig));
}

bool applyFromPersist(const PersistConfig &pc) {
  if (pc.magic != CFG_MAGIC || pc.size != sizeof(PersistConfig)) return false;
  PersistConfig tmp = pc;
  uint32_t crc = tmp.crc32;
  tmp.crc32 = 0;
  if (crc32_update(0, (const uint8_t*)&tmp, sizeof(PersistConfig)) != crc) return false;
  if (pc.version != CFG_VERSION) return false;

  memcpy(diCfg, pc.diCfg, sizeof(diCfg));
  memcpy(chCfg, pc.chCfg, sizeof(chCfg));
  memcpy(ledCfg, pc.ledCfg, sizeof(ledCfg));
  memcpy(btnCfg, pc.btnCfg, sizeof(btnCfg));

  // Into volatile via loop
  for (int i = 0; i < NUM_CH; ++i) {
    chLevel[i] = pc.chLevel[i];
    chLastNonZero[i] = pc.chLastNonZero[i];
  }

  g_mb_address = pc.mb_address;
  g_mb_baud    = pc.mb_baud;
  return true;
}

bool saveConfigFS() {
  PersistConfig pc{};
  captureToPersist(pc);
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) { WebSerial.send("message", "save: open failed"); return false; }
  size_t n = f.write((const uint8_t*)&pc, sizeof(pc));
  f.flush(); f.close();
  if (n != sizeof(pc)) { WebSerial.send("message", String("save: short write ")+n); return false; }

  File r = LittleFS.open(CFG_PATH, "r");
  if (!r) { WebSerial.send("message", "save: reopen failed"); return false; }
  if ((size_t)r.size() != sizeof(PersistConfig)) { WebSerial.send("message", "save: size mismatch"); r.close(); return false; }
  PersistConfig back{};
  size_t nr = r.read((uint8_t*)&back, sizeof(back)); r.close();
  if (nr != sizeof(back)) { WebSerial.send("message", "save: short readback"); return false; }
  PersistConfig tmp2 = back; uint32_t crc = tmp2.crc32; tmp2.crc32 = 0;
  if (crc32_update(0, (const uint8_t*)&tmp2, sizeof(tmp2)) != crc) { WebSerial.send("message", "save: CRC verify failed"); return false; }
  return true;
}

bool loadConfigFS() {
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) { WebSerial.send("message", "load: open failed"); return false; }
  if (f.size() != sizeof(PersistConfig)) { WebSerial.send("message", String("load: size ")+f.size()+" != "+sizeof(PersistConfig)); f.close(); return false; }
  PersistConfig pc{}; size_t n = f.read((uint8_t*)&pc, sizeof(pc)); f.close();
  if (n != sizeof(pc)) { WebSerial.send("message", "load: short read"); return false; }
  if (!applyFromPersist(pc)) { WebSerial.send("message", "load: magic/version/crc mismatch"); return false; }
  return true;
}

// ================== Guarded FS init ==================
bool initFilesystemAndConfig() {
  if (!LittleFS.begin()) {
    WebSerial.send("message", "LittleFS mount failed. Formatting…");
    if (!LittleFS.format() || !LittleFS.begin()) {
      WebSerial.send("message", "FATAL: FS mount/format failed"); return false;
    }
  }
  if (loadConfigFS()) { WebSerial.send("message", "Config loaded from flash"); return true; }
  WebSerial.send("message", "No valid config. Using defaults.");
  setDefaults();
  if (saveConfigFS()) { WebSerial.send("message", "Defaults saved"); return true; }
  WebSerial.send("message", "First save failed. Formatting FS…");
  if (!LittleFS.format() || !LittleFS.begin()) {
    WebSerial.send("message", "FATAL: FS format failed"); return false;
  }
  setDefaults();
  if (saveConfigFS()) { WebSerial.send("message", "FS formatted and config saved"); return true; }
  WebSerial.send("message", "FATAL: save still failing after format");
  return false;
}

// ================== SFINAE helper ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id) -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) { m.setSlaveId(id); }
inline void setSlaveIdIfAvailable(...) {}

// ================== Modbus map ==================
// Discrete inputs (FC=02)
enum : uint16_t {
  ISTS_DI_BASE   = 1,   // 1..4  : IN1..IN4 (after enable+invert)
  ISTS_CH_BASE   = 50,  // 50..51: CH1..CH2 logical "on" (level>0 && enabled)
  ISTS_LED_BASE  = 90   // 90..93: LED1..LED4 logical state
};
// Coils (FC=05/15)
enum : uint16_t {
  CMD_CH_ON_BASE  = 200, // 200..201 : pulse turn CH1..2 ON (to lastNonZero)
  CMD_CH_OFF_BASE = 210, // 210..211 : pulse turn CH1..2 OFF
  CMD_DI_EN_BASE  = 300, // 300..303 : pulse ENABLE IN1..IN4
  CMD_DI_DIS_BASE = 320  // 320..323 : pulse DISABLE IN1..IN4
};
// Holding registers (FC=03/16)
enum : uint16_t { HREG_DIM_LEVEL_BASE = 400  /* 400..401: CH1..CH2 level 0..255 */ };

// ================== Fw decls (helpers) ==================
void applyModbusSettings(uint8_t addr, uint32_t baud);
void handleValues(JSONVar values);
void handleUnifiedConfig(JSONVar obj);
void handleCommand(JSONVar obj);
void performReset();
JSONVar LedConfigListFromCfg();
void sendAllEchoesOnce();
void processModbusCommandPulses();
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now);
void clampAndSetLevel(uint8_t ch, int value);

// ================== Setup ==================
void setup() {
  Serial.begin(57600);

  // GPIO directions
  for (uint8_t i=0;i<NUM_DI;i++) pinMode(DI_PINS[i], INPUT); // change to INPUT_PULLUP if your DI hardware expects it
  for (uint8_t i=0;i<NUM_LED;i++) { pinMode(LED_PINS[i], OUTPUT); digitalWrite(LED_PINS[i], LOW); }
  for (uint8_t i=0;i<NUM_BTN;i++) pinMode(BTN_PINS[i], INPUT_PULLUP); // active-LOW

  // Dimmer pins
  for (uint8_t i=0;i<NUM_CH;i++) { pinMode(GATE_PINS[i], OUTPUT); digitalWrite(GATE_PINS[i], LOW); }
  // Choose pull that matches your ZC detector (open-collector => PULLUP)
  pinMode(ZC_PINS[0], INPUT_PULLUP);
  pinMode(ZC_PINS[1], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ZC_PINS[0]), zc_isr_ch0, FALLING);
  attachInterrupt(digitalPinToInterrupt(ZC_PINS[1]), zc_isr_ch1, FALLING);

  setDefaults();

  // Guarded FS init
  if (!initFilesystemAndConfig()) {
    WebSerial.send("message", "FATAL: Filesystem/config init failed");
  }

  // Serial2 / Modbus
  Serial2.setTX(TX2);
  Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("DIM-420-R1 RP2350A");

  // ==== Modbus states (discrete inputs) ====
  for (uint16_t i=0;i<NUM_DI;i++)  mb.addIsts(ISTS_DI_BASE + i);
  for (uint16_t i=0;i<NUM_CH;i++)  mb.addIsts(ISTS_CH_BASE + i);
  for (uint16_t i=0;i<NUM_LED;i++) mb.addIsts(ISTS_LED_BASE + i);

  // ==== Coils ====
  for (uint16_t i=0;i<NUM_CH;i++){ mb.addCoil(CMD_CH_ON_BASE + i);  mb.setCoil(CMD_CH_ON_BASE + i,  false); }
  for (uint16_t i=0;i<NUM_CH;i++){ mb.addCoil(CMD_CH_OFF_BASE + i); mb.setCoil(CMD_CH_OFF_BASE + i, false); }
  for (uint16_t i=0;i<NUM_DI;i++){ mb.addCoil(CMD_DI_EN_BASE + i);  mb.setCoil(CMD_DI_EN_BASE + i,  false); }
  for (uint16_t i=0;i<NUM_DI;i++){ mb.addCoil(CMD_DI_DIS_BASE + i); mb.setCoil(CMD_DI_DIS_BASE + i, false); }

  // ==== Holding registers (levels) ====
  for (uint16_t i=0;i<NUM_CH;i++){ mb.addHreg(HREG_DIM_LEVEL_BASE + i, chLevel[i]); }

  // Status for UI
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);
  WebSerial.send("message", "Boot OK (DIM-420-R1 RP2350A)");
  sendAllEchoesOnce();
}

// ================== Command handler / reset ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) { WebSerial.send("message", "command: missing 'action'"); return; }
  String act = String(actC); act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    bool ok = saveConfigFS();
    WebSerial.send("message", ok ? "Saved. Rebooting…" : "WARNING: Save verify FAILED. Rebooting anyway…");
    delay(400); performReset();
  } else if (act == "save") {
    WebSerial.send("message", saveConfigFS() ? "Configuration saved" : "ERROR: Save failed");
  } else if (act == "load") {
    if (loadConfigFS()) { WebSerial.send("message", "Configuration loaded"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address, g_mb_baud); }
    else WebSerial.send("message", "ERROR: Load failed/invalid");
  } else if (act == "factory") {
    setDefaults();
    if (saveConfigFS()) { WebSerial.send("message", "Factory defaults restored & saved"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address, g_mb_baud); }
    else WebSerial.send("message", "ERROR: Save after factory reset failed");
  } else {
    WebSerial.send("message", String("Unknown command: ") + actC);
  }
}

void performReset() {
  if (Serial) Serial.flush();
  delay(50);
  watchdog_reboot(0, 0, 0);
  while (true) { __asm__("wfi"); }
}

void applyModbusSettings(uint8_t addr, uint32_t baud) {
  if ((uint32_t)modbusStatus["baud"] != baud) {
    Serial2.end(); Serial2.begin(baud); mb.config(baud);
  }
  setSlaveIdIfAvailable(mb, addr);
  g_mb_address = addr; g_mb_baud = baud;
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
}

// ================== WebSerial config handlers ==================
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);
  applyModbusSettings((uint8_t)addr, (uint32_t)baud);
  WebSerial.send("message", "Modbus configuration updated");
  cfgDirty = true; lastCfgTouchMs = millis();
}

// Supported types: inputEnable, inputInvert, inputAction, inputTarget, channels, buttons, leds
void handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"];
  JSONVar list = obj["list"];
  if (!t) return;
  String type = String(t);
  bool changed = false;

  if (type == "inputEnable") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].enabled = (bool)list[i];
    WebSerial.send("message", "Input Enabled list updated"); changed = true;
  } else if (type == "inputInvert") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].inverted = (bool)list[i];
    WebSerial.send("message", "Input Invert list updated"); changed = true;
  } else if (type == "inputAction") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].action = (uint8_t)constrain((int)list[i], 0, 2);
    WebSerial.send("message", "Input Action list updated"); changed = true;
  } else if (type == "inputTarget") {
    for (int i=0;i<NUM_DI && i<list.length();i++) {
      int tgt = (int)list[i]; // 4=None, 0=All, 1..2 = CH1..CH2
      diCfg[i].target = (uint8_t)((tgt==4 || tgt==0 || (tgt>=1 && tgt<=2)) ? tgt : 0);
    }
    WebSerial.send("message", "Input Control Target list updated"); changed = true;
  } else if (type == "channels") {
    for (int i=0; i<NUM_CH && i<list.length(); i++) {
      chCfg[i].enabled = (bool)list[i]["enabled"];
      int lvl = (int)list[i]["level"];
      clampAndSetLevel(i, lvl);
    }
    WebSerial.send("message", "Channels Configuration updated"); changed = true;
  } else if (type == "buttons") {
    for (int i=0;i<NUM_BTN && i<list.length();i++) {
      int a = (int)list[i]["action"];
      btnCfg[i].action = (uint8_t)constrain(a, 0, 6);
    }
    WebSerial.send("message", "Buttons Configuration updated"); changed = true;
  } else if (type == "leds") {
    for (int i=0;i<NUM_LED && i<list.length();i++) {
      ledCfg[i].mode = (uint8_t)constrain((int)list[i]["mode"], 0, 1); // 0 steady, 1 blink
      int src = (int)list[i]["source"]; // 0=None, 1..2=CH1..CH2
      ledCfg[i].source = (uint8_t)((src==0 || src==1 || src==2) ? src : 0);
    }
    WebSerial.send("message", "LEDs Configuration updated"); changed = true;
  } else {
    WebSerial.send("message", "Unknown Config type");
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); }
}

// ================== Helpers ==================
void clampAndSetLevel(uint8_t ch, int value) {
  if (ch >= NUM_CH) return;
  value = constrain(value, 0, 255);
  chLevel[ch] = (uint8_t)value;
  if (value > 0) chLastNonZero[ch] = (uint8_t)value;
  mb.setHreg(HREG_DIM_LEVEL_BASE + ch, chLevel[ch]);
}

// Apply DI action to a dimmer target
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now) {
  auto doCh = [&](int idx) {
    if (idx < 0 || idx >= NUM_CH) return;
    if (action == 1) { // Toggle
      if (chLevel[idx] == 0) clampAndSetLevel(idx, chLastNonZero[idx]);
      else clampAndSetLevel(idx, 0);
      chPulseUntil[idx] = 0;
    } else if (action == 2) { // Pulse -> full then restore
      chPulseUntil[idx] = now + PULSE_MS;
      clampAndSetLevel(idx, 255);
    }
  };
  if (action == 0) return; // None
  if (target == 4) return;  // None

  if (target == 0) { for (int i=0;i<NUM_CH;i++) doCh(i); }
  else if (target>=1 && target<=2) { doCh(target-1); }

  cfgDirty = true; lastCfgTouchMs = now;
}

// Consume Modbus coils + handle Hreg level writes
void processModbusCommandPulses() {
  // Channel ON/OFF
  for (int c=0;c<NUM_CH;c++) {
    if (mb.Coil(CMD_CH_ON_BASE + c)) {
      mb.setCoil(CMD_CH_ON_BASE + c, false);
      clampAndSetLevel(c, chLastNonZero[c]); cfgDirty = true; lastCfgTouchMs = millis();
    }
    if (mb.Coil(CMD_CH_OFF_BASE + c)) {
      mb.setCoil(CMD_CH_OFF_BASE + c, false);
      clampAndSetLevel(c, 0); cfgDirty = true; lastCfgTouchMs = millis();
    }
  }
  // DI enable/disable
  for (int i=0;i<NUM_DI;i++) {
    if (mb.Coil(CMD_DI_EN_BASE + i))  { mb.setCoil(CMD_DI_EN_BASE + i,  false); if (!diCfg[i].enabled){ diCfg[i].enabled=true;  cfgDirty=true; lastCfgTouchMs=millis(); } }
    if (mb.Coil(CMD_DI_DIS_BASE + i)) { mb.setCoil(CMD_DI_DIS_BASE + i, false); if ( diCfg[i].enabled){ diCfg[i].enabled=false; cfgDirty=true; lastCfgTouchMs=millis(); } }
  }
  // Levels via holding registers (external masters can write)
  for (int c=0;c<NUM_CH;c++) {
    uint16_t val = mb.Hreg(HREG_DIM_LEVEL_BASE + c);
    clampAndSetLevel(c, (int)val);
  }
}

JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < NUM_LED; i++) {
    JSONVar o;
    o["mode"]   = ledCfg[i].mode;   // 0 steady, 1 blink
    o["source"] = ledCfg[i].source; // 0=None, 1..2=CH1..CH2
    arr[i] = o;
  }
  return arr;
}

void sendAllEchoesOnce() {
  // DI config
  JSONVar enableList, invertList, actionList, targetList;
  for (int i=0;i<NUM_DI;i++) { enableList[i]=diCfg[i].enabled; invertList[i]=diCfg[i].inverted; actionList[i]=diCfg[i].action; targetList[i]=diCfg[i].target; }
  WebSerial.send("enableList", enableList);
  WebSerial.send("invertList", invertList);
  WebSerial.send("inputActionList", actionList);
  WebSerial.send("inputTargetList", targetList);

  // Channels summary
  JSONVar chEnabled, chLevels;
  for (int i=0;i<NUM_CH;i++){ chEnabled[i]=chCfg[i].enabled; chLevels[i]= (int)chLevel[i]; }
  WebSerial.send("channelEnabled", chEnabled);
  WebSerial.send("channelLevels", chLevels);

  // Buttons mapping
  JSONVar ButtonGroupList; for (int i=0;i<NUM_BTN;i++) ButtonGroupList[i]=btnCfg[i].action;
  WebSerial.send("ButtonGroupList", ButtonGroupList);

  WebSerial.send("LedConfigList", LedConfigListFromCfg());

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  mb.task();
  processModbusCommandPulses();

  // Blink phase (for LED blink mode)
  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  // Auto-save after quiet period
  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  // -------- Buttons: read (ACTIVE-LOW), rising edge ----------
  for (int i = 0; i < NUM_BTN; i++) {
    bool pressed = (digitalRead(BTN_PINS[i]) == LOW);
    buttonPrev[i] = buttonState[i];
    buttonState[i] = pressed;
    if (!buttonPrev[i] && buttonState[i]) {
      switch (btnCfg[i].action) {
        case 1: applyActionToTarget(1, 1 /*toggle*/, now); break;
        case 2: applyActionToTarget(2, 1 /*toggle*/, now); break;
        case 3: clampAndSetLevel(0, chLevel[0] + 10); cfgDirty=true; lastCfgTouchMs=now; break;
        case 4: clampAndSetLevel(0, chLevel[0] - 10); cfgDirty=true; lastCfgTouchMs=now; break;
        case 5: clampAndSetLevel(1, chLevel[1] + 10); cfgDirty=true; lastCfgTouchMs=now; break;
        case 6: clampAndSetLevel(1, chLevel[1] - 10); cfgDirty=true; lastCfgTouchMs=now; break;
        default: break;
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
    bool prev = diState[i];
    diPrev[i] = prev;
    diState[i] = val;
    inputs[i] = val;
    mb.setIsts(ISTS_DI_BASE + i, val);

    // Edge detection
    bool rising  = (!prev && val);
    bool falling = (prev && !val);

    uint8_t act = diCfg[i].action;
    if (act == 1) { if (rising || falling) { applyActionToTarget(diCfg[i].target, 1 /*toggle*/, now); } }
    else if (act == 2) { if (rising) { applyActionToTarget(diCfg[i].target, 2 /*pulse*/, now); } }
  }

  // Handle pulse timeout restore
  for (int c=0;c<NUM_CH;c++) {
    if (chPulseUntil[c] != 0 && timeAfter32(now, chPulseUntil[c])) {
      clampAndSetLevel(c, chLastNonZero[c]);
      chPulseUntil[c] = 0;
    }
  }

  // -------- Channel "on" states & LEDs ----------
  JSONVar LedStateList;
  for (int i=0;i<NUM_LED;i++) {
    bool srcActive = false;
    uint8_t src = ledCfg[i].source; // 0=None, 1..2
    if (src >= 1 && src <= 2) {
      int c = src - 1;
      bool chOn = (chCfg[c].enabled && chLevel[c] > 0);
      srcActive = chOn;
    }
    bool phys = (ledCfg[i].mode == 0) ? srcActive : (srcActive && blinkPhase);
    LedStateList[i] = phys;
    digitalWrite(LED_PINS[i], phys ? HIGH : LOW);
    mb.setIsts(ISTS_LED_BASE + i, phys);
  }

  // Push channel "on" discrete inputs
  for (int c=0;c<NUM_CH;c++) {
    bool on = (chCfg[c].enabled && chLevel[c] > 0);
    mb.setIsts(ISTS_CH_BASE + c, on);
  }

  // -------- WebSerial UI updates --------
  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();
    WebSerial.check();

    WebSerial.send("status", modbusStatus);

    JSONVar invertList, enableList, actionList, targetList;
    for (int i=0;i<NUM_DI;i++) {
      invertList[i] = diCfg[i].inverted;
      enableList[i] = diCfg[i].enabled;
      actionList[i] = diCfg[i].action; // 0=None,1=Toggle,2=Pulse
      targetList[i] = diCfg[i].target; // 4=None,0=All,1..2
    }

    JSONVar channelEnabled, channelLevels;
    for (int i=0;i<NUM_CH;i++) {
      channelEnabled[i] = chCfg[i].enabled;
      channelLevels[i]  = (int)chLevel[i];
    }

    JSONVar ButtonStateList, ButtonGroupList;
    for (int i=0;i<NUM_BTN;i++) {
      ButtonStateList[i] = buttonState[i];
      ButtonGroupList[i] = btnCfg[i].action;
    }

    JSONVar LedConfigList = LedConfigListFromCfg();

    WebSerial.send("inputs", inputs);
    WebSerial.send("invertList", invertList);
    WebSerial.send("enableList", enableList);
    WebSerial.send("inputActionList", actionList);
    WebSerial.send("inputTargetList", targetList);
    WebSerial.send("channelEnabled", channelEnabled);
    WebSerial.send("channelLevels", channelLevels);
    WebSerial.send("ButtonStateList", ButtonStateList);
    WebSerial.send("ButtonGroupList", ButtonGroupList);
    WebSerial.send("LedConfigList", LedConfigList);
    WebSerial.send("LedStateList", LedStateList);
  }
}
