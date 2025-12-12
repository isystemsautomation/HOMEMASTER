// ==== AIO-422-R1 (RP2350) SIMPLE FW (ALL ANALOG → HOLDING REGS + 4x PID) ====
// FIXED: PID settings no longer revert. PID config is stored in pid[] (single source of truth),
// and Modbus regs are mirrored for visibility. updatePids() does NOT overwrite config from Modbus.

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
#include <math.h>                 // for roundf / lroundf
#include "hardware/watchdog.h"

// ================== UART2 (RS-485 / Modbus) ==================
#define TX2   4
#define RX2   5
const int TxenPin = -1;   // -1 if RS-485 TXEN not used
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== GPIO MAP ==================
static const uint8_t LED_PINS[4] = {18, 19, 20, 21};
static const uint8_t BTN_PINS[4] = {22, 23, 24, 25};

static const uint8_t NUM_LED = 4;
static const uint8_t NUM_BTN = 4;

// ================== I2C / SPI PINS (MATCH WORKING FW) ==================
// I2C on Wire1
#define SDA1 6
#define SCL1 7

// SOFTWARE SPI pins for MAX31865 (like working firmware)
#define RTD1_CS   13
#define RTD2_CS   14
#define RTD_DI    11
#define RTD_DO    12
#define RTD_CLK   10

// ================== Sensors / IO devices ==================
// ADS1115 via ADS1X15.h, on Wire1 at 0x48
ADS1115 ads(0x48, &Wire1);

// MCP4725 DACs (0x60, 0x61) on Wire1
Adafruit_MCP4725 dac0;                // 0x60
Adafruit_MCP4725 dac1;                // 0x61

// MAX31865 RTDs via SOFTWARE SPI
Adafruit_MAX31865 rtd1(RTD1_CS, RTD_DI, RTD_DO, RTD_CLK);
Adafruit_MAX31865 rtd2(RTD2_CS, RTD_DI, RTD_DO, RTD_CLK);

// Flags for successful init
bool ads_ok     = false;
bool dac_ok[2]  = {false, false};
bool rtd_ok[2]  = {false, false};

// RTD parameters (from schematic: Rref = 400Ω, PT100 assumed)
const float RTD_RREF      = 200.0f;     // NOTE: schematic says 400Ω, firmware uses 200Ω
const float RTD_RNOMINAL  = 100.0f;
const max31865_numwires_t RTD_WIRES = MAX31865_2WIRE;  // same as working FW

// ================== ADC field scaling ==================
// Front-end attenuates 0–10V field → ~0–3.3V at ADS input
// So field ≈ ADC * 10/3.3 ≈ 3.0303 × ADC
#define ADC_FIELD_SCALE_NUM 30303   // 3.0303 * 10000
#define ADC_FIELD_SCALE_DEN 10000
#define ADC_FIELD_SCALE ((float)ADC_FIELD_SCALE_NUM / (float)ADC_FIELD_SCALE_DEN)

// ================== Runtime state ==================
bool buttonState[NUM_BTN] = {false,false,false,false};
bool buttonPrev[NUM_BTN]  = {false,false,false,false};
bool ledState[NUM_LED]    = {false,false,false,false};

int16_t  aiRaw[4]   = {0,0,0,0};      // ADS1115 raw codes
uint16_t aiMv[4]    = {0,0,0,0};      // field mV (0..10000)
int16_t  rtdTemp_x10[2] = {0,0};      // temperature *10 °C

// DAC raw values (0..4095)
uint16_t dacRaw[2] = {0,0};

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend       = 0;
const unsigned long sendInterval   = 250;

unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 200;

// PID timing
unsigned long lastPidUpdateMs = 0;
const unsigned long pidIntervalMs = 200;   // PID update every 200 ms

// ================== Persisted Modbus settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
struct PersistConfig {
  uint32_t magic;
  uint16_t version;
  uint16_t size;

  uint16_t dacRaw[2];
  uint8_t  mb_address;
  uint32_t mb_baud;

  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314F4941UL; // 'AIO1'
static const uint16_t CFG_VERSION = 0x0001;
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

inline bool timeAfter32(uint32_t a, uint32_t b) {
  return (int32_t)(a - b) >= 0;
}

// ---- helpers (FIX) ----
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
}

void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);

  pc.dacRaw[0] = dacRaw[0];
  pc.dacRaw[1] = dacRaw[1];

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

  dacRaw[0] = pc.dacRaw[0];
  dacRaw[1] = pc.dacRaw[1];

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
  f.flush();
  f.close();
  if (n != sizeof(pc)) {
    WebSerial.send("message", String("save: short write ")+n);
    return false;
  }

  // quick verify
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

// ================== Guarded FS init ==================
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

  WebSerial.send("message", "First save failed. Formatting FS…");
  if (!LittleFS.format() || !LittleFS.begin()) {
    WebSerial.send("message", "FATAL: FS mount/format failed");
    return false;
  }

  setDefaults();
  if (saveConfigFS()) {
    WebSerial.send("message", "FS formatted and config saved");
    return true;
  }

  WebSerial.send("message", "FATAL: save still failing after format");
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

// ================== Modbus map ==================
// NOTE: all analog values are now HOLDING REGS (FC3)
enum : uint16_t {
  ISTS_BTN_BASE   = 1,     // 1..4  : buttons        (discrete inputs)
  ISTS_LED_BASE   = 20,    // 20..23: LED states     (discrete inputs)

  HREG_AI_BASE     = 100,  // 100..103: ADS1115 raw counts (int16)
  HREG_TEMP_BASE   = 120,  // 120..121: RTD temps *10°C (signed)
  HREG_AI_MV_BASE  = 140,  // 140..143: AI field mV (0..10000)
  HREG_DAC_BASE    = 200,  // 200..201: DAC raw value 0..4095

  // PID-related holding registers
  HREG_SP_BASE        = 300,  // 300..303: SP1..SP4 (raw units, signed)
  HREG_PID_EN_BASE    = 310,  // 310..313: enable (0=off,1=on)
  HREG_PID_PVSEL_BASE = 320,  // 320..323: PV source (0=none,1..4=AI1..4,5=RTD1,6=RTD2)
  HREG_PID_SPSEL_BASE = 330,  // 330..333: SP source (0=manual,1..4=SP1..4,5..8=virt out1..4)
  HREG_PID_OUTSEL_BASE= 340,  // 340..343: OUT target (0=none,1=AO1,2=AO2,3=virtual-only)
  HREG_PID_KP_BASE    = 350,  // 350..353: Kp *100
  HREG_PID_KI_BASE    = 360,  // 360..363: Ki *100
  HREG_PID_KD_BASE    = 370,  // 370..373: Kd *100
  HREG_PID_OUT_BASE   = 380,  // 380..383: PID output (0..4095) (raw)
  HREG_PID_PVVAL_BASE = 390,  // 390..393: PID PV snapshot (raw units)
  HREG_PID_ERR_BASE   = 400   // 400..403: PID error (SP-PV, raw units, signed)
};

// ================== PID state ==================
struct PIDState {
  bool    enabled;
  uint8_t pvSource;
  uint8_t spSource;
  uint8_t outTarget;   // 0=none,1=AO1,2=AO2,3=virtual-only
  uint8_t mode;        // 0 = direct, 1 = reverse (indirect)

  float   Kp;
  float   Ki;
  float   Kd;

  float   integral;
  float   prevError;   // in % domain
  float   output;      // RAW output (0..4095)

  // Scaling (raw → %)
  float   pvMin;
  float   pvMax;
  float   outMin;
  float   outMax;

  // Last scaled values (for Web UI)
  float   pvPct;
  float   spPct;
  float   outPct;
};

PIDState pid[4];

// Per-PID virtual outputs (raw and %)
float pidVirtRaw[4] = {0,0,0,0};
float pidVirtPct[4] = {0,0,0,0};

// ================== Fw decls ==================
void handleValues(JSONVar values);
void handleCommand(JSONVar obj);
void handleDac(JSONVar obj);
void handlePid(JSONVar obj);
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

// expects: { "list":[dac0,dac1] } values 0..4095
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

// expects object:
// {
//   "sp":[...], "en":[...], "pv":[...],
//   "sp_src":[...], "out":[...],
//   "kp":[...], "ki":[...], "kd":[...],
//   "pv_min":[...], "pv_max":[...],
//   "out_min":[...], "out_max":[...],
//   "mode":[...]            // 0=direct,1=reverse
// }
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
  JSONVar mode   = obj["mode"];

  for (int i = 0; i < 4; i++) {
    PIDState &p = pid[i];

    // Manual SP stays in Modbus regs (used by getPidSpValue when src==0)
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
      mb.Hreg(HREG_PID_PVSEL_BASE + i, (uint16_t)p.pvSource);
    }

    if (JSON.typeof(spSrc) == "array" && i < (int)spSrc.length()) {
      int v = (int)spSrc[i];
      p.spSource = clamp_u8(v, 0, 8);
      mb.Hreg(HREG_PID_SPSEL_BASE + i, (uint16_t)p.spSource);
    }

    if (JSON.typeof(out) == "array" && i < (int)out.length()) {
      int v = (int)out[i];
      p.outTarget = clamp_u8(v, 0, 3);
      mb.Hreg(HREG_PID_OUTSEL_BASE + i, (uint16_t)p.outTarget);
    }

    // Gains are sent as integer *100 from UI
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

    // Scaling from UI (raw units)
    if (JSON.typeof(pvMin) == "array" && i < (int)pvMin.length()) p.pvMin = (float)((double)pvMin[i]);
    if (JSON.typeof(pvMax) == "array" && i < (int)pvMax.length()) p.pvMax = (float)((double)pvMax[i]);
    if (JSON.typeof(outMin)== "array" && i < (int)outMin.length()) p.outMin = (float)((double)outMin[i]);
    if (JSON.typeof(outMax)== "array" && i < (int)outMax.length()) p.outMax = (float)((double)outMax[i]);

    // Mode only from WebSerial
    if (JSON.typeof(mode) == "array" && i < (int)mode.length()) {
      int m = (int)mode[i];
      p.mode = clamp_u8(m, 0, 1);
    }
  }

  WebSerial.send("message", "PID configuration updated via WebSerial");
}

// ================== DAC write helper ==================
void writeDac(int idx, uint16_t value) {
  if (idx == 0 && dac_ok[0]) {
    dac0.setVoltage(value, false);
  } else if (idx == 1 && dac_ok[1]) {
    dac1.setVoltage(value, false);
  }
}

// ================== Sensor read helper ==================
void readSensors() {
  // --- Analog inputs via ADS1115 (ADS1X15) ---
  if (ads_ok) {
    for (int ch=0; ch<4; ch++) {
      int16_t raw = ads.readADC(ch);      // single-ended channel ch
      aiRaw[ch] = raw;

      // Convert to volts at ADS pin, then to field mV (0–10V)
      float v_adc   = ads.toVoltage(raw);         // V at ADC input
      float v_field = v_adc * ADC_FIELD_SCALE;    // V at field side
      long  mv      = lroundf(v_field * 1000.0f); // mV

      if (mv < 0)      mv = 0;
      if (mv > 65535)  mv = 65535;

      aiMv[ch] = (uint16_t)mv;

      // Modbus: raw counts and field mV -> HOLDING registers
      mb.Hreg(HREG_AI_BASE    + ch, (uint16_t)raw);
      mb.Hreg(HREG_AI_MV_BASE + ch, aiMv[ch]);
    }
  } else {
    for (int ch=0; ch<4; ch++) {
      aiRaw[ch] = 0;
      aiMv[ch]  = 0;
      mb.Hreg(HREG_AI_BASE    + ch, 0);
      mb.Hreg(HREG_AI_MV_BASE + ch, 0);
    }
  }

  // --- RTDs via MAX31865 (software SPI) ---
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
    // AI1..AI4 (mV)
    uint8_t idx = src - 1;
    if (idx < 4) {
      ok = true;
      return (float)((int32_t)aiMv[idx]);  // raw mV
    }
  } else if (src == 5) {
    // RTD1
    ok = true;
    return (float)rtdTemp_x10[0];          // raw *10°C
  } else if (src == 6) {
    // RTD2
    ok = true;
    return (float)rtdTemp_x10[1];          // raw *10°C
  }
  return 0.0f;
}

// src == 0 ("Manual") → use this PID's own SPn register (300 + pidIndex)
// src  1..4           → use global SP1..SP4 from Modbus
// src  5..8           → use virtual output from PID1..4 (raw)
float getPidSpValue(uint8_t pidIndex, uint8_t src, bool &ok) {
  ok = false;

  if (src >= 1 && src <= 4) {
    uint8_t idx = src - 1;
    uint16_t reg = mb.Hreg(HREG_SP_BASE + idx);   // SP1..SP4
    int16_t raw = (int16_t)reg;
    ok = true;
    return (float)raw;
  } else if (src == 0) {
    // Manual SP (SP1 for PID1, SP2 for PID2, ...)
    uint16_t reg = mb.Hreg(HREG_SP_BASE + pidIndex);
    int16_t raw = (int16_t)reg;
    ok = true;
    return (float)raw;
  } else if (src >= 5 && src <= 8) {
    uint8_t idx = src - 5; // 0..3 → PID1..4 virtual outputs
    if (idx < 4) {
      ok = true;
      return pidVirtRaw[idx];   // raw virtual output
    }
  }

  return 0.0f;
}

// ================== PID update ==================
void updatePids() {
  unsigned long now = millis();
  if (now - lastPidUpdateMs < pidIntervalMs) return;

  float dt = (now - lastPidUpdateMs) / 1000.0f;
  if (dt <= 0.0f) dt = pidIntervalMs / 1000.0f;
  lastPidUpdateMs = now;

  for (int i = 0; i < 4; i++) {
    PIDState &p = pid[i];

    // FIX: DO NOT re-read PID config from Modbus here (it caused reverting).
    bool  pvOk   = false;
    bool  spOk   = false;
    float pvRaw  = getPidPvValue(p.pvSource, pvOk);         // raw PV (mV or °C×10)
    float spRaw  = getPidSpValue(i, p.spSource, spOk);      // raw SP (same units)
    float errRaw = 0.0f;

    // Defaults if user never set ranges
    if (p.pvMax <= p.pvMin) {
      p.pvMin = 0.0f;
      p.pvMax = 10000.0f;         // 0–10V mV default
    }
    if (p.outMax <= p.outMin) {
      p.outMin = 0.0f;
      p.outMax = 4095.0f;
    }

    if (!(p.enabled && pvOk && spOk)) {
      // PID disabled or invalid config: reset state and publish zeros
      p.integral  = 0.0f;
      p.prevError = 0.0f;
      p.output    = 0.0f;
      p.pvPct     = 0.0f;
      p.spPct     = 0.0f;
      p.outPct    = 0.0f;
      pidVirtRaw[i] = 0.0f;
      pidVirtPct[i] = 0.0f;

      mb.Hreg(HREG_PID_OUT_BASE   + i, 0);
      mb.Hreg(HREG_PID_PVVAL_BASE + i, 0);
      mb.Hreg(HREG_PID_ERR_BASE   + i, 0);
      continue;
    }

    // ----- RAW error (for Modbus) -----
    errRaw = spRaw - pvRaw;

    // ----- Scale RAW → % for internal PID -----
    float pvPct = (pvRaw - p.pvMin) * 100.0f / (p.pvMax - p.pvMin);
    float spPct = (spRaw - p.pvMin) * 100.0f / (p.pvMax - p.pvMin);

    // clamp 0..100
    if (pvPct < 0.0f)   pvPct = 0.0f;
    if (pvPct > 100.0f) pvPct = 100.0f;
    if (spPct < 0.0f)   spPct = 0.0f;
    if (spPct > 100.0f) spPct = 100.0f;

    // Working mode: 0=direct, 1=reverse (indirect)
    float errorPct = spPct - pvPct;
    if (p.mode == 1) {
      // reverse action
      errorPct = -errorPct;
    }

    // basic PID in % domain
    p.integral += errorPct * dt * p.Ki;
    // anti-windup clamp in % domain (allow negative)
    if (p.integral > 100.0f)  p.integral = 100.0f;
    if (p.integral < -100.0f) p.integral = -100.0f;

    float derivPct = 0.0f;
    if (dt > 0.0f) {
      derivPct = (errorPct - p.prevError) / dt;
    }

    float uPct = p.Kp * errorPct + p.integral + p.Kd * derivPct;
    p.prevError = errorPct;

    // Clamp controller output to 0..100%
    if (uPct < 0.0f)   uPct = 0.0f;
    if (uPct > 100.0f) uPct = 100.0f;

    // Scale % → RAW output
    float outSpan = (p.outMax - p.outMin);
    if (outSpan < 1.0f) outSpan = 1.0f;

    float outRawF = p.outMin + (uPct / 100.0f) * outSpan;
    if (outRawF < 0.0f)    outRawF = 0.0f;
    if (outRawF > 4095.0f) outRawF = 4095.0f;

    p.output = outRawF;
    p.pvPct  = pvPct;
    p.spPct  = spPct;
    p.outPct = uPct;

    // update this PID's virtual output (always, independent of physical target)
    pidVirtRaw[i] = p.output;
    pidVirtPct[i] = uPct;

    // Write diagnostics back to Modbus (RAW values only)
    mb.Hreg(HREG_PID_OUT_BASE   + i, (uint16_t)lroundf(p.output));
    mb.Hreg(HREG_PID_PVVAL_BASE + i, (uint16_t)(int16_t)lroundf(pvRaw));
    mb.Hreg(HREG_PID_ERR_BASE   + i, (uint16_t)(int16_t)lroundf(errRaw));

    // Apply output to AO if mapped
    if (p.outTarget == 1 || p.outTarget == 2) {
      int ch = p.outTarget - 1;    // 1->0, 2->1
      uint16_t val = (uint16_t)lroundf(p.output);
      dacRaw[ch] = val;
      mb.Hreg(HREG_DAC_BASE + ch, dacRaw[ch]);  // reflect actual AO
      writeDac(ch, dacRaw[ch]);
    }
    // p.outTarget == 0 or 3 → virtual-only / none: no physical AO written
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
    // SP stays from Modbus regs (manual SP storage)
    spArr[i]      = (int16_t)mb.Hreg(HREG_SP_BASE + i);

    // FIX: report config from pid[] (single source of truth)
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

  pidObj["sp"]      = spArr;
  pidObj["en"]      = enArr;
  pidObj["pv"]      = pvArr;
  pidObj["sp_src"]  = spSrcArr;
  pidObj["out"]     = outArr;
  pidObj["kp"]      = kpArr;
  pidObj["ki"]      = kiArr;
  pidObj["kd"]      = kdArr;

  pidObj["pv_min"]  = pvMinArr;
  pidObj["pv_max"]  = pvMaxArr;
  pidObj["out_min"] = outMinArr;
  pidObj["out_max"] = outMaxArr;

  pidObj["pv_pct"]  = pvPctArr;
  pidObj["sp_pct"]  = spPctArr;
  pidObj["out_pct"] = outPctArr;

  pidObj["mode"]    = modeArr;

  pidObj["virt_raw"] = virtRawArr;
  pidObj["virt_pct"] = virtPctArr;

  WebSerial.send("pidState", pidObj);
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);

  // GPIO
  for (uint8_t i=0;i<NUM_LED;i++) {
    pinMode(LED_PINS[i], OUTPUT);
    digitalWrite(LED_PINS[i], LOW);
    ledState[i] = false;
  }
  for (uint8_t i=0;i<NUM_BTN;i++) {
    // BUTTON_USES_PULLUP == 0 → INPUT, HIGH = pressed
    pinMode(BTN_PINS[i], INPUT);
    buttonState[i] = buttonPrev[i] = false;
  }

  setDefaults();

  // Init PID states
  for (int i=0;i<4;i++) {
    pid[i].enabled   = false;
    pid[i].pvSource  = 0;
    pid[i].spSource  = 0;
    pid[i].outTarget = 0;
    pid[i].mode      = 0;       // direct
    pid[i].Kp = pid[i].Ki = pid[i].Kd = 0.0f;
    pid[i].integral  = 0.0f;
    pid[i].prevError = 0.0f;
    pid[i].output    = 0.0f;

    pid[i].pvMin     = 0.0f;
    pid[i].pvMax     = 10000.0f;   // default 0–10V in mV
    pid[i].outMin    = 0.0f;
    pid[i].outMax    = 4095.0f;    // full DAC range

    pid[i].pvPct     = 0.0f;
    pid[i].spPct     = 0.0f;
    pid[i].outPct    = 0.0f;

    pidVirtRaw[i]    = 0.0f;
    pidVirtPct[i]    = 0.0f;
  }

  // WebSerial hooks
  WebSerial.on("values",  handleValues);
  WebSerial.on("command", handleCommand);
  WebSerial.on("dac",     handleDac);
  WebSerial.on("pid",     handlePid);

  // Guarded FS init
  if (!initFilesystemAndConfig()) {
    WebSerial.send("message", "FATAL: Filesystem/config init failed");
  }

  // I2C setup (Wire1 on SDA1/SCL1)
  Wire1.setSDA(SDA1);
  Wire1.setSCL(SCL1);
  Wire1.begin();
  Wire1.setClock(400000);

  // --- ADS1115 ---
  ads_ok = ads.begin();
  if (ads_ok) {
    ads.setGain(1);        // GAIN_ONE (±4.096V)
    ads.setDataRate(4);    // 128 SPS
    WebSerial.send("message", "ADS1115 OK @0x48 (Wire1)");
  } else {
    WebSerial.send("message", "ERROR: ADS1115 not found @0x48");
  }

  // --- DACs MCP4725 (Wire1) ---
  dac_ok[0] = dac0.begin(0x60, &Wire1);
  dac_ok[1] = dac1.begin(0x61, &Wire1);
  if (dac_ok[0]) WebSerial.send("message", "MCP4725 #0 OK @0x60 (Wire1)");
  else           WebSerial.send("message", "ERROR: MCP4725 #0 not found");
  if (dac_ok[1]) WebSerial.send("message", "MCP4725 #1 OK @0x61 (Wire1)");
  else           WebSerial.send("message", "ERROR: MCP4725 #1 not found");

  // --- MAX31865 RTDs (software SPI) ---
  rtd_ok[0] = rtd1.begin(RTD_WIRES);
  rtd_ok[1] = rtd2.begin(RTD_WIRES);
  if (rtd_ok[0]) WebSerial.send("message", "MAX31865 RTD1 OK");
  else           WebSerial.send("message", "ERROR: MAX31865 RTD1 fail");
  if (rtd_ok[1]) WebSerial.send("message", "MAX31865 RTD2 OK");
  else           WebSerial.send("message", "ERROR: MAX31865 RTD2 fail");

  // Apply persisted DAC outputs
  writeDac(0, dacRaw[0]);
  writeDac(1, dacRaw[1]);

  // Serial2 / Modbus
  Serial2.setTX(TX2);
  Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("AIO422-AIO");

  // Modbus status object
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  // ---- Modbus map ----
  // Discrete inputs (FC02)
  for (uint16_t i=0;i<NUM_BTN;i++) mb.addIsts(ISTS_BTN_BASE + i);
  for (uint16_t i=0;i<NUM_LED;i++) mb.addIsts(ISTS_LED_BASE + i);

  // Holding registers (AI raw, RTD temps, AI mV, DACs) via FC03
  for (uint16_t i=0;i<4;i++) mb.addHreg(HREG_AI_BASE    + i);
  for (uint16_t i=0;i<2;i++) mb.addHreg(HREG_TEMP_BASE  + i);
  for (uint16_t i=0;i<4;i++) mb.addHreg(HREG_AI_MV_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addHreg(HREG_DAC_BASE   + i, dacRaw[i]);

  // PID-related holding registers (mirrors; config lives in pid[])
  for (uint16_t i=0;i<4;i++) {
    mb.addHreg(HREG_SP_BASE        + i, 0);
    mb.addHreg(HREG_PID_EN_BASE    + i, 0);
    mb.addHreg(HREG_PID_PVSEL_BASE + i, 0);
    mb.addHreg(HREG_PID_SPSEL_BASE + i, 0);
    mb.addHreg(HREG_PID_OUTSEL_BASE+ i, 0);
    mb.addHreg(HREG_PID_KP_BASE    + i, 0);
    mb.addHreg(HREG_PID_KI_BASE    + i, 0);
    mb.addHreg(HREG_PID_KD_BASE    + i, 0);
    mb.addHreg(HREG_PID_OUT_BASE   + i, 0);
    mb.addHreg(HREG_PID_PVVAL_BASE + i, 0);
    mb.addHreg(HREG_PID_ERR_BASE   + i, 0);
  }

  WebSerial.send("message",
    "Boot OK (AIO-422-R1 RP2350: ADS1115@Wire1, 2xMCP4725@Wire1, 2xMAX31865 softSPI, 4 BTN, 4 LED, 4xPID with % scaling + virtual outputs)");

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

  // PID snapshot to UI
  sendPidSnapshot();
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  mb.task();      // Modbus polling

  // --- auto-save after quiet period ---
  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else               WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  // --- buttons & LEDs (simple: button i toggles LED i) ---
  for (int i=0;i<NUM_BTN;i++) {
    bool pressed = (digitalRead(BTN_PINS[i]) == HIGH);  // HIGH = pressed
    buttonPrev[i]  = buttonState[i];
    buttonState[i] = pressed;

    // rising edge (inactive -> active)
    if (!buttonPrev[i] && buttonState[i]) {
      ledState[i] = !ledState[i];
      digitalWrite(LED_PINS[i], ledState[i] ? HIGH : LOW);
    }

    mb.setIsts(ISTS_BTN_BASE + i, pressed);
  }

  for (int i=0;i<NUM_LED;i++) {
    mb.setIsts(ISTS_LED_BASE + i, ledState[i]);
  }

  // --- DACs from Modbus holding registers (manual control) ---
  // NOTE: PIDs may override dacRaw[] and HREG_DAC_BASE later in updatePids()
  for (int i=0;i<2;i++) {
    uint16_t regVal = mb.Hreg(HREG_DAC_BASE + i);
    if (regVal != dacRaw[i]) {
      dacRaw[i] = regVal;
      writeDac(i, dacRaw[i]);
      cfgDirty = true;
      lastCfgTouchMs = now;
    }
  }

  // --- periodic sensor read ---
  if (now - lastSensorRead >= sensorInterval) {
    lastSensorRead = now;
    readSensors();
  }

  // --- PID update (runs every pidIntervalMs) ---
  updatePids();

  // --- WebSerial periodic updates ---
  if (now - lastSend >= sendInterval) {
    lastSend = now;

    WebSerial.check();
    WebSerial.send("status", modbusStatus);

    // send field mV values to UI
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

    // keep Web UI PID cards in sync with pid[] config + scaled values
    sendPidSnapshot();
  }
}
