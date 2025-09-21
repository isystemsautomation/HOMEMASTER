// ==== ENM-223-R1 (RP2350/RP2040) ====
// ATM90E32 via SPI1 (NO external library) + 2x Relays on GPIO0/1
// WebSerial UI (REAL VALUES ONLY), Modbus RTU (scaled integers), LittleFS persistence
// Buttons: GPIO22..25, LEDs: GPIO18..21
// Per-relay Button Override (R1/R2), LED alarm sources
//
// NEW (2025-09):
// - 1 Hz UI push: LED/Relay/Button config, Meter Options, Alarms, Calibration, Live Meter (REAL VALUES ONLY)
// - UI exposes NO engineering scales or raw counts
// - Modbus exposes scaled integers only (no floats)
// - Per-phase + total energy counters from ATM90E32 (AP,AN,RP,RN,S)
// ============================================================================

struct PersistConfig;
struct AlarmRule;
struct MetricsSnapshot;

#include <Arduino.h>
#include <SPI.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <math.h>
#include <limits>

// ================== Hardware ==================
constexpr uint8_t LED_PINS[4]    = {18,19,20,21};
constexpr uint8_t BUTTON_PINS[4] = {22,23,24,25};
constexpr uint8_t RELAY_PINS[2]  = {0,1};

constexpr bool LED_ACTIVE_LOW        = false;
constexpr bool BUTTON_USES_PULLUP    = false; // pressed == HIGH if false
constexpr bool RELAY_ACTIVE_LOW_DFLT = false;

// ===== ATM90E32 on SPI1 (NO LIB) =====
#define MCM_SPI SPI1
constexpr uint8_t PIN_SPI_SCK  = 10;
constexpr uint8_t PIN_SPI_MOSI = 11;
constexpr uint8_t PIN_SPI_MISO = 12;
constexpr uint8_t PIN_ATM_CS   = 13;
constexpr uint8_t PIN_PM1 = 2;
constexpr uint8_t PIN_PM0 = 3;

constexpr bool     CS_ACTIVE_HIGH = false;
constexpr uint8_t  ATM_SPI_MODE   = SPI_MODE0;
constexpr uint32_t SPI_HZ         = 200000;

inline void csAssert()  { digitalWrite(PIN_ATM_CS, CS_ACTIVE_HIGH ? HIGH : LOW); }
inline void csRelease() { digitalWrite(PIN_ATM_CS, CS_ACTIVE_HIGH ? LOW  : HIGH); }

// ================== Modbus / RS-485 ==================
constexpr int TX2 = 4;
constexpr int RX2 = 5;
constexpr int TxenPin = -1;     // not used
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== Buttons & LEDs ==================
struct LedCfg   { uint8_t mode; uint8_t source; }; // mode: 0 steady, 1 blink
struct ButtonCfg{ uint8_t action; };

LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool      ledManual[4] = {false,false,false,false};
bool      ledPhys[4]   = {false,false,false,false};

// Buttons runtime
constexpr unsigned long BTN_DEBOUNCE_MS = 30;
constexpr unsigned long BTN_LONG_MS     = 3000; // 3s
bool buttonState[4]      = {false,false,false,false};
bool buttonPrev[4]       = {false,false,false,false};
unsigned long btnChangeAt[4] = {0,0,0,0};
unsigned long btnPressAt[4]  = {0,0,0,0};
bool btnLongDone[4]      = {false,false,false,false};

// ---- Button override (per-relay) ----
bool buttonOverrideMode[2]  = {false,false};
bool buttonOverrideState[2] = {false,false};

// Relays (logical state; true = ON)
bool relayState[2] = {false,false};

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
constexpr unsigned long sendInterval   = 1000; // 1 Hz UI push
unsigned long lastBlinkToggle = 0;
constexpr unsigned long blinkPeriodMs  = 500;
bool blinkPhase = false;
unsigned long lastSample = 0;

// ================== Persisted settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;
uint16_t sample_ms    = 200;   // meter poll period

// ATM90E32 options (persisted)
uint16_t atm_lineFreqHz = 50;  // 50/60
uint8_t  atm_sumModeAbs = 1;   // affects MMode0
uint16_t atm_ucal       = 25256; // default Ugain (used by vSag calc)

// ===== Per-phase calibration (persisted) =====
uint16_t cal_Ugain[3]  = {25256,25256,25256}; // A,B,C
uint16_t cal_Igain[3]  = {0,0,0};             // A,B,C
int16_t  cal_Uoffset[3]= {0,0,0};             // A,B,C
int16_t  cal_Ioffset[3]= {0,0,0};             // A,B,C

// ===== Internal fixed conversions (not exposed) =====
uint32_t p_scale_mW_per_lsb            = 1;
uint32_t q_scale_mvar_per_lsb_x1000    = 1;
uint32_t s_scale_mva_per_lsb_x1000     = 1;

// ATM90E32 energy scales (k-units/count, in 1e-5)
uint32_t e_ap_kWh_per_cnt_x1e5   = 1;
uint32_t e_an_kWh_per_cnt_x1e5   = 1;
uint32_t e_rp_kvarh_per_cnt_x1e5 = 1;
uint32_t e_rn_kvarh_per_cnt_x1e5 = 1;
uint32_t e_s_kVAh_per_cnt_x1e5   = 1;

// Persisted relay polarity & defaults
uint8_t  relay_active_low = RELAY_ACTIVE_LOW_DFLT;
bool     relay_default[2] = {false,false};

// ================== ALARMS (config + runtime) ==================
enum : uint8_t { CH_L1=0, CH_L2, CH_L3, CH_TOT, CH_COUNT };
enum : uint8_t { AK_ALARM=0, AK_WARNING, AK_EVENT, AK_COUNT };

enum AlarmMetric : uint8_t {
  AM_VOLTAGE=0,   // Urms (0.01 V)
  AM_CURRENT,     // Irms (0.001 A)
  AM_P_ACTIVE,    // W
  AM_Q_REACTIVE,  // var
  AM_S_APPARENT,  // VA
  AM_FREQ         // (0.01 Hz) ch ignored
};

struct AlarmRule { bool enabled; int32_t min; int32_t max; uint8_t metric; };
struct AlarmRuntime { bool conditionNow; bool active; bool latched; };
struct ChannelAlarmCfg { bool ackRequired; AlarmRule rule[AK_COUNT]; };

ChannelAlarmCfg g_alarmCfg[CH_COUNT];
AlarmRuntime    g_alarmRt [CH_COUNT][AK_COUNT];

// ====== Relay mode / source ======
enum : uint8_t { RM_NONE=0, RM_MODBUS=1, RM_ALARM=2 };
struct RelayAlarmSrc { uint8_t ch; uint8_t kindsMask; }; // bit0=Alarm,1=Warn,2=Event
uint8_t       relay_mode[2]      = { RM_MODBUS, RM_MODBUS };
RelayAlarmSrc relay_alarm_src[2] = { {CH_TOT, 0b001}, {CH_TOT, 0b001} };

// ================== Persistence (config) ==================
struct PersistConfig {
  uint32_t magic;       // 'ENM2'
  uint16_t version;
  uint16_t size;

  // Modbus
  uint8_t  mb_address;
  uint32_t mb_baud;

  // App timing
  uint16_t sample_ms;

  // LEDs & Buttons
  LedCfg   ledCfg[4];
  ButtonCfg buttonCfg[4];

  // Relays
  uint8_t  relay_active_low;
  uint8_t  relay_default0;
  uint8_t  relay_default1;

  // ATM90E32 options
  uint16_t atm_lineFreqHz;
  uint8_t  atm_sumModeAbs;
  uint16_t atm_ucal;

  // Per-phase calibration
  uint16_t cal_Ugain[3];
  uint16_t cal_Igain[3];
  int16_t  cal_Uoffset[3];
  int16_t  cal_Ioffset[3];

  // Internal scales (kept for compatibility, not exposed)
  uint32_t p_scale_mW_per_lsb;
  uint32_t q_scale_mvar_per_lsb_x1000;
  uint32_t s_scale_mva_per_lsb_x1000;

  uint32_t e_ap_kWh_per_cnt_x1e5;
  uint32_t e_an_kWh_per_cnt_x1e5;
  uint32_t e_rp_kvarh_per_cnt_x1e5;
  uint32_t e_rn_kvarh_per_cnt_x1e5;
  uint32_t e_s_kVAh_per_cnt_x1e5;

  // Alarms
  uint8_t  alarm_ack_required[CH_COUNT];
  struct PackedRule { uint8_t enabled; int32_t min; int32_t max; uint8_t metric; } alarm_rules[CH_COUNT][AK_COUNT];

  // Relays
  uint8_t  relay_mode0;
  uint8_t  relay_mode1;
  uint8_t  relay_alarm_ch0;
  uint8_t  relay_alarm_ch1;
  uint8_t  relay_alarm_mask0;
  uint8_t  relay_alarm_mask1;

  uint32_t crc32;
} __attribute__((packed));

constexpr uint32_t CFG_MAGIC   = 0x324D4E45UL; // 'ENM2'
constexpr uint16_t CFG_VERSION = 0x0008;
static const char* CFG_PATH    = "/enm223.bin";

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
constexpr uint32_t  CFG_AUTOSAVE_MS = 1200;

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

inline bool readPressed(int i){
#if BUTTON_USES_PULLUP
  return digitalRead(BUTTON_PINS[i]) == LOW;   // pressed = LOW
#else
  return digitalRead(BUTTON_PINS[i]) == HIGH;  // pressed = HIGH
#endif
}

// ================== Defaults ==================
static inline void setAlarmDefaults() {
  const AlarmRule defAlarm = {true, 0, 100000, (uint8_t)AM_S_APPARENT};
  const AlarmRule defWarn  = {true, 0, 120000, (uint8_t)AM_S_APPARENT};
  const AlarmRule defEvent = {true, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), (uint8_t)AM_S_APPARENT};
  for (int ch=0; ch<CH_COUNT; ++ch) {
    g_alarmCfg[ch].ackRequired = false;
    g_alarmCfg[ch].rule[AK_ALARM]   = defAlarm;
    g_alarmCfg[ch].rule[AK_WARNING] = defWarn;
    g_alarmCfg[ch].rule[AK_EVENT]   = defEvent;
    for (int k=0;k<AK_COUNT;++k) g_alarmRt[ch][k] = {false,false,false};
  }
}

void setDefaults() {
  g_mb_address   = 3;
  g_mb_baud      = 19200;
  sample_ms      = 200;

  for (int i=0;i<4;i++) {
    ledCfg[i] = {0,0};
    buttonCfg[i].action = 0;
    ledManual[i] = false;
  }

  relay_active_low = RELAY_ACTIVE_LOW_DFLT;
  relay_default[0] = false;
  relay_default[1] = false;

  relay_mode[0] = RM_MODBUS; relay_mode[1] = RM_MODBUS;
  relay_alarm_src[0] = { CH_TOT, 0b001 };
  relay_alarm_src[1] = { CH_TOT, 0b001 };

  atm_lineFreqHz = 50;
  atm_sumModeAbs = 1;
  atm_ucal       = 25256;

  for (int i=0;i<3;i++){
    cal_Ugain[i]   = atm_ucal;
    cal_Igain[i]   = 0;
    cal_Uoffset[i] = 0;
    cal_Ioffset[i] = 0;
  }

  p_scale_mW_per_lsb         = 1;
  q_scale_mvar_per_lsb_x1000 = 1;
  s_scale_mva_per_lsb_x1000  = 1;

  e_ap_kWh_per_cnt_x1e5   = 1;
  e_an_kWh_per_cnt_x1e5   = 1;
  e_rp_kvarh_per_cnt_x1e5 = 1;
  e_rn_kvarh_per_cnt_x1e5 = 1;
  e_s_kVAh_per_cnt_x1e5   = 1;

  setAlarmDefaults();

  buttonOverrideMode[0] = buttonOverrideMode[1] = false;
  buttonOverrideState[0] = buttonOverrideState[1] = false;
}

void captureToPersist(PersistConfig &pc) {
  pc = {};
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);

  pc.mb_address   = g_mb_address;
  pc.mb_baud      = g_mb_baud;
  pc.sample_ms    = sample_ms;

  memcpy(pc.ledCfg,   ledCfg,   sizeof(ledCfg));
  memcpy(pc.buttonCfg,buttonCfg,sizeof(buttonCfg));

  pc.relay_active_low = relay_active_low;
  pc.relay_default0   = relay_default[0];
  pc.relay_default1   = relay_default[1];

  pc.atm_lineFreqHz = atm_lineFreqHz;
  pc.atm_sumModeAbs = atm_sumModeAbs;
  pc.atm_ucal       = atm_ucal;

  memcpy(pc.cal_Ugain,   cal_Ugain,   sizeof(cal_Ugain));
  memcpy(pc.cal_Igain,   cal_Igain,   sizeof(cal_Igain));
  memcpy(pc.cal_Uoffset, cal_Uoffset, sizeof(cal_Uoffset));
  memcpy(pc.cal_Ioffset, cal_Ioffset, sizeof(cal_Ioffset));

  pc.p_scale_mW_per_lsb         = p_scale_mW_per_lsb;
  pc.q_scale_mvar_per_lsb_x1000 = q_scale_mvar_per_lsb_x1000;
  pc.s_scale_mva_per_lsb_x1000  = s_scale_mva_per_lsb_x1000;

  pc.e_ap_kWh_per_cnt_x1e5   = e_ap_kWh_per_cnt_x1e5;
  pc.e_an_kWh_per_cnt_x1e5   = e_an_kWh_per_cnt_x1e5;
  pc.e_rp_kvarh_per_cnt_x1e5 = e_rp_kvarh_per_cnt_x1e5;
  pc.e_rn_kvarh_per_cnt_x1e5 = e_rn_kvarh_per_cnt_x1e5;
  pc.e_s_kVAh_per_cnt_x1e5   = e_s_kVAh_per_cnt_x1e5;

  for (int ch=0; ch<CH_COUNT; ++ch) {
    pc.alarm_ack_required[ch] = g_alarmCfg[ch].ackRequired ? 1 : 0;
    for (int k=0;k<AK_COUNT;++k) {
      pc.alarm_rules[ch][k].enabled = g_alarmCfg[ch].rule[k].enabled ? 1 : 0;
      pc.alarm_rules[ch][k].min     = g_alarmCfg[ch].rule[k].min;
      pc.alarm_rules[ch][k].max     = g_alarmCfg[ch].rule[k].max;
      pc.alarm_rules[ch][k].metric  = g_alarmCfg[ch].rule[k].metric;
    }
  }

  pc.relay_mode0       = relay_mode[0];
  pc.relay_mode1       = relay_mode[1];
  pc.relay_alarm_ch0   = relay_alarm_src[0].ch;
  pc.relay_alarm_ch1   = relay_alarm_src[1].ch;
  pc.relay_alarm_mask0 = relay_alarm_src[0].kindsMask;
  pc.relay_alarm_mask1 = relay_alarm_src[1].kindsMask;

  pc.crc32 = 0;
  pc.crc32 = crc32_update(0, reinterpret_cast<const uint8_t*>(&pc), sizeof(PersistConfig));
}
bool applyFromPersist(const PersistConfig &pc) {
  if (pc.magic != CFG_MAGIC || pc.version != CFG_VERSION || pc.size != sizeof(PersistConfig)) return false;
  PersistConfig tmp = pc; uint32_t crc = tmp.crc32; tmp.crc32 = 0;
  if (crc32_update(0, reinterpret_cast<const uint8_t*>(&tmp), sizeof(PersistConfig)) != crc) return false;

  g_mb_address   = pc.mb_address;
  g_mb_baud      = pc.mb_baud;
  sample_ms      = pc.sample_ms;

  memcpy(ledCfg,    pc.ledCfg,    sizeof(ledCfg));
  memcpy(buttonCfg, pc.buttonCfg, sizeof(buttonCfg));

  relay_active_low = pc.relay_active_low;
  relay_default[0] = pc.relay_default0;
  relay_default[1] = pc.relay_default1;

  atm_lineFreqHz = pc.atm_lineFreqHz;
  atm_sumModeAbs = pc.atm_sumModeAbs;
  atm_ucal       = pc.atm_ucal;

  memcpy(cal_Ugain,   pc.cal_Ugain,   sizeof(cal_Ugain));
  memcpy(cal_Igain,   pc.cal_Igain,   sizeof(cal_Igain));
  memcpy(cal_Uoffset, pc.cal_Uoffset, sizeof(cal_Uoffset));
  memcpy(cal_Ioffset, pc.cal_Ioffset, sizeof(cal_Ioffset));

  p_scale_mW_per_lsb         = pc.p_scale_mW_per_lsb;
  q_scale_mvar_per_lsb_x1000 = pc.q_scale_mvar_per_lsb_x1000;
  s_scale_mva_per_lsb_x1000  = pc.s_scale_mva_per_lsb_x1000;

  e_ap_kWh_per_cnt_x1e5   = pc.e_ap_kWh_per_cnt_x1e5;
  e_an_kWh_per_cnt_x1e5   = pc.e_an_kWh_per_cnt_x1e5;
  e_rp_kvarh_per_cnt_x1e5 = pc.e_rp_kvarh_per_cnt_x1e5;
  e_rn_kvarh_per_cnt_x1e5 = pc.e_rn_kvarh_per_cnt_x1e5;
  e_s_kVAh_per_cnt_x1e5   = pc.e_s_kVAh_per_cnt_x1e5;

  setAlarmDefaults();
  for (int ch=0; ch<CH_COUNT; ++ch) {
    g_alarmCfg[ch].ackRequired = pc.alarm_ack_required[ch] ? true : false;
    for (int k=0;k<AK_COUNT;++k) {
      g_alarmCfg[ch].rule[k].enabled = pc.alarm_rules[ch][k].enabled ? true : false;
      g_alarmCfg[ch].rule[k].min     = pc.alarm_rules[ch][k].min;
      g_alarmCfg[ch].rule[k].max     = pc.alarm_rules[ch][k].max;
      g_alarmCfg[ch].rule[k].metric  = pc.alarm_rules[ch][k].metric;
      g_alarmRt[ch][k] = {false,false,false};
    }
  }

  relay_mode[0] = (pc.relay_mode0 <= RM_ALARM) ? pc.relay_mode0 : RM_MODBUS;
  relay_mode[1] = (pc.relay_mode1 <= RM_ALARM) ? pc.relay_mode1 : RM_MODBUS;
  relay_alarm_src[0].ch        = (pc.relay_alarm_ch0 < CH_COUNT) ? pc.relay_alarm_ch0 : CH_TOT;
  relay_alarm_src[1].ch        = (pc.relay_alarm_ch1 < CH_COUNT) ? pc.relay_alarm_ch1 : CH_TOT;
  relay_alarm_src[0].kindsMask = pc.relay_alarm_mask0 & 0b111;
  relay_alarm_src[1].kindsMask = pc.relay_alarm_mask1 & 0b111;

  for (int i=0;i<4;i++) ledManual[i] = false;
  buttonOverrideMode[0] = buttonOverrideMode[1] = false;
  buttonOverrideState[0] = buttonOverrideState[1] = false;

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

// ================== ATM90E32 REG MAP (subset) ==================
#define WRITE 0
#define READ  1
#define MeterEn       0x00
#define SagPeakDetCfg 0x05
#define ZXConfig      0x07
#define SagTh         0x08
#define FreqLoTh      0x0C
#define FreqHiTh      0x0D
#define SoftReset     0x70
#define EMMState0     0x71
#define EMMState1     0x72
#define EMMIntState0  0x73
#define EMMIntState1  0x74
#define EMMIntEn0     0x75
#define EMMIntEn1     0x76
#define LastSPIData   0x78
#define CRCErrStatus  0x79
#define CfgRegAccEn   0x7F
#define PLconstH 0x31
#define PLconstL 0x32
#define MMode0   0x33
#define MMode1   0x34
#define PStartTh 0x35
#define QStartTh 0x36
#define SStartTh 0x37
#define PPhaseTh 0x38
#define QPhaseTh 0x39
#define SPhaseTh 0x3A
#define UgainA   0x61
#define IgainA   0x62
#define UoffsetA 0x63
#define IoffsetA 0x64
#define UgainB   0x65
#define IgainB   0x66
#define UoffsetB 0x67
#define IoffsetB 0x68
#define UgainC   0x69
#define IgainC   0x6A
#define UoffsetC 0x6B
#define IoffsetC 0x6C
// Energy (total + per-phase)
#define APenergyT 0x80
#define APenergyA 0x81
#define APenergyB 0x82
#define APenergyC 0x83
#define ANenergyT 0x84
#define ANenergyA 0x85
#define ANenergyB 0x86
#define ANenergyC 0x87
#define RPenergyT 0x88
#define RPenergyA 0x89
#define RPenergyB 0x8A
#define RPenergyC 0x8B
#define RNenergyT 0x8C
#define RNenergyA 0x8D
#define RNenergyB 0x8E
#define RNenergyC 0x8F
#define SAenergyT 0x90
#define SAenergyA 0x91
#define SAenergyB 0x92
#define SAenergyC 0x93
// Averages / live
#define PmeanT  0xB0
#define PmeanA  0xB1
#define PmeanB  0xB2
#define PmeanC  0xB3
#define QmeanT  0xB4
#define QmeanA  0xB5
#define QmeanB  0xB6
#define QmeanC  0xB7
#define SmeanT  0xB8
#define SmeanA  0xB9
#define SmeanB  0xBA
#define SmeanC  0xBB
#define PFmeanT 0xBC
#define PFmeanA 0xBD
#define PFmeanB 0xBE
#define PFmeanC 0xBF
#define PmeanTLSB 0xC0
#define PmeanALSB 0xC1
#define PmeanBLSB 0xC2
#define PmeanCLSB 0xC3
#define QmeanTLSB 0xC4
#define QmeanALSB 0xC5
#define QmeanBLSB 0xC6
#define QmeanCLSB 0xC7
#define SAmeanTLSB 0xC8
#define SmeanALSB  0xC9
#define SmeanBLSB  0xCA
#define SmeanCLSB  0xCB
#define UrmsA     0xD9
#define UrmsB     0xDA
#define UrmsC     0xDB
#define IrmsA     0xDD
#define IrmsB     0xDE
#define IrmsC     0xDF
#define UrmsALSB  0xE9
#define UrmsBLSB  0xEA
#define UrmsCLSB  0xEB
#define IrmsALSB  0xED
#define IrmsBLSB  0xEE
#define IrmsCLSB  0xEF
#define Freq      0xF8
#define PAngleA   0xF9
#define PAngleB   0xFA
#define PAngleC   0xFB
#define Temp      0xFC

// ================== Low-level SPI ==================
static uint16_t atm90_transfer(uint8_t rw, uint16_t reg, uint16_t val)
{
  const uint16_t data_swapped = (uint16_t)((val >> 8) | (val << 8));
  const uint16_t addr = reg | (rw ? 0x8000 : 0x0000);
  const uint16_t addr_swapped = (uint16_t)((addr >> 8) | (addr << 8));

  SPISettings settings(SPI_HZ, MSBFIRST, ATM_SPI_MODE);

  MCM_SPI.beginTransaction(settings);
  csAssert();
  delayMicroseconds(10);

  const uint8_t *pa = reinterpret_cast<const uint8_t*>(&addr_swapped);
  MCM_SPI.transfer(pa[0]);
  MCM_SPI.transfer(pa[1]);

  delayMicroseconds(4);

  uint16_t out = 0;
  if (rw) {
    uint8_t b0 = MCM_SPI.transfer(0x00);
    uint8_t b1 = MCM_SPI.transfer(0x00);
    const uint16_t raw = (uint16_t)((uint16_t)b0 | ((uint16_t)b1 << 8));
    out = (uint16_t)((raw >> 8) | (raw << 8));
  } else {
    const uint8_t *pd = reinterpret_cast<const uint8_t*>(&data_swapped);
    MCM_SPI.transfer(pd[0]);
    MCM_SPI.transfer(pd[1]);
    out = val;
  }

  csRelease();
  delayMicroseconds(10);
  MCM_SPI.endTransaction();
  return out;
}
static inline uint16_t atm90_read16(uint16_t reg) { return atm90_transfer(READ, reg, 0xFFFF); }
static inline void     atm90_write16(uint16_t reg, uint16_t val) { atm90_transfer(WRITE, reg, val); }

static inline int32_t read_power32(uint16_t regH, uint16_t regLSB)
{
  int16_t  h = (int16_t)atm90_read16(regH);
  uint16_t l = atm90_read16(regLSB);
  int32_t val24 = ((int32_t)h << 8) | ((l >> 8) & 0xFF);
  if (val24 & 0x00800000) val24 |= 0xFF000000;
  return val24;
}

static inline double read_rms_like(uint16_t regH, uint16_t regLSB, double sH, double sLb)
{
  const uint16_t h = atm90_read16(regH);
  const uint16_t l = atm90_read16(regLSB);
  return (h * sH) + (((l >> 8) & 0xFF) * sLb);
}

// ===== Simple init =====
static inline uint8_t gainCode(uint8_t g){ if(g==1)return 0; if(g==2)return 1; if(g==4)return 2; return 1; }

static void apply_calibration_to_chip() {
  atm90_write16(UgainA,   cal_Ugain[0]); atm90_write16(UgainB,   cal_Ugain[1]); atm90_write16(UgainC,   cal_Ugain[2]);
  atm90_write16(IgainA,   cal_Igain[0]); atm90_write16(IgainB,   cal_Igain[1]); atm90_write16(IgainC,   cal_Igain[2]);
  atm90_write16(UoffsetA, (uint16_t)cal_Uoffset[0]); atm90_write16(UoffsetB, (uint16_t)cal_Uoffset[1]); atm90_write16(UoffsetC, (uint16_t)cal_Uoffset[2]);
  atm90_write16(IoffsetA, (uint16_t)cal_Ioffset[0]); atm90_write16(IoffsetB, (uint16_t)cal_Ioffset[1]); atm90_write16(IoffsetC, (uint16_t)cal_Ioffset[2]);
}

static void atm90_init()
{
  pinMode(PIN_PM1, OUTPUT); pinMode(PIN_PM0, OUTPUT);
  digitalWrite(PIN_PM1, HIGH); digitalWrite(PIN_PM0, HIGH);
  delay(5);

  atm90_write16(SoftReset, 0x789A);
  delay(5);
  atm90_write16(CfgRegAccEn, 0x55AA);
  atm90_write16(MeterEn, 0x0001);

  uint16_t sagV, FreqHiThresh, FreqLoThresh;
  if (atm_lineFreqHz == 60){ sagV=90;  FreqHiThresh=6100; FreqLoThresh=5900; }
  else                     { sagV=190; FreqHiThresh=5100; FreqLoThresh=4900; }
  const uint16_t vSagTh = (uint16_t)((sagV * 100.0 * sqrt(2.0)) / (2.0 * (atm_ucal / 32768.0)));

  atm90_write16(SagPeakDetCfg, 0x143F);
  atm90_write16(SagTh,        vSagTh);
  atm90_write16(FreqHiTh,     FreqHiThresh);
  atm90_write16(FreqLoTh,     FreqLoThresh);
  atm90_write16(EMMIntEn0, 0xB76F);
  atm90_write16(EMMIntEn1, 0xDDFD);
  atm90_write16(EMMIntState0, 0x0001);
  atm90_write16(EMMIntState1, 0x0001);
  atm90_write16(ZXConfig, 0xD654);

  uint16_t m0 = 0x019D;
  if (atm_lineFreqHz == 60) m0 |= (1u<<12); else m0 &= ~(1u<<12);
  m0 &= ~(0b11u<<3);
  m0 |= ((atm_sumModeAbs ? 0b11u : 0b00u) << 3);
  m0 &= ~0b111u; m0 |= 0b101u;

  const uint8_t gIA=1,gIB=1,gIC=1;
  uint8_t m1=0; m1|=(gainCode(gIA)<<0); m1|=(gainCode(gIB)<<2); m1|=(gainCode(gIC)<<4);

  atm90_write16(PLconstH, 0x0861);
  atm90_write16(PLconstL, 0xC468);
  atm90_write16(MMode0, m0);
  atm90_write16(MMode1, m1);
  atm90_write16(PStartTh, 0x1D4C);
  atm90_write16(QStartTh, 0x1D4C);
  atm90_write16(SStartTh, 0x1D4C);
  atm90_write16(PPhaseTh, 0x02EE);
  atm90_write16(QPhaseTh, 0x02EE);
  atm90_write16(SPhaseTh, 0x02EE);

  apply_calibration_to_chip();

  atm90_write16(CfgRegAccEn, 0x0000);
}

// ================== Modbus map (scaled ints, no floats) ==================
enum : uint16_t {
  IREG_URMS_BASE    = 100,  // 100..102: Urms L1..L3 in 0.01 V (u16)
  IREG_IRMS_BASE    = 110,  // 110..112: Irms L1..L3 in 0.001 A (u16)

  // P/Q/S in real units (W/var/VA), s32 pairs (Hi,Lo)
  IREG_P_BASE       = 200,  // 200..207 : L1..L3,Tot (8 regs)
  IREG_Q_BASE       = 210,  // 210..217
  IREG_S_BASE       = 220,  // 220..227

  IREG_PF_BASE      = 240,  // 240..242 PF L1..L3 ×1000 (s16)
  IREG_PF_TOT       = 243,  // 243 PF total ×1000 (s16)
  IREG_ANGLE_BASE   = 244,  // 244..246 Phase angle L1..L3 ×10 (0.1°) s16
  IREG_FREQ_X100    = 250,  // Hz ×100 (u16)
  IREG_TEMP_C       = 251,  // °C s16

  // Energies (Wh/varh/VAh) u32 pairs — per-phase and totals
  // Active + (AP)
  IREG_E_AP_A_Wh    = 300,  // 300/301
  IREG_E_AP_B_Wh    = 302,  // 302/303
  IREG_E_AP_C_Wh    = 304,  // 304/305
  IREG_E_AP_T_Wh    = 306,  // 306/307
  // Active - (AN)
  IREG_E_AN_A_Wh    = 308,  // 308/309
  IREG_E_AN_B_Wh    = 310,  // 310/311
  IREG_E_AN_C_Wh    = 312,  // 312/313
  IREG_E_AN_T_Wh    = 314,  // 314/315
  // Reactive + (RP)
  IREG_E_RP_A_varh  = 316,  // 316/317
  IREG_E_RP_B_varh  = 318,
  IREG_E_RP_C_varh  = 320,
  IREG_E_RP_T_varh  = 322,
  // Reactive - (RN)
  IREG_E_RN_A_varh  = 324,
  IREG_E_RN_B_varh  = 326,
  IREG_E_RN_C_varh  = 328,
  IREG_E_RN_T_varh  = 330,
  // Apparent (S)
  IREG_E_S_A_VAh    = 332,
  IREG_E_S_B_VAh    = 334,
  IREG_E_S_C_VAh    = 336,
  IREG_E_S_T_VAh    = 338,

  // Diagnostics
  IREG_EMM0         = 360,
  IREG_EMM1         = 361,
  IREG_INT0         = 362,
  IREG_INT1         = 363,
  IREG_CRCERR       = 364,
  IREG_LASTSPI      = 365,

  // Holding (RW) — minimal; NO scaling HREGs
  HREG_SMPLL_MS     = 400,
  HREG_LFREQ_HZ     = 401,
  HREG_SUMMODE      = 402,
  HREG_UCAL         = 403,

  // Discrete & coils
  ISTS_LED_BASE    = 500,
  ISTS_BTN_BASE    = 520,
  ISTS_RLY_BASE    = 540,
  ISTS_ALARM_BASE  = 560,  // 560..571 (12 inputs): [ch*3 + type]
  COIL_RLY1        = 600,
  COIL_RLY2        = 601,
  COIL_ALARM_ACK_BASE = 610 // 610..613
};

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
void setRelayPhys(uint8_t idx, bool on){
  bool drive = on ^ (relay_active_low != 0);
  digitalWrite(RELAY_PINS[idx], drive ? HIGH : LOW);
  relayState[idx] = on;
  mb.setIsts(ISTS_RLY_BASE + idx, on);
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

// ---- WebSerial echo helpers ----
void sendStatusEcho() {
  modbusStatus["address"] = g_mb_address;
  modbusStatus["baud"]    = g_mb_baud;
  WebSerial.send("status", modbusStatus);
}
JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) { JSONVar o; o["mode"]=ledCfg[i].mode; o["source"]=ledCfg[i].source; arr[i]=o; }
  return arr;
}
JSONVar ButtonConfigListFromCfg() {
  JSONVar arr;
  for (int i=0;i<4;i++) arr[i] = buttonCfg[i].action;
  return arr;
}
JSONVar RelayConfigFromCfg() {
  JSONVar r;
  r["active_low"] = relay_active_low;
  r["def0"] = relay_default[0];
  r["def1"] = relay_default[1];
  JSONVar modes; modes[0]=relay_mode[0]; modes[1]=relay_mode[1];
  r["mode"] = modes;
  JSONVar alarms;
  for (int i=0;i<2;i++){ JSONVar a; a["ch"]=relay_alarm_src[i].ch; a["mask"]=relay_alarm_src[i].kindsMask; alarms[i]=a; }
  r["alarm"] = alarms;
  return r;
}
void echoRelayState() {
  JSONVar rly; rly[0]=relayState[0]; rly[1]=relayState[1];
  WebSerial.send("RelayState", rly);
}

// ===== Alarms helpers =====
static inline int32_t p_raw_to_W(int32_t p_raw){
  int64_t num = (int64_t)p_raw * (int64_t)p_scale_mW_per_lsb;
  return (int32_t)(num / 1000);
}
static inline int32_t q_raw_to_var(int32_t q_raw){
  int64_t num = (int64_t)q_raw * (int64_t)q_scale_mvar_per_lsb_x1000;
  return (int32_t)(num / 1000);
}
static inline int32_t s_raw_to_VA(int32_t s_raw){
  int64_t num = (int64_t)s_raw * (int64_t)s_scale_mva_per_lsb_x1000;
  return (int32_t)(num / 1000);
}
static inline bool out_of_band(int32_t v, const AlarmRule& r){
  if (!r.enabled) return false;
  return (v < r.min) || (v > r.max);
}
static void alarms_ack_channel(uint8_t ch){
  if (ch >= CH_COUNT) return;
  for (int k=0;k<AK_COUNT;++k) {
    if (!g_alarmRt[ch][k].conditionNow) {
      g_alarmRt[ch][k].latched = false;
      g_alarmRt[ch][k].active  = false;
    }
  }
}
static void alarms_publish_cfg(){
  JSONVar alCfg;
  for (int ch=0; ch<CH_COUNT; ++ch) {
    JSONVar chO;
    chO["ack"]= g_alarmCfg[ch].ackRequired;
    for (int k=0;k<AK_COUNT;++k) {
      JSONVar r;
      r["enabled"]= g_alarmCfg[ch].rule[k].enabled;
      r["min"]    = (int32_t)g_alarmCfg[ch].rule[k].min;
      r["max"]    = (int32_t)g_alarmCfg[ch].rule[k].max;
      r["metric"] = (uint8_t)g_alarmCfg[ch].rule[k].metric;
      chO[(int)k]=r;
    }
    alCfg[(int)ch]=chO;
  }
  WebSerial.send("AlarmsCfg", alCfg);
}
static void alarms_publish_state(){
  JSONVar st;
  for (int ch=0; ch<CH_COUNT; ++ch) {
    JSONVar chO;
    for (int k=0;k<AK_COUNT;++k) {
      JSONVar r;
      r["cond"]   = g_alarmRt[ch][k].conditionNow;
      r["active"] = g_alarmRt[ch][k].active;
      r["latched"]= g_alarmRt[ch][k].latched;
      chO[(int)k]=r;
    }
    st[(int)ch]=chO;
  }
  WebSerial.send("AlarmsState", st);
}

// ===== Calibration helpers =====
static JSONVar calib_json_from_cfg(){
  JSONVar arr;
  for (int ph=0; ph<3; ++ph){
    JSONVar o;
    o["Ug"] = (uint32_t)cal_Ugain[ph];
    o["Ig"] = (uint32_t)cal_Igain[ph];
    o["Uo"] = (int32_t)cal_Uoffset[ph];
    o["Io"] = (int32_t)cal_Ioffset[ph];
    arr[ph]=o;
  }
  return arr;
}
static void calib_publish_cfg(){
  WebSerial.send("CalibCfg", calib_json_from_cfg());
}
static bool set_calib_phase_from_obj(int ph, JSONVar obj, bool &changed){
  if (ph < 0 || ph > 2) return false;
  bool any=false;
  if (obj.hasOwnProperty("Ug")) { uint32_t v=(uint32_t)obj["Ug"]; if (v>65535) v=65535; if (cal_Ugain[ph] != (uint16_t)v){ cal_Ugain[ph]=(uint16_t)v; any=true; } }
  if (obj.hasOwnProperty("Ig")) { uint32_t v=(uint32_t)obj["Ig"]; if (v>65535) v=65535; if (cal_Igain[ph] != (uint16_t)v){ cal_Igain[ph]=(uint16_t)v; any=true; } }
  if (obj.hasOwnProperty("Uo")) { int32_t v=(int32_t)obj["Uo"]; if (v<-32768) v=-32768; if (v>32767) v=32767; if (cal_Uoffset[ph] != (int16_t)v){ cal_Uoffset[ph]=(int16_t)v; any=true; } }
  if (obj.hasOwnProperty("Io")) { int32_t v=(int32_t)obj["Io"]; if (v<-32768) v=-32768; if (v>32767) v=32767; if (cal_Ioffset[ph] != (int16_t)v){ cal_Ioffset[ph]=(int16_t)v; any=true; } }
  changed |= any;
  return any;
}

// Snapshot used for alarms (integer eng units)
struct MetricsSnapshot {
  int32_t Urms_cV[3];   // 0.01 V
  int32_t Irms_mA[3];   // 0.001 A
  int32_t P_W[4];       // W L1..L3, total
  int32_t Q_var[4];     // var
  int32_t S_VA[4];      // VA
  int32_t Freq_cHz;     // 0.01 Hz
};

static int32_t pick_metric_value(uint8_t ch, uint8_t metric, const MetricsSnapshot& m) {
  switch ((AlarmMetric)metric) {
    case AM_VOLTAGE:    return (ch<3)? m.Urms_cV[ch] : (m.Urms_cV[0]+m.Urms_cV[1]+m.Urms_cV[2])/3;
    case AM_CURRENT:    return (ch<3)? m.Irms_mA[ch] : (m.Irms_mA[0]+m.Irms_mA[1]+m.Irms_mA[2]);
    case AM_P_ACTIVE:   return m.P_W[ch<3?ch:3];
    case AM_Q_REACTIVE: return m.Q_var[ch<3?ch:3];
    case AM_S_APPARENT: return m.S_VA[ch<3?ch:3];
    case AM_FREQ:       return m.Freq_cHz;
    default:            return 0;
  }
}
static void eval_alarms_with_metrics(const MetricsSnapshot& snap){
  for (int ch=0; ch<CH_COUNT; ++ch) {
    for (int k=0; k<AK_COUNT; ++k) {
      const AlarmRule& rule = g_alarmCfg[ch].rule[k];
      const int32_t v = pick_metric_value(ch, rule.metric, snap);
      const bool cond = out_of_band(v, rule);
      g_alarmRt[ch][k].conditionNow = cond;
      if (g_alarmCfg[ch].ackRequired) {
        if (cond) { g_alarmRt[ch][k].latched = true; g_alarmRt[ch][k].active = true; }
        else { if (!g_alarmRt[ch][k].latched) g_alarmRt[ch][k].active = false; }
      } else {
        g_alarmRt[ch][k].active  = cond;
        g_alarmRt[ch][k].latched = false;
      }
      mb.setIsts(ISTS_ALARM_BASE + (ch*AK_COUNT + k), g_alarmRt[ch][k].active);
    }
  }
}

// ================== UI config JSON (no Scaling) ==================
static JSONVar MeterOptionsJSON() {
  JSONVar mo;
  mo["lineHz"] = atm_lineFreqHz;
  mo["sumAbs"] = atm_sumModeAbs;
  mo["ucal"]   = atm_ucal;
  mo["sample_ms"] = sample_ms;
  return mo;
}
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
  bool changed = false, reinit = false;

  if (type == "meter") {
    JSONVar c = obj["cfg"];
    if (c.hasOwnProperty("lineHz")) {
      uint16_t lf = (uint16_t)constrain((int)c["lineHz"], 50, 60);
      if (lf != atm_lineFreqHz) { atm_lineFreqHz = lf; reinit = true; }
      mb.Hreg(HREG_LFREQ_HZ, atm_lineFreqHz);
      changed = true;
    }
    if (c.hasOwnProperty("sumAbs")) {
      uint8_t  sm = (uint8_t)constrain((int)c["sumAbs"], 0, 1);
      if (sm != atm_sumModeAbs) { atm_sumModeAbs = sm; reinit = true; }
      mb.Hreg(HREG_SUMMODE, atm_sumModeAbs);
      changed = true;
    }
    if (c.hasOwnProperty("ucal")) {
      uint16_t uc = (uint16_t)constrain((int)c["ucal"], 1, 65535);
      atm_ucal = uc;
      mb.Hreg(HREG_UCAL, atm_ucal);
      changed = true;
    }
    if (c.hasOwnProperty("sample_ms")) {
      int smp = (int)c["sample_ms"];
      sample_ms = (uint16_t)constrain(smp, 10, 5000);
      mb.Hreg(HREG_SMPLL_MS, sample_ms);
      changed = true;
    }
    if (reinit) atm90_init();
    if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); WebSerial.send("message", "Meter options updated"); }
  }
  else if (type == "leds") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
      ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 25);
    }
    WebSerial.send("message", "LEDs configuration updated");
    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    cfgDirty = true; lastCfgTouchMs = millis();
  }
  else if (type == "buttons") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      int a = (int)list[i]["action"]; if (a < 0) a = 0; if (a > 8) a = 8;
      buttonCfg[i].action = (uint8_t)a;
    }
    WebSerial.send("message", "Buttons configuration updated");
    WebSerial.send("ButtonConfigList", ButtonConfigListFromCfg());
    cfgDirty = true; lastCfgTouchMs = millis();
  }
  else {
    WebSerial.send("message", String("Unknown Config type: ") + t);
  }
}

// --- Direct relay control via WebSerial ---
void handleRelaysSet(JSONVar v){
  bool changed=false;
  auto canWrite = [&](int idx)->bool{
    return (relay_mode[idx] != RM_ALARM) && !buttonOverrideMode[idx];
  };
  if (v.hasOwnProperty("list") && v["list"].length() >= 2) {
    bool r0 = (bool)v["list"][0];
    bool r1 = (bool)v["list"][1];
    if (canWrite(0) && relayState[0] != r0) { setRelayPhys(0, r0); changed=true; }
    if (canWrite(1) && relayState[1] != r1) { setRelayPhys(1, r1); changed=true; }
  } else if (v.hasOwnProperty("r1") || v.hasOwnProperty("r2")) {
    if (v.hasOwnProperty("r1")) { bool r0=(bool)v["r1"]; if (canWrite(0) && relayState[0]!=r0){ setRelayPhys(0,r0); changed=true; } }
    if (v.hasOwnProperty("r2")) { bool r1=(bool)v["r2"]; if (canWrite(1) && relayState[1]!=r1){ setRelayPhys(1,r1); changed=true; } }
  } else if (v.hasOwnProperty("idx")) {
    int idx = constrain((int)v["idx"], 0, 1);
    if (!canWrite(idx)) {
      WebSerial.send("message","RelaysSet: blocked (ALARM mode or Button Override)");
    } else if (v.hasOwnProperty("toggle") && (bool)v["toggle"]) {
      setRelayPhys(idx, !relayState[idx]); changed=true;
    } else if (v.hasOwnProperty("on")) {
      bool on = (bool)v["on"];
      if (relayState[idx] != on) { setRelayPhys(idx, on); changed=true; }
    }
  } else {
    WebSerial.send("message","RelaysSet: no-op (bad payload)");
  }
  if (changed) {
    mb.Coil(COIL_RLY1, relayState[0]);
    mb.Coil(COIL_RLY2, relayState[1]);
    echoRelayState();
    WebSerial.send("message","Relays updated");
  }
}
void handleRelaysCfg(JSONVar v){
  bool touched=false;
  if (v.hasOwnProperty("active_low")) {
    relay_active_low = (uint8_t)constrain((int)v["active_low"],0,1);
    touched=true;
  }
  if (v.hasOwnProperty("def0")) { relay_default[0] = (bool)v["def0"]; touched=true; }
  if (v.hasOwnProperty("def1")) { relay_default[1] = (bool)v["def1"]; touched=true; }
  if (v.hasOwnProperty("mode") && v["mode"].length() >= 2) {
    for (int i=0;i<2;i++){
      int m = (int)v["mode"][i];
      if (m<RM_NONE) m=RM_NONE; if (m>RM_ALARM) m=RM_ALARM;
      relay_mode[i] = (uint8_t)m;
    }
    touched=true;
  }
  if (v.hasOwnProperty("alarm") && v["alarm"].length() >= 2) {
    for (int i=0;i<2;i++){
      JSONVar a = v["alarm"][i];
      if (a.hasOwnProperty("ch")) {
        int ch = (int)a["ch"]; ch = constrain(ch, 0, CH_COUNT-1);
        relay_alarm_src[i].ch = (uint8_t)ch;
      }
      if (a.hasOwnProperty("mask")) {
        int mk = (int)a["mask"]; mk &= 0b111;
        relay_alarm_src[i].kindsMask = (uint8_t)mk;
      } else if (a.hasOwnProperty("kinds") && a["kinds"].length()>=3){
        uint8_t mk = ((bool)a["kinds"][0]?1:0)
                   | ((bool)a["kinds"][1]?2:0)
                   | ((bool)a["kinds"][2]?4:0);
        relay_alarm_src[i].kindsMask = mk;
      }
    }
    touched=true;
  }
  setRelayPhys(0, relayState[0]);
  setRelayPhys(1, relayState[1]);
  if (touched) { cfgDirty = true; lastCfgTouchMs = millis(); }
  WebSerial.send("message","RelaysCfg updated");
  echoRelayState();
}

// =============== Alarms cfg/ack (WebSerial) ===============
void handleAlarmsCfg(JSONVar cfgArr){
  if (cfgArr.hasOwnProperty("ch") || cfgArr.hasOwnProperty("cfg")) {
    JSONVar v = cfgArr;
    if (!v.hasOwnProperty("ch")) return;
    int ch = constrain((int)v["ch"], 0, CH_COUNT-1);
    JSONVar payload = v.hasOwnProperty("cfg") ? v["cfg"] : v;

    bool changed=false;
    if (payload.hasOwnProperty("ack")) { g_alarmCfg[ch].ackRequired = (bool)payload["ack"]; changed = true; }
    for (int k=0;k<AK_COUNT;++k){
      String key = String(k); if (!payload.hasOwnProperty(key)) continue;
      JSONVar r = payload[key];
      if (r.hasOwnProperty("enabled")) { g_alarmCfg[ch].rule[k].enabled = (bool)r["enabled"]; changed=true; }
      if (r.hasOwnProperty("min"))     { g_alarmCfg[ch].rule[k].min     = (int32_t)r["min"]; changed=true; }
      if (r.hasOwnProperty("max"))     { g_alarmCfg[ch].rule[k].max     = (int32_t)r["max"]; changed=true; }
      if (r.hasOwnProperty("metric"))  {
        int m=(int)r["metric"]; if(m<0)m=0; if(m>5)m=5;
        g_alarmCfg[ch].rule[k].metric=(uint8_t)m; changed=true;
      }
    }
    JSONVar chO; chO["ack"]=g_alarmCfg[ch].ackRequired;
    for (int k=0;k<AK_COUNT;++k){
      JSONVar rr; rr["enabled"]=g_alarmCfg[ch].rule[k].enabled;
      rr["min"]=g_alarmCfg[ch].rule[k].min; rr["max"]=g_alarmCfg[ch].rule[k].max;
      rr["metric"]=g_alarmCfg[ch].rule[k].metric; chO[k]=rr;
    }
    JSONVar one; one[ch]=chO; WebSerial.send("AlarmsCfg", one);
    alarms_publish_state();

    if (changed) {
      if (saveConfigFS()) WebSerial.send("message","AlarmsCfgCh saved");
      else                WebSerial.send("message","ERROR: saving AlarmsCfgCh failed");
    } else {
      WebSerial.send("message","AlarmsCfgCh: no changes");
    }
    return;
  }

  bool changed=false;
  for (int ch=0; ch<CH_COUNT && ch<cfgArr.length(); ++ch) {
    JSONVar c = cfgArr[ch];
    if (c.hasOwnProperty("ack")) {
      g_alarmCfg[ch].ackRequired = (bool)c["ack"];
      changed = true;
    }
    for (int k=0; k<AK_COUNT; ++k) {
      if (!c.hasOwnProperty(String(k))) continue;
      JSONVar r = c[String(k)];
      if (r.hasOwnProperty("enabled")) { g_alarmCfg[ch].rule[k].enabled = (bool)r["enabled"]; changed=true; }
      if (r.hasOwnProperty("min"))     { g_alarmCfg[ch].rule[k].min     = (int32_t)r["min"]; changed=true; }
      if (r.hasOwnProperty("max"))     { g_alarmCfg[ch].rule[k].max     = (int32_t)r["max"]; changed=true; }
      if (r.hasOwnProperty("metric"))  {
        int m = (int)r["metric"]; if (m < 0) m = 0; if (m > 5) m = 5;
        g_alarmCfg[ch].rule[k].metric = (uint8_t)m; changed=true;
      }
    }
  }

  alarms_publish_cfg();
  if (changed) {
    if (saveConfigFS()) WebSerial.send("message","AlarmsCfg saved");
    else                WebSerial.send("message","ERROR: saving AlarmsCfg failed");
  } else {
    WebSerial.send("message","AlarmsCfg: no changes");
  }
}
void handleAlarmsAck(JSONVar v){
  bool any = false;
  if (v.hasOwnProperty("ch")) {
    int _ch = constrain((int)v["ch"], 0, CH_COUNT-1);
    alarms_ack_channel((uint8_t)_ch);
    any = true;
  }
  if (v.hasOwnProperty("list") && v["list"].length() >= CH_COUNT) {
    for (int ch = 0; ch < CH_COUNT; ++ch) {
      if ((bool)v["list"][ch]) { alarms_ack_channel((uint8_t)ch); any = true; }
    }
  }
  if (any) { WebSerial.send("message","Alarms acknowledged"); alarms_publish_state(); }
  else     { WebSerial.send("message","AlarmsAck: no-op"); }
}

// =============== Calibration cfg (WebSerial) ===============
void handleCalibCfg(JSONVar payload){
  bool changed=false;
  if (String(JSON.typeof(payload)) == "array") {
    for (int ph=0; ph<3 && ph<payload.length(); ++ph){
      JSONVar o = payload[ph];
      set_calib_phase_from_obj(ph, o, changed);
    }
  } else if (payload.hasOwnProperty("ph")) {
    int ph = constrain((int)payload["ph"], 0, 2);
    if (payload.hasOwnProperty("cfg")) {
      JSONVar o = payload["cfg"];
      set_calib_phase_from_obj(ph, o, changed);
    } else {
      set_calib_phase_from_obj(ph, payload, changed);
    }
  } else {
    WebSerial.send("message","CalibCfg: no-op (bad payload)");
    calib_publish_cfg();
    return;
  }
  if (changed) {
    atm90_write16(CfgRegAccEn, 0x55AA);
    apply_calibration_to_chip();
    atm90_write16(CfgRegAccEn, 0x0000);
    cfgDirty = true; lastCfgTouchMs = millis();
    if (saveConfigFS()) WebSerial.send("message","CalibCfg saved & applied");
    else                WebSerial.send("message","ERROR: saving CalibCfg failed");
  } else {
    WebSerial.send("message","CalibCfg: no changes");
  }
  calib_publish_cfg();
}

// ================== Cached last meter read (for 1 Hz UI echo) ==================
static double   g_urms[3] = {0,0,0};
static double   g_irms[3] = {0,0,0};
static int32_t  g_p_W[4] = {0,0,0,0};
static int32_t  g_q_var[4] = {0,0,0,0};
static int32_t  g_s_VA[4] = {0,0,0,0};
static int16_t  g_pf_raw[4] = {0,0,0,0};
static int16_t  g_ang_raw[3] = {0,0,0};
static uint16_t g_f_x100 = 0;
static int16_t  g_tempC = 0;

// Energies (Wh/varh/VAh) from chip, per-phase + totals (u32 internal)
static uint32_t g_e_ap_Wh[4]={0,0,0,0}, g_e_an_Wh[4]={0,0,0,0}, g_e_rp_varh[4]={0,0,0,0}, g_e_rn_varh[4]={0,0,0,0}, g_e_s_VAh[4]={0,0,0,0};
static bool     g_haveMeter = false;

// ================== Meter echo (UI only real values) ==================
void sendMeterEcho()
{
  JSONVar m;

  for (int i=0;i<3;i++){ m["Urms"][i]=g_urms[i]; m["Irms"][i]=g_irms[i]; }

  JSONVar pW, qVar, sVA;
  for (int i=0;i<4;i++){ pW[i]=g_p_W[i]; qVar[i]=g_q_var[i]; sVA[i]=g_s_VA[i]; }
  m["P_W"]=pW; m["Q_var"]=qVar; m["S_VA"]=sVA;
  m["Ptot_W"]=g_p_W[3]; m["Qtot_var"]=g_q_var[3]; m["Stot_VA"]=g_s_VA[3];

  JSONVar pfR; for(int i=0;i<4;i++) pfR[i] = ((double)g_pf_raw[i])/1000.0;
  m["PF"]=pfR; m["PFtot"]=((double)g_pf_raw[3])/1000.0;

  JSONVar ang; for(int i=0;i<3;i++) ang[i]=((double)g_ang_raw[i])/10.0;
  m["Angle_deg"]=ang;

  m["FreqHz"]=((double)g_f_x100)/100.0; m["TempC"]=(int)g_tempC;

  // Energies: UI shows k-units
  auto to_k = [](uint32_t Wh)->double{ return Wh/1000.0; };
  JSONVar E;
  for (int i=0;i<3;i++){
    JSONVar ph;
    ph["AP_kWh"]=to_k(g_e_ap_Wh[i]);
    ph["AN_kWh"]=to_k(g_e_an_Wh[i]);
    ph["RP_kvarh"]=to_k(g_e_rp_varh[i]);
    ph["RN_kvarh"]=to_k(g_e_rn_varh[i]);
    ph["S_kVAh"]=to_k(g_e_s_VAh[i]);
    E[i]=ph;
  }
  JSONVar tot;
  tot["AP_kWh"]=to_k(g_e_ap_Wh[3]);
  tot["AN_kWh"]=to_k(g_e_an_Wh[3]);
  tot["RP_kvarh"]=to_k(g_e_rp_varh[3]);
  tot["RN_kvarh"]=to_k(g_e_rn_varh[3]);
  tot["S_kVAh"]=to_k(g_e_s_VAh[3]);
  E["tot"]=tot;
  m["E"]=E;

  // Alarms state
  JSONVar st;
  for (int ch=0; ch<CH_COUNT; ++ch) {
    JSONVar chO;
    for (int k=0;k<AK_COUNT;++k) {
      JSONVar r; r["cond"]=g_alarmRt[ch][k].conditionNow; r["active"]=g_alarmRt[ch][k].active; r["latched"]=g_alarmRt[ch][k].latched;
      chO[k]=r;
    }
    st[ch]=chO;
  }
  m["AlarmsState"]=st;

  WebSerial.send("ENM_Meter", m);
}

// ================== Setup ==================
void setup() {
  Serial.begin(115200);

  for (uint8_t i=0;i<4;i++) { pinMode(LED_PINS[i], OUTPUT); setLedPhys(i, false); }
  for (uint8_t i=0;i<4;i++) {
#if BUTTON_USES_PULLUP
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
#else
    pinMode(BUTTON_PINS[i], INPUT);
#endif
  }
  for (uint8_t i=0;i<2;i++) { pinMode(RELAY_PINS[i], OUTPUT); }

  setDefaults();

  if (!LittleFS.begin()) { WebSerial.send("message", "LittleFS mount failed. Formatting…"); LittleFS.format(); LittleFS.begin(); }
  if (loadConfigFS()) WebSerial.send("message", "Config loaded from flash");
  else { WebSerial.send("message", "No valid config. Using defaults."); saveConfigFS(); }

  setRelayPhys(0, relay_default[0]);
  setRelayPhys(1, relay_default[1]);

  pinMode(PIN_ATM_CS, OUTPUT); csRelease();
  MCM_SPI.setSCK(PIN_SPI_SCK);
  MCM_SPI.setTX(PIN_SPI_MOSI);
  MCM_SPI.setRX(PIN_SPI_MISO);
  MCM_SPI.begin();

  atm90_init();

  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);

  // ---- Register telemetry regs ----
  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_URMS_BASE + i);
  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_IRMS_BASE + i);

  for (uint16_t i=0;i<8;i++) mb.addIreg(IREG_P_BASE + i);
  for (uint16_t i=0;i<8;i++) mb.addIreg(IREG_Q_BASE + i);
  for (uint16_t i=0;i<8;i++) mb.addIreg(IREG_S_BASE + i);

  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_PF_BASE + i);
  mb.addIreg(IREG_PF_TOT);
  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_ANGLE_BASE + i);

  mb.addIreg(IREG_FREQ_X100);
  mb.addIreg(IREG_TEMP_C);

  // Energies per-phase + totals: 20 pairs = 40 regs
  for (uint16_t r=IREG_E_AP_A_Wh; r<=IREG_E_S_T_VAh+1; ++r) mb.addIreg(r);

  // Diagnostics
  mb.addIreg(IREG_EMM0); mb.addIreg(IREG_EMM1); mb.addIreg(IREG_INT0); mb.addIreg(IREG_INT1);
  mb.addIreg(IREG_CRCERR); mb.addIreg(IREG_LASTSPI);

  // ---- Holding (RW) ----
  mb.addHreg(HREG_SMPLL_MS); mb.Hreg(HREG_SMPLL_MS, sample_ms);
  mb.addHreg(HREG_LFREQ_HZ); mb.Hreg(HREG_LFREQ_HZ, atm_lineFreqHz);
  mb.addHreg(HREG_SUMMODE);  mb.Hreg(HREG_SUMMODE,  atm_sumModeAbs);
  mb.addHreg(HREG_UCAL);     mb.Hreg(HREG_UCAL,     atm_ucal);

  // ---- Discrete & Coils ----
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_LED_BASE + i);
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_BTN_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addIsts(ISTS_RLY_BASE + i);
  for (uint16_t i=0;i<CH_COUNT*AK_COUNT;i++) mb.addIsts(ISTS_ALARM_BASE + i);

  mb.addCoil(COIL_RLY1); mb.Coil(COIL_RLY1, relayState[0]);
  mb.addCoil(COIL_RLY2); mb.Coil(COIL_RLY2, relayState[1]);
  for (uint16_t i=0;i<CH_COUNT;i++){ mb.addCoil(COIL_ALARM_ACK_BASE + i); mb.Coil(COIL_ALARM_ACK_BASE + i, 0); }

  // WebSerial handlers
  WebSerial.on("values",     handleValues);
  WebSerial.on("Config",     handleUnifiedConfig);
  WebSerial.on("RelaysSet",  handleRelaysSet);
  WebSerial.on("RelaysCfg",  handleRelaysCfg);
  WebSerial.on("AlarmsCfg",  handleAlarmsCfg);
  WebSerial.on("AlarmsAck",  handleAlarmsAck);
  WebSerial.on("CalibCfg",   handleCalibCfg);

  WebSerial.send("message", "Boot OK (ENM-223-R1, real-values UI + scaled-int Modbus + per-phase energy)");
}

// ================== HREG write watcher (minimal; no scales) ==================
void serviceHregWrites() {
  static uint16_t prevSm = 0xFFFF, prevLf = 0xFFFF, prevSu = 0xFFFF, prevUc = 0xFFFF;
  uint16_t sm = mb.Hreg(HREG_SMPLL_MS);
  uint16_t lf = mb.Hreg(HREG_LFREQ_HZ);
  uint16_t su = mb.Hreg(HREG_SUMMODE);
  uint16_t uc = mb.Hreg(HREG_UCAL);

  bool reinit=false;
  if (sm != prevSm) { prevSm=sm; sample_ms = constrain((int)sm, 10, 5000); cfgDirty=true; lastCfgTouchMs=millis(); }
  if (lf != prevLf) { prevLf=lf; atm_lineFreqHz = (uint16_t)((lf==60)?60:50); reinit=true; }
  if (su != prevSu) { prevSu=su; atm_sumModeAbs = (uint8_t)((su)?1:0); reinit=true; }
  if (uc != prevUc) { prevUc=uc; atm_ucal = uc; }

  if (reinit) { atm90_init(); cfgDirty=true; lastCfgTouchMs=millis(); }
}

// ================== Button actions ==================
void doButtonAction(uint8_t idx) {
  uint8_t act = buttonCfg[idx].action;
  switch (act) {
    case 0: break;
    case 1: setRelayPhys(0, !relayState[0]); break;
    case 2: setRelayPhys(1, !relayState[1]); break;
    case 3: case 4: case 5: case 6: { uint8_t led = act - 3; ledManual[led] = !ledManual[led]; break; }
    default: break;
  }
}

// ================== LED source ==================
bool evalLedSource(uint8_t src) {
  auto anyKinds = [&](uint8_t ch)->bool {
    if (ch >= CH_COUNT) return false;
    return g_alarmRt[ch][AK_ALARM].active
        || g_alarmRt[ch][AK_WARNING].active
        || g_alarmRt[ch][AK_EVENT].active;
  };
  auto alarmKind = [&](uint8_t ch, uint8_t kind)->bool {
    if (ch >= CH_COUNT || kind >= AK_COUNT) return false;
    return g_alarmRt[ch][kind].active;
  };
  switch (src) {
    case 0:  return false;
    case 8:  return buttonOverrideMode[0];
    case 9:  return buttonOverrideMode[1];
    case 10: return alarmKind(CH_L1, AK_ALARM);
    case 11: return alarmKind(CH_L2, AK_ALARM);
    case 12: return alarmKind(CH_L3, AK_ALARM);
    case 13: return alarmKind(CH_TOT,AK_ALARM);
    case 14: return alarmKind(CH_L1, AK_WARNING);
    case 15: return alarmKind(CH_L2, AK_WARNING);
    case 16: return alarmKind(CH_L3, AK_WARNING);
    case 17: return alarmKind(CH_TOT,AK_WARNING);
    case 18: return alarmKind(CH_L1, AK_EVENT);
    case 19: return alarmKind(CH_L2, AK_EVENT);
    case 20: return alarmKind(CH_L3, AK_EVENT);
    case 21: return alarmKind(CH_TOT,AK_EVENT);
    case 22: return anyKinds(CH_L1);
    case 23: return anyKinds(CH_L2);
    case 24: return anyKinds(CH_L3);
    case 25: return anyKinds(CH_TOT);
    default: return false;
  }
}

// ================== Main loop ==================
void loop() {
  const unsigned long now = millis();

  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  mb.task();
  serviceHregWrites();

  // ===== Buttons (debounce + basic actions) =====
  for (int i = 0; i < 4; i++) {
    const bool pressed = readPressed(i);
    if (pressed != buttonState[i] && (now - btnChangeAt[i] >= BTN_DEBOUNCE_MS)) {
      btnChangeAt[i] = now; buttonPrev[i]  = buttonState[i]; buttonState[i] = pressed;
      const uint8_t act = buttonCfg[i].action;
      if (!buttonPrev[i] && buttonState[i]) { btnPressAt[i] = now; btnLongDone[i] = false; }
      if (buttonPrev[i] && !buttonState[i]) {
        if (!btnLongDone[i]) {
          if (act == 7 || act == 8) {
            const int r = (act == 7) ? 0 : 1;
            if (buttonOverrideMode[r]) { buttonOverrideState[r] = !buttonOverrideState[r]; }
          } else {
            doButtonAction(i);
            echoRelayState();
          }
        }
      }
    }
    if (buttonState[i] && !btnLongDone[i] && (now - btnPressAt[i] >= BTN_LONG_MS)) {
      const uint8_t act = buttonCfg[i].action;
      if (act == 7 || act == 8) {
        const int r = (act == 7) ? 0 : 1;
        if (!buttonOverrideMode[r]) { buttonOverrideMode[r]  = true; buttonOverrideState[r] = relayState[r]; }
        else { buttonOverrideMode[r]  = false; if (r == 0) mb.Coil(COIL_RLY1, relayState[0]); else mb.Coil(COIL_RLY2, relayState[1]); }
      }
      btnLongDone[i] = true;
    }
    mb.setIsts(ISTS_BTN_BASE + i, buttonState[i]);
  }

  // ===== Modbus coils control =====
  auto canExternalWrite = [&](int idx)->bool{
    return (relay_mode[idx] == RM_MODBUS) && !buttonOverrideMode[idx];
  };
  if (canExternalWrite(0)) {
    if (mb.Coil(COIL_RLY1) != relayState[0]) setRelayPhys(0, mb.Coil(COIL_RLY1));
  } else { if (mb.Coil(COIL_RLY1) != relayState[0]) mb.Coil(COIL_RLY1, relayState[0]); }
  if (canExternalWrite(1)) {
    if (mb.Coil(COIL_RLY2) != relayState[1]) setRelayPhys(1, mb.Coil(COIL_RLY2));
  } else { if (mb.Coil(COIL_RLY2) != relayState[1]) mb.Coil(COIL_RLY2, relayState[1]); }

  for (uint16_t ch=0; ch<CH_COUNT; ++ch) {
    uint16_t coil = COIL_ALARM_ACK_BASE + ch;
    if (mb.Coil(coil)) { alarms_ack_channel((uint8_t)ch); mb.Coil(coil, 0); }
  }

  // ===== Sampling =====
  if (now - lastSample >= sample_ms) {
    lastSample = now;

    // ---- RMS ----
    double urms[3];
    urms[0] = read_rms_like(UrmsA, UrmsALSB, 0.01, 0.01/256.0);
    urms[1] = read_rms_like(UrmsB, UrmsBLSB, 0.01, 0.01/256.0);
    urms[2] = read_rms_like(UrmsC, UrmsCLSB, 0.01, 0.01/256.0);
    double irms[3];
    irms[0] = read_rms_like(IrmsA, IrmsALSB, 0.001, 0.001/256.0);
    irms[1] = read_rms_like(IrmsB, IrmsBLSB, 0.001, 0.001/256.0);
    irms[2] = read_rms_like(IrmsC, IrmsCLSB, 0.001, 0.001/256.0);

    auto clampU = [](double v){ long x = lroundf(v*100.0f); if(x<0)x=0; if(x>65535)x=65535; return (uint16_t)x; };
    auto clampI = [](double a){ long x = lroundf(a*1000.0f); if(x<0)x=0; if(x>65535)x=65535; return (uint16_t)x; };
    for (int i=0;i<3;i++) mb.Ireg(IREG_URMS_BASE + i, clampU(urms[i]));
    for (int i=0;i<3;i++) mb.Ireg(IREG_IRMS_BASE + i, clampI(irms[i]));

    // ---- Powers (scaled to real W/var/VA) into s32 pairs ----
// ---- PF & Angles (read first) ----
g_pf_raw[0]=(int16_t)atm90_read16(PFmeanA);
g_pf_raw[1]=(int16_t)atm90_read16(PFmeanB);
g_pf_raw[2]=(int16_t)atm90_read16(PFmeanC);
g_pf_raw[3]=(int16_t)atm90_read16(PFmeanT);
for (int i=0;i<3;i++) mb.Ireg(IREG_PF_BASE + i, (uint16_t)g_pf_raw[i]);
mb.Ireg(IREG_PF_TOT, (uint16_t)g_pf_raw[3]);

g_ang_raw[0]=(int16_t)atm90_read16(PAngleA);
g_ang_raw[1]=(int16_t)atm90_read16(PAngleB);
g_ang_raw[2]=(int16_t)atm90_read16(PAngleC);
for (int i=0;i<3;i++) mb.Ireg(IREG_ANGLE_BASE + i, (uint16_t)g_ang_raw[i]);

// ---- Compute REAL S, P, Q from Urms/Irms/PF/Angle ----
// PF from chip is ×1000 -> 0..1
auto pf01 = [](int16_t pf_raw){ return constrain(pf_raw/1000.0, -1.0, 1.0); };

double S_va_d[3], P_w_d[3], Q_var_d[3];
for (int i=0;i<3;i++) {
  const double S = urms[i] * irms[i];                 // VA
  const double PF = pf01(g_pf_raw[i]);                // 0..1 (signed if chip reports sign)
  const double P  = S * PF;                           // W
  const double ang_deg = g_ang_raw[i] / 10.0;         // 0.1°
  const double ang_rad = ang_deg * (M_PI/180.0);
  const double Qmag = sqrt(fmax(0.0, (S*S) - (P*P))); // var magnitude
  // sign from angle (flip once here if your site uses opposite reactive sign)
  const double Q = (sin(ang_rad) >= 0.0) ? Qmag : -Qmag;

  S_va_d[i] = S; P_w_d[i] = P; Q_var_d[i] = Q;
}

// Totals
double S_tot_d = S_va_d[0] + S_va_d[1] + S_va_d[2];
double P_tot_d = P_w_d[0]  + P_w_d[1]  + P_w_d[2];
double Q_tot_d = Q_var_d[0]+ Q_var_d[1]+ Q_var_d[2];

// ---- Publish P/Q/S as scaled integers over Modbus (s32, whole units) ----
auto put_s32 = [&](uint16_t base, int32_t v){
  mb.Ireg(base+0, (uint16_t)((v>>16)&0xFFFF));
  mb.Ireg(base+1, (uint16_t)(v & 0xFFFF));
};

g_p_W[0] = (int32_t)lround(P_w_d[0]);
g_p_W[1] = (int32_t)lround(P_w_d[1]);
g_p_W[2] = (int32_t)lround(P_w_d[2]);
g_p_W[3] = (int32_t)lround(P_tot_d);

g_q_var[0] = (int32_t)lround(Q_var_d[0]);
g_q_var[1] = (int32_t)lround(Q_var_d[1]);
g_q_var[2] = (int32_t)lround(Q_var_d[2]);
g_q_var[3] = (int32_t)lround(Q_tot_d);

g_s_VA[0] = (int32_t)lround(S_va_d[0]);
g_s_VA[1] = (int32_t)lround(S_va_d[1]);
g_s_VA[2] = (int32_t)lround(S_va_d[2]);
g_s_VA[3] = (int32_t)lround(S_tot_d);

// high‑word first, pairs every +2 regs
put_s32(IREG_P_BASE+0, g_p_W[0]); put_s32(IREG_P_BASE+2, g_p_W[1]); put_s32(IREG_P_BASE+4, g_p_W[2]); put_s32(IREG_P_BASE+6, g_p_W[3]);
put_s32(IREG_Q_BASE+0, g_q_var[0]); put_s32(IREG_Q_BASE+2, g_q_var[1]); put_s32(IREG_Q_BASE+4, g_q_var[2]); put_s32(IREG_Q_BASE+6, g_q_var[3]);
put_s32(IREG_S_BASE+0, g_s_VA[0]); put_s32(IREG_S_BASE+2, g_s_VA[1]); put_s32(IREG_S_BASE+4, g_s_VA[2]); put_s32(IREG_S_BASE+6, g_s_VA[3]);


    // ---- Freq/Temp ----
    uint16_t fraw = atm90_read16(Freq);
    int16_t  tC   = (int16_t)atm90_read16(Temp);
    g_f_x100 = fraw; g_tempC = tC;
    mb.Ireg(IREG_FREQ_X100, fraw);
    mb.Ireg(IREG_TEMP_C, (uint16_t)tC);

    // ---- Energies (read from chip, convert to Wh/varh/VAh for Modbus u32; UI shows k-units) ----
    // Helper: convert chip raw count -> Wh (or varh/VAh) using k-units per count scale (in 1e-5)
    auto cnt_to_Wh    = [&](uint16_t cnt)->uint32_t{ double kWh = ((double)cnt * (double)e_ap_kWh_per_cnt_x1e5)/100000.0; return (uint32_t)lround(kWh*1000.0); };
    auto cnt_to_Wh_AN = [&](uint16_t cnt)->uint32_t{ double kWh = ((double)cnt * (double)e_an_kWh_per_cnt_x1e5)/100000.0; return (uint32_t)lround(kWh*1000.0); };
    auto cnt_to_varhP = [&](uint16_t cnt)->uint32_t{ double kvarh = ((double)cnt * (double)e_rp_kvarh_per_cnt_x1e5)/100000.0; return (uint32_t)lround(kvarh*1000.0); };
    auto cnt_to_varhN = [&](uint16_t cnt)->uint32_t{ double kvarh = ((double)cnt * (double)e_rn_kvarh_per_cnt_x1e5)/100000.0; return (uint32_t)lround(kvarh*1000.0); };
    auto cnt_to_VAh   = [&](uint16_t cnt)->uint32_t{ double kVAh = ((double)cnt * (double)e_s_kVAh_per_cnt_x1e5 )/100000.0; return (uint32_t)lround(kVAh*1000.0); };

    uint16_t apA=atm90_read16(APenergyA), apB=atm90_read16(APenergyB), apC=atm90_read16(APenergyC), apT=atm90_read16(APenergyT);
    uint16_t anA=atm90_read16(ANenergyA), anB=atm90_read16(ANenergyB), anC=atm90_read16(ANenergyC), anT=atm90_read16(ANenergyT);
    uint16_t rpA=atm90_read16(RPenergyA), rpB=atm90_read16(RPenergyB), rpC=atm90_read16(RPenergyC), rpT=atm90_read16(RPenergyT);
    uint16_t rnA=atm90_read16(RNenergyA), rnB=atm90_read16(RNenergyB), rnC=atm90_read16(RNenergyC), rnT=atm90_read16(RNenergyT);
    uint16_t sA =atm90_read16(SAenergyA), sB =atm90_read16(SAenergyB), sC =atm90_read16(SAenergyC), sT =atm90_read16(SAenergyT);

    g_e_ap_Wh[0]=cnt_to_Wh(apA); g_e_ap_Wh[1]=cnt_to_Wh(apB); g_e_ap_Wh[2]=cnt_to_Wh(apC); g_e_ap_Wh[3]=cnt_to_Wh(apT);
    g_e_an_Wh[0]=cnt_to_Wh_AN(anA); g_e_an_Wh[1]=cnt_to_Wh_AN(anB); g_e_an_Wh[2]=cnt_to_Wh_AN(anC); g_e_an_Wh[3]=cnt_to_Wh_AN(anT);
    g_e_rp_varh[0]=cnt_to_varhP(rpA); g_e_rp_varh[1]=cnt_to_varhP(rpB); g_e_rp_varh[2]=cnt_to_varhP(rpC); g_e_rp_varh[3]=cnt_to_varhP(rpT);
    g_e_rn_varh[0]=cnt_to_varhN(rnA); g_e_rn_varh[1]=cnt_to_varhN(rnB); g_e_rn_varh[2]=cnt_to_varhN(rnC); g_e_rn_varh[3]=cnt_to_varhN(rnT);
    g_e_s_VAh[0]=cnt_to_VAh(sA); g_e_s_VAh[1]=cnt_to_VAh(sB); g_e_s_VAh[2]=cnt_to_VAh(sC); g_e_s_VAh[3]=cnt_to_VAh(sT);

    auto put_u32 = [&](uint16_t base, uint32_t v){
      mb.Ireg(base+0, (uint16_t)((v>>16)&0xFFFF));
      mb.Ireg(base+1, (uint16_t)(v & 0xFFFF));
    };
    // AP
    put_u32(IREG_E_AP_A_Wh, g_e_ap_Wh[0]); put_u32(IREG_E_AP_B_Wh, g_e_ap_Wh[1]); put_u32(IREG_E_AP_C_Wh, g_e_ap_Wh[2]); put_u32(IREG_E_AP_T_Wh, g_e_ap_Wh[3]);
    // AN
    put_u32(IREG_E_AN_A_Wh, g_e_an_Wh[0]); put_u32(IREG_E_AN_B_Wh, g_e_an_Wh[1]); put_u32(IREG_E_AN_C_Wh, g_e_an_Wh[2]); put_u32(IREG_E_AN_T_Wh, g_e_an_Wh[3]);
    // RP
    put_u32(IREG_E_RP_A_varh, g_e_rp_varh[0]); put_u32(IREG_E_RP_B_varh, g_e_rp_varh[1]); put_u32(IREG_E_RP_C_varh, g_e_rp_varh[2]); put_u32(IREG_E_RP_T_varh, g_e_rp_varh[3]);
    // RN
    put_u32(IREG_E_RN_A_varh, g_e_rn_varh[0]); put_u32(IREG_E_RN_B_varh, g_e_rn_varh[1]); put_u32(IREG_E_RN_C_varh, g_e_rn_varh[2]); put_u32(IREG_E_RN_T_varh, g_e_rn_varh[3]);
    // S
    put_u32(IREG_E_S_A_VAh, g_e_s_VAh[0]); put_u32(IREG_E_S_B_VAh, g_e_s_VAh[1]); put_u32(IREG_E_S_C_VAh, g_e_s_VAh[2]); put_u32(IREG_E_S_T_VAh, g_e_s_VAh[3]);

    // ---- Build metrics snapshot for alarms ----
    MetricsSnapshot ms{};
    for (int i=0;i<3;i++){
      ms.Urms_cV[i] = (int32_t)lround(urms[i]*100.0);
      ms.Irms_mA[i] = (int32_t)lround(irms[i]*1000.0);
    }
    for (int i=0;i<4;i++){
      ms.P_W[i]   = g_p_W[i];
      ms.Q_var[i] = g_q_var[i];
      ms.S_VA[i]  = g_s_VA[i];
    }
    ms.Freq_cHz = (int32_t)g_f_x100;
    eval_alarms_with_metrics(ms);

    // ---- Relay Alarm-mode evaluation ----
    auto alarm_any_selected = [&](uint8_t ch, uint8_t mask)->bool{
      bool any = false;
      if (mask & 0b001) any |= g_alarmRt[ch][AK_ALARM].active;
      if (mask & 0b010) any |= g_alarmRt[ch][AK_WARNING].active;
      if (mask & 0b100) any |= g_alarmRt[ch][AK_EVENT].active;
      return any;
    };
    if (relay_mode[0] == RM_ALARM && !buttonOverrideMode[0]) {
      bool on0 = alarm_any_selected(relay_alarm_src[0].ch, relay_alarm_src[0].kindsMask);
      if (relayState[0] != on0) setRelayPhys(0, on0);
    }
    if (relay_mode[1] == RM_ALARM && !buttonOverrideMode[1]) {
      bool on1 = alarm_any_selected(relay_alarm_src[1].ch, relay_alarm_src[1].kindsMask);
      if (relayState[1] != on1) setRelayPhys(1, on1);
    }
    for (int r=0; r<2; ++r) if (buttonOverrideMode[r]) if (relayState[r] != buttonOverrideState[r]) setRelayPhys(r, buttonOverrideState[r]);

    // Cache latest meter
    for (int i=0;i<3;i++){ g_urms[i]=urms[i]; g_irms[i]=irms[i]; }
    g_haveMeter = true;
  }

  // LEDs (driven continuously)
  JSONVar ledStateList;
  for (int i=0;i<4;i++) {
    bool activeFromSource = evalLedSource(ledCfg[i].source);
    bool active = (activeFromSource || ledManual[i]);
    bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase);
    setLedPhys(i, phys);
    ledStateList[i] = phys;
  }

  // ===== 1 Hz UI push =====
  if (now - lastSend >= sendInterval) {
    lastSend = now;

    WebSerial.check();
    sendStatusEcho();

    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    WebSerial.send("ButtonConfigList", ButtonConfigListFromCfg());
    WebSerial.send("RelaysCfg", RelayConfigFromCfg());
    WebSerial.send("MeterOptions", MeterOptionsJSON());
    alarms_publish_cfg();
    calib_publish_cfg();

    if (g_haveMeter) sendMeterEcho();

    JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i];
    WebSerial.send("ButtonStateList", btnState);
    WebSerial.send("LedStateList", ledStateList);
    echoRelayState();
    alarms_publish_state();
  }
}
