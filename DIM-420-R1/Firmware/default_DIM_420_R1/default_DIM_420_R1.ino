/********** Arduino preprocessor fix: forward declare before includes **********/
struct PersistConfig;

/**************************************************************
 * DIM-420-R1 — RP2350A (Pico 2) firmware (QUIET WEB CONFIG)
 * - Sends a compact JSON "config" snapshot via WebSerial once/second
 * - Waits for settings from WebConfig ("values", "Config")
 * - No handshakes/hello/echo/status messages
 **************************************************************/

#include <Arduino.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include <math.h>
#include "hardware/watchdog.h"  // Pico SDK (RP2350)
#include "pico/time.h"          // time_us_64(), add_alarm_in_us(), alarm_id_t

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
static const uint8_t GATE_PINS[2] = {1, 3};       // CH1/CH2 gate (MOSFET driver)
// User LEDs (active-HIGH)
static const uint8_t LED_PINS[4]  = {18, 19, 20, 21};
// Buttons (schematic inverted: active-HIGH)
static const uint8_t BTN_PINS[4]  = {22, 23, 24, 25};

// ================== Sizes ==================
static const uint8_t NUM_DI   = 4;
static const uint8_t NUM_CH   = 2;  // dimmer channels
static const uint8_t NUM_LED  = 4;
static const uint8_t NUM_BTN  = 4;

// ================== AC timing / MOSFET gating ==================
constexpr uint32_t HALF_US_DEFAULT  = 10000u; // fallback (50 Hz)
constexpr uint32_t ZC_BLANK_US      = 100u;   // guard after ZC before switching
constexpr uint32_t MOS_OFF_GUARD_US = 150u;   // guard before next ZC to switch OFF
constexpr uint32_t GATE_PULSE_US    = 120u;   // (not used, MOSFET windows are timed)

// ---- State (volatile: accessed in ISR) ----
volatile uint8_t  chLevel[NUM_CH] = {0,0};           // 0..255 current
volatile uint8_t  chLastNonZero[NUM_CH] = {200,200}; // remembered last non-zero

// ================== Digital Input switch model ==================
enum DiSwitchType : uint8_t { DI_SW_MOMENTARY=0, DI_SW_LATCHING=1 };
enum DiPressType  : uint8_t { PRESS_SHORT=0, PRESS_LONG=1, PRESS_DOUBLE_SHORT=2, PRESS_SHORT_THEN_LONG=3, PRESS_COUNT=4 };
enum DiAction     : uint8_t {
  DI_ACT_NONE=0, DI_ACT_TOGGLE_TO_PRESET=1, DI_ACT_TURN_OFF=2,
  DI_ACT_TOGGLE_OUTPUT=3, DI_ACT_INC=4, DI_ACT_DEC=5, DI_ACT_INC_THEN_DEC=6
};
enum DiTarget     : uint8_t { DI_TGT_NONE=0, DI_TGT_CH1=1, DI_TGT_CH2=2, DI_TGT_ALL=4 };
enum DiLatchMode  : uint8_t { LATCH_TOGGLE_TO_PRESET_OR_0=0, LATCH_PINGPONG_UNTIL_TOGGLE=1 };

// ================== Config & runtime ==================
struct InCfg {
  bool     enabled;
  bool     inverted;
  uint8_t  switchType;                 // DiSwitchType
  uint8_t  pressAction[PRESS_COUNT];   // DiAction per press
  uint8_t  pressTarget[PRESS_COUNT];   // DiTarget per press
  uint8_t  latchMode;                  // DiLatchMode
  uint8_t  latchTarget;                // DiTarget
};
struct ChCfg { bool enabled; };
struct LedCfg { uint8_t mode; uint8_t source; };
struct BtnCfg { uint8_t action; };

InCfg  diCfg[NUM_DI];
ChCfg  chCfg[NUM_CH];
LedCfg ledCfg[NUM_LED];
BtnCfg btnCfg[NUM_BTN];

bool buttonState[NUM_BTN] = {false,false,false,false};
bool buttonPrev[NUM_BTN]  = {false,false,false,false};

uint32_t chPulseUntil[NUM_CH] = {0,0};
const uint32_t PULSE_MS = 500;

// ================== ZC monitor (per-channel) ==================
volatile uint32_t zcLastEdgeMs[NUM_CH] = {0,0}; // last ISR edge time (ms)
bool zcOk[NUM_CH]      = {false,false};
bool zcPrevOk[NUM_CH]  = {false,false};

// Debounce / hysteresis for AC presence
const uint32_t ZC_FAULT_TIMEOUT_MS = 1000;
const uint8_t  ZC_OK_STREAK_N      = 6;
const uint8_t  ZC_FAULT_STREAK_N   = 6;
uint8_t zcOkStreak[NUM_CH]    = {0,0};
uint8_t zcFaultStreak[NUM_CH] = {0,0};

// ================== Frequency measurement (robust) ==================
volatile uint64_t zcPrevEdgeUs64[NUM_CH] = {0,0};
volatile uint32_t zcHalfUsLatest[NUM_CH] = {HALF_US_DEFAULT,HALF_US_DEFAULT};
volatile uint32_t zcSampleSeq[NUM_CH]    = {0,0};

constexpr uint32_t HALF_MIN_US = 7500;   // ~66.6 Hz
constexpr uint32_t HALF_MAX_US = 12000;  // ~41.6 Hz

constexpr int MED_W = 3;
constexpr int AVG_N = 32;
uint32_t med3[NUM_CH][MED_W] = {{HALF_US_DEFAULT,HALF_US_DEFAULT,HALF_US_DEFAULT},
                                {HALF_US_DEFAULT,HALF_US_DEFAULT,HALF_US_DEFAULT}};
uint8_t  medIdx[NUM_CH]      = {0,0};
uint32_t avgBuf[NUM_CH][AVG_N] = {{HALF_US_DEFAULT}};
uint8_t  avgIdx[NUM_CH]        = {0,0};
uint64_t avgSum[NUM_CH]        = {(uint64_t)HALF_US_DEFAULT * AVG_N,(uint64_t)HALF_US_DEFAULT * AVG_N};
uint32_t lastSeqConsumed[NUM_CH] = {0,0};
constexpr int CLOCK_PPM_CORR = 0;
inline double corr_half_us(double half_us){ return half_us * (1.0 + (CLOCK_PPM_CORR / 1e6)); }
uint16_t freq_x100[NUM_CH] = {5000,5000}; // Hz × 100
volatile uint32_t gateHalfUs[NUM_CH] = {HALF_US_DEFAULT,HALF_US_DEFAULT};

// ================== Dimming thresholds (per-channel) ==================
uint8_t chLower[NUM_CH] = {20,20};
uint8_t chUpper[NUM_CH] = {255,255};

// ================== Load type + percent setpoint ==================
enum LoadType : uint8_t { LOAD_LAMP=0, LOAD_HEATER=1, LOAD_KEY=2 };
uint8_t  chLoadType[NUM_CH] = {LOAD_LAMP, LOAD_LAMP};
uint16_t chPctX10[NUM_CH]   = {0,0};     // percent ×10 (0..1000)

// ================== MOSFET cutoff (waveform) mode ==================
enum CutMode : uint8_t { CUT_LEADING=0, CUT_TRAILING=1 };
uint8_t chCutMode[NUM_CH] = {CUT_LEADING, CUT_LEADING};

// ================== Per-channel preset level (used by toggle) ==================
uint8_t chPreset[NUM_CH] = {200,200};

// ================== Web Serial & Modbus status ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 1000; // << once per second
unsigned long lastBlinkToggle = 0;
const unsigned long blinkPeriodMs = 400;
bool blinkPhase = false;

// ================== Persisted Modbus settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
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
  uint8_t chLower[NUM_CH];
  uint8_t chUpper[NUM_CH];
  uint8_t chLoadType[NUM_CH];
  uint16_t chPctX10[NUM_CH];
  uint8_t chCutMode[NUM_CH];
  uint8_t chPreset[NUM_CH];
  uint8_t mb_address;
  uint32_t mb_baud;
  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314D4449UL; // 'IDM1'
static const uint16_t CFG_VERSION = 0x0006;
static const char*    CFG_PATH    = "/cfg.bin";

volatile bool   cfgDirty       = false;
uint32_t        lastCfgTouchMs = 0;
const uint32_t  CFG_AUTOSAVE_MS= 1500;

// ===== DI echo gating (unused but kept for state) =====
volatile bool diCfgEchoPending = false;

// (Removed UI handshake/announce flags)

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

// ---- MOSFET gate set callbacks (continuous drive windows) ----
static int64_t mos_on_cb_ch0 (alarm_id_t, void*) { digitalWrite(GATE_PINS[0], HIGH); return 0; }
static int64_t mos_on_cb_ch1 (alarm_id_t, void*) { digitalWrite(GATE_PINS[1], HIGH); return 0; }
static int64_t mos_off_cb_ch0(alarm_id_t, void*) { digitalWrite(GATE_PINS[0], LOW ); return 0; }
static int64_t mos_off_cb_ch1(alarm_id_t, void*) { digitalWrite(GATE_PINS[1], LOW ); return 0; }

// ---- Zero-cross ISR (shared) ----
void zc_isr_common(uint8_t ch) {
  uint64_t now = time_us_64();
  uint64_t prev = zcPrevEdgeUs64[ch];
  zcPrevEdgeUs64[ch] = now;

  if (prev) {
    uint32_t delta = (uint32_t)(now - prev);
    if (delta >= HALF_MIN_US && delta <= HALF_MAX_US) {
      zcHalfUsLatest[ch] = delta;
      zcSampleSeq[ch]++;
    }
  }

  zcLastEdgeMs[ch] = millis();

  // Start each half-cycle safely: drive gate LOW near the ZC
  digitalWrite(GATE_PINS[ch], LOW);

  // MOSFET dimming window planning
  const uint8_t lvl = chLevel[ch];
  if (lvl == 0 || !chCfg[ch].enabled) return;

  uint32_t half_us = gateHalfUs[ch];
  if (half_us < HALF_MIN_US || half_us > HALF_MAX_US) half_us = HALF_US_DEFAULT;

  const uint32_t usable = (half_us > (ZC_BLANK_US + MOS_OFF_GUARD_US + 20))
                        ? (half_us - ZC_BLANK_US - MOS_OFF_GUARD_US)
                        : (half_us/2);

  auto on_cb  = (ch==0) ? mos_on_cb_ch0  : mos_on_cb_ch1;
  auto off_cb = (ch==0) ? mos_off_cb_ch0 : mos_off_cb_ch1;

  if (chCutMode[ch] == CUT_TRAILING) {
    // Trailing-edge (RC): ON shortly after ZC, OFF earlier than next ZC
    uint32_t on_time = (uint32_t)((uint64_t)lvl * usable / 255u);
    if (on_time == 0) return;
    uint32_t t_on_from_zc  = ZC_BLANK_US;
    uint32_t t_off_from_zc = ZC_BLANK_US + on_time;
    uint32_t latest_off    = half_us - MOS_OFF_GUARD_US;
    if (t_off_from_zc > latest_off) t_off_from_zc = latest_off;
    if (t_on_from_zc >= t_off_from_zc) return;

    add_alarm_in_us((int64_t)t_on_from_zc,  on_cb,  nullptr, true);
    add_alarm_in_us((int64_t)t_off_from_zc, off_cb, nullptr, true);
  } else {
    // Leading-edge (RL): OFF after ZC, then ON until near next ZC
    uint32_t delay_to_on   = ZC_BLANK_US + (uint32_t)((uint64_t)(255 - lvl) * usable / 255u);
    uint32_t t_off_from_zc = half_us - MOS_OFF_GUARD_US;
    if (delay_to_on >= t_off_from_zc) return;

    add_alarm_in_us((int64_t)delay_to_on,   on_cb,  nullptr, true);
    add_alarm_in_us((int64_t)t_off_from_zc, off_cb, nullptr, true);
  }
}
void zc_isr_ch0(){ zc_isr_common(0); }
void zc_isr_ch1(){ zc_isr_common(1); }

// ================== Mapping helpers (percent -> level) ==================
constexpr double LAMP_LOG_MAX = 6.0;
inline uint8_t clamp8(int v){ return (uint8_t)constrain(v, 0, 255); }

uint8_t mapLampPercentToLevel(uint8_t ch, double pct) {
  if (pct <= 0.0) return 0;
  if (pct > 100.0) pct = 100.0;
  if (pct <= 1.0) return chLower[ch];
  const double L = 1.0 + (LAMP_LOG_MAX - 1.0) * ((pct - 1.0) / 99.0);
  const double t = (L - 1.0) / (LAMP_LOG_MAX - 1.0);
  const double span = (double)(chUpper[ch] - chLower[ch]);
  int lvl = (int)lround(chLower[ch] + t * span);
  if (lvl > 0 && lvl < chLower[ch]) lvl = chLower[ch];
  if (lvl > chUpper[ch]) lvl = chUpper[ch];
  return clamp8(lvl);
}

uint8_t mapHeaterPercentToLevel(uint8_t ch, double pct) {
  if (pct <= 0.0) return 0;
  if (pct < 1.0)  return chLower[ch];
  if (pct > 100.0) pct = 100.0;
  double span = (double)(chUpper[ch] - chLower[ch]);
  int lvl = (int)lround(chLower[ch] + (span * (pct/100.0)));
  if (lvl < chLower[ch]) lvl = chLower[ch];
  if (lvl > chUpper[ch]) lvl = chUpper[ch];
  return clamp8(lvl);
}

// KEY mode: 0% => 0; >0% => upper threshold
uint8_t mapKeyPercentToLevel(uint8_t ch, double pct) {
  if (pct <= 0.0) return 0;
  return chUpper[ch];
}

uint8_t mapPercentToLevel(uint8_t ch, double pct) {
  switch ((LoadType)chLoadType[ch]) {
    case LOAD_LAMP:   return mapLampPercentToLevel(ch, pct);
    case LOAD_HEATER: return mapHeaterPercentToLevel(ch, pct);
    case LOAD_KEY:    return mapKeyPercentToLevel(ch, pct);
  }
  return mapLampPercentToLevel(ch, pct);
}

void setLevelDirect(uint8_t ch, uint8_t lvl) {
  chLevel[ch] = lvl;
  if (lvl > 0) chLastNonZero[ch] = lvl;
  mb.setHreg(400 + ch, chLevel[ch]);
}

// ================== Defaults / persist ==================
static inline void setThresholds(uint8_t ch, int lower, int upper);
static inline void clampAndApplyPreset(uint8_t ch) {
  if (ch >= NUM_CH) return;
  if (chPreset[ch] > 0) {
    if (chPreset[ch] < chLower[ch]) chPreset[ch] = chLower[ch];
    if (chPreset[ch] > chUpper[ch]) chPreset[ch] = chUpper[ch];
  }
}

void initFreqEstimator() {
  for (int i=0;i<NUM_CH;i++) {
    zcPrevEdgeUs64[i] = 0;
    zcHalfUsLatest[i] = HALF_US_DEFAULT;
    zcSampleSeq[i]    = 0;
    med3[i][0] = med3[i][1] = med3[i][2] = HALF_US_DEFAULT;
    medIdx[i] = 0;
    for (int k=0;k<AVG_N;k++) avgBuf[i][k] = HALF_US_DEFAULT;
    avgIdx[i] = 0;
    avgSum[i] = (uint64_t)HALF_US_DEFAULT * AVG_N;
    lastSeqConsumed[i] = 0;
    freq_x100[i] = 5000;
    gateHalfUs[i] = HALF_US_DEFAULT;
  }
}

void setDefaults() {
  // DI defaults
  for (int i=0;i<NUM_DI;i++) {
    diCfg[i].enabled    = true;
    diCfg[i].inverted   = false;
    diCfg[i].switchType = DI_SW_MOMENTARY;
    for (int p=0;p<PRESS_COUNT;p++){ diCfg[i].pressAction[p] = DI_ACT_NONE; diCfg[i].pressTarget[p] = DI_TGT_NONE; }
    diCfg[i].latchMode  = LATCH_TOGGLE_TO_PRESET_OR_0;
    diCfg[i].latchTarget= DI_TGT_NONE;
  }

  for (int i=0; i<NUM_CH; i++)  chCfg[i] = { true };
  for (int i=0; i<NUM_LED; i++) ledCfg[i] = { 0, 0 };
  for (int i=0; i<NUM_BTN; i++) btnCfg[i] = { 0 };

  for (int i=0;i<NUM_CH;i++) {
    chLevel[i] = 0;
    chLastNonZero[i] = 200;
    chPulseUntil[i] = 0;
    zcLastEdgeMs[i] = 0;
    zcOk[i] = zcPrevOk[i] = false;
    zcOkStreak[i] = zcFaultStreak[i] = 0;
    chLower[i] = 20;
    chUpper[i] = 255;
    chLoadType[i] = LOAD_LAMP;
    chPctX10[i]   = 0;
    chCutMode[i]  = CUT_LEADING;
    chPreset[i]   = 200;
  }

  g_mb_address = 3;
  g_mb_baud    = 19200;

  initFreqEstimator();
}

// ===== Persist helpers =====
void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);

  memcpy(pc.diCfg, diCfg, sizeof(diCfg));
  memcpy(pc.chCfg, chCfg, sizeof(chCfg));
  memcpy(pc.ledCfg, ledCfg, sizeof(ledCfg));
  memcpy(pc.btnCfg, btnCfg, sizeof(btnCfg));

  for (int i = 0; i < NUM_CH; ++i) {
    pc.chLevel[i]       = chLevel[i];
    pc.chLastNonZero[i] = chLastNonZero[i];
    pc.chLower[i]       = chLower[i];
    pc.chUpper[i]       = chUpper[i];
    pc.chLoadType[i]    = chLoadType[i];
    pc.chPctX10[i]      = chPctX10[i];
    pc.chCutMode[i]     = chCutMode[i];
    pc.chPreset[i]      = chPreset[i];
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

  for (int i = 0; i < NUM_CH; ++i) {
    chLower[i]       = pc.chLower[i];
    chUpper[i]       = pc.chUpper[i];
    chLastNonZero[i] = constrain(pc.chLastNonZero[i], chLower[i], chUpper[i]);
    chLoadType[i]    = (pc.chLoadType[i] <= LOAD_KEY) ? pc.chLoadType[i] : LOAD_LAMP;
    chPctX10[i]      = (pc.chPctX10[i] > 1000) ? 1000 : pc.chPctX10[i];
    chCutMode[i]     = (pc.chCutMode[i] <= CUT_TRAILING) ? pc.chCutMode[i] : CUT_LEADING;
    chPreset[i]      = pc.chPreset[i];

    clampAndApplyPreset(i);

    uint8_t lvl = pc.chLevel[i];
    if (lvl == 0) chLevel[i] = 0;
    else if (lvl < chLower[i]) chLevel[i] = 0;
    else if (lvl > chUpper[i]) chLevel[i] = chUpper[i];
    else chLevel[i] = lvl;
  }

  g_mb_address = pc.mb_address;
  g_mb_baud    = pc.mb_baud;
  return true;
}

bool saveConfigFS() {
  PersistConfig pc{}; captureToPersist(pc);
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  size_t n = f.write((const uint8_t*)&pc, sizeof(pc));
  f.flush(); f.close();
  if (n != sizeof(pc)) return false;

  File r = LittleFS.open(CFG_PATH, "r");
  if (!r) return false;
  if ((size_t)r.size() != sizeof(PersistConfig)) { r.close(); return false; }
  PersistConfig back{}; size_t nr = r.read((uint8_t*)&back, sizeof(back)); r.close();
  if (nr != sizeof(back)) return false;
  PersistConfig tmp2 = back; uint32_t crc = tmp2.crc32; tmp2.crc32 = 0;
  if (crc32_update(0, (const uint8_t*)&tmp2, sizeof(tmp2)) != crc) return false;
  return true;
}

bool loadConfigFS() {
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;
  if (f.size() != sizeof(PersistConfig)) { f.close(); return false; }
  PersistConfig pc{}; size_t n = f.read((uint8_t*)&pc, sizeof(pc)); f.close();
  if (n != sizeof(pc)) return false;
  if (!applyFromPersist(pc)) return false;
  return true;
}

// ================== Guarded FS init ==================
bool initFilesystemAndConfig() {
  if (!LittleFS.begin()) {
    if (!LittleFS.format() || !LittleFS.begin()) {
      return false;
    }
  }
  if (loadConfigFS()) return true;
  setDefaults();
  if (saveConfigFS()) return true;
  if (!LittleFS.format() || !LittleFS.begin()) return false;
  setDefaults();
  if (saveConfigFS()) return true;
  return false;
}

// ================== SFINAE helper ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id) -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) { m.setSlaveId(id); }
inline void setSlaveIdIfAvailable(...) {}

// ================== Modbus map ==================
// Discrete inputs (FC=02)
enum : uint16_t {
  ISTS_DI_BASE     = 1,
  ISTS_CH_BASE     = 50,
  ISTS_LED_BASE    = 90,
  ISTS_ZC_OK_BASE  = 120
};
// Coils (FC=05/15)
enum : uint16_t {
  CMD_CH_ON_BASE  = 200,
  CMD_CH_OFF_BASE = 210,
  CMD_DI_EN_BASE  = 300,
  CMD_DI_DIS_BASE = 320
};
// Holding registers (FC=03/16)
enum : uint16_t {
  HREG_DIM_LEVEL_BASE   = 400,  // 400..401: 0..255 (actual)
  HREG_DIM_LO_BASE      = 410,  // 410..411: 0..255
  HREG_DIM_HI_BASE      = 420,  // 420..421: 0..255
  HREG_FREQ_X100_BASE   = 430,  // 430..431: Hz ×100
  HREG_PCT_X10_BASE     = 440,  // 440..441: setpoint percent ×10 (0..1000)
  HREG_LOADTYPE_BASE    = 460,  // 460..461: 0=Lamp,1=Heater,2=Key
  HREG_CUTMODE_BASE     = 470,  // 470..471: 0=Leading,1=Trailing
  HREG_PRESET_BASE      = 480   // 480..481: preset level (0..255)
};

// ================== Fw decls (helpers) ==================
void applyModbusSettings(uint8_t addr, uint32_t baud);
void handleValues(JSONVar values);
void handleUnifiedConfig(JSONVar obj);
void handleCommand(JSONVar obj);
void performReset();
JSONVar LedConfigListFromCfg();
void processModbusCommandPulses();
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now);
void clampAndSetLevel(uint8_t ch, int value);

// ================== DI press detection runtime ==================
const uint32_t SHORT_MAX_MS = 350;
const uint32_t LONG_MIN_MS  = 700;
const uint32_t DOUBLE_GAP_MS= 300;
const uint8_t  STEP_DELTA   = 10;
const uint16_t RAMP_TICK_MS = 80;

struct DiRuntime {
  bool     cur = false;
  bool     prev = false;
  uint32_t lastChange = 0;
  uint32_t pressStart = 0;
  bool     waitingSecond = false;
  uint32_t firstShortAt = 0;
  bool     shortThenLongArmed = false;

  bool     rampActive = false;
  int8_t   rampDir = +1;
  uint32_t lastRampTick = 0;
};
DiRuntime diRt[NUM_DI];

// ============ COMPACT CONFIG SNAPSHOT SENDER ============
void sendConfigSnapshot() {
  JSONVar cfg;

  // System
  cfg["mb"]["address"] = (int)g_mb_address;
  cfg["mb"]["baud"]    = (int)g_mb_baud;

  // Per-channel runtime + config
  for (int i=0;i<NUM_CH;i++){
    JSONVar ch;
    ch["enabled"]  = chCfg[i].enabled;
    ch["level"]    = (int)chLevel[i];
    ch["lower"]    = (int)chLower[i];
    ch["upper"]    = (int)chUpper[i];
    ch["loadType"] = (int)chLoadType[i];
    ch["percent"]  = (int)min((int)(chPctX10[i]/10), 100);
    ch["cutMode"]  = (int)chCutMode[i];
    ch["preset"]   = (int)chPreset[i];
    ch["freq_x100"]= (int)freq_x100[i];
    ch["zc_ok"]    = zcOk[i];
    cfg["ch"][i]   = ch;
  }

  // DIs
  for (int i=0;i<NUM_DI;i++){
    JSONVar d;
    d["enabled"]    = diCfg[i].enabled;
    d["invert"]     = diCfg[i].inverted;
    d["switchType"] = diCfg[i].switchType;
    d["press"]["short"]["action"]        = diCfg[i].pressAction[PRESS_SHORT];
    d["press"]["short"]["target"]        = diCfg[i].pressTarget[PRESS_SHORT];
    d["press"]["long"]["action"]         = diCfg[i].pressAction[PRESS_LONG];
    d["press"]["long"]["target"]         = diCfg[i].pressTarget[PRESS_LONG];
    d["press"]["doubleShort"]["action"]  = diCfg[i].pressAction[PRESS_DOUBLE_SHORT];
    d["press"]["doubleShort"]["target"]  = diCfg[i].pressTarget[PRESS_DOUBLE_SHORT];
    d["press"]["shortThenLong"]["action"]= diCfg[i].pressAction[PRESS_SHORT_THEN_LONG];
    d["press"]["shortThenLong"]["target"]= diCfg[i].pressTarget[PRESS_SHORT_THEN_LONG];
    d["latchMode"]  = diCfg[i].latchMode;
    d["latchTarget"]= diCfg[i].latchTarget;
    cfg["di"][i] = d;
  }

  // Buttons
  for (int i=0;i<NUM_BTN;i++){
    JSONVar b;
    b["action"] = btnCfg[i].action;
    b["state"]  = buttonState[i];
    cfg["btn"][i] = b;
  }

  // LEDs
  for (int i=0;i<NUM_LED;i++){
    JSONVar L;
    L["mode"]   = ledCfg[i].mode;
    L["source"] = ledCfg[i].source;
    bool srcActive = false;
    uint8_t src = ledCfg[i].source;
    if (src >= 1 && src <= 2) {
      int c = src - 1;
      bool chOn = (chCfg[c].enabled && chLevel[c] > 0);
      srcActive = chOn;
    }
    bool phys = (ledCfg[i].mode == 0) ? srcActive : (srcActive && (blinkPhase));
    L["state"] = phys;
    cfg["led"][i] = L;
  }

  WebSerial.send("config", cfg);
}

// ================== Setup ==================
void setup() {
  Serial.begin(57600);

  // GPIO directions
  for (uint8_t i=0;i<NUM_DI;i++) pinMode(DI_PINS[i], INPUT);
  for (uint8_t i=0;i<NUM_LED;i++) { pinMode(LED_PINS[i], OUTPUT); digitalWrite(LED_PINS[i], LOW); }
  for (uint8_t i=0;i<NUM_BTN;i++) pinMode(BTN_PINS[i], INPUT); // inverted, HIGH=pressed

  for (uint8_t i=0;i<NUM_CH;i++) { pinMode(GATE_PINS[i], OUTPUT); digitalWrite(GATE_PINS[i], LOW); }
  pinMode(ZC_PINS[0], INPUT_PULLUP);
  pinMode(ZC_PINS[1], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ZC_PINS[0]), zc_isr_ch0, FALLING);
  attachInterrupt(digitalPinToInterrupt(ZC_PINS[1]), zc_isr_ch1, FALLING);

  setDefaults();
  initFreqEstimator();
  initFilesystemAndConfig();

  uint32_t now = millis();
  for (int i=0;i<NUM_CH;i++) { zcLastEdgeMs[i] = now; zcOk[i] = zcPrevOk[i] = false; zcOkStreak[i]=zcFaultStreak[i]=0; }

  // Serial2 / Modbus
  Serial2.setTX(TX2);
  Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("DIM-420-R1 RP2350A");

  // Modbus maps
  for (uint16_t i=0;i<NUM_DI;i++)  mb.addIsts(ISTS_DI_BASE + i);
  for (uint16_t i=0;i<NUM_CH;i++)  mb.addIsts(ISTS_CH_BASE + i);
  for (uint16_t i=0;i<NUM_LED;i++) mb.addIsts(ISTS_LED_BASE + i);
  for (uint16_t i=0;i<NUM_CH;i++)  mb.addIsts(ISTS_ZC_OK_BASE + i);

  for (uint16_t i=0;i<NUM_CH;i++){
    mb.addHreg(HREG_DIM_LEVEL_BASE + i, chLevel[i]);
    mb.addHreg(HREG_DIM_LO_BASE    + i, chLower[i]);
    mb.addHreg(HREG_DIM_HI_BASE    + i, chUpper[i]);
    mb.addHreg(HREG_FREQ_X100_BASE + i, freq_x100[i]);
    mb.addHreg(HREG_PCT_X10_BASE   + i, chPctX10[i]);
    mb.addHreg(HREG_LOADTYPE_BASE  + i, chLoadType[i]);
    mb.addHreg(HREG_CUTMODE_BASE   + i, chCutMode[i]);
    mb.addHreg(HREG_PRESET_BASE    + i, chPreset[i]);
  }

  for (uint16_t i=0;i<NUM_CH;i++){ mb.addCoil(CMD_CH_ON_BASE + i);  mb.setCoil(CMD_CH_ON_BASE + i,  false); }
  for (uint16_t i=0;i<NUM_CH;i++){ mb.addCoil(CMD_CH_OFF_BASE + i); mb.setCoil(CMD_CH_OFF_BASE + i, false); }
  for (uint16_t i=0;i<NUM_DI;i++){ mb.addCoil(CMD_DI_EN_BASE + i);  mb.setCoil(CMD_DI_EN_BASE + i,  false); }
  for (uint16_t i=0;i<NUM_DI;i++){ mb.addCoil(CMD_DI_DIS_BASE + i); mb.setCoil(CMD_DI_DIS_BASE + i, false); }

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  // WebSerial handlers (quiet)
  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);

  // (No boot messages, no echo/handshake)
}

// ================== Command handler / reset ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) return;
  String act = String(actC); act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    saveConfigFS();
    delay(200);
    performReset();

  } else if (act == "save") {
    saveConfigFS();

  } else if (act == "load") {
    if (loadConfigFS()) {
      applyModbusSettings(g_mb_address, g_mb_baud);
    }

  } else if (act == "factory") {
    setDefaults();
    if (saveConfigFS()) {
      applyModbusSettings(g_mb_address, g_mb_baud);
    }
  }
  // Unknowns are ignored silently (quiet mode)
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

// ================== WebSerial config handlers (quiet) ==================
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);
  applyModbusSettings((uint8_t)addr, (uint32_t)baud);
  cfgDirty = true; lastCfgTouchMs = millis();
}

// Supported t: inputEnable/inputInvert/inputSwitchType/press maps/latch modes + targets
// channels, buttons, leds
void handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"];
  JSONVar list = obj["list"];
  if (!t) return;
  String type = String(t);
  bool changed = false;

  if (type == "inputEnable") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].enabled = (bool)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputInvert") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].inverted = (bool)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputSwitchType") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].switchType = (uint8_t)constrain((int)list[i], 0, 1);
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressActionShort") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressAction[PRESS_SHORT] = (uint8_t)constrain((int)list[i], 0, 6);
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressTargetShort") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressTarget[PRESS_SHORT] = (uint8_t)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressActionLong") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressAction[PRESS_LONG] = (uint8_t)constrain((int)list[i], 0, 6);
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressTargetLong") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressTarget[PRESS_LONG] = (uint8_t)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressActionDoubleShort") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressAction[PRESS_DOUBLE_SHORT] = (uint8_t)constrain((int)list[i], 0, 6);
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressTargetDoubleShort") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressTarget[PRESS_DOUBLE_SHORT] = (uint8_t)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressActionShortThenLong") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressAction[PRESS_SHORT_THEN_LONG] = (uint8_t)constrain((int)list[i], 0, 6);
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputPressTargetShortThenLong") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].pressTarget[PRESS_SHORT_THEN_LONG] = (uint8_t)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputLatchMode") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].latchMode = (uint8_t)constrain((int)list[i], 0, 1);
    changed = true; diCfgEchoPending = true;

  } else if (type == "inputLatchTarget") {
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].latchTarget = (uint8_t)list[i];
    changed = true; diCfgEchoPending = true;

  } else if (type == "channels") {
    for (int i=0; i<NUM_CH && i<list.length(); i++) {
      chCfg[i].enabled = (bool)list[i]["enabled"];

      int lo = (int)list[i]["lower"];
      int hi = (int)list[i]["upper"];
      setThresholds(i, lo, hi);

      if (list[i].hasOwnProperty("loadType")) {
        int lt = (int)list[i]["loadType"];
        chLoadType[i] = (uint8_t)constrain(lt, 0, 2);
        mb.setHreg(HREG_LOADTYPE_BASE + i, chLoadType[i]);
      }

      if (list[i].hasOwnProperty("cutMode")) {
        int cm = (int)list[i]["cutMode"];
        chCutMode[i] = (uint8_t)constrain(cm, 0, 1);
        mb.setHreg(HREG_CUTMODE_BASE + i, chCutMode[i]);
      }

      if (list[i].hasOwnProperty("preset")) {
        int pv = (int)list[i]["preset"];
        pv = constrain(pv, 0, 255);
        chPreset[i] = (uint8_t)pv;
        clampAndApplyPreset(i);
        mb.setHreg(HREG_PRESET_BASE + i, chPreset[i]);
      }

      if (list[i].hasOwnProperty("percent")) {
        double pct = (double)list[i]["percent"];
        if ((LoadType)chLoadType[i] != LOAD_KEY) pct = constrain(pct, 0.0, 100.0);
        chPctX10[i] = (uint16_t)constrain((int)lround(pct*10.0), 0, 1000);
        mb.setHreg(HREG_PCT_X10_BASE + i, chPctX10[i]);

        uint8_t lvl = mapPercentToLevel(i, pct);
        setLevelDirect(i, lvl);
      } else if (list[i].hasOwnProperty("level")) {
        int lvl = (int)list[i]["level"];
        clampAndSetLevel(i, lvl);
      }
    }
    changed = true;

  } else if (type == "buttons") {
    for (int i=0;i<NUM_BTN && i<list.length();i++) {
      int a = (int)list[i]["action"];
      btnCfg[i].action = (uint8_t)constrain(a, 0, 6);
    }
    changed = true;

  } else if (type == "leds") {
    for (int i=0;i<NUM_LED && i<list.length();i++) {
      ledCfg[i].mode = (uint8_t)constrain((int)list[i]["mode"], 0, 1);
      int src = (int)list[i]["source"];
      ledCfg[i].source = (uint8_t)((src==0 || src==1 || src==2) ? src : 0);
    }
    changed = true;
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); }
}

// ================== Helpers ==================
static inline void setThresholds(uint8_t ch, int lower, int upper) {
  if (ch >= NUM_CH) return;
  lower = constrain(lower, 0, 255);
  upper = constrain(upper, 0, 255);
  if (upper < lower) upper = lower;
  chLower[ch] = (uint8_t)lower;
  chUpper[ch] = (uint8_t)upper;

  chLastNonZero[ch] = constrain(chLastNonZero[ch], chLower[ch], chUpper[ch]);
  clampAndApplyPreset(ch);

  if (chLevel[ch] > 0) {
    if (chLevel[ch] < chLower[ch]) chLevel[ch] = chLower[ch];
    else if (chLevel[ch] > chUpper[ch]) chLevel[ch] = chUpper[ch];
  }

  mb.setHreg(HREG_DIM_LO_BASE + ch, chLower[ch]);
  mb.setHreg(HREG_DIM_HI_BASE + ch, chUpper[ch]);
  mb.setHreg(HREG_DIM_LEVEL_BASE + ch, chLevel[ch]);
}

void clampAndSetLevel(uint8_t ch, int value) {
  if (ch >= NUM_CH) return;
  value = constrain(value, 0, 255);

  uint8_t cur = chLevel[ch];
  uint8_t out;

  if (value == 0) out = 0;
  else if (value > chUpper[ch]) out = chUpper[ch];
  else if (value >= chLower[ch]) out = (uint8_t)value;
  else out = (cur == 0) ? chLower[ch] : 0;

  setLevelDirect(ch, out);
}

// Legacy helper used by on-board buttons
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now) {
  auto doCh = [&](int idx) {
    if (idx < 0 || idx >= NUM_CH) return;
    if (action == 1) {
      if (chLevel[idx] == 0) clampAndSetLevel(idx, chPreset[idx]);
      else clampAndSetLevel(idx, 0);
      chPulseUntil[idx] = 0;
    } else if (action == 2) {
      chPulseUntil[idx] = now + PULSE_MS;
      clampAndSetLevel(idx, 255);
    }
  };
  if (action == 0) return;
  if (target == 4) return;

  if (target == 0) { for (int i=0;i<NUM_CH;i++) doCh(i); }
  else if (target>=1 && target<=2) { doCh(target-1); }

  cfgDirty = true; lastCfgTouchMs = now;
}

// ================== Apply DI action ==================
void applyDiAction(uint8_t target, uint8_t action, uint32_t now){
  if (action == DI_ACT_NONE || target == DI_TGT_NONE) return;

  auto doCh = [&](int idx){
    if (idx<0 || idx>=NUM_CH) return;
    switch(action){
      case DI_ACT_TOGGLE_TO_PRESET:
        if (chLevel[idx]==0) clampAndSetLevel(idx, chPreset[idx]); else clampAndSetLevel(idx,0);
        break;
      case DI_ACT_TURN_OFF:
        clampAndSetLevel(idx,0); break;
      case DI_ACT_TOGGLE_OUTPUT:
        clampAndSetLevel(idx, (chLevel[idx]==0) ? chLastNonZero[idx] : 0); break;
      case DI_ACT_INC:
        clampAndSetLevel(idx, chLevel[idx] + STEP_DELTA); break;
      case DI_ACT_DEC:
        clampAndSetLevel(idx, chLevel[idx] - STEP_DELTA); break;
      case DI_ACT_INC_THEN_DEC: {
        static const uint8_t burst = 8;
        for (uint8_t k=0;k<burst;k++){
          if (chLevel[idx] < chUpper[idx]) clampAndSetLevel(idx, chLevel[idx] + STEP_DELTA);
        }
        if (chLevel[idx] >= chUpper[idx]){
          for (uint8_t k=0;k<burst;k++){
            if (chLevel[idx] > 0) clampAndSetLevel(idx, chLevel[idx] - STEP_DELTA);
          }
        }
        break;
      }
    }
  };

  if (target == DI_TGT_ALL){ doCh(0); doCh(1); }
  else if (target == DI_TGT_CH1){ doCh(0); }
  else if (target == DI_TGT_CH2){ doCh(1); }

  cfgDirty = true; lastCfgTouchMs = now;
}

JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < NUM_LED; i++) {
    JSONVar o;
    o["mode"]   = ledCfg[i].mode;
    o["source"] = ledCfg[i].source;
    arr[i] = o;
  }
  return arr;
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  mb.task();
  processModbusCommandPulses();

  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  // ===== Debounced ZC presence/fault status =====
  for (int c=0;c<NUM_CH;c++) {
    bool okNow = ((uint32_t)(now - zcLastEdgeMs[c]) <= ZC_FAULT_TIMEOUT_MS);

    if (okNow) {
      if (zcOkStreak[c] < 255) zcOkStreak[c]++;
      zcFaultStreak[c] = 0;
      if (!zcOk[c] && zcOkStreak[c] >= ZC_OK_STREAK_N) {
        zcOk[c] = true;
        mb.setIsts(ISTS_ZC_OK_BASE + c, true);
      }
    } else {
      if (zcFaultStreak[c] < 255) zcFaultStreak[c]++;
      zcOkStreak[c] = 0;
      if (zcOk[c] && zcFaultStreak[c] >= ZC_FAULT_STREAK_N) {
        zcOk[c] = false;
        mb.setIsts(ISTS_ZC_OK_BASE + c, false);
      }
    }
  }

  // ===== Frequency estimator =====
  for (uint8_t ch=0; ch<NUM_CH; ++ch) {
    uint32_t seq = zcSampleSeq[ch];
    if (seq != lastSeqConsumed[ch]) {
      noInterrupts();
      uint32_t half = zcHalfUsLatest[ch];
      interrupts();

      med3[ch][medIdx[ch] % MED_W] = half;
      medIdx[ch]++;
      uint32_t a=med3[ch][0], b=med3[ch][1], c=med3[ch][2];
      uint32_t med = (a>b) ? ((b>c)? b : (a>c? c:a)) : ((a>c)? a : (b>c? c:b));

      avgSum[ch] -= avgBuf[ch][avgIdx[ch]];
      avgBuf[ch][avgIdx[ch]] = med;
      avgSum[ch] += med;
      avgIdx[ch] = (uint8_t)((avgIdx[ch] + 1) % AVG_N);

      double mean_half = corr_half_us((double)avgSum[ch] / (double)AVG_N);
      double fx100 = 50000000.0 / mean_half; // 100*1e6 / (2*half_us)
      if (fx100 < 0.0) fx100 = 0.0;
      if (fx100 > 65535.0) fx100 = 65535.0;
      freq_x100[ch] = (uint16_t)lround(fx100);
      mb.setHreg(HREG_FREQ_X100_BASE + ch, freq_x100[ch]);

      uint32_t stable_half = (uint32_t)lround(mean_half);
      if (stable_half < HALF_MIN_US || stable_half > HALF_MAX_US) stable_half = HALF_US_DEFAULT;
      gateHalfUs[ch] = stable_half;

      lastSeqConsumed[ch] = seq;
    }
  }

  // Auto-save after quiet period
  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    saveConfigFS();
    cfgDirty = false;
  }

  // Buttons (inverted: HIGH = pressed)
  for (int i = 0; i < NUM_BTN; i++) {
    bool pressed = (digitalRead(BTN_PINS[i]) == HIGH);
    buttonPrev[i] = buttonState[i];
    buttonState[i] = pressed;
    if (!buttonPrev[i] && buttonState[i]) {
      switch (btnCfg[i].action) {
        case 1: applyActionToTarget(1, 1, now); break;
        case 2: applyActionToTarget(2, 1, now); break;
        case 3: clampAndSetLevel(0, chLevel[0] + 10); cfgDirty=true; lastCfgTouchMs=now; break;
        case 4: clampAndSetLevel(0, chLevel[0] - 10); cfgDirty=true; lastCfgTouchMs=now; break;
        case 5: clampAndSetLevel(1, chLevel[1] + 10); cfgDirty=true; lastCfgTouchMs=now; break;
        case 6: clampAndSetLevel(1, chLevel[1] - 10); cfgDirty=true; lastCfgTouchMs=now; break;
        default: break;
      }
    }
  }

  // ================== Digital Inputs (Momentary/Latching) ==================
  for (int i = 0; i < NUM_DI; i++) {
    bool val = false;
    if (diCfg[i].enabled) {
      val = (digitalRead(DI_PINS[i]) == HIGH);
      if (diCfg[i].inverted) val = !val;
    }
    DiRuntime &rt = diRt[i];
    rt.prev = rt.cur;
    rt.cur  = val;
    mb.setIsts(ISTS_DI_BASE + i, val);

    uint32_t nowMs = millis();

    // Edge detection
    bool rising  = (!rt.prev && rt.cur);
    bool falling = (rt.prev && !rt.cur);

    if (rising) {
      rt.pressStart = nowMs;

      if (diCfg[i].switchType == DI_SW_LATCHING) {
        if (diCfg[i].latchMode == LATCH_TOGGLE_TO_PRESET_OR_0) {
          applyDiAction(diCfg[i].latchTarget, DI_ACT_TOGGLE_TO_PRESET, nowMs);
        } else {
          rt.rampActive = true;
          rt.rampDir = +1;
          rt.lastRampTick = nowMs;
        }
      }

      rt.lastChange = nowMs;

    } else if (falling) {
      uint32_t dur = nowMs - rt.pressStart;

      if (diCfg[i].switchType == DI_SW_MOMENTARY) {
        if (dur >= LONG_MIN_MS) {
          if (rt.shortThenLongArmed) {
            applyDiAction(diCfg[i].pressTarget[PRESS_SHORT_THEN_LONG], diCfg[i].pressAction[PRESS_SHORT_THEN_LONG], nowMs);
            rt.shortThenLongArmed = false;
            rt.waitingSecond = false;
          } else {
            applyDiAction(diCfg[i].pressTarget[PRESS_LONG], diCfg[i].pressAction[PRESS_LONG], nowMs);
          }
        } else if (dur <= SHORT_MAX_MS) {
          if (!rt.waitingSecond) {
            rt.waitingSecond = true;
            rt.firstShortAt = nowMs;
            rt.shortThenLongArmed = true;
          } else {
            applyDiAction(diCfg[i].pressTarget[PRESS_DOUBLE_SHORT], diCfg[i].pressAction[PRESS_DOUBLE_SHORT], nowMs);
            rt.waitingSecond = false;
            rt.shortThenLongArmed = false;
          }
        }
      } else {
        if (diCfg[i].latchMode == LATCH_PINGPONG_UNTIL_TOGGLE) {
          rt.rampActive = false;
        }
      }

      rt.lastChange = nowMs;
    }

    // If waiting for a second short and time expired -> treat as SINGLE SHORT
    if (diCfg[i].switchType == DI_SW_MOMENTARY && rt.waitingSecond) {
      if ((uint32_t)(nowMs - rt.firstShortAt) > DOUBLE_GAP_MS) {
        applyDiAction(diCfg[i].pressTarget[PRESS_SHORT], diCfg[i].pressAction[PRESS_SHORT], nowMs);
        rt.waitingSecond = false;
        rt.shortThenLongArmed = false;
      }
    }

    // Latching ping-pong ramping
    if (diCfg[i].switchType == DI_SW_LATCHING && diCfg[i].latchMode == LATCH_PINGPONG_UNTIL_TOGGLE && rt.rampActive) {
      if ((uint32_t)(nowMs - rt.lastRampTick) >= RAMP_TICK_MS) {
        rt.lastRampTick = nowMs;

        auto rampOne = [&](int ch){
          if (ch<0 || ch>=NUM_CH) return;
          int next = (int)chLevel[ch] + (rt.rampDir>0 ? STEP_DELTA : -STEP_DELTA);
          if (next >= (int)chUpper[ch]) { next = chUpper[ch]; rt.rampDir = -1; }
          if (next <= 0) { next = 0; rt.rampDir = +1; }
          clampAndSetLevel(ch, next);
        };

        if      (diCfg[i].latchTarget == DI_TGT_CH1) rampOne(0);
        else if (diCfg[i].latchTarget == DI_TGT_CH2) rampOne(1);
        else if (diCfg[i].latchTarget == DI_TGT_ALL){ rampOne(0); rampOne(1); }
      }
    }
  }

  // Pulse timeout restore (legacy)
  for (int c=0;c<NUM_CH;c++) {
    if (chPulseUntil[c] != 0 && timeAfter32(now, chPulseUntil[c])) {
      clampAndSetLevel(c, chLastNonZero[c]);
      chPulseUntil[c] = 0;
    }
  }

  // LEDs drive + Modbus mirror
  for (int i=0;i<NUM_LED;i++) {
    bool srcActive = false;
    uint8_t src = ledCfg[i].source;
    if (src >= 1 && src <= 2) {
      int c = src - 1;
      bool chOn = (chCfg[c].enabled && chLevel[c] > 0);
      srcActive = chOn;
    }
    bool phys = (ledCfg[i].mode == 0) ? srcActive : (srcActive && blinkPhase);
    digitalWrite(LED_PINS[i], phys ? HIGH : LOW);
    mb.setIsts(ISTS_LED_BASE + i, phys);
  }

  // Channel "on" mirror
  for (int c=0;c<NUM_CH;c++) {
    bool onb = (chCfg[c].enabled && chLevel[c] > 0);
    mb.setIsts(ISTS_CH_BASE + c, onb);
  }

  // ===== Quiet, periodic config snapshot =====
  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();
    WebSerial.check();
    sendConfigSnapshot();
  }
}

// ================== Modbus side helpers ==================
void processModbusCommandPulses() {
  // Channel ON/OFF coils
  for (int c=0;c<NUM_CH;c++) {
    if (mb.Coil(CMD_CH_ON_BASE + c)) {
      mb.setCoil(CMD_CH_ON_BASE + c, false);
      clampAndSetLevel(c, chPreset[c]); // use preset when ON
      cfgDirty = true; lastCfgTouchMs = millis();
    }
    if (mb.Coil(CMD_CH_OFF_BASE + c)) {
      mb.setCoil(CMD_CH_OFF_BASE + c, false);
      clampAndSetLevel(c, 0);
      cfgDirty = true; lastCfgTouchMs = millis();
    }
  }
  // DI enable/disable coils
  for (int i=0;i<NUM_DI;i++) {
    if (mb.Coil(CMD_DI_EN_BASE + i))  { mb.setCoil(CMD_DI_EN_BASE + i,  false); if (!diCfg[i].enabled){ diCfg[i].enabled=true;  cfgDirty=true; lastCfgTouchMs=millis(); diCfgEchoPending=true; } }
    if (mb.Coil(CMD_DI_DIS_BASE + i)) { mb.setCoil(CMD_DI_DIS_BASE + i, false); if ( diCfg[i].enabled){ diCfg[i].enabled=false; cfgDirty=true; lastCfgTouchMs=millis(); diCfgEchoPending=true; } }
  }

  // HREG writes (levels + thresholds + percent/loadtype/cutmode/preset)
  for (int c=0;c<NUM_CH;c++) {
    // Thresholds
    uint16_t lo = mb.Hreg(HREG_DIM_LO_BASE + c);
    uint16_t hi = mb.Hreg(HREG_DIM_HI_BASE + c);
    setThresholds(c, (int)lo, (int)hi);

    // Load type
    uint16_t lt = mb.Hreg(HREG_LOADTYPE_BASE + c);
    lt = constrain((int)lt, 0, 2);
    if (lt != chLoadType[c]) { chLoadType[c] = (uint8_t)lt; cfgDirty = true; lastCfgTouchMs = millis(); }

    // Cutoff mode
    uint16_t cm = mb.Hreg(HREG_CUTMODE_BASE + c);
    cm = constrain((int)cm, 0, 1);
    if (cm != chCutMode[c]) { chCutMode[c] = (uint8_t)cm; cfgDirty = true; lastCfgTouchMs = millis(); }

    // Preset
    uint16_t pv = mb.Hreg(HREG_PRESET_BASE + c);
    pv = constrain((int)pv, 0, 255);
    if (pv != chPreset[c]) {
      chPreset[c] = (uint8_t)pv;
      clampAndApplyPreset(c);
      cfgDirty = true; lastCfgTouchMs = millis();
    }

    // Percent setpoint (preferred)
    uint16_t p10 = mb.Hreg(HREG_PCT_X10_BASE + c);
    if (p10 > 1000 && chLoadType[c] != LOAD_KEY) p10 = 1000; // KEY: >0 => max
    if (p10 != chPctX10[c]) {
      chPctX10[c] = p10;
      double pct = chPctX10[c] / 10.0;
      uint8_t lvl = mapPercentToLevel(c, pct);
      setLevelDirect(c, lvl);
      cfgDirty = true; lastCfgTouchMs = millis();
    }

    // Legacy direct level write still supported
    uint16_t lvl = mb.Hreg(HREG_DIM_LEVEL_BASE + c);
    clampAndSetLevel(c, (int)lvl);
  }
}
