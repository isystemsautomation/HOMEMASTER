// ==== AIO-422-R1 (RP2350) SIMPLE FW (ALL ANALOG → HOLDING REGS + 4x PID) ====
// FIXED: PID settings no longer revert. PID config is stored in pid[] (single source of truth),
// and Modbus regs are mirrored for visibility. updatePids() does NOT overwrite config from Modbus.
//
// ADDED (drop-in, no Modbus map changes):
// - PID Working Mode: Direct/Reverse (mode 0/1) set ONLY from Web config via WebSerial "pidMode" message
// - Mode is persisted in LittleFS config (survives reboot / factory / load/save)
//
// IMPORTANT (this build):
// - SP source supports PID outputs: sp_src 5..8 = PID1..PID4 virtual OUT (raw)
// - updatePids() uses a 2-pass solve so PID->PID SP is stable within each cycle.
//
// 4x AI via ADS1115 (ADS1X15.h) on Wire1 (SDA=6, SCL=7)
// 2x AO via 2x MCP4725 on Wire1 (0x60,0x61)
// 2x RTD via 2x MAX31865 over SOFTWARE SPI (CS1=13, CS2=14, DI=11, DO=12, CLK=10)
// 4x Buttons (GPIO22..25), 4x LEDs (GPIO18..21)
// WebSerial + Modbus RTU + LittleFS persistence of Modbus/DAC + 4x PID

#include <Arduino.h>
#include <Wire.h>
#include <ADS1X15.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_MAX31865.h>

#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include <math.h>
#include "hardware/watchdog.h"

// ================== UART2 (RS-485 / Modbus) ==================
#define TX2   4
#define RX2   5
const int TxenPin = -1;
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== GPIO MAP ==================
static const uint8_t LED_PINS[4] = {18, 19, 20, 21};
static const uint8_t BTN_PINS[4] = {22, 23, 24, 25};

static const uint8_t NUM_LED = 4;
static const uint8_t NUM_BTN = 4;

// ================== I2C / SPI PINS ==================
#define SDA1 6
#define SCL1 7

#define RTD1_CS   13
#define RTD2_CS   14
#define RTD_DI    11
#define RTD_DO    12
#define RTD_CLK   10

// ================== Sensors / IO devices ==================
ADS1115 ads(0x48, &Wire1);

Adafruit_MCP4725 dac0;
Adafruit_MCP4725 dac1;

Adafruit_MAX31865 rtd1(RTD1_CS, RTD_DI, RTD_DO, RTD_CLK);
Adafruit_MAX31865 rtd2(RTD2_CS, RTD_DI, RTD_DO, RTD_CLK);

bool ads_ok     = false;
bool dac_ok[2]  = {false, false};
bool rtd_ok[2]  = {false, false};

// RTD parameters
const float RTD_RREF      = 200.0f;
const float RTD_RNOMINAL  = 100.0f;
const max31865_numwires_t RTD_WIRES = MAX31865_2WIRE;

// ================== ADC field scaling ==================
#define ADC_FIELD_SCALE_NUM 30303
#define ADC_FIELD_SCALE_DEN 10000
#define ADC_FIELD_SCALE ((float)ADC_FIELD_SCALE_NUM / (float)ADC_FIELD_SCALE_DEN)

// ================== Runtime state ==================
bool buttonState[NUM_BTN] = {false,false,false,false};
bool buttonPrev[NUM_BTN]  = {false,false,false,false};
bool ledState[NUM_LED]    = {false,false,false,false};

int16_t  aiRaw[4]   = {0,0,0,0};
uint16_t aiMv[4]    = {0,0,0,0};
int16_t  rtdTemp_x10[2] = {0,0};

uint16_t dacRaw[2] = {0,0};

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend       = 0;
const unsigned long sendInterval   = 250;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 200;

unsigned long lastPidUpdateMs = 0;
const unsigned long pidIntervalMs = 200;

// ================== Persisted Modbus settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Modbus map ==================
enum : uint16_t {
  ISTS_BTN_BASE   = 1,
  ISTS_LED_BASE   = 20,

  HREG_AI_BASE     = 100,
  HREG_TEMP_BASE   = 120,
  HREG_AI_MV_BASE  = 140,
  HREG_DAC_BASE    = 200,

  HREG_SP_BASE        = 300,
  HREG_PID_EN_BASE    = 310,
  HREG_PID_PVSEL_BASE = 320,
  HREG_PID_SPSEL_BASE = 330,
  HREG_PID_OUTSEL_BASE= 340,
  HREG_PID_KP_BASE    = 350,
  HREG_PID_KI_BASE    = 360,
  HREG_PID_KD_BASE    = 370,
  HREG_PID_OUT_BASE   = 380,
  HREG_PID_PVVAL_BASE = 390,
  HREG_PID_ERR_BASE   = 400
};

// ================== PID state ==================
struct PIDState {
  bool    enabled;
  uint8_t pvSource;
  uint8_t spSource;
  uint8_t outTarget;   // 0=none,1=AO1,2=AO2,3=virtual-only
  uint8_t mode;        // 0=direct,1=reverse

  float   Kp;
  float   Ki;
  float   Kd;

  float   integral;
  float   prevError;
  float   output;      // RAW output (0..4095)

  float   pvMin;
  float   pvMax;
  float   outMin;
  float   outMax;

  float   pvPct;
  float   spPct;
  float   outPct;
};

PIDState pid[4];

float pidVirtRaw[4] = {0,0,0,0};
float pidVirtPct[4] = {0,0,0,0};

// ================== Persistence (LittleFS) ==================
struct PersistConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t size;

  uint16_t dacRaw[2];
  uint8_t  mb_address;
  uint32_t mb_baud;

  uint8_t  pid_mode[4];

  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314F4941UL;
static const uint16_t CFG_VERSION = 0x0002;
static const char*    CFG_PATH    = "/cfg.bin";

volatile bool  cfgDirty        = false;
uint32_t       lastCfgTouchMs  = 0;
const uint32_t CFG_AUTOSAVE_MS = 1500;

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

static inline uint8_t clamp_u8(int v, int lo, int hi) {
  if (v < lo) v = lo;
  if (v > hi) v = hi;
  return (uint8_t)v;
}

// ================== Defaults / persist ==================
void setDefaults() {
  for (int i=0;i<NUM_LED;i++) { ledState[i] = false; }
  for (int i=0;i<NUM_BTN;i++) { buttonState[i] = buttonPrev[i] = false; }

  dacRaw[0] = 0;
  dacRaw[1] = 0;

  g_mb_address = 3;
  g_mb_baud    = 19200;

  for (int i=0;i<4;i++) pid[i].mode = 0;
}

void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);

  pc.dacRaw[0] = dacRaw[0];
  pc.dacRaw[1] = dacRaw[1];

  pc.mb_address = g_mb_address;
  pc.mb_baud    = g_mb_baud;

  for (int i=0;i<4;i++) pc.pid_mode[i] = pid[i].mode;

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

  dacRaw[0] = pc.dacRaw[0];
  dacRaw[1] = pc.dacRaw[1];

  g_mb_address = pc.mb_address;
  g_mb_baud    = pc.mb_baud;

  for (int i=0;i<4;i++) pid[i].mode = clamp_u8((int)pc.pid_mode[i], 0, 1);

  return true;
}

bool saveConfigFS() {
  PersistConfig pc{};
  captureToPersist(pc);

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) { WebSerial.send("message", "save: open failed"); return false; }
  size_t n = f.write((const uint8_t*)&pc, sizeof(pc));
  f.flush();
  f.close();
  if (n != sizeof(pc)) {
    WebSerial.send("message", String("save: short write ")+n);
    return false;
  }

  File r = LittleFS.open(CFG_PATH, "r");
  if (!r) { WebSerial.send("message", "save: reopen failed"); return false; }
  if ((size_t)r.size() != sizeof(PersistConfig)) {
    WebSerial.send("message", "save: size mismatch after write");
    r.close();
    return false;
  }
  PersistConfig back{};
  size_t nr = r.read((uint8_t*)&back, sizeof(back));
  r.close();
  if (nr != sizeof(back)) {
    WebSerial.send("message", "save: short readback");
    return false;
  }
  PersistConfig tmp = back;
  uint32_t crc = tmp.crc32;
  tmp.crc32 = 0;
  if (crc32_update(0, (const uint8_t*)&tmp, sizeof(tmp)) != crc) {
    WebSerial.send("message", "save: CRC verify failed");
    return false;
  }
  return true;
}

bool loadConfigFS() {
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) { WebSerial.send("message", "load: open failed"); return false; }
  if (f.size() != sizeof(PersistConfig)) {
    WebSerial.send("message", String("load: size ")+f.size()+" != "+sizeof(PersistConfig));
    f.close();
    return false;
  }
  PersistConfig pc{};
  size_t n = f.read((uint8_t*)&pc, sizeof(pc));
  f.close();
  if (n != sizeof(pc)) { WebSerial.send("message", "load: short read"); return false; }
  if (!applyFromPersist(pc)) { WebSerial.send("message", "load: magic/version/crc mismatch"); return false; }
  return true;
}

bool initFilesystemAndConfig() {
  if (!LittleFS.begin()) {
    WebSerial.send("message", "LittleFS mount failed. Formatting…");
    if (!LittleFS.format() || !LittleFS.begin()) {
      WebSerial.send("message", "FATAL: FS mount/format failed");
      return false;
    }
  }

  if (loadConfigFS()) {
    WebSerial.send("message", "Config loaded from flash");
    return true;
  }

  WebSerial.send("message", "No valid config. Using defaults.");
  setDefaults();
  if (saveConfigFS()) {
    WebSerial.send("message", "Defaults saved");
    return true;
  }

  WebSerial.send("message", "FATAL: first save failed");
  return false;
}

// ================== SFINAE helper for ModbusSerial ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id)
  -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) { m.setSlaveId(id); }
inline void setSlaveIdIfAvailable(...) {}

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

// ================== Fw decls ==================
void handleValues(JSONVar values);
void handleCommand(JSONVar obj);
void handleDac(JSONVar obj);
void handlePid(JSONVar obj);
void handlePidMode(JSONVar obj);

void performReset();
void sendAllEchoesOnce();
void sendPidSnapshot();
void writeDac(int idx, uint16_t value);
void readSensors();
void updatePids();

float getPidPvValue(uint8_t src, bool &ok);
float getPidSpValue(uint8_t pidIndex, uint8_t src, bool &ok);

// ================== Command handler / reset ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) { WebSerial.send("message", "command: missing 'action'"); return; }
  String act = String(actC);
  act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    bool ok = saveConfigFS();
    WebSerial.send("message", ok ? "Saved. Rebooting…" : "WARNING: Save verify FAILED. Rebooting anyway…");
    delay(400);
    performReset();
  } else if (act == "save") {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else               WebSerial.send("message", "ERROR: Save failed");
  } else if (act == "load") {
    if (loadConfigFS()) {
      WebSerial.send("message", "Configuration loaded");
      applyModbusSettings(g_mb_address, g_mb_baud);
      writeDac(0, dacRaw[0]);
      writeDac(1, dacRaw[1]);
      sendAllEchoesOnce();
    } else {
      WebSerial.send("message", "ERROR: Load failed/invalid");
    }
  } else if (act == "factory") {
    setDefaults();
    if (saveConfigFS()) {
      WebSerial.send("message", "Factory defaults restored & saved");
      applyModbusSettings(g_mb_address, g_mb_baud);
      writeDac(0, dacRaw[0]);
      writeDac(1, dacRaw[1]);
      sendAllEchoesOnce();
    } else {
      WebSerial.send("message", "ERROR: Save after factory reset failed");
    }
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

// ================== WebSerial handlers ==================
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);

  applyModbusSettings((uint8_t)addr, (uint32_t)baud);
  WebSerial.send("message", "Modbus configuration updated");

  cfgDirty = true;
  lastCfgTouchMs = millis();
}

void handleDac(JSONVar obj) {
  JSONVar list = obj["list"];
  if (JSON.typeof(list) != "array") return;

  uint32_t now = millis();

  for (int i = 0; i < 2 && i < (int)list.length(); i++) {
    long v = (long)list[i];
    v = constrain(v, 0L, 4095L);
    dacRaw[i] = (uint16_t)v;
    writeDac(i, dacRaw[i]);
    mb.Hreg(HREG_DAC_BASE + i, dacRaw[i]);
  }

  cfgDirty       = true;
  lastCfgTouchMs = now;
  WebSerial.send("message", "DAC values updated");
}

void handlePid(JSONVar obj) {
  JSONVar sp     = obj["sp"];
  JSONVar en     = obj["en"];
  JSONVar pv     = obj["pv"];
  JSONVar spSrc  = obj["sp_src"];
  JSONVar out    = obj["out"];
  JSONVar kp     = obj["kp"];
  JSONVar ki     = obj["ki"];
  JSONVar kd     = obj["kd"];

  JSONVar pvMin  = obj["pv_min"];
  JSONVar pvMax  = obj["pv_max"];
  JSONVar outMin = obj["out_min"];
  JSONVar outMax = obj["out_max"];

  for (int i = 0; i < 4; i++) {
    PIDState &p = pid[i];

    if (JSON.typeof(sp) == "array" && i < (int)sp.length()) {
      int16_t spv = (int16_t)((int)sp[i]);
      mb.Hreg(HREG_SP_BASE + i, (uint16_t)spv);
    }

    if (JSON.typeof(en) == "array" && i < (int)en.length()) {
      int v = (int)en[i];
      p.enabled = (v != 0);
      mb.Hreg(HREG_PID_EN_BASE + i, (uint16_t)(p.enabled ? 1 : 0));
    }

    if (JSON.typeof(pv) == "array" && i < (int)pv.length()) {
      int v = (int)pv[i];
      p.pvSource = clamp_u8(v, 0, 6);
    }

    if (JSON.typeof(spSrc) == "array" && i < (int)spSrc.length()) {
      int v = (int)spSrc[i];
      p.spSource = clamp_u8(v, 0, 8);   // 0..4 = manual/SP1..4, 5..8 = PID1..4 OUT
    }

    if (JSON.typeof(out) == "array" && i < (int)out.length()) {
      int v = (int)out[i];
      p.outTarget = clamp_u8(v, 0, 3);
    }

    if (JSON.typeof(kp) == "array" && i < (int)kp.length()) {
      int16_t raw = (int16_t)((int)kp[i]);
      p.Kp = (float)raw / 100.0f;
      mb.Hreg(HREG_PID_KP_BASE + i, (uint16_t)raw);
    }
    if (JSON.typeof(ki) == "array" && i < (int)ki.length()) {
      int16_t raw = (int16_t)((int)ki[i]);
      p.Ki = (float)raw / 100.0f;
      mb.Hreg(HREG_PID_KI_BASE + i, (uint16_t)raw);
    }
    if (JSON.typeof(kd) == "array" && i < (int)kd.length()) {
      int16_t raw = (int16_t)((int)kd[i]);
      p.Kd = (float)raw / 100.0f;
      mb.Hreg(HREG_PID_KD_BASE + i, (uint16_t)raw);
    }

    if (JSON.typeof(pvMin) == "array" && i < (int)pvMin.length()) p.pvMin = (float)((double)pvMin[i]);
    if (JSON.typeof(pvMax) == "array" && i < (int)pvMax.length()) p.pvMax = (float)((double)pvMax[i]);
    if (JSON.typeof(outMin)== "array" && i < (int)outMin.length()) p.outMin = (float)((double)outMin[i]);
    if (JSON.typeof(outMax)== "array" && i < (int)outMax.length()) p.outMax = (float)((double)outMax[i]);
  }

  WebSerial.send("message", "PID configuration updated via WebSerial");
  cfgDirty = true;
  lastCfgTouchMs = millis();
}

void handlePidMode(JSONVar obj) {
  JSONVar mode = obj["mode"];
  if (JSON.typeof(mode) != "array") {
    WebSerial.send("message", "pidMode: missing 'mode' array");
    return;
  }

  for (int i = 0; i < 4; i++) {
    if (i >= (int)mode.length()) break;
    int m = (int)mode[i];
    pid[i].mode = clamp_u8(m, 0, 1);
  }

  WebSerial.send("message", "PID mode updated via WebSerial (pidMode)");
  cfgDirty = true;
  lastCfgTouchMs = millis();
}

// ================== DAC write helper ==================
void writeDac(int idx, uint16_t value) {
  if (idx == 0 && dac_ok[0]) dac0.setVoltage(value, false);
  else if (idx == 1 && dac_ok[1]) dac1.setVoltage(value, false);
}

// ================== Sensor read helper ==================
void readSensors() {
  if (ads_ok) {
    for (int ch=0; ch<4; ch++) {
      int16_t raw = ads.readADC(ch);
      aiRaw[ch] = raw;

      float v_adc   = ads.toVoltage(raw);
      float v_field = v_adc * ADC_FIELD_SCALE;
      long  mv      = lroundf(v_field * 1000.0f);

      if (mv < 0)      mv = 0;
      if (mv > 65535)  mv = 65535;

      aiMv[ch] = (uint16_t)mv;

      mb.Hreg(HREG_AI_MV_BASE + ch, aiMv[ch]);
    }
  } else {
    for (int ch=0; ch<4; ch++) {
      aiRaw[ch] = 0;
      aiMv[ch]  = 0;
      mb.Hreg(HREG_AI_MV_BASE + ch, 0);
    }
  }

  Adafruit_MAX31865* rtds[2] = { &rtd1, &rtd2 };
  for (int i=0;i<2;i++) {
    if (!rtd_ok[i]) {
      rtdTemp_x10[i] = 0;
      mb.Hreg(HREG_TEMP_BASE + i, 0);
      continue;
    }
    float temp = rtds[i]->temperature(RTD_RNOMINAL, RTD_RREF);
    int16_t t10 = (int16_t)roundf(temp * 10.0f);
    rtdTemp_x10[i] = t10;
    mb.Hreg(HREG_TEMP_BASE + i, (uint16_t)t10);
  }
}

// ================== PID helpers ==================
float getPidPvValue(uint8_t src, bool &ok) {
  ok = false;
  if (src >= 1 && src <= 4) {
    uint8_t idx = src - 1;
    ok = true;
    return (float)((int32_t)aiMv[idx]);
  } else if (src == 5) {
    ok = true;
    return (float)rtdTemp_x10[0];
  } else if (src == 6) {
    ok = true;
    return (float)rtdTemp_x10[1];
  }
  return 0.0f;
}

// src:
// 0      = Manual (this PID's own SPn register)
// 1..4   = SP1..SP4 (global Modbus regs 300..303)
// 5..8   = PID1..PID4 OUT (pidVirtRaw[0..3])
float getPidSpValue(uint8_t pidIndex, uint8_t src, bool &ok) {
  ok = false;

  if (src >= 1 && src <= 4) {
    uint8_t idx = src - 1;
    int16_t raw = (int16_t)mb.Hreg(HREG_SP_BASE + idx);
    ok = true;
    return (float)raw;
  } else if (src == 0) {
    int16_t raw = (int16_t)mb.Hreg(HREG_SP_BASE + pidIndex);
    ok = true;
    return (float)raw;
  } else if (src >= 5 && src <= 8) {
    uint8_t idx = src - 5;
    ok = true;
    return pidVirtRaw[idx];
  }

  return 0.0f;
}

// ================== PID update (2-pass for PID->PID SP stability) ==================
void updatePids() {
  unsigned long now = millis();
  if (now - lastPidUpdateMs < pidIntervalMs) return;

  float dt = (now - lastPidUpdateMs) / 1000.0f;
  if (dt <= 0.0f) dt = pidIntervalMs / 1000.0f;
  lastPidUpdateMs = now;

  // Pass 0: start from previous virt outputs (already in pidVirtRaw[])
  float newOutRaw[4] = { pidVirtRaw[0], pidVirtRaw[1], pidVirtRaw[2], pidVirtRaw[3] };
  float newOutPct[4] = { pidVirtPct[0], pidVirtPct[1], pidVirtPct[2], pidVirtPct[3] };

  // Pass 1: compute all outputs into temp arrays (don’t overwrite pidVirtRaw yet)
  for (int i = 0; i < 4; i++) {
    PIDState &p = pid[i];

    bool  pvOk   = false;
    bool  spOk   = false;
    float pvRaw  = getPidPvValue(p.pvSource, pvOk);
    float spRaw  = getPidSpValue((uint8_t)i, p.spSource, spOk);
    float errRaw = spRaw - pvRaw;

    if (p.pvMax <= p.pvMin) { p.pvMin = 0.0f; p.pvMax = 10000.0f; }
    if (p.outMax <= p.outMin) { p.outMin = 0.0f; p.outMax = 4095.0f; }

    if (!(p.enabled && pvOk && spOk)) {
      p.integral  = 0.0f;
      p.prevError = 0.0f;
      p.output    = 0.0f;
      p.pvPct     = 0.0f;
      p.spPct     = 0.0f;
      p.outPct    = 0.0f;

      newOutRaw[i] = 0.0f;
      newOutPct[i] = 0.0f;

      mb.Hreg(HREG_PID_OUT_BASE   + i, 0); 
      continue;
    }

    float pvPct = (pvRaw - p.pvMin) * 100.0f / (p.pvMax - p.pvMin);
    float spPct = (spRaw - p.pvMin) * 100.0f / (p.pvMax - p.pvMin);

    pvPct = constrain(pvPct, 0.0f, 100.0f);
    spPct = constrain(spPct, 0.0f, 100.0f);

    float errorPct = spPct - pvPct;
    if (p.mode == 1) errorPct = -errorPct;

    p.integral += errorPct * dt * p.Ki;
    p.integral = constrain(p.integral, -100.0f, 100.0f);

    float derivPct = (dt > 0.0f) ? ((errorPct - p.prevError) / dt) : 0.0f;
    float uPct = p.Kp * errorPct + p.integral + p.Kd * derivPct;
    p.prevError = errorPct;

    uPct = constrain(uPct, 0.0f, 100.0f);

    float outSpan = (p.outMax - p.outMin);
    if (outSpan < 1.0f) outSpan = 1.0f;

    float outRawF = p.outMin + (uPct / 100.0f) * outSpan;
    outRawF = constrain(outRawF, 0.0f, 4095.0f);

    p.output = outRawF;
    p.pvPct  = pvPct;
    p.spPct  = spPct;
    p.outPct = uPct;

    newOutRaw[i] = outRawF;
    newOutPct[i] = uPct;

    mb.Hreg(HREG_PID_OUT_BASE   + i, (uint16_t)lroundf(outRawF));
  }

  // Pass 2: publish virtual outputs together (so PID->PID SP sees stable values next cycle)
  for (int i=0;i<4;i++) {
    pidVirtRaw[i] = newOutRaw[i];
    pidVirtPct[i] = newOutPct[i];
  }

  // Apply physical outputs (AO) after all PIDs computed
  for (int i = 0; i < 4; i++) {
    PIDState &p = pid[i];
    if (p.outTarget == 1 || p.outTarget == 2) {
      int ch = p.outTarget - 1;
      uint16_t val = (uint16_t)lroundf(pidVirtRaw[i]);
      dacRaw[ch] = val;
      mb.Hreg(HREG_DAC_BASE + ch, dacRaw[ch]);
      writeDac(ch, dacRaw[ch]);
    }
  }
}

// ================== PID snapshot helper ==================
void sendPidSnapshot() {
  JSONVar pidObj;
  JSONVar spArr, enArr, pvArr, spSrcArr, outArr, kpArr, kiArr, kdArr;
  JSONVar pvMinArr, pvMaxArr, outMinArr, outMaxArr;
  JSONVar pvPctArr, spPctArr, outPctArr;
  JSONVar modeArr;
  JSONVar virtRawArr, virtPctArr;

  for (int i = 0; i < 4; i++) {
    spArr[i]      = (int16_t)mb.Hreg(HREG_SP_BASE + i);

    enArr[i]      = (int)(pid[i].enabled ? 1 : 0);
    pvArr[i]      = (int)pid[i].pvSource;
    spSrcArr[i]   = (int)pid[i].spSource;
    outArr[i]     = (int)pid[i].outTarget;
    modeArr[i]    = (int)pid[i].mode;

    kpArr[i]      = (int16_t)lroundf(pid[i].Kp * 100.0f);
    kiArr[i]      = (int16_t)lroundf(pid[i].Ki * 100.0f);
    kdArr[i]      = (int16_t)lroundf(pid[i].Kd * 100.0f);

    pvMinArr[i]   = pid[i].pvMin;
    pvMaxArr[i]   = pid[i].pvMax;
    outMinArr[i]  = pid[i].outMin;
    outMaxArr[i]  = pid[i].outMax;

    pvPctArr[i]   = pid[i].pvPct;
    spPctArr[i]   = pid[i].spPct;
    outPctArr[i]  = pid[i].outPct;

    virtRawArr[i] = pidVirtRaw[i];
    virtPctArr[i] = pidVirtPct[i];
  }

  pidObj["sp"]       = spArr;
  pidObj["en"]       = enArr;
  pidObj["pv"]       = pvArr;
  pidObj["sp_src"]   = spSrcArr;
  pidObj["out"]      = outArr;
  pidObj["kp"]       = kpArr;
  pidObj["ki"]       = kiArr;
  pidObj["kd"]       = kdArr;

  pidObj["pv_min"]   = pvMinArr;
  pidObj["pv_max"]   = pvMaxArr;
  pidObj["out_min"]  = outMinArr;
  pidObj["out_max"]  = outMaxArr;

  pidObj["pv_pct"]   = pvPctArr;
  pidObj["sp_pct"]   = spPctArr;
  pidObj["out_pct"]  = outPctArr;

  pidObj["mode"]     = modeArr;

  pidObj["virt_raw"] = virtRawArr;
  pidObj["virt_pct"] = virtPctArr;

  WebSerial.send("pidState", pidObj);
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);

  for (uint8_t i=0;i<NUM_LED;i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
    ledState[i] = false;
  }
  for (uint8_t i=0;i<NUM_BTN;i++) {
    pinMode(BTN_PINS[i], INPUT);
    buttonState[i] = buttonPrev[i] = false;
  }

  for (int i=0;i<4;i++) {
    pid[i].enabled   = false;
    pid[i].pvSource  = 0;
    pid[i].spSource  = 0;
    pid[i].outTarget = 0;
    pid[i].mode      = 0;
    pid[i].Kp = pid[i].Ki = pid[i].Kd = 0.0f;
    pid[i].integral  = 0.0f;
    pid[i].prevError = 0.0f;
    pid[i].output    = 0.0f;

    pid[i].pvMin     = 0.0f;
    pid[i].pvMax     = 10000.0f;
    pid[i].outMin    = 0.0f;
    pid[i].outMax    = 4095.0f;

    pid[i].pvPct     = 0.0f;
    pid[i].spPct     = 0.0f;
    pid[i].outPct    = 0.0f;

    pidVirtRaw[i]    = 0.0f;
    pidVirtPct[i]    = 0.0f;
  }

  setDefaults();

  WebSerial.on("values",  handleValues);
  WebSerial.on("command", handleCommand);
  WebSerial.on("dac",     handleDac);
  WebSerial.on("pid",     handlePid);
  WebSerial.on("pidMode", handlePidMode);

  if (!initFilesystemAndConfig()) {
    WebSerial.send("message", "FATAL: Filesystem/config init failed");
  }

  Wire1.setSDA(SDA1);
  Wire1.setSCL(SCL1);
  Wire1.begin();
  Wire1.setClock(400000);

  ads_ok = ads.begin();
  if (ads_ok) {
    ads.setGain(1);
    ads.setDataRate(4);
    WebSerial.send("message", "ADS1115 OK @0x48 (Wire1)");
  } else {
    WebSerial.send("message", "ERROR: ADS1115 not found @0x48");
  }

  dac_ok[0] = dac0.begin(0x60, &Wire1);
  dac_ok[1] = dac1.begin(0x61, &Wire1);
  WebSerial.send("message", dac_ok[0] ? "MCP4725 #0 OK @0x60 (Wire1)" : "ERROR: MCP4725 #0 not found");
  WebSerial.send("message", dac_ok[1] ? "MCP4725 #1 OK @0x61 (Wire1)" : "ERROR: MCP4725 #1 not found");

  rtd_ok[0] = rtd1.begin(RTD_WIRES);
  rtd_ok[1] = rtd2.begin(RTD_WIRES);
  WebSerial.send("message", rtd_ok[0] ? "MAX31865 RTD1 OK" : "ERROR: MAX31865 RTD1 fail");
  WebSerial.send("message", rtd_ok[1] ? "MAX31865 RTD2 OK" : "ERROR: MAX31865 RTD2 fail");

  writeDac(0, dacRaw[0]);
  writeDac(1, dacRaw[1]);

  Serial2.setTX(TX2);
  Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("AIO422-AIO");

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  for (uint16_t i=0;i<NUM_BTN;i++) mb.addIsts(ISTS_BTN_BASE + i);
  for (uint16_t i=0;i<NUM_LED;i++) mb.addIsts(ISTS_LED_BASE + i);

  for (uint16_t i=0;i<2;i++) mb.addHreg(HREG_TEMP_BASE  + i);
  for (uint16_t i=0;i<4;i++) mb.addHreg(HREG_AI_MV_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addHreg(HREG_DAC_BASE   + i, dacRaw[i]);

  for (uint16_t i=0;i<4;i++) {
    mb.addHreg(HREG_SP_BASE        + i, 0);
    mb.addHreg(HREG_PID_EN_BASE    + i, 0);
    mb.addHreg(HREG_PID_KP_BASE    + i, 0);
    mb.addHreg(HREG_PID_KI_BASE    + i, 0);
    mb.addHreg(HREG_PID_KD_BASE    + i, 0);
    mb.addHreg(HREG_PID_OUT_BASE   + i, 0);
  }

  WebSerial.send("message",
    "Boot OK (AIO-422-R1 RP2350: ADS1115@Wire1, 2xMCP4725@Wire1, 2xMAX31865 softSPI, 4 BTN, 4 LED, 4xPID with % scaling + virtual outputs + PID->PID SP)");

  sendAllEchoesOnce();
}

// ================== send initial state ==================
void sendAllEchoesOnce() {
  JSONVar dacList;
  dacList[0] = dacRaw[0];
  dacList[1] = dacRaw[1];
  WebSerial.send("dacValues", dacList);

  JSONVar ledList;
  for (int i=0;i<NUM_LED;i++) ledList[i] = ledState[i];
  WebSerial.send("LedStateList", ledList);

  JSONVar btnList;
  for (int i=0;i<NUM_BTN;i++) btnList[i] = buttonState[i];
  WebSerial.send("ButtonStateList", btnList);

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);

  sendPidSnapshot();
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  mb.task();

  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else               WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  for (int i=0;i<NUM_BTN;i++) {
    bool pressed = (digitalRead(BTN_PINS[i]) == HIGH);
    buttonPrev[i]  = buttonState[i];
    buttonState[i] = pressed;

    if (!buttonPrev[i] && buttonState[i]) {
      ledState[i] = !ledState[i];
      digitalWrite(LED_PINS[i], ledState[i] ? HIGH : LOW);
    }

    mb.setIsts(ISTS_BTN_BASE + i, pressed);
  }

  for (int i=0;i<NUM_LED;i++) {
    mb.setIsts(ISTS_LED_BASE + i, ledState[i]);
  }

  for (int i=0;i<2;i++) {
    uint16_t regVal = mb.Hreg(HREG_DAC_BASE + i);
    if (regVal != dacRaw[i]) {
      dacRaw[i] = regVal;
      writeDac(i, dacRaw[i]);
      cfgDirty = true;
      lastCfgTouchMs = now;
    }
  }

  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readSensors();
  }

  updatePids();

  if (now - lastSend >= sendInterval) {
    lastSend = now;

    WebSerial.check();
    WebSerial.send("status", modbusStatus);

    JSONVar aiList;
    for (int i=0;i<4;i++) aiList[i] = aiMv[i];
    WebSerial.send("aiValues", aiList);

    JSONVar tempList;
    for (int i=0;i<2;i++) tempList[i] = rtdTemp_x10[i];
    WebSerial.send("rtdTemps_x10", tempList);

    JSONVar dacList;
    dacList[0] = dacRaw[0];
    dacList[1] = dacRaw[1];
    WebSerial.send("dacValues", dacList);

    JSONVar ledList;
    for (int i=0;i<NUM_LED;i++) ledList[i] = ledState[i];
    WebSerial.send("LedStateList", ledList);

    JSONVar btnList;
    for (int i=0;i<NUM_BTN;i++) btnList[i] = buttonState[i];
    WebSerial.send("ButtonStateList", btnList);

    sendPidSnapshot();
  }
}
