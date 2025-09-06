/********** Arduino preprocessor fix: forward declare before includes **********/
struct PersistConfig;

/**************************************************************
 * DIM-420-R1 — RP2350A (Pico 2) firmware (DROP-IN)
 * - 4× DI:     GPIO8, GPIO9, GPIO15, GPIO16
 * - 2× DIM:    CH1 ZC=GPIO0, Gate=GPIO1; CH2 ZC=GPIO2, Gate=GPIO3
 * - 4× BTN:    GPIO22..25 (board buttons; schematic is INVERTED)
 * - 4× LEDs:   GPIO18..21 (active-HIGH)
 * - Modbus/RS485 on Serial2: TX=GPIO4, RX=GPIO5
 *
 * Additions:
 * - 50/60 Hz auto: robust per-channel mains frequency estimation (centi-Hz)
 * - Per-channel Load Type: 0=Lamp (log map), 1=Heater (linear), 2=Key
 * - Per-channel Cutoff Mode for MOSFET dimmer:
 *        0 = Leading edge (RL type load), 1 = Trailing edge (RC type load)
 * - Percent setpoint input (0–100%), mapped to level 0–255 with thresholds
 * - Modbus HREG 440..441 => setpoint percent ×10 (0..1000)
 * - Modbus HREG 460..461 => load type (0,1,2)
 * - Modbus HREG 470..471 => cutoff mode (0=Leading, 1=Trailing)
 * - NEW: Per-channel Preset Level (used on toggle from DI/BTN)
 *        Modbus HREG 480..481 => preset level (0..255)
 * - UI still shows actual 0–255 & thresholds; UI sends percent / preset (+ optional cutMode)
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
// Buttons (schematic inverted: active-HIGH, use INPUT_PULLDOWN or pad with logic)
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
constexpr uint32_t GATE_PULSE_US    = 120u;   // kept for compatibility (not used for MOSFET)

// ---- State (volatile: accessed in ISR) ----
volatile uint8_t  chLevel[NUM_CH] = {0,0};           // 0..255 current
volatile uint8_t  chLastNonZero[NUM_CH] = {200,200}; // remembered last non-zero

// ================== Config & runtime ==================
struct InCfg { bool enabled; bool inverted; uint8_t action; uint8_t target; };
struct ChCfg { bool enabled; };
struct LedCfg { uint8_t mode; uint8_t source; };
struct BtnCfg { uint8_t action; };

InCfg  diCfg[NUM_DI];
ChCfg  chCfg[NUM_CH];
LedCfg ledCfg[NUM_LED];
BtnCfg btnCfg[NUM_BTN];

bool buttonState[NUM_BTN] = {false,false,false,false};
bool buttonPrev[NUM_BTN]  = {false,false,false,false};
bool diState[NUM_DI]      = {false,false,false,false};
bool diPrev[NUM_DI]       = {false,false,false,false};

// Pulse handling (DI action=Pulse => full for a short time)
uint32_t chPulseUntil[NUM_CH] = {0,0};
const uint32_t PULSE_MS = 500;

// ================== ZC monitor (per-channel) ==================
volatile uint32_t zcLastEdgeMs[NUM_CH] = {0,0}; // last ISR edge time (ms)
bool zcOk[NUM_CH]      = {false,false};
bool zcPrevOk[NUM_CH]  = {false,false};
const uint32_t ZC_FAULT_TIMEOUT_MS = 1000;

// ================== Frequency measurement (robust) ==================
volatile uint64_t zcPrevEdgeUs64[NUM_CH] = {0,0};
volatile uint32_t zcHalfUsLatest[NUM_CH] = {HALF_US_DEFAULT,HALF_US_DEFAULT};
volatile uint32_t zcSampleSeq[NUM_CH]    = {0,0};

// 50/60 Hz guard window
constexpr uint32_t HALF_MIN_US = 7500;   // ~66.6 Hz upper bound
constexpr uint32_t HALF_MAX_US = 12000;  // ~41.6 Hz lower bound

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
volatile uint32_t gateHalfUs[NUM_CH] = {HALF_US_DEFAULT,HALF_US_DEFAULT}; // used by ISR

// ================== Dimming thresholds (per-channel) ==================
uint8_t chLower[NUM_CH] = {20,20};
uint8_t chUpper[NUM_CH] = {255,255};

// ================== Load type + percent setpoint ==================
enum LoadType : uint8_t { LOAD_LAMP=0, LOAD_HEATER=1, LOAD_KEY=2 };
uint8_t  chLoadType[NUM_CH] = {LOAD_LAMP, LOAD_LAMP};
uint16_t chPctX10[NUM_CH]   = {0,0};   // percent ×10 (0..1000)

// ================== NEW: MOSFET cutoff (waveform) mode ==================
enum CutMode : uint8_t { CUT_LEADING=0, CUT_TRAILING=1 };
uint8_t chCutMode[NUM_CH] = {CUT_LEADING, CUT_LEADING};

// ================== NEW: Per-channel preset level (used by toggle) ==================
uint8_t chPreset[NUM_CH] = {200,200};

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
  uint8_t chPreset[NUM_CH];         // NEW
  uint8_t mb_address;
  uint32_t mb_baud;
  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x314D4449UL; // 'IDM1'
// VERSION BUMP for preset support
static const uint16_t CFG_VERSION = 0x0005;
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

  // Time budget inside half-wave where we can conduct
  const uint32_t usable = (half_us > (ZC_BLANK_US + MOS_OFF_GUARD_US + 20))
                        ? (half_us - ZC_BLANK_US - MOS_OFF_GUARD_US)
                        : (half_us/2);

  auto on_cb  = (ch==0) ? mos_on_cb_ch0  : mos_on_cb_ch1;
  auto off_cb = (ch==0) ? mos_off_cb_ch0 : mos_off_cb_ch1;

  if (chCutMode[ch] == CUT_TRAILING) {
    // Trailing-edge (RC): ON shortly after ZC, OFF earlier than next ZC
    uint32_t on_time = (uint32_t)((uint64_t)lvl * usable / 255u);
    if (on_time == 0) return; // no conduction
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
    if (delay_to_on >= t_off_from_zc) return; // nothing to conduct

    add_alarm_in_us((int64_t)delay_to_on,   on_cb,  nullptr, true);
    add_alarm_in_us((int64_t)t_off_from_zc, off_cb, nullptr, true);
  }
}
void zc_isr_ch0(){ zc_isr_common(0); }
void zc_isr_ch1(){ zc_isr_common(1); }

// ================== Mapping helpers (percent -> level) ==================
constexpr double LAMP_LOG_MAX = 6.0;  // log "top". log=1 -> lower, log=LMAX -> upper
inline uint8_t clamp8(int v){ return (uint8_t)constrain(v, 0, 255); }

// Lamp mapping (unchanged)
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

// KEY mode per user spec: 0% => 0 (off); >0% => go to maximum threshold
uint8_t mapKeyPercentToLevel(uint8_t ch, double pct) {
  if (pct <= 0.0) return 0;
  return chUpper[ch];
}

uint8_t mapPercentToLevel(uint8_t ch, double pct) {
  switch ((LoadType)chLoadType[ch]) {
    case LOAD_LAMP:   return mapLampPercentToLevel(ch, pct);
    case LOAD_HEATER: return mapHeaterPercentToLevel(ch, pct);
    case LOAD_KEY:    return mapKeyPercentToLevel(ch, pct);
    default:          return mapLampPercentToLevel(ch, pct);
  }
}

void setLevelDirect(uint8_t ch, uint8_t lvl) {
  chLevel[ch] = lvl;
  if (lvl > 0) chLastNonZero[ch] = lvl;
  mb.setHreg(400 + ch, chLevel[ch]); // reflect in HREG_DIM_LEVEL_BASE
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
    freq_x100[i] = 5000; // ~50.00 Hz initial
    gateHalfUs[i] = HALF_US_DEFAULT;
  }
}

void setDefaults() {
  for (int i = 0; i < NUM_DI; i++)  diCfg[i] = { true, false, 0, 0 };
  for (int i = 0; i < NUM_CH; i++)  chCfg[i] = { true };
  for (int i = 0; i < NUM_LED; i++) ledCfg[i] = { 0, 0 };
  for (int i = 0; i < NUM_BTN; i++) btnCfg[i] = { 0 };

  for (int i=0;i<NUM_CH;i++) {
    chLevel[i] = 0;
    chLastNonZero[i] = 200;
    chPulseUntil[i] = 0;
    zcLastEdgeMs[i] = 0;
    zcOk[i] = zcPrevOk[i] = false;
    chLower[i] = 20;
    chUpper[i] = 255;
    chLoadType[i] = LOAD_LAMP;
    chPctX10[i]   = 0;
    chCutMode[i]  = CUT_LEADING; // default
    chPreset[i]   = 200;         // default preset
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
  if (!f) { WebSerial.send("message", "save: open failed"); return false; }
  size_t n = f.write((const uint8_t*)&pc, sizeof(pc));
  f.flush(); f.close();
  if (n != sizeof(pc)) { WebSerial.send("message", String("save: short write ")+n); return false; }

  File r = LittleFS.open(CFG_PATH, "r");
  if (!r) { WebSerial.send("message", "save: reopen failed"); return false; }
  if ((size_t)r.size() != sizeof(PersistConfig)) { WebSerial.send("message", "save: size mismatch"); r.close(); return false; }
  PersistConfig back{}; size_t nr = r.read((uint8_t*)&back, sizeof(back)); r.close();
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
  HREG_PRESET_BASE      = 480   // 480..481: preset level (0..255)  NEW
};

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
  for (uint8_t i=0;i<NUM_DI;i++) pinMode(DI_PINS[i], INPUT);
  for (uint8_t i=0;i<NUM_LED;i++) { pinMode(LED_PINS[i], OUTPUT); digitalWrite(LED_PINS[i], LOW); }
  // Schematic says user buttons are inverted (active-HIGH). Use INPUT and read HIGH as "pressed".
  for (uint8_t i=0;i<NUM_BTN;i++) pinMode(BTN_PINS[i], INPUT);

  // Dimmer pins
  for (uint8_t i=0;i<NUM_CH;i++) { pinMode(GATE_PINS[i], OUTPUT); digitalWrite(GATE_PINS[i], LOW); }
  pinMode(ZC_PINS[0], INPUT_PULLUP);
  pinMode(ZC_PINS[1], INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ZC_PINS[0]), zc_isr_ch0, FALLING);
  attachInterrupt(digitalPinToInterrupt(ZC_PINS[1]), zc_isr_ch1, FALLING);

  setDefaults();
  initFreqEstimator();
  initFilesystemAndConfig();

  uint32_t now = millis();
  for (int i=0;i<NUM_CH;i++) { zcLastEdgeMs[i] = now; zcOk[i] = zcPrevOk[i] = false; }

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
    mb.addHreg(HREG_PRESET_BASE    + i, chPreset[i]); // NEW
  }

  for (uint16_t i=0;i<NUM_CH;i++){ mb.addCoil(CMD_CH_ON_BASE + i);  mb.setCoil(CMD_CH_ON_BASE + i,  false); }
  for (uint16_t i=0;i<NUM_CH;i++){ mb.addCoil(CMD_CH_OFF_BASE + i); mb.setCoil(CMD_CH_OFF_BASE + i, false); }
  for (uint16_t i=0;i<NUM_DI;i++){ mb.addCoil(CMD_DI_EN_BASE + i);  mb.setCoil(CMD_DI_EN_BASE + i,  false); }
  for (uint16_t i=0;i<NUM_DI;i++){ mb.addCoil(CMD_DI_DIS_BASE + i); mb.setCoil(CMD_DI_DIS_BASE + i, false); }

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  modbusStatus["state"]   = 0;

  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);
  WebSerial.send("message", "Boot OK (50/60 Hz auto, LoadType+Percent, CutMode, Preset; BTN inverted)");
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
// For "channels" entries, you may provide:
//  - enabled (bool)
//  - lower (0..255), upper (0..255)
//  - percent (double 0..100)  <<< preferred
//  - loadType (0,1,2)
//  - cutMode (0=Leading,1=Trailing)
//  - preset (0..255)          <<< NEW
//  - level (0..255)           (legacy fallback)
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
      int tgt = (int)list[i];
      diCfg[i].target = (uint8_t)((tgt==4 || tgt==0 || (tgt>=1 && tgt<=2)) ? tgt : 0);
    }
    WebSerial.send("message", "Input Control Target list updated"); changed = true;
  } else if (type == "channels") {
    for (int i=0; i<NUM_CH && i<list.length(); i++) {
      chCfg[i].enabled = (bool)list[i]["enabled"];

      // thresholds
      int lo = (int)list[i]["lower"];
      int hi = (int)list[i]["upper"];
      setThresholds(i, lo, hi);

      // load type
      if (list[i].hasOwnProperty("loadType")) {
        int lt = (int)list[i]["loadType"];
        chLoadType[i] = (uint8_t)constrain(lt, 0, 2);
        mb.setHreg(HREG_LOADTYPE_BASE + i, chLoadType[i]);
      }

      // cutoff mode
      if (list[i].hasOwnProperty("cutMode")) {
        int cm = (int)list[i]["cutMode"];
        chCutMode[i] = (uint8_t)constrain(cm, 0, 1);
        mb.setHreg(HREG_CUTMODE_BASE + i, chCutMode[i]);
      }

      // preset (NEW)
      if (list[i].hasOwnProperty("preset")) {
        int pv = (int)list[i]["preset"];
        pv = constrain(pv, 0, 255);
        chPreset[i] = (uint8_t)pv;
        clampAndApplyPreset(i);
        mb.setHreg(HREG_PRESET_BASE + i, chPreset[i]);
      }

      // preferred: percent setpoint
      if (list[i].hasOwnProperty("percent")) {
        double pct = (double)list[i]["percent"];
        if ((LoadType)chLoadType[i] != LOAD_KEY) pct = constrain(pct, 0.0, 100.0); // KEY allows >0 => max
        chPctX10[i] = (uint16_t)constrain((int)lround(pct*10.0), 0, 1000);
        mb.setHreg(HREG_PCT_X10_BASE + i, chPctX10[i]);

        uint8_t lvl = mapPercentToLevel(i, pct);
        setLevelDirect(i, lvl);
      }
      // legacy fallback: direct level
      else if (list[i].hasOwnProperty("level")) {
        int lvl = (int)list[i]["level"];
        clampAndSetLevel(i, lvl);
      }
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
      ledCfg[i].mode = (uint8_t)constrain((int)list[i]["mode"], 0, 1);
      int src = (int)list[i]["source"];
      ledCfg[i].source = (uint8_t)((src==0 || src==1 || src==2) ? src : 0);
    }
    WebSerial.send("message", "LEDs Configuration updated"); changed = true;
  } else {
    WebSerial.send("message", "Unknown Config type");
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

// Direction-aware clamp (legacy paths: buttons/DI/Modbus level writes)
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

void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now) {
  auto doCh = [&](int idx) {
    if (idx < 0 || idx >= NUM_CH) return;
    if (action == 1) {
      // Toggle -> use PRESET when turning ON (NEW)
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
    if (mb.Coil(CMD_DI_EN_BASE + i))  { mb.setCoil(CMD_DI_EN_BASE + i,  false); if (!diCfg[i].enabled){ diCfg[i].enabled=true;  cfgDirty=true; lastCfgTouchMs=millis(); } }
    if (mb.Coil(CMD_DI_DIS_BASE + i)) { mb.setCoil(CMD_DI_DIS_BASE + i, false); if ( diCfg[i].enabled){ diCfg[i].enabled=false; cfgDirty=true; lastCfgTouchMs=millis(); } }
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

    // Preset (NEW)
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

void sendAllEchoesOnce() {
  // DI config echoes
  JSONVar enableList, invertList, actionList, targetList;
  for (int i=0;i<NUM_DI;i++) { enableList[i]=diCfg[i].enabled; invertList[i]=diCfg[i].inverted; actionList[i]=diCfg[i].action; targetList[i]=diCfg[i].target; }
  WebSerial.send("enableList", enableList);
  WebSerial.send("invertList", invertList);
  WebSerial.send("inputActionList", actionList);
  WebSerial.send("inputTargetList", targetList);

  // Channels summary (enabled, level, thresholds, percent, loadType, cutMode, preset)
  JSONVar chEnabled, chLevels, chLowers, chUppers, chPercents, chTypes, cutModes, chPresets;
  for (int i=0;i<NUM_CH;i++){
    chEnabled[i]=chCfg[i].enabled;
    chLevels[i]= (int)chLevel[i];        // 0..255 (actual for UI display)
    chLowers[i]= (int)chLower[i];
    chUppers[i]= (int)chUpper[i];
    chPercents[i]= (int)min((int)(chPctX10[i]/10), 100); // echo 0..100
    chTypes[i]= (int)chLoadType[i];
    cutModes[i]= (int)chCutMode[i];
    chPresets[i]= (int)chPreset[i];
  }
  WebSerial.send("channelEnabled", chEnabled);
  WebSerial.send("channelLevels", chLevels);
  WebSerial.send("channelLower", chLowers);
  WebSerial.send("channelUpper", chUppers);
  WebSerial.send("channelPercent", chPercents);
  WebSerial.send("LoadTypeList", chTypes);
  WebSerial.send("CutModeList", cutModes);
  WebSerial.send("channelPreset", chPresets);   // NEW

  // Buttons mapping
  JSONVar ButtonGroupList; for (int i=0;i<NUM_BTN;i++) ButtonGroupList[i]=btnCfg[i].action;
  WebSerial.send("ButtonGroupList", ButtonGroupList);

  WebSerial.send("LedConfigList", LedConfigListFromCfg());

  // ZC flags + frequency
  JSONVar zcList; for (int i=0;i<NUM_CH;i++) zcList[i] = zcOk[i];
  WebSerial.send("ZcOkList", zcList);

  JSONVar freqList; for (int i=0;i<NUM_CH;i++) freqList[i] = (int)freq_x100[i];
  WebSerial.send("FreqX100List", freqList);

  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  mb.task();
  processModbusCommandPulses();

  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  // ZC presence/fault status
  for (int c=0;c<NUM_CH;c++) {
    bool ok = ((uint32_t)(now - zcLastEdgeMs[c]) <= ZC_FAULT_TIMEOUT_MS);
    zcOk[c] = ok;
    mb.setIsts(ISTS_ZC_OK_BASE + c, ok);

    if (zcOk[c] != zcPrevOk[c]) {
      zcPrevOk[c] = zcOk[c];
      if (ok) WebSerial.send("message", String("CH") + (c+1) + ": AC present (zero-cross OK)");
      else    WebSerial.send("message", String("CH") + (c+1) + ": No AC on L-N detected — check wiring");
    }
  }

  // Frequency estimator (median-of-3 -> moving average of 32)
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
      avgIdx[ch] = (avgIdx[ch] + 1) % AVG_N;

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
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  // Buttons — schematic inverted: HIGH = pressed (NEW)
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

  // Inputs (4)
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

    bool rising  = (!prev && val);
    bool falling = (prev && !val);

    uint8_t act = diCfg[i].action;
    if (act == 1) { if (rising || falling) { applyActionToTarget(diCfg[i].target, 1, now); } }
    else if (act == 2) { if (rising) { applyActionToTarget(diCfg[i].target, 2, now); } }
  }

  // Pulse timeout restore
  for (int c=0;c<NUM_CH;c++) {
    if (chPulseUntil[c] != 0 && timeAfter32(now, chPulseUntil[c])) {
      clampAndSetLevel(c, chLastNonZero[c]);
      chPulseUntil[c] = 0;
    }
  }

  // LEDs
  JSONVar LedStateList;
  for (int i=0;i<NUM_LED;i++) {
    bool srcActive = false;
    uint8_t src = ledCfg[i].source;
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

  // Channel "on" discrete inputs
  for (int c=0;c<NUM_CH;c++) {
    bool on = (chCfg[c].enabled && chLevel[c] > 0);
    mb.setIsts(ISTS_CH_BASE + c, on);
  }

  // WebSerial UI updates
  if (millis() - lastSend >= sendInterval) {
    lastSend = millis();
    WebSerial.check();

    JSONVar zcList; for (int i=0;i<NUM_CH;i++) zcList[i] = zcOk[i];
    modbusStatus["zc_ok"] = zcList;
    WebSerial.send("ZcOkList", zcList);

    JSONVar freqList; for (int i=0;i<NUM_CH;i++) freqList[i] = (int)freq_x100[i];
    WebSerial.send("FreqX100List", freqList);
    modbusStatus["freq_x100"] = freqList;

    WebSerial.send("status", modbusStatus);

    JSONVar invertList, enableList, actionList, targetList;
    for (int i=0;i<NUM_DI;i++) {
      invertList[i] = diCfg[i].inverted;
      enableList[i] = diCfg[i].enabled;
      actionList[i] = diCfg[i].action;
      targetList[i] = diCfg[i].target;
    }

    JSONVar channelEnabled, channelLevels, channelLower, channelUpper, channelPercent, loadTypes, cutModes, channelPreset;
    for (int i=0;i<NUM_CH;i++) {
      channelEnabled[i] = chCfg[i].enabled;
      channelLevels[i]  = (int)chLevel[i];             // show 0..255
      channelLower[i]   = (int)chLower[i];
      channelUpper[i]   = (int)chUpper[i];
      channelPercent[i] = (int)min((int)(chPctX10[i]/10), 100); // echo 0..100 for UI
      loadTypes[i]      = (int)chLoadType[i];
      cutModes[i]       = (int)chCutMode[i];
      channelPreset[i]  = (int)chPreset[i];
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
    WebSerial.send("channelLower", channelLower);
    WebSerial.send("channelUpper", channelUpper);
    WebSerial.send("channelPercent", channelPercent);  // UI should send this back on change
    WebSerial.send("LoadTypeList", loadTypes);
    WebSerial.send("CutModeList",  cutModes);
    WebSerial.send("channelPreset", channelPreset);    // NEW
    WebSerial.send("ButtonStateList", ButtonStateList);
    WebSerial.send("ButtonGroupList", ButtonGroupList);
    WebSerial.send("LedConfigList", LedConfigList);
    WebSerial.send("LedStateList", LedStateList);
  }
}
