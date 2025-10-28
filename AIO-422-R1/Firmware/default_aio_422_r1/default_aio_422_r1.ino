// ---- FIX for Arduino auto-prototype: make PersistConfig visible to auto-prototypes
struct PersistConfig;
struct PidState;

// ==== AIO-422-R1 (RP2350 ONLY) ====
// ADS1115 via ADS1X15.h on Wire1 (SDA=6, SCL=7), 4ch analog (reported as field 0–10V mV)
// + TWO MAX31865 RTD channels over SOFTWARE SPI (CS/DI/DO/CLK)
// + TWO MCP4725 12-bit DACs on Wire1 (0x61 and 0x60) -> post-amp to field 0–10V
// 4x PID controllers: flexible PV routing (AI/RTD/EXTPV), SP routing (STATIC/EXTSP/PID OUT),
// and output routing (AO0/AO1 or OUT_VAR0..3 to Modbus).
// WebSerial UI, Modbus RTU, LittleFS persistence
// Buttons: GPIO22..25, LEDs: GPIO18..21
// =========================================================================

#include <Arduino.h>
#include <Wire.h>
#include <ADS1X15.h>
#include <Adafruit_MAX31865.h>
#include <Adafruit_MCP4725.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include "hardware/watchdog.h"
#include <math.h>

// ================== Hardware pins ==================
#define LED_ACTIVE_LOW     0
#define BUTTON_USES_PULLUP 0

const uint8_t LED_PINS[4]    = {18,19,20,21};
const uint8_t BUTTON_PINS[4] = {22,23,24,25};

// I2C pins (ADS1115 + both MCP4725) on Wire1
#define SDA1 6
#define SCL1 7

// SOFTWARE SPI pins for MAX31865 (DI=MOSI, DO=MISO, CLK=SCK)
#define RTD1_CS   13
#define RTD2_CS   14
#define RTD_DI    11
#define RTD_DO    12
#define RTD_CLK   10

// RTD parameters
static const float RTD_NOMINAL_OHMS = 100.0f;   // PT100
static const float RTD_REF_OHMS     = 400.0f;   // FieldBoard
static const max31865_numwires_t RTD_WIRES = MAX31865_3WIRE;

// MCP4725 parameters (chip side 0–5V, post-amp → 0–10V)
#define MCP4725_ADDR0  0x61   // AO1_DAC
#define MCP4725_ADDR1  0x60   // AO0_DAC
#define DAC_VREF_MV    5000

// DAC post-amp gain (≈×2)
#ifndef DAC_POSTAMP_GAIN_NUM
#define DAC_POSTAMP_GAIN_NUM 2
#define DAC_POSTAMP_GAIN_DEN 1
#endif
#define DAC_GAIN_MULT      ((float)DAC_POSTAMP_GAIN_NUM / (float)DAC_POSTAMP_GAIN_DEN)
#define DAC_FIELD_VREF_MV  ((uint16_t)lroundf((float)DAC_VREF_MV * DAC_GAIN_MULT)) // 10000 mV nominal

// ADC field scaling (front-end attenuates 0–10V → ~0–3.3V at ADS input)
#ifndef ADC_FIELD_SCALE_NUM
#define ADC_FIELD_SCALE_NUM 30303   // 3.0303 * 10000
#define ADC_FIELD_SCALE_DEN 10000
#endif
#define ADC_FIELD_SCALE ((float)ADC_FIELD_SCALE_NUM / (float)ADC_FIELD_SCALE_DEN)

// ================== Modbus / RS-485 ==================
#define TX2 4
#define RX2 5
const int TxenPin = -1;
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== ADS1115 (ADS1X15) ==================
ADS1115 ads(0x48, &Wire1);
uint8_t  ads_gain = 1;      // ±4.096V (good for ~0..3.3V)
uint8_t  ads_data_rate = 4; // 128 SPS
uint16_t sample_ms = 50;

// ================== MAX31865 (2x RTD, SOFTWARE SPI) ==================
Adafruit_MAX31865 rtd1(RTD1_CS, RTD_DI, RTD_DO, RTD_CLK);
Adafruit_MAX31865 rtd2(RTD2_CS, RTD_DI, RTD_DO, RTD_CLK);

// ================== MCP4725 DACs ==================
Adafruit_MCP4725 dac0;  // 0x61
Adafruit_MCP4725 dac1;  // 0x60
uint16_t dac_raw[2] = {0, 0};  // 0..4095

// ================== Buttons & LEDs config ==================
struct LedCfg   { uint8_t mode;   uint8_t source; };
struct ButtonCfg{ uint8_t action; };

LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool      ledManual[4] = {false,false,false,false};
bool      ledPhys[4]   = {false,false,false,false};

// Button runtime
constexpr unsigned long BTN_DEBOUNCE_MS = 30;
bool buttonState[4]      = {false,false,false,false};
bool buttonPrev[4]       = {false,false,false,false};
unsigned long btnChangeAt[4] = {0,0,0,0};

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 500;
unsigned long lastBlinkToggle = 0;
const unsigned long blinkPeriodMs = 500;
bool blinkPhase = false;
unsigned long lastSample = 0;
bool samplingTick = false;

// ================== Persisted settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// Full definition (appears after the forward declaration)
struct PersistConfig {
  uint32_t magic;       // 'A422'
  uint16_t version;
  uint16_t size;
  uint8_t  mb_address;
  uint32_t mb_baud;
  uint8_t  ads_gain;
  uint8_t  ads_data_rate;
  uint16_t sample_ms;
  LedCfg   ledCfg[4];
  ButtonCfg buttonCfg[4];
  uint16_t dac_raw[2];
  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x32323441UL; // 'A422'
static const uint16_t CFG_VERSION = 0x000A;       // bump for 4x PID features
static const char*    CFG_PATH    = "/aio422.bin";

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
const uint32_t  CFG_AUTOSAVE_MS = 1200;

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

// ---- local clamp helper for Arduino builds (int range)
static inline int clampi(int v, int lo, int hi){
  if(v < lo) return lo;
  if(v > hi) return hi;
  return v;
}

inline bool readPressed(int i){
#if BUTTON_USES_PULLUP
  return digitalRead(BUTTON_PINS[i]) == LOW;
#else
  return digitalRead(BUTTON_PINS[i]) == HIGH;
#endif
}

// ================== Defaults ==================
void setDefaults() {
  g_mb_address   = 3;
  g_mb_baud      = 19200;
  ads_gain       = 1;
  ads_data_rate  = 4;
  sample_ms      = 50;
  dac_raw[0]     = 0;
  dac_raw[1]     = 0;

  for (int i=0;i<4;i++) {
    ledCfg[i].mode = 0;
    ledCfg[i].source = (i==0) ? 1 : 0; // LED0 heartbeat
    buttonCfg[i].action = 0;
    ledManual[i] = false;
  }
}

void captureToPersist(PersistConfig &pc) {
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);
  pc.mb_address   = g_mb_address;
  pc.mb_baud      = g_mb_baud;
  pc.ads_gain     = ads_gain;
  pc.ads_data_rate= ads_data_rate;
  pc.sample_ms    = sample_ms;
  memcpy(pc.ledCfg,   ledCfg,   sizeof(ledCfg));
  memcpy(pc.buttonCfg,buttonCfg,sizeof(buttonCfg));
  pc.dac_raw[0] = dac_raw[0];
  pc.dac_raw[1] = dac_raw[1];
  pc.crc32 = 0;
  pc.crc32 = crc32_update(0, reinterpret_cast<const uint8_t*>(&pc), sizeof(PersistConfig));
}

bool applyFromPersist(const PersistConfig &pc) {
  if (pc.magic != CFG_MAGIC) return false;
  if (pc.version != CFG_VERSION) return false;
  if (pc.size != sizeof(PersistConfig)) return false;
  PersistConfig tmp = pc; uint32_t crc = tmp.crc32; tmp.crc32 = 0;
  if (crc32_update(0, reinterpret_cast<const uint8_t*>(&tmp), sizeof(PersistConfig)) != crc)
    return false;
  g_mb_address   = pc.mb_address;
  g_mb_baud      = pc.mb_baud;
  ads_gain       = pc.ads_gain;
  ads_data_rate  = pc.ads_data_rate;
  sample_ms      = pc.sample_ms;
  memcpy(ledCfg,    pc.ledCfg,    sizeof(ledCfg));
  memcpy(buttonCfg, pc.buttonCfg, sizeof(buttonCfg));
  dac_raw[0] = pc.dac_raw[0];
  dac_raw[1] = pc.dac_raw[1];
  for (int i=0;i<4;i++) ledManual[i] = false;
  return true;
}

bool saveConfigFS() {
  PersistConfig pc{}; captureToPersist(pc);
  File f = LittleFS.open(CFG_PATH, "w"); if (!f) return false;
  size_t n = f.write(reinterpret_cast<const uint8_t*>(&pc), sizeof(pc)); f.close();
  return n == sizeof(pc);
}

bool loadConfigFS() {
  File f = LittleFS.open(CFG_PATH, "r"); if (!f) return false;
  if (f.size() != sizeof(PersistConfig)) { f.close(); return false; }
  PersistConfig pc{}; size_t n = f.read(reinterpret_cast<uint8_t*>(&pc), sizeof(pc)); f.close();
  if (n != sizeof(pc)) return false;
  return applyFromPersist(pc);
}

// ================== Modbus maps ==================
enum : uint16_t {
  // ADS1115
  IREG_RAW_BASE     = 100,   // 100..103 : raw counts (int16) ch0..ch3
  IREG_MV_BASE      = 120,   // 120..123 : field millivolts (uint16) ch0..ch3

  // MAX31865 RTD
  IREG_RTD_C_BASE     = 140,   // 140..141 : temperature °C x100 (int16)
  IREG_RTD_OHM_BASE   = 150,   // 150..151 : resistance ohms x100 (uint16)
  IREG_RTD_FAULT_BASE = 160,   // 160..161 : fault bitmask (uint16)

  // MCP4725 DACs (field side mV)
  IREG_DAC_MV_BASE  = 180,   // 180..181 : DAC field outputs in mV (uint16, 0–10000)

  // ===== EXTERNAL PVs & SPs (write via Modbus Holding) =====
  HREG_EXTPV_BASE   = 700,   // 700..703 : EXTPV0..3 (mV)
  HREG_EXTSP_BASE   = 710,   // 710..713 : EXTSP0..3 (mV)

  // ===== PID TELEMETRY (Input Regs, per PID, offset 16) =====
  IREG_PID_BASE     = 900,   // per PID i: base = 900 + i*16
  IREG_PID_PV       = 0,     // PV mV
  IREG_PID_SP       = 1,     // SP mV (effective)
  IREG_PID_OUT      = 2,     // OUT mV
  IREG_PID_ERR      = 3,     // ERR mV (signed cast)
  // 4 general OUT variables (if output routed to Modbus vars)
  IREG_OUTVAR_BASE  = 950,   // 950..953 : OUT_VAR0..3 (mV)

  // ===== PID CONFIG (Holding Regs, per PID, offset 24) =====
  HREG_PID_BASE     = 800,   // per PID i: base = 800 + i*24
  HREG_PID_MODE     = 0,     // 0=MANUAL, 1=AUTO
  HREG_PID_PV_TYPE  = 1,     // 0=AI, 1=RTD, 2=EXTPV
  HREG_PID_PV_IDX   = 2,     // AI 0..3, RTD 0..1, EXTPV 0..3
  HREG_PID_SP_TYPE  = 3,     // 0=STATIC, 1=EXTSP, 2=PID_OUT
  HREG_PID_SP_STATIC= 4,     // static SP mV (0..10000)
  HREG_PID_SP_IDX   = 5,     // EXTSP idx 0..3 or PID idx 0..3
  HREG_PID_KP_Q8_8  = 6,     // Kp
  HREG_PID_KI_Q8_8  = 7,     // Ki (/s)
  HREG_PID_KD_Q8_8  = 8,     // Kd (s)
  HREG_PID_OUT_MIN  = 9,     // mV clamp
  HREG_PID_OUT_MAX  = 10,    // mV clamp
  HREG_PID_MAN_MV   = 11,    // manual mV
  HREG_PID_ROUTE    = 12,    // 0=None, 1=AO0, 2=AO1, 10..13=OUT_VAR0..3

  // Existing config (kept)
  HREG_GAIN         = 200,
  HREG_DRATE        = 201,
  HREG_SMPLL_MS     = 202,
  HREG_DAC0_RAW     = 210,
  HREG_DAC1_RAW     = 211,

  // Discrete
  ISTS_LED_BASE     = 300,
  ISTS_BTN_BASE     = 320,
  ISTS_RTD_BASE     = 360,

  // Coils
  COIL_LEDTEST      = 400,
  COIL_SAVE_CFG     = 401,
  COIL_REBOOT       = 402,
  COIL_DAC0_EESAVE  = 403,
  COIL_DAC1_EESAVE  = 404
};

// ================== PID state (4 controllers) ==================
struct PidState {
  // config (mirrors HREGs)
  uint16_t mode;        // 0=MAN,1=AUTO
  uint16_t pv_type;     // 0=AI,1=RTD,2=EXTPV
  uint16_t pv_idx;      // per type
  uint16_t sp_type;     // 0=STATIC,1=EXTSP,2=PID_OUT
  uint16_t sp_static;   // mV
  uint16_t sp_idx;      // EXTSP idx or PID idx
  uint16_t kp_q8_8, ki_q8_8, kd_q8_8;
  uint16_t out_min_mv, out_max_mv;
  uint16_t man_mv;
  uint16_t route;       // 0 None, 1 AO0, 2 AO1, 10..13 OUT_VAR0..3

  // runtime
  float i_term;
  uint16_t last_pv_mv;
  bool initialized;
  uint16_t last_out_mv; // last loop output (for cross-PID SP source if future dependency)
};
PidState pid[4];

// general OUT variables (routable destinations)
uint16_t outVar_mV[4] = {0,0,0,0};

// External PVs / SPs (holding regs mirror)
uint16_t extPV_mV[4] = {0,0,0,0};
uint16_t extSP_mV[4] = {0,0,0,0};

// Keep last PID telemetry for Trend
uint16_t lastPidPV_mV[4]  = {0,0,0,0};
uint16_t lastPidSP_mV[4]  = {0,0,0,0};
uint16_t lastPidOUT_mV[4] = {0,0,0,0};

// helper: Q8.8 to float
static inline float q88_to_f(uint16_t q){ return (float)q / 256.0f; }
static inline uint16_t clamp_u16(uint16_t v, uint16_t lo, uint16_t hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static inline uint16_t mv_to_dac_code(uint16_t mv){
  long c = lroundf((float)mv * 4095.0f / (float)DAC_FIELD_VREF_MV);
  if (c < 0) c = 0; if (c > 4095) c = 4095;
  return (uint16_t)c;
}

// ================== Helpers ==================
void setLedPhys(uint8_t idx, bool on){
#if LED_ACTIVE_LOW
  digitalWrite(LED_PINS[idx], on ? LOW : HIGH);
#else
  digitalWrite(LED_PINS[idx], on ? HIGH : LOW);
#endif
  ledPhys[idx] = on;
  mb.setIsts(ISTS_LED_BASE + idx, on);
}

template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id)
  -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) { m.setSlaveId(id); }
inline void setSlaveIdIfAvailable(...) {}

void applyModbusSettings(uint8_t addr, uint32_t baud) {
  if ((uint32_t)modbusStatus["baud"] != baud) {
    Serial2.end(); Serial2.begin(baud); mb.config(baud);
  }
  setSlaveIdIfAvailable(mb, addr);
  g_mb_address = addr; g_mb_baud = baud;
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
}

void performReset() {
  if (Serial) Serial.flush();
  delay(50);
  watchdog_reboot(0, 0, 0);
  while (true) { __asm__("wfi"); }
}

// ---- WebSerial echo (minimal) ----
void sendStatusEcho() {
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}
void sendAdsEcho() {
  JSONVar adsEcho;
  adsEcho["gainIndex"]     = ads_gain;
  adsEcho["dataRateIndex"] = ads_data_rate;
  adsEcho["sample_ms"]     = sample_ms;
  WebSerial.send("ADS", adsEcho);
}
void sendDacEcho() {
  JSONVar obj;
  JSONVar rawArr; rawArr[0] = dac_raw[0]; rawArr[1] = dac_raw[1];
  JSONVar mvArr;
  mvArr[0] = (uint16_t)lroundf((float)dac_raw[0] * (float)DAC_FIELD_VREF_MV / 4095.0f);
  mvArr[1] = (uint16_t)lroundf((float)dac_raw[1] * (float)DAC_FIELD_VREF_MV / 4095.0f);
  obj["raw"] = rawArr;
  obj["mv"]  = mvArr;
  WebSerial.send("DAC", obj);
}
JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) { JSONVar o; o["mode"]=ledCfg[i].mode; o["source"]=ledCfg[i].source; arr[i]=o; }
  return arr;
}

// ---- NEW: PID config echo helpers ----
JSONVar PidConfigListFromRegs() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) {
    uint16_t b = (uint16_t)(HREG_PID_BASE + i*24);
    JSONVar o;
    o["i"]          = i;
    o["mode"]       = (int)mb.Hreg(b + HREG_PID_MODE);
    o["pv_type"]    = (int)mb.Hreg(b + HREG_PID_PV_TYPE);
    o["pv_idx"]     = (int)mb.Hreg(b + HREG_PID_PV_IDX);
    o["sp_type"]    = (int)mb.Hreg(b + HREG_PID_SP_TYPE);
    o["sp_static"]  = (int)mb.Hreg(b + HREG_PID_SP_STATIC);
    o["sp_idx"]     = (int)mb.Hreg(b + HREG_PID_SP_IDX);
    o["kp_q8_8"]    = (int)mb.Hreg(b + HREG_PID_KP_Q8_8);
    o["ki_q8_8"]    = (int)mb.Hreg(b + HREG_PID_KI_Q8_8);
    o["kd_q8_8"]    = (int)mb.Hreg(b + HREG_PID_KD_Q8_8);
    o["out_min_mv"] = (int)mb.Hreg(b + HREG_PID_OUT_MIN);
    o["out_max_mv"] = (int)mb.Hreg(b + HREG_PID_OUT_MAX);
    o["man_mv"]     = (int)mb.Hreg(b + HREG_PID_MAN_MV);
    o["route"]      = (int)mb.Hreg(b + HREG_PID_ROUTE);
    arr[i] = o;
  }
  return arr;
}
void sendPidConfigEcho() {
  WebSerial.send("PIDConfigList", PidConfigListFromRegs());
}

void sendAllEchoesOnce() {
  sendStatusEcho(); sendAdsEcho(); sendDacEcho();
  JSONVar btnCfg; for(int i=0;i<4;i++) btnCfg[i]=buttonCfg[i].action;
  WebSerial.send("ButtonConfigList", btnCfg);
  WebSerial.send("LedConfigList", LedConfigListFromCfg());
  JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i];
  WebSerial.send("ButtonStateList", btnState);
  JSONVar ledState; for(int i=0;i<4;i++) ledState[i]=ledPhys[i];
  WebSerial.send("LedStateList", ledState);
  // ---- NEW: also dump PID config
  sendPidConfigEcho();
}

void sendSamplesEcho(const int16_t raw[4], const uint16_t mv[4]) {
  JSONVar smp; for (int i=0;i<4;i++){ smp["raw"][i] = raw[i]; smp["mv"][i] = mv[i]; }
  WebSerial.send("AIO_Samples", smp);
}

void sendRTDEcho(const int16_t c_x100[2], const uint16_t ohm_x100[2], const uint16_t fault[2]) {
  JSONVar obj;
  for (int i=0;i<2;i++) { obj["c"][i]=c_x100[i]; obj["ohm"][i]=ohm_x100[i]; obj["fault"][i]=fault[i]; }
  WebSerial.send("AIO_RTD", obj);
}

// ================== TREND LOGGING (LittleFS ring buffer) ==================
#define TREND_FILE_PATH "/trend.bin"
#define TREND_INTERVAL_MS 5000UL
#define TREND_DURATION_MS (30UL * 60UL * 1000UL) // 30 minutes
#define TREND_RECORDS_MAX (TREND_DURATION_MS / TREND_INTERVAL_MS) // 360

struct TrendHeader {
  uint32_t magic;       // 'TRND'
  uint16_t version;     // 0x0001
  uint16_t rec_size;    // sizeof(TrendRecord)
  uint16_t capacity;    // TREND_RECORDS_MAX
  uint16_t windex;      // next write index [0..capacity-1]
  uint16_t count;       // number of valid records [0..capacity]
  uint32_t crc32;       // header CRC
} __attribute__((packed));

struct TrendRecord {
  uint32_t ts_ms;
  uint16_t ai_mV[4];
  int16_t  rtdC_x100[2];
  uint16_t ao_mV[2];
  uint16_t pidPV[4];
  uint16_t pidSP[4];
  uint16_t pidOUT[4];
} __attribute__((packed));

static const uint32_t TREND_MAGIC = 0x444E5254UL; // 'TRND'
static const uint16_t TREND_VER   = 0x0001;

static unsigned long lastTrendSave = 0;
static TrendHeader tHdr;

// fwd decls
void trend_init();
void trend_clear();
bool trend_load_header(File &f, TrendHeader &h);
bool trend_save_header(File &f, TrendHeader &h);
bool trend_append(const TrendRecord &rec);
void trend_send_info();
void trend_dump_all();
void trend_send_append_echo(const TrendRecord &rec);

// --- Trend impl
bool trend_load_header(File &f, TrendHeader &h){
  if (!f) return false;
  if (f.size() < (int)sizeof(TrendHeader)) return false;
  TrendHeader tmp{};
  if (f.read((uint8_t*)&tmp, sizeof(tmp)) != sizeof(tmp)) return false;
  uint32_t crc = tmp.crc32; tmp.crc32 = 0;
  if (crc32_update(0, (uint8_t*)&tmp, sizeof(tmp)) != crc) return false;
  if (tmp.magic != TREND_MAGIC) return false;
  if (tmp.version != TREND_VER) return false;
  if (tmp.rec_size != sizeof(TrendRecord)) return false;
  if (tmp.capacity != TREND_RECORDS_MAX) return false;
  if (tmp.windex >= tmp.capacity) tmp.windex = 0;
  if (tmp.count > tmp.capacity) tmp.count = 0;
  h = tmp;
  return true;
}

bool trend_save_header(File &f, TrendHeader &h){
  if (!f) return false;
  h.magic = TREND_MAGIC;
  h.version = TREND_VER;
  h.rec_size = sizeof(TrendRecord);
  h.capacity = TREND_RECORDS_MAX;
  h.windex = (h.windex >= h.capacity) ? 0 : h.windex;
  if (h.count > h.capacity) h.count = h.capacity;
  h.crc32 = 0;
  h.crc32 = crc32_update(0, (uint8_t*)&h, sizeof(h));
  f.seek(0, SeekSet);
  size_t n = f.write((uint8_t*)&h, sizeof(h));
  return n == sizeof(h);
}

void trend_init(){
  File f = LittleFS.open(TREND_FILE_PATH, "r+");
  if (trend_load_header(f, tHdr)) {
    WebSerial.send("message","Trend: existing ring buffer OK");
    f.close();
    trend_send_info();
    return;
  }
  if (f) f.close();
  // create fresh file
  TrendHeader h{};
  h.magic = TREND_MAGIC; h.version = TREND_VER;
  h.rec_size = sizeof(TrendRecord);
  h.capacity = TREND_RECORDS_MAX;
  h.windex = 0; h.count = 0; h.crc32 = 0;

  // Pre-size file: header + capacity * rec_size
  File nf = LittleFS.open(TREND_FILE_PATH, "w+");
  if (!nf) { WebSerial.send("message","Trend: create FAILED"); return; }
  // write header
  if (!trend_save_header(nf, h)) { nf.close(); WebSerial.send("message","Trend: header write FAILED"); return; }
  // seek to last byte of data area and write 0 to allocate
  size_t total = sizeof(TrendHeader) + (size_t)h.capacity * (size_t)h.rec_size;
  nf.seek(total - 1, SeekSet);
  uint8_t z = 0; nf.write(&z, 1);
  nf.flush(); nf.close();
  tHdr = h;
  WebSerial.send("message","Trend: new ring buffer created");
  trend_send_info();
}

void trend_clear(){
  tHdr.windex = 0; tHdr.count = 0;
  File f = LittleFS.open(TREND_FILE_PATH, "r+");
  if (!f) { trend_init(); return; }
  if (!trend_save_header(f, tHdr)) { WebSerial.send("message","Trend: clear header write FAILED"); }
  f.close();
  trend_send_info();
}

bool trend_append(const TrendRecord &rec){
  File f = LittleFS.open(TREND_FILE_PATH, "r+");
  if (!f) { WebSerial.send("message","Trend: open FAILED"); return false; }
  TrendHeader h{};
  if (!trend_load_header(f, h)) {
    f.close(); trend_init(); return false;
  }
  size_t off = sizeof(TrendHeader) + (size_t)h.windex * (size_t)h.rec_size;
  if (!f.seek(off, SeekSet)) { f.close(); WebSerial.send("message","Trend: seek FAILED"); return false; }
  size_t n = f.write((const uint8_t*)&rec, sizeof(rec));
  if (n != sizeof(rec)) { f.close(); WebSerial.send("message","Trend: write FAILED"); return false; }
  h.windex = (h.windex + 1) % h.capacity;
  if (h.count < h.capacity) h.count++;
  if (!trend_save_header(f, h)) { f.close(); WebSerial.send("message","Trend: header upd FAILED"); return false; }
  f.close();
  tHdr = h;
  trend_send_append_echo(rec);
  return true;
}

void trend_send_info(){
  JSONVar o; o["capacity"] = tHdr.capacity; o["count"] = tHdr.count; o["windex"] = tHdr.windex;
  WebSerial.send("TrendInfo", o);
}

void trend_dump_all(){
  File f = LittleFS.open(TREND_FILE_PATH, "r");
  if (!f) { WebSerial.send("message","Trend dump: open FAILED"); return; }
  TrendHeader h{};
  if (!trend_load_header(f, h)) { f.close(); WebSerial.send("message","Trend dump: header bad"); return; }

  JSONVar arr; // array of records
  if (h.count == 0) {
    WebSerial.send("TrendFull", arr);
    f.close(); return;
  }

  // oldest index
  uint16_t start = (h.windex + h.capacity - h.count) % h.capacity;
  TrendRecord rec{};
  for (uint16_t i=0;i<h.count;i++){
    size_t idx = (start + i) % h.capacity;
    size_t off = sizeof(TrendHeader) + idx * (size_t)h.rec_size;
    f.seek(off, SeekSet);
    size_t n = f.read((uint8_t*)&rec, sizeof(rec));
    if (n != sizeof(rec)) break;
    JSONVar o;
    o["t"] = (int)rec.ts_ms;
    for (int k=0;k<4;k++){ o["ai"][k] = rec.ai_mV[k]; }
    for (int k=0;k<2;k++){ o["rtd"][k] = rec.rtdC_x100[k]; }
    for (int k=0;k<2;k++){ o["ao"][k]  = rec.ao_mV[k]; }
    for (int k=0;k<4;k++){ o["pv"][k]  = rec.pidPV[k]; }
    for (int k=0;k<4;k++){ o["sp"][k]  = rec.pidSP[k]; }
    for (int k=0;k<4;k++){ o["out"][k] = rec.pidOUT[k]; }
    arr[i] = o;
  }
  f.close();
  WebSerial.send("TrendFull", arr);
}

void trend_send_append_echo(const TrendRecord &rec){
  JSONVar o;
  o["t"] = (int)rec.ts_ms;
  for (int k=0;k<4;k++){ o["ai"][k]  = rec.ai_mV[k]; }
  for (int k=0;k<2;k++){ o["rtd"][k] = rec.rtdC_x100[k]; }
  for (int k=0;k<2;k++){ o["ao"][k]  = rec.ao_mV[k]; }
  for (int k=0;k<4;k++){ o["pv"][k]  = rec.pidPV[k]; }
  for (int k=0;k<4;k++){ o["sp"][k]  = rec.pidSP[k]; }
  for (int k=0;k<4;k++){ o["out"][k] = rec.pidOUT[k]; }
  WebSerial.send("TrendAppend", o);
}

// ---- WebSerial handlers (unchanged semantics) ----
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);
  applyModbusSettings((uint8_t)addr, (uint32_t)baud);
  WebSerial.send("message", "Modbus configuration updated");
  cfgDirty = true; lastCfgTouchMs = millis();
}

void handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"];
  if (!t) { WebSerial.send("message", "Config: missing 't'"); return; }
  String type = String(t);
  bool changed = false;

  if (type == "ads") {
    JSONVar cfg = obj["cfg"];
    int g  = (int)cfg["gainIndex"];
    int dr = (int)cfg["dataRateIndex"];
    int sm = (int)cfg["sample_ms"];
    ads_gain      = (uint8_t)constrain(g,  0, 5);
    ads_data_rate = (uint8_t)constrain(dr, 0, 7);
    sample_ms     = (uint16_t)constrain(sm,10,5000);
    ads.setGain(ads_gain);
    ads.setDataRate(ads_data_rate);
    mb.Hreg(HREG_GAIN, ads_gain);
    mb.Hreg(HREG_DRATE, ads_data_rate);
    mb.Hreg(HREG_SMPLL_MS, sample_ms);
    sendAdsEcho();
    WebSerial.send("message", "ADS configuration updated");
    changed = true;

  } else if (type == "dac") {
    JSONVar cfg = obj["cfg"];
    int idx = (cfg.hasOwnProperty("idx")) ? (int)cfg["idx"] : 0;
    idx = constrain(idx, 0, 1);
    int raw = (int)cfg["raw"];
    if (idx == 0) { dac_raw[0] = (uint16_t)constrain(raw, 0, 4095); dac0.setVoltage(dac_raw[0], false); mb.Hreg(HREG_DAC0_RAW, dac_raw[0]); }
    else          { dac_raw[1] = (uint16_t)constrain(raw, 0, 4095); dac1.setVoltage(dac_raw[1], false); mb.Hreg(HREG_DAC1_RAW, dac_raw[1]); }
    sendDacEcho();
    WebSerial.send("message", "DAC updated");
    changed = true;

  } else if (type == "buttons") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) buttonCfg[i].action = (uint8_t)constrain((int)list[i]["action"], 0, 8);
    WebSerial.send("message", "Buttons configuration updated");
    JSONVar btnCfg; for(int i=0;i<4;i++) btnCfg[i]=buttonCfg[i].action;
    WebSerial.send("ButtonConfigList", btnCfg);
    changed = true;

  } else if (type == "leds") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
      ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 7);
    }
    WebSerial.send("message", "LEDs configuration updated");
    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    changed = true;

  // ---- NEW: PID config from UI ----
  } else if (type == "pid") {
    int i = constrain((int)obj["i"], 0, 3);
    JSONVar c = obj["cfg"];
    uint16_t b = (uint16_t)(HREG_PID_BASE + i*24);

    auto setH = [&](uint16_t off, int v){ mb.Hreg(b + off, (uint16_t)v); };

    if (c.hasOwnProperty("mode"))       setH(HREG_PID_MODE,       (int)c["mode"]);
    if (c.hasOwnProperty("pv_type"))    setH(HREG_PID_PV_TYPE,    (int)c["pv_type"]);
    if (c.hasOwnProperty("pv_idx"))     setH(HREG_PID_PV_IDX,     (int)c["pv_idx"]);
    if (c.hasOwnProperty("sp_type"))    setH(HREG_PID_SP_TYPE,    (int)c["sp_type"]);
    if (c.hasOwnProperty("sp_static"))  setH(HREG_PID_SP_STATIC,  (uint16_t)clampi((int)c["sp_static"], 0, 10000));
    if (c.hasOwnProperty("sp_idx"))     setH(HREG_PID_SP_IDX,     (int)c["sp_idx"]);
    if (c.hasOwnProperty("kp_q8_8"))    setH(HREG_PID_KP_Q8_8,    (int)c["kp_q8_8"]);
    if (c.hasOwnProperty("ki_q8_8"))    setH(HREG_PID_KI_Q8_8,    (int)c["ki_q8_8"]);
    if (c.hasOwnProperty("kd_q8_8"))    setH(HREG_PID_KD_Q8_8,    (int)c["kd_q8_8"]);

    // handle min/max together and keep sane
    if (c.hasOwnProperty("out_min_mv") || c.hasOwnProperty("out_max_mv")) {
      uint16_t omin = c.hasOwnProperty("out_min_mv") ? (uint16_t)clampi((int)c["out_min_mv"], 0, 10000)
                                                     : mb.Hreg(b + HREG_PID_OUT_MIN);
      uint16_t omax = c.hasOwnProperty("out_max_mv") ? (uint16_t)clampi((int)c["out_max_mv"], 0, 10000)
                                                     : mb.Hreg(b + HREG_PID_OUT_MAX);
      if (omin > omax) { uint16_t t = omin; omin = omax; omax = t; }
      setH(HREG_PID_OUT_MIN, omin);
      setH(HREG_PID_OUT_MAX, omax);
    }

    if (c.hasOwnProperty("man_mv"))     setH(HREG_PID_MAN_MV,     (uint16_t)clampi((int)c["man_mv"], 0, 10000));
    if (c.hasOwnProperty("route"))      setH(HREG_PID_ROUTE,      (int)c["route"]);

    // keep runtime structure in sync
    pid_load_from_regs(i);

    // echo back the full list so UI stays in sync
    sendPidConfigEcho();
    WebSerial.send("message", String("PID[") + i + "] configuration updated");
    changed = true;

  } else {
    WebSerial.send("message", String("Unknown Config type: ") + t);
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); }
}

// Commands
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if (!actC) { WebSerial.send("message", "command: missing 'action'"); return; }
  String act = String(actC); act.toLowerCase();

  if (act == "reset" || act == "reboot") {
    saveConfigFS();
    WebSerial.send("message", "Rebooting…");
    delay(120);
    performReset();

  } else if (act == "save") {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");

  } else if (act == "load") {
    if (loadConfigFS()) {
      WebSerial.send("message", "Configuration loaded");
      applyModbusSettings(g_mb_address, g_mb_baud);
      ads.setGain(ads_gain);
      ads.setDataRate(ads_data_rate);
      dac0.setVoltage(dac_raw[0], false);
      dac1.setVoltage(dac_raw[1], false);
      sendAllEchoesOnce();
    } else WebSerial.send("message", "ERROR: Load failed/invalid");

  } else if (act == "factory") {
    setDefaults();
    if (saveConfigFS()) {
      WebSerial.send("message", "Factory defaults restored & saved");
      applyModbusSettings(g_mb_address, g_mb_baud);
      ads.setGain(ads_gain);
      ads.setDataRate(ads_data_rate);
      dac0.setVoltage(dac_raw[0], false);
      dac1.setVoltage(dac_raw[1], false);
      sendAllEchoesOnce();
    } else WebSerial.send("message", "ERROR: Save after factory reset failed");

  } else if (act == "ledtest") {
    for (int k=0;k<2;k++) {
      for (int i=0;i<4;i++) setLedPhys(i, true);
      delay(120);
      for (int i=0;i<4;i++) setLedPhys(i, false);
      delay(120);
    }
    WebSerial.send("message", "LED test completed");

  } else if (act == "trend_dump") {
    trend_dump_all();

  } else if (act == "trend_clear") {
    trend_clear();

  } else if (act == "trend_info") {
    trend_send_info();

  } else {
    WebSerial.send("message", String("Unknown command: ") + actC);
  }
}

// ================== PID helpers ==================
static inline uint16_t pid_baseH(uint8_t i){ return (uint16_t)(HREG_PID_BASE + i*24); }
static inline uint16_t pid_baseI(uint8_t i){ return (uint16_t)(IREG_PID_BASE + i*16); }

void pid_load_from_regs(uint8_t i) {
  uint16_t b = pid_baseH(i);
  PidState &p = pid[i];
  p.mode       = mb.Hreg(b + HREG_PID_MODE);
  p.pv_type    = mb.Hreg(b + HREG_PID_PV_TYPE);
  p.pv_idx     = mb.Hreg(b + HREG_PID_PV_IDX);
  p.sp_type    = mb.Hreg(b + HREG_PID_SP_TYPE);
  p.sp_static  = clamp_u16(mb.Hreg(b + HREG_PID_SP_STATIC), 0, 10000);
  p.sp_idx     = mb.Hreg(b + HREG_PID_SP_IDX);
  p.kp_q8_8    = mb.Hreg(b + HREG_PID_KP_Q8_8);
  p.ki_q8_8    = mb.Hreg(b + HREG_PID_KI_Q8_8);
  p.kd_q8_8    = mb.Hreg(b + HREG_PID_KD_Q8_8);
  p.out_min_mv = mb.Hreg(b + HREG_PID_OUT_MIN);
  p.out_max_mv = mb.Hreg(b + HREG_PID_OUT_MAX);
  if (p.out_min_mv > p.out_max_mv){ uint16_t t=p.out_min_mv; p.out_min_mv=p.out_max_mv; p.out_max_mv=t; }
  p.out_min_mv = clamp_u16(p.out_min_mv, 0, 10000);
  p.out_max_mv = clamp_u16(p.out_max_mv, 0, 10000);
  p.man_mv     = clamp_u16(mb.Hreg(b + HREG_PID_MAN_MV), p.out_min_mv, p.out_max_mv);
  p.route      = mb.Hreg(b + HREG_PID_ROUTE);
}

void pid_store_defaults_and_regs() {
  // External PVs/SPs holding
  for (int i=0;i<4;i++) { mb.addHreg(HREG_EXTPV_BASE + i); mb.Hreg(HREG_EXTPV_BASE + i, 0); }
  for (int i=0;i<4;i++) { mb.addHreg(HREG_EXTSP_BASE + i); mb.Hreg(HREG_EXTSP_BASE + i, 0); }

  // PID configs & telemetry
  for (int i=0;i<4;i++) {
    uint16_t hb = pid_baseH(i);
    mb.addHreg(hb + HREG_PID_MODE);      mb.Hreg(hb + HREG_PID_MODE, 0);
    mb.addHreg(hb + HREG_PID_PV_TYPE);   mb.Hreg(hb + HREG_PID_PV_TYPE, 0);     // AI
    mb.addHreg(hb + HREG_PID_PV_IDX);    mb.Hreg(hb + HREG_PID_PV_IDX, i%4);    // AIi
    mb.addHreg(hb + HREG_PID_SP_TYPE);   mb.Hreg(hb + HREG_PID_SP_TYPE, 0);     // STATIC
    mb.addHreg(hb + HREG_PID_SP_STATIC); mb.Hreg(hb + HREG_PID_SP_STATIC, 0);
    mb.addHreg(hb + HREG_PID_SP_IDX);    mb.Hreg(hb + HREG_PID_SP_IDX, 0);
    mb.addHreg(hb + HREG_PID_KP_Q8_8);   mb.Hreg(hb + HREG_PID_KP_Q8_8, (uint16_t)(1.00f*256));
    mb.addHreg(hb + HREG_PID_KI_Q8_8);   mb.Hreg(hb + HREG_PID_KI_Q8_8, (uint16_t)(0.50f*256));
    mb.addHreg(hb + HREG_PID_KD_Q8_8);   mb.Hreg(hb + HREG_PID_KD_Q8_8, (uint16_t)(0.00f*256));
    mb.addHreg(hb + HREG_PID_OUT_MIN);   mb.Hreg(hb + HREG_PID_OUT_MIN, 0);
    mb.addHreg(hb + HREG_PID_OUT_MAX);   mb.Hreg(hb + HREG_PID_OUT_MAX, 10000);
    mb.addHreg(hb + HREG_PID_MAN_MV);    mb.Hreg(hb + HREG_PID_MAN_MV, 0);
    mb.addHreg(hb + HREG_PID_ROUTE);     mb.Hreg(hb + HREG_PID_ROUTE, 0);

    uint16_t ib = pid_baseI(i);
    mb.addIreg(ib + IREG_PID_PV);  mb.Ireg(ib + IREG_PID_PV, 0);
    mb.addIreg(ib + IREG_PID_SP);  mb.Ireg(ib + IREG_PID_SP, 0);
    mb.addIreg(ib + IREG_PID_OUT); mb.Ireg(ib + IREG_PID_OUT, 0);
    mb.addIreg(ib + IREG_PID_ERR); mb.Ireg(ib + IREG_PID_ERR, 0);

    pid[i].i_term=0.0f; pid[i].last_pv_mv=0; pid[i].initialized=false; pid[i].last_out_mv=0;
  }

  // OUT_VARs telemetry
  for (int i=0;i<4;i++){ mb.addIreg(IREG_OUTVAR_BASE + i); mb.Ireg(IREG_OUTVAR_BASE + i, 0); }
}

// Read PV mV based on pv_type/pv_idx
uint16_t pid_read_pv(uint16_t pv_type, uint16_t pv_idx,
                     const uint16_t ai_field_mV[4],
                     const int16_t rtdC_x100[2]) {
  switch (pv_type) {
    case 0: { // AI
      if (pv_idx > 3) pv_idx = 3;
      return ai_field_mV[pv_idx];
    }
    case 1: { // RTD -> use temperature °C*100 mapped to 0..10000mV
      if (pv_idx > 1) pv_idx = 1;
      int16_t cx100 = rtdC_x100[pv_idx];
      int32_t mv = (int32_t)cx100; // treat as mV-like engineering units
      if (mv < 0) mv = 0; if (mv > 10000) mv = 10000;
      return (uint16_t)mv;
    }
    case 2: { // EXTPV
      if (pv_idx > 3) pv_idx = 3;
      return extPV_mV[pv_idx];
    }
    default: return 0;
  }
}

// Resolve SP mV (STATIC / EXTSP / PID_OUT)
uint16_t pid_resolve_sp(uint8_t self_i, uint16_t sp_type, uint16_t sp_static, uint16_t sp_idx,
                        const uint16_t lastPidOut_mV[4]) {
  switch (sp_type) {
    case 0: return sp_static;
    case 1: { // EXTSP
      if (sp_idx > 3) sp_idx = 3;
      return extSP_mV[sp_idx];
    }
    case 2: { // PID_OUT
      if (sp_idx > 3) sp_idx = 3;
      return lastPidOut_mV[sp_idx];
    }
    default: return 0;
  }
}

uint16_t pid_step_compute(PidState &p, uint16_t pv_mv, float dt_s, uint16_t eff_sp_mv, uint8_t pid_index) {
  float e  = (float)eff_sp_mv - (float)pv_mv;
  float Kp = q88_to_f(p.kp_q8_8);
  float Ki = q88_to_f(p.ki_q8_8);
  float Kd = q88_to_f(p.kd_q8_8);

  // D on measurement
  float d_meas = 0.0f;
  if (p.initialized && dt_s > 1e-6f) d_meas = ((float)pv_mv - (float)p.last_pv_mv) / dt_s;
  p.last_pv_mv = pv_mv;
  p.initialized = true;

  // PI + D_on_meas
  p.i_term += Ki * e * dt_s;
  if (p.i_term > p.out_max_mv) p.i_term = (float)p.out_max_mv;
  if (p.i_term < p.out_min_mv) p.i_term = (float)p.out_min_mv;

  float u = Kp*e + p.i_term - Kd*d_meas;
  if (u > p.out_max_mv) u = (float)p.out_max_mv;
  if (u < p.out_min_mv) u = (float)p.out_min_mv;

  // telemetry
  uint16_t ib = pid_baseI(pid_index);
  mb.Ireg(ib + IREG_PID_PV,  pv_mv);
  mb.Ireg(ib + IREG_PID_SP,  eff_sp_mv);
  mb.Ireg(ib + IREG_PID_OUT, (uint16_t)lroundf(u));
  mb.Ireg(ib + IREG_PID_ERR, (int16_t)lroundf(e));

  return (uint16_t)lroundf(u);
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);

  // GPIO init
  for (uint8_t i=0;i<4;i++) { pinMode(LED_PINS[i], OUTPUT); setLedPhys(i, false); }
  for (uint8_t i=0;i<4;i++) {
#if BUTTON_USES_PULLUP
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
#else
    pinMode(BUTTON_PINS[i], INPUT);
#endif
  }

  setDefaults();

  // LittleFS
  if (!LittleFS.begin()) {
    WebSerial.send("message", "LittleFS mount failed. Formatting…");
    LittleFS.format();
    LittleFS.begin();
  }
  // Trend needs FS; init early
  trend_init();

  if (loadConfigFS()) WebSerial.send("message", "Config loaded from flash");
  else { WebSerial.send("message", "No valid config. Using defaults."); saveConfigFS(); }

  // I2C init
  Wire1.setSDA(SDA1); Wire1.setSCL(SCL1);
  Wire1.begin(); Wire1.setClock(400000);

  // ADS1115
  if (!ads.begin()) WebSerial.send("message", "ERROR: ADS1115 not found at 0x48");
  ads.setGain(ads_gain);
  ads.setDataRate(ads_data_rate);
  ads.setMode(1);

  // DACs
  if (!dac0.begin(MCP4725_ADDR0, &Wire1)) WebSerial.send("message", "ERROR: MCP4725 #0 not found at 0x61");
  else dac0.setVoltage(dac_raw[0], false);
  if (!dac1.begin(MCP4725_ADDR1, &Wire1)) WebSerial.send("message", "ERROR: MCP4725 #1 not found at 0x60");
  else dac1.setVoltage(dac_raw[1], false);

  // MAX31865
  if (!rtd1.begin(RTD_WIRES)) WebSerial.send("message", "ERROR: RTD1 init failed");
  if (!rtd2.begin(RTD_WIRES)) WebSerial.send("message", "ERROR: RTD2 init failed");

  // Serial2 / Modbus RTU
  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);

  // ---- Register telemetry ----
  for (uint16_t i=0;i<4;i++) mb.addIreg(IREG_RAW_BASE + i);
  for (uint16_t i=0;i<4;i++) mb.addIreg(IREG_MV_BASE  + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_RTD_C_BASE     + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_RTD_OHM_BASE   + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_RTD_FAULT_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_DAC_MV_BASE + i);

  // ---- Existing config ----
  mb.addHreg(HREG_GAIN);         mb.Hreg(HREG_GAIN, ads_gain);
  mb.addHreg(HREG_DRATE);        mb.Hreg(HREG_DRATE, ads_data_rate);
  mb.addHreg(HREG_SMPLL_MS);     mb.Hreg(HREG_SMPLL_MS, sample_ms);
  mb.addHreg(HREG_DAC0_RAW);     mb.Hreg(HREG_DAC0_RAW, dac_raw[0]);
  mb.addHreg(HREG_DAC1_RAW);     mb.Hreg(HREG_DAC1_RAW, dac_raw[1]);

  // Discrete stats / coils
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_LED_BASE + i);
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_BTN_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addIsts(ISTS_RTD_BASE + i);
  mb.addCoil(COIL_LEDTEST); mb.addCoil(COIL_SAVE_CFG);
  mb.addCoil(COIL_REBOOT);  mb.addCoil(COIL_DAC0_EESAVE);
  mb.addCoil(COIL_DAC1_EESAVE);

  // ---- PID regs & defaults ----
  pid_store_defaults_and_regs();
  for (int i=0;i<4;i++) pid_load_from_regs(i);

  // WebSerial
  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);

  // Also allow structured trend commands
  WebSerial.on("trend", [](JSONVar obj){
    const char* actC = (const char*)obj["action"];
    if (!actC){ WebSerial.send("message","trend: missing action"); return; }
    String act = String(actC); act.toLowerCase();
    if (act == "dump")       trend_dump_all();
    else if (act == "clear") trend_clear();
    else if (act == "info")  trend_send_info();
    else WebSerial.send("message", String("trend: unknown '")+act+"'");
  });

  // Initial echoes
  sendAllEchoesOnce();
  WebSerial.send("message", "Boot OK (AIO-422-R1, RP2350, ADS1115 field AI, 2x MAX31865, 2x MCP4725 field AO, + 4x PID)");
}

// ================== HREG write watcher ==================
void serviceHregWrites() {
  static uint16_t prevGain = 0xFFFF, prevDr = 0xFFFF, prevSm = 0xFFFF;
  static uint16_t prevDac0 = 0xFFFF, prevDac1 = 0xFFFF;

  uint16_t g  = mb.Hreg(HREG_GAIN);
  uint16_t dr = mb.Hreg(HREG_DRATE);
  uint16_t sm = mb.Hreg(HREG_SMPLL_MS);
  uint16_t dv0 = mb.Hreg(HREG_DAC0_RAW);
  uint16_t dv1 = mb.Hreg(HREG_DAC1_RAW);

  bool changed = false;

  if (g != prevGain)   { prevGain = g; ads_gain = constrain((int)g, 0, 5); ads.setGain(ads_gain); changed = true; }
  if (dr != prevDr)    { prevDr = dr; ads_data_rate = constrain((int)dr, 0, 7); ads.setDataRate(ads_data_rate); changed = true; }
  if (sm != prevSm)    { prevSm = sm; sample_ms = constrain((int)sm, 10, 5000); changed = true; }

  // Manual raw writes only take effect if a PID isn't routing to the same AO.
  if (dv0 != prevDac0) { prevDac0 = dv0; dac_raw[0] = constrain((int)dv0, 0, 4095); dac0.setVoltage(dac_raw[0], false); changed = true; }
  if (dv1 != prevDac1) { prevDac1 = dv1; dac_raw[1] = constrain((int)dv1, 0, 4095); dac1.setVoltage(dac_raw[1], false); changed = true; }

  // Mirror EXTPV/EXTSP holding to runtime buffers
  for (int i=0;i<4;i++) {
    extPV_mV[i] = clamp_u16(mb.Hreg(HREG_EXTPV_BASE + i), 0, 10000);
    extSP_mV[i] = clamp_u16(mb.Hreg(HREG_EXTSP_BASE + i), 0, 10000);
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); sendAdsEcho(); sendDacEcho(); }

  // Always refresh PID runtime on any HREG changes
  for (int i=0;i<4;i++) pid_load_from_regs(i);
}

// ================== Button actions ==================
void doButtonAction(uint8_t idx) {
  uint8_t act = buttonCfg[idx].action;
  switch (act) {
    case 0: break;
    case 1: case 2: case 3: case 4: {
      uint8_t led = act - 1;
      ledManual[led] = !ledManual[led];
      break;
    }
    case 5: {
      ads_gain = (ads_gain + 1) % 6;
      ads.setGain(ads_gain);
      mb.Hreg(HREG_GAIN, ads_gain);
      sendAdsEcho();
      WebSerial.send("message", String("ADS gain -> index ") + ads_gain);
      cfgDirty = true; lastCfgTouchMs = millis();
      break;
    }
    case 6: {
      for (int k=0;k<2;k++) {
        for (int i=0;i<4;i++) setLedPhys(i, true);
        delay(100);
        for (int i=0;i<4;i++) setLedPhys(i, false);
        delay(100);
      }
      break;
    }
    case 7: {
      if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
      else                WebSerial.send("message", "ERROR: Save failed");
      break;
    }
    case 8: {
      WebSerial.send("message", "Rebooting…");
      delay(120);
      performReset();
      break;
    }
  }
}

// ================== LED source evaluation ==================
bool evalLedSource(uint8_t src) {
  switch (src) {
    case 0: return false;
    case 1: return blinkPhase;
    case 2: return buttonState[0];
    case 3: return buttonState[1];
    case 4: return buttonState[2];
    case 5: return buttonState[3];
    case 6: return buttonState[0]||buttonState[1]||buttonState[2]||buttonState[3];
    case 7: return samplingTick;
    default: return false;
  }
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  mb.task();
  serviceHregWrites();

  // Coils
  if (mb.Coil(COIL_LEDTEST)) {
    for (int k=0;k<2;k++) { for (int i=0;i<4;i++) setLedPhys(i, true); delay(120); for (int i=0;i<4;i++) setLedPhys(i, false); delay(120); }
    mb.Coil(COIL_LEDTEST, false);
  }
  if (mb.Coil(COIL_SAVE_CFG)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    mb.Coil(COIL_SAVE_CFG, false);
  }
  if (mb.Coil(COIL_REBOOT)) { WebSerial.send("message", "Rebooting…"); delay(120); performReset(); }
  if (mb.Coil(COIL_DAC0_EESAVE)) { dac0.setVoltage(dac_raw[0], true); WebSerial.send("message", "DAC0: code written to EEPROM"); mb.Coil(COIL_DAC0_EESAVE, false); }
  if (mb.Coil(COIL_DAC1_EESAVE)) { dac1.setVoltage(dac_raw[1], true); WebSerial.send("message", "DAC1: code written to EEPROM"); mb.Coil(COIL_DAC1_EESAVE, false); }

  // Buttons debounce
  for (int i = 0; i < 4; i++) {
    bool pressed = readPressed(i);
    if (pressed != buttonState[i] && (now - btnChangeAt[i] >= BTN_DEBOUNCE_MS)) {
      btnChangeAt[i] = now;
      buttonPrev[i]  = buttonState[i];
      buttonState[i] = pressed;
      if (buttonPrev[i] && !buttonState[i]) doButtonAction(i); // on release
    }
    mb.setIsts(ISTS_BTN_BASE + i, buttonState[i]);
  }

  // Sampling
  static int16_t  rawArr[4] = {0,0,0,0};
  static uint16_t ai_mV[4]  = {0,0,0,0};  // field mV
  static int16_t  rtdC_x100[2]   = {0,0};
  static uint16_t rtdOhm_x100[2] = {0,0};
  static uint16_t rtdFault[2]    = {0,0};

  samplingTick = false;

  if (now - lastSample >= sample_ms) {
    float dt_s = (float)(now - lastSample) / 1000.0f;
    lastSample = now;
    samplingTick = true;

    // ADS1115 -> field mV
    for (int ch=0; ch<4; ch++) {
      int16_t raw = ads.readADC(ch);
      float   v_adc   = ads.toVoltage(raw);
      float   v_field = v_adc * ADC_FIELD_SCALE;
      long    mv      = lroundf(v_field * 1000.0f);
      if (mv < 0) mv = 0; if (mv > 65535) mv = 65535;
      rawArr[ch] = raw;
      ai_mV[ch]  = (uint16_t)mv;
      mb.Ireg(IREG_RAW_BASE + ch, (int16_t)rawArr[ch]);
      mb.Ireg(IREG_MV_BASE  + ch, (uint16_t)ai_mV[ch]);
    }

    // RTD1
    {
      uint16_t rtd = rtd1.readRTD();
      float ohms = (float)rtd * RTD_REF_OHMS / 32768.0f;
      float tC   = rtd1.temperature(RTD_NOMINAL_OHMS, RTD_REF_OHMS);
      uint8_t f  = rtd1.readFault(); if (f) rtd1.clearFault();
      int32_t cx100 = lroundf(tC * 100.0f);
      int32_t ox100 = lroundf(ohms * 100.0f);
      if (cx100 < -32768) cx100 = -32768; if (cx100 > 32767) cx100 = 32767;
      if (ox100 < 0) ox100 = 0; if (ox100 > 65535) ox100 = 65535;
      rtdC_x100[0]   = (int16_t)cx100;
      rtdOhm_x100[0] = (uint16_t)ox100;
      rtdFault[0]    = (uint16_t)f;
      mb.Ireg(IREG_RTD_C_BASE + 0, (int16_t)rtdC_x100[0]);
      mb.Ireg(IREG_RTD_OHM_BASE + 0, (uint16_t)rtdOhm_x100[0]);
      mb.Ireg(IREG_RTD_FAULT_BASE + 0, (uint16_t)rtdFault[0]);
      mb.setIsts(ISTS_RTD_BASE + 0, (f != 0));
    }
    // RTD2
    {
      uint16_t rtd = rtd2.readRTD();
      float ohms = (float)rtd * RTD_REF_OHMS / 32768.0f;
      float tC   = rtd2.temperature(RTD_NOMINAL_OHMS, RTD_REF_OHMS);
      uint8_t f  = rtd2.readFault(); if (f) rtd2.clearFault();
      int32_t cx100 = lroundf(tC * 100.0f);
      int32_t ox100 = lroundf(ohms * 100.0f);
      if (cx100 < -32768) cx100 = -32768; if (cx100 > 32767) cx100 = 32767;
      if (ox100 < 0) ox100 = 0; if (ox100 > 65535) ox100 = 65535;
      rtdC_x100[1]   = (int16_t)cx100;
      rtdOhm_x100[1] = (uint16_t)ox100;
      rtdFault[1]    = (uint16_t)f;
      mb.Ireg(IREG_RTD_C_BASE + 1, (int16_t)rtdC_x100[1]);
      mb.Ireg(IREG_RTD_OHM_BASE + 1, (uint16_t)rtdOhm_x100[1]);
      mb.Ireg(IREG_RTD_FAULT_BASE + 1, (uint16_t)rtdFault[1]);
      mb.setIsts(ISTS_RTD_BASE + 1, (f != 0));
    }

    // ---- PID compute (4 controllers) ----
    uint16_t lastPidOut_mV_snapshot[4]; // to resolve SP from PID_OUT safely
    for (int i=0;i<4;i++) lastPidOut_mV_snapshot[i] = pid[i].last_out_mv;

    for (uint8_t i=0;i<4;i++) {
      PidState &p = pid[i];

      // PV
      uint16_t pv_mv = pid_read_pv(p.pv_type, p.pv_idx, ai_mV, rtdC_x100);

      // SP
      uint16_t sp_mv = pid_resolve_sp(i, p.sp_type, p.sp_static, p.sp_idx, lastPidOut_mV_snapshot);
      if (sp_mv > 10000) sp_mv = 10000;

      // Compute OUT
      uint16_t out_mv = (p.mode == 1)
        ? pid_step_compute(p, pv_mv, max(dt_s, 0.001f), sp_mv, i)
        : clamp_u16(p.man_mv, p.out_min_mv, p.out_max_mv);

      p.last_out_mv = out_mv;

      // Save for trend (latest)
      lastPidPV_mV[i]  = pv_mv;
      lastPidSP_mV[i]  = sp_mv;
      lastPidOUT_mV[i] = out_mv;

      // Route output
      switch (p.route) {
        case 1: { // AO0
          uint16_t code = mv_to_dac_code(out_mv);
          dac_raw[0] = code; dac0.setVoltage(code, false);
          mb.Ireg(IREG_DAC_MV_BASE + 0, out_mv);
          mb.Hreg(HREG_DAC0_RAW, code);
          break;
        }
        case 2: { // AO1
          uint16_t code = mv_to_dac_code(out_mv);
          dac_raw[1] = code; dac1.setVoltage(code, false);
          mb.Ireg(IREG_DAC_MV_BASE + 1, out_mv);
          mb.Hreg(HREG_DAC1_RAW, code);
          break;
        }
        case 10: case 11: case 12: case 13: { // OUT_VAR0..3
          uint8_t vi = (uint8_t)(p.route - 10);
          outVar_mV[vi] = out_mv;
          mb.Ireg(IREG_OUTVAR_BASE + vi, out_mv);
          break;
        }
        default: break; // None
      }
    }

    // WebSerial echoes
    sendSamplesEcho(rawArr, ai_mV);
    // RTD echo already prepared
    sendRTDEcho(rtdC_x100, rtdOhm_x100, rtdFault);

    // ---- TREND: every 5 seconds append snapshot ----
    if (now - lastTrendSave >= TREND_INTERVAL_MS) {
      lastTrendSave = now;
      TrendRecord tr{};
      tr.ts_ms = now;
      for (int i=0;i<4;i++) tr.ai_mV[i] = ai_mV[i];
      tr.rtdC_x100[0] = rtdC_x100[0];
      tr.rtdC_x100[1] = rtdC_x100[1];
      // AO field mV derived from DAC raw codes
      tr.ao_mV[0] = (uint16_t)lroundf((float)dac_raw[0] * (float)DAC_FIELD_VREF_MV / 4095.0f);
      tr.ao_mV[1] = (uint16_t)lroundf((float)dac_raw[1] * (float)DAC_FIELD_VREF_MV / 4095.0f);
      for (int i=0;i<4;i++){ tr.pidPV[i]  = lastPidPV_mV[i]; }
      for (int i=0;i<4;i++){ tr.pidSP[i]  = lastPidSP_mV[i]; }
      for (int i=0;i<4;i++){ tr.pidOUT[i] = lastPidOUT_mV[i]; }
      trend_append(tr); // also emits TrendAppend for UI
    }
  }

  // Drive LEDs
  JSONVar ledStateList;
  for (int i=0;i<4;i++) {
    bool activeFromSource = evalLedSource(ledCfg[i].source);
    bool active = (activeFromSource || ledManual[i]);
    bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase);
    setLedPhys(i, phys);
    ledStateList[i] = phys;
  }

  // Periodic status echo
  if (now - lastSend >= sendInterval) {
    lastSend = now;
    WebSerial.check();
    sendStatusEcho();
    JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i];
    WebSerial.send("ButtonStateList", btnState);
    WebSerial.send("LedStateList", ledStateList);
  }
}
