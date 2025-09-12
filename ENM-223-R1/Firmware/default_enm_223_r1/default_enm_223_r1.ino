// ==== ENM-223-R1 (RP2350/RP2040) ====
// ATM90E32 via SPI1 (NO external library) + 2x Relays on GPIO0/1
// WebSerial UI, Modbus RTU, LittleFS persistence
// Buttons: GPIO22..25, LEDs: GPIO18..21
// ============================================================================

// ---- FIX for Arduino auto-prototype: make types visible to auto-prototypes
struct PersistConfig;
struct AlarmRule;
struct MetricsSnapshot;   // <-- forward declare to satisfy auto-prototypes

#include <Arduino.h>
#include <SPI.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <utility>
#include <math.h>
#include <limits>

// ================== Hardware pins ==================
#define LED_ACTIVE_LOW       0   // 1 if LEDs are active-LOW
#define BUTTON_USES_PULLUP   0   // 1 uses INPUT_PULLUP (pressed=LOW)
#define RELAY_ACTIVE_LOW     0   // 1 if relays are active-LOW

const uint8_t LED_PINS[4]    = {18,19,20,21};
const uint8_t BUTTON_PINS[4] = {22,23,24,25};
const uint8_t RELAY_PINS[2]  = {0,1};

// ===== ATM90E32 on SPI1 (NO LIB) =====
#define MCM_SPI SPI1
static const uint8_t PIN_SPI_SCK  = 10;
static const uint8_t PIN_SPI_MOSI = 11;
static const uint8_t PIN_SPI_MISO = 12;
static const uint8_t PIN_ATM_CS   = 13;

static const uint8_t PIN_PM1 = 2;
static const uint8_t PIN_PM0 = 3;

#define CS_ACTIVE_HIGH 0
#define ATM_SPI_MODE   SPI_MODE0
#define SPI_HZ         200000     // conservative during init + reads

inline void csAssert()  { digitalWrite(PIN_ATM_CS, CS_ACTIVE_HIGH ? HIGH : LOW); }
inline void csRelease() { digitalWrite(PIN_ATM_CS, CS_ACTIVE_HIGH ? LOW  : HIGH); }

// ================== Modbus / RS-485 ==================
#define TX2 4
#define RX2 5
const int TxenPin = -1;     // -1 if not using TXEN
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== Buttons & LEDs config ==================
struct LedCfg   { uint8_t mode;   uint8_t source; }; // mode: 0 steady, 1 blink; source: see evalLedSource()
struct ButtonCfg{ uint8_t action; };                 // actions below

LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool      ledManual[4] = {false,false,false,false};   // manual toggles
bool      ledPhys[4]   = {false,false,false,false};   // physical pin state tracking

// Button runtime
constexpr unsigned long BTN_DEBOUNCE_MS = 30;
bool buttonState[4]      = {false,false,false,false};
bool buttonPrev[4]       = {false,false,false,false};
unsigned long btnChangeAt[4] = {0,0,0,0};

// Relays (logical state; true = ON before polarity)
bool relayState[2] = {false,false}; 

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
uint16_t sample_ms    = 200;     // meter poll period

// ATM90E32 options (persisted)
uint16_t atm_lineFreqHz = 50;    // 50 or 60
uint8_t  atm_sumModeAbs = 1;     // affects MMode0
uint16_t atm_ucal       = 25256; // voltage calibration (used by vSag calc)

// Measurement scaling (persisted)
uint32_t p_scale_mW_per_lsb            = 1;
uint32_t q_scale_mvar_per_lsb_x1000    = 1;
uint32_t s_scale_mva_per_lsb_x1000     = 1;

// Energy scales (persisted)
uint32_t e_ap_kWh_per_cnt_x1e5   = 1;
uint32_t e_an_kWh_per_cnt_x1e5   = 1;
uint32_t e_rp_kvarh_per_cnt_x1e5 = 1;
uint32_t e_rn_kvarh_per_cnt_x1e5 = 1;  // NOTE: kvarh
uint32_t e_s_kVAh_per_cnt_x1e5   = 1;

// Persisted relay polarity & defaults
uint8_t  relay_active_low = RELAY_ACTIVE_LOW;
bool     relay_default[2] = {false,false};

// ================== ALARMS (config + runtime) ==================
enum : uint8_t { CH_L1=0, CH_L2=1, CH_L3=2, CH_TOT=3, CH_COUNT=4 };
enum : uint8_t { AK_ALARM=0, AK_WARNING=1, AK_EVENT=2, AK_COUNT=3 };

enum AlarmMetric : uint8_t {
  AM_VOLTAGE   = 0,  // Urms   (centivolts, 0.01 V)
  AM_CURRENT   = 1,  // Irms   (milliamps, 0.001 A)
  AM_P_ACTIVE  = 2,  // P      (W)
  AM_Q_REACTIVE= 3,  // Q      (var)
  AM_S_APPARENT= 4,  // S (VA)
  AM_FREQ      = 5   // Freq   (centi-Hz, 0.01 Hz), channel ignored
};

struct AlarmRule {
  bool     enabled;
  int32_t  min;      // inclusive
  int32_t  max;      // inclusive
  uint8_t  metric;   // AlarmMetric
};

struct AlarmRuntime {
  bool conditionNow;
  bool active;
  bool latched;
};

struct ChannelAlarmCfg {
  bool ackRequired;
  AlarmRule rule[AK_COUNT];     // 0 Alarm, 1 Warning, 2 Event
};

ChannelAlarmCfg g_alarmCfg[CH_COUNT];
AlarmRuntime    g_alarmRt [CH_COUNT][AK_COUNT];

// ================== Persistence blob ==================
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

  // Scales
  uint32_t p_scale_mW_per_lsb;
  uint32_t q_scale_mvar_per_lsb_x1000;
  uint32_t s_scale_mva_per_lsb_x1000;

  uint32_t e_ap_kWh_per_cnt_x1e5;
  uint32_t e_an_kWh_per_cnt_x1e5;
  uint32_t e_rp_kvarh_per_cnt_x1e5;
  uint32_t e_rn_kvarh_per_cnt_x1e5;   // NOTE: kvarh
  uint32_t e_s_kVAh_per_cnt_x1e5;

  // ===== Alarms (persisted) =====
  uint8_t  alarm_ack_required[CH_COUNT];
  struct PackedRule { uint8_t enabled; int32_t min; int32_t max; uint8_t metric; } alarm_rules[CH_COUNT][AK_COUNT];

  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x324D4E45UL; // 'ENM2'
static const uint16_t CFG_VERSION = 0x0006;       // version with metric support
static const char*    CFG_PATH    = "/enm223.bin";

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

inline bool readPressed(int i){
#if BUTTON_USES_PULLUP
  return digitalRead(BUTTON_PINS[i]) == LOW;   // pressed = LOW
#else
  return digitalRead(BUTTON_PINS[i]) == HIGH;  // pressed = HIGH
#endif
}

// ================== Defaults ==================
static inline void setAlarmDefaults() {
  const AlarmRule defAlarm   = {true,   0, 100000, (uint8_t)AM_S_APPARENT};
  const AlarmRule defWarn    = {true,   0, 120000, (uint8_t)AM_S_APPARENT};
  const AlarmRule defEvent   = {true,   std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), (uint8_t)AM_S_APPARENT};
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
    ledCfg[i].mode = 0;      // steady
    ledCfg[i].source = (i==0) ? 1 : 0; // LED0 heartbeat
    buttonCfg[i].action = 0; // None
    ledManual[i] = false;
  }

  relay_active_low = RELAY_ACTIVE_LOW;
  relay_default[0] = false;
  relay_default[1] = false;

  atm_lineFreqHz = 50;
  atm_sumModeAbs = 1;
  atm_ucal       = 25256;

  p_scale_mW_per_lsb         = 1;
  q_scale_mvar_per_lsb_x1000 = 1;
  s_scale_mva_per_lsb_x1000  = 1;

  e_ap_kWh_per_cnt_x1e5   = 1;
  e_an_kWh_per_cnt_x1e5   = 1;
  e_rp_kvarh_per_cnt_x1e5 = 1;
  e_rn_kvarh_per_cnt_x1e5 = 1;
  e_s_kVAh_per_cnt_x1e5   = 1;

  setAlarmDefaults();
}

void captureToPersist(PersistConfig &pc) {
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
  sample_ms      = pc.sample_ms;

  memcpy(ledCfg,    pc.ledCfg,    sizeof(ledCfg));
  memcpy(buttonCfg, pc.buttonCfg, sizeof(buttonCfg));

  relay_active_low = pc.relay_active_low;
  relay_default[0] = pc.relay_default0;
  relay_default[1] = pc.relay_default1;

  atm_lineFreqHz = pc.atm_lineFreqHz;
  atm_sumModeAbs = pc.atm_sumModeAbs;
  atm_ucal       = pc.atm_ucal;

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
#define APenergyT 0x80
#define ANenergyT 0x84
#define RPenergyT 0x88
#define RNenergyT 0x8C
#define SAenergyT 0x90
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
  uint16_t data_swapped = (uint16_t)((val >> 8) | (val << 8));
  uint16_t addr = reg | (rw ? 0x8000 : 0x0000);
  uint16_t addr_swapped = (uint16_t)((addr >> 8) | (addr << 8));

  SPISettings settings(SPI_HZ, MSBFIRST, ATM_SPI_MODE);

  MCM_SPI.beginTransaction(settings);
  csAssert();
  delayMicroseconds(10);

  uint8_t *pa = reinterpret_cast<uint8_t*>(&addr_swapped);
  MCM_SPI.transfer(pa[0]);
  MCM_SPI.transfer(pa[1]);

  delayMicroseconds(4);

  uint16_t out = 0;
  if (rw) {
    uint8_t b0 = MCM_SPI.transfer(0x00);
    uint8_t b1 = MCM_SPI.transfer(0x00);
    uint16_t raw = (uint16_t)((uint16_t)b0 | ((uint16_t)b1 << 8));
    out = (uint16_t)((raw >> 8) | (raw << 8));
  } else {
    uint8_t *pd = reinterpret_cast<uint8_t*>(&data_swapped);
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
  int16_t h = (int16_t)atm90_read16(regH);
  uint16_t l = atm90_read16(regLSB);
  int32_t val24 = ((int32_t)h << 8) | ((l >> 8) & 0xFF);
  if (val24 & 0x00800000) val24 |= 0xFF000000;
  return val24;
}

static inline double read_rms_like(uint16_t regH, uint16_t regLSB, double sH, double sLb)
{
  uint16_t h = atm90_read16(regH);
  uint16_t l = atm90_read16(regLSB);
  return (h * sH) + (((l >> 8) & 0xFF) * sLb);
}

// ===== Simple init of the chip =====
static inline uint8_t gainCode(uint8_t g){ if(g==1)return 0; if(g==2)return 1; if(g==4)return 2; return 1; }
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
  uint16_t vSagTh = (uint16_t)((sagV * 100.0 * sqrt(2.0)) / (2.0 * (atm_ucal / 32768.0)));

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

  uint8_t gIA=2,gIB=2,gIC=2;
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

  atm90_write16(UgainA, atm_ucal); atm90_write16(UgainB, atm_ucal); atm90_write16(UgainC, atm_ucal);
  atm90_write16(IgainA, 0);        atm90_write16(IgainB, 0);        atm90_write16(IgainC, 0);
  atm90_write16(UoffsetA, 0);      atm90_write16(UoffsetB, 0);      atm90_write16(UoffsetC, 0);
  atm90_write16(IoffsetA, 0);      atm90_write16(IoffsetB, 0);      atm90_write16(IoffsetC, 0);

  atm90_write16(CfgRegAccEn, 0x0000);
}

// ================== Modbus map ==================
enum : uint16_t {
  IREG_URMS_BASE    = 100,  // 100..102: Urms L1..L3 in 0.01 V (u16)
  IREG_IRMS_BASE    = 110,  // 110..112: Irms L1..L3 in 0.001 A (u16)
  IREG_P_BASE       = 200,  // pairs L1..L3 P RAW
  IREG_Q_BASE       = 210,  // pairs L1..L3 Q RAW
  IREG_S_BASE       = 220,  // pairs L1..L3 S RAW
  IREG_PTOT         = 230,  // 230/231 P_total RAW
  IREG_QTOT         = 232,  // 232/233 Q_total RAW
  IREG_STOT         = 234,  // 234/235 S_total RAW
  IREG_PF_BASE      = 240,  // 240..242 PF L1..L3 raw s16
  IREG_PF_TOT       = 243,  // 243 PF total raw s16
  IREG_ANGLE_BASE   = 244,  // 244..246 Phase angle L1..L3, 0.1° s16
  IREG_FREQ_X100    = 250,  // Hz ×100
  IREG_TEMP_C       = 251,  // °C s16
  IREG_E_AP_RAW     = 300,
  IREG_E_AN_RAW     = 301,
  IREG_E_RP_RAW     = 302,
  IREG_E_RN_RAW     = 303,
  IREG_E_S_RAW      = 304,
  IREG_E_AP_MILLI   = 310,  // engineered energies u32 pairs
  IREG_E_AN_MILLI   = 312,
  IREG_E_RP_MILLI   = 314,
  IREG_E_RN_MILLI   = 316,
  IREG_E_S_MILLI    = 318,
  IREG_EMM0         = 360,
  IREG_EMM1         = 361,
  IREG_INT0         = 362,
  IREG_INT1         = 363,
  IREG_CRCERR       = 364,
  IREG_LASTSPI      = 365,

  // Holding
  HREG_SMPLL_MS     = 400,
  HREG_LFREQ_HZ     = 401,
  HREG_SUMMODE      = 402,
  HREG_UCAL         = 403,
  HREG_P_SCALE_mW_L = 404,
  HREG_P_SCALE_mW_H = 405,
  HREG_Q_SCALE_X1k_L= 406,
  HREG_Q_SCALE_X1k_H= 407,
  HREG_S_SCALE_X1k_L= 408,
  HREG_S_SCALE_X1k_H= 409,
  HREG_E_AP_kWh_x1e5_L = 410, HREG_E_AP_kWh_x1e5_H = 411,
  HREG_E_AN_kWh_x1e5_L = 412, HREG_E_AN_kWh_x1e5_H = 413,
  HREG_E_RP_kvarh_x1e5_L = 414, HREG_E_RP_kvarh_x1e5_H = 415,
  HREG_E_RN_kvarh_x1e5_L = 416, HREG_E_RN_kvarh_x1e5_H = 417,
  HREG_E_S_kVAh_x1e5_L = 418,  HREG_E_S_kVAh_x1e5_H = 419,

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
void echoRelayState() {
  JSONVar rly; rly[0]=relayState[0]; rly[1]=relayState[1];
  WebSerial.send("RelayState", rly);
}

// ===== Alarms helpers =====
static inline int32_t p_raw_to_W(int32_t p_raw){
  int64_t num = (int64_t)p_raw * (int64_t)p_scale_mW_per_lsb;
  return (int32_t)(num / 1000); // mW -> W
}
static inline int32_t q_raw_to_var(int32_t q_raw){
  int64_t num = (int64_t)q_raw * (int64_t)q_scale_mvar_per_lsb_x1000;
  return (int32_t)(num / 1000); // mvar*1000 -> var
}
static inline int32_t s_raw_to_VA(int32_t s_raw){
  int64_t num = (int64_t)s_raw * (int64_t)s_scale_mva_per_lsb_x1000;
  return (int32_t)(num / 1000); // mVA -> VA
}

static inline bool out_of_band(int32_t v, const AlarmRule& r){
  if (!r.enabled) return false;
  if (v < r.min) return true;
  if (v > r.max) return true;
  return false;
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
      chO[k]=r;
    }
    alCfg[ch]=chO;
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
      chO[k]=r;
    }
    st[ch]=chO;
  }
  WebSerial.send("AlarmsState", st);
}

// Snapshot of measurements (integer engineering units)
struct MetricsSnapshot {
  int32_t Urms_cV[3];   // 0.01 V
  int32_t Irms_mA[3];   // 0.001 A
  int32_t P_W[4];       // W (L1..L3, total)
  int32_t Q_var[4];     // var (L1..L3, total)
  int32_t S_VA[4];      // VA (L1..L3, total)
  int32_t Freq_cHz;     // 0.01 Hz
};

// ---- Explicit prototypes ----
static int32_t pick_metric_value(uint8_t ch, uint8_t metric, const MetricsSnapshot& m);
static void    eval_alarms_with_metrics(const MetricsSnapshot& snap);

// ---- Alarm evaluation ----
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
      int32_t v = pick_metric_value(ch, rule.metric, snap);
      bool cond = out_of_band(v, rule);
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

// ---- WebSerial alarm handlers ----
// 1) Full-array handler. If a per-channel object lands here, forward to AlarmsCfgCh.
void handleAlarmsCfg(JSONVar cfgArr){
  // Accept wrong-shape (per-channel) sent to "AlarmsCfg"
  if (cfgArr.hasOwnProperty("ch") || cfgArr.hasOwnProperty("cfg")) {
    handleAlarmsCfgCh(cfgArr);
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

// 2) Per-channel handler. Accepts both shapes:
//    A) { ch:0, ack:bool, "0":{...},"1":{...},"2":{...} }
//    B) { ch:0, cfg:{ ack:bool, "0":{...},"1":{...},"2":{...} } }
void handleAlarmsCfgCh(JSONVar v){
  if (!v.hasOwnProperty("ch")) { WebSerial.send("message","AlarmsCfgCh: missing ch"); return; }
  int ch = constrain((int)v["ch"], 0, CH_COUNT-1);

  JSONVar payload = v;
  if (v.hasOwnProperty("cfg")) payload = v["cfg"];  // unwrap nested

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

  // Echo back only this channel
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
}

// 3) Acknowledge alarms
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

// ================== WebSerial combined echo ==================
void sendAllEchoesOnce() {
  sendStatusEcho();

  JSONVar btnCfg; for(int i=0;i<4;i++) btnCfg[i]=buttonCfg[i].action;
  WebSerial.send("ButtonConfigList", btnCfg);

  WebSerial.send("LedConfigList", LedConfigListFromCfg());

  echoRelayState();

  JSONVar opts;
  opts["lineHz"]=atm_lineFreqHz; opts["sumAbs"]=atm_sumModeAbs; opts["ucal"]=atm_ucal;
  opts["sample_ms"]=sample_ms;
  opts["p_mW_per_lsb"]=p_scale_mW_per_lsb;
  opts["q_mvar_x1k_per_lsb"]=q_scale_mvar_per_lsb_x1000;
  opts["s_mva_per_lsb_x1000"]=s_scale_mva_per_lsb_x1000;
  opts["e_ap_kWh_x1e5"]=e_ap_kWh_per_cnt_x1e5;
  opts["e_an_kWh_x1e5"]=e_an_kWh_per_cnt_x1e5;
  opts["e_rp_kvarh_x1e5"]=e_rp_kvarh_per_cnt_x1e5;
  opts["e_rn_kWh_x1e5"]=e_rn_kvarh_per_cnt_x1e5;
  opts["e_rn_kvarh_x1e5"]=e_rn_kvarh_per_cnt_x1e5;
  opts["e_s_kVAh_x1e5"]=e_s_kVAh_per_cnt_x1e5;
  WebSerial.send("MeterOptions", opts);

  alarms_publish_cfg();
  alarms_publish_state();
}

void sendMeterEcho(double urms[3], double irms[3],
                   int32_t p_raw[4], int32_t q_raw[4], int32_t s_raw[4],
                   int16_t pf_raw[4], int16_t ang_raw[3],
                   uint16_t f_x100, int16_t tC,
                   uint16_t e_ap_raw, uint16_t e_an_raw, uint16_t e_rp_raw, uint16_t e_rn_raw, uint16_t e_s_raw)
{
  JSONVar m;
  for (int i=0;i<3;i++){ m["Urms"][i]=urms[i]; m["Irms"][i]=irms[i]; }

  for (int i=0;i<3;i++){ m["Praw"][i]=p_raw[i]; m["Qraw"][i]=q_raw[i]; m["Sraw"][i]=s_raw[i]; }
  m["Ptot_raw"]=p_raw[3]; m["Qtot_raw"]=q_raw[3]; m["Stot_raw"]=s_raw[3];

  for (int i=0;i<3;i++){ m["PFraw"][i]=pf_raw[i]; m["Ang0p1deg"][i]=ang_raw[i]; }
  m["PFtot_raw"]=pf_raw[3];

  m["FreqHz"]=f_x100/100.0; m["TempC"]=tC;

  m["Eraw"]["AP"]=e_ap_raw; m["Eraw"]["AN"]=e_an_raw; m["Eraw"]["RP"]=e_rp_raw; m["Eraw"]["RN"]=e_rn_raw; m["Eraw"]["S"]=e_s_raw;

  auto e_milli = [](uint16_t raw, uint32_t k_per_cnt_x1e5)->uint32_t{
    return (uint32_t)(( (uint64_t)raw * (uint64_t)k_per_cnt_x1e5 ) / 100ULL);
  };
  m["E"]["AP_mkWh"]= e_milli(e_ap_raw, e_ap_kWh_per_cnt_x1e5);
  m["E"]["AN_mkWh"]= e_milli(e_an_raw, e_an_kWh_per_cnt_x1e5);
  m["E"]["RP_mkvarh"]= e_milli(e_rp_raw, e_rp_kvarh_per_cnt_x1e5);
  m["E"]["RN_mkvarh"]= e_milli(e_rn_raw, e_rn_kvarh_per_cnt_x1e5);
  m["E"]["S_mkVAh"]= e_milli(e_s_raw, e_s_kVAh_per_cnt_x1e5);

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

// ---- WebSerial generic handlers ----
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
    uint16_t lf = (uint16_t)constrain((int)c["lineHz"], 50, 60);
    uint8_t  sm = (uint8_t)constrain((int)c["sumAbs"], 0, 1);
    uint16_t uc = (uint16_t)constrain((int)c["ucal"], 1, 65535);
    if (lf != atm_lineFreqHz) { atm_lineFreqHz = lf; reinit = true; }
    if (sm != atm_sumModeAbs) { atm_sumModeAbs = sm; reinit = true; }
    if (uc != atm_ucal)       { atm_ucal       = uc; reinit = true; }
    int smp = (int)c["sample_ms"]; sample_ms = (uint16_t)constrain(smp, 10, 5000);

    uint32_t v;
    if ((v=(uint32_t)c["p_mW_per_lsb"]))              p_scale_mW_per_lsb = v;
    if ((v=(uint32_t)c["q_mvar_x1k_per_lsb"]))        q_scale_mvar_per_lsb_x1000 = v;
    if ((v=(uint32_t)c["s_mva_per_lsb_x1000"]))       s_scale_mva_per_lsb_x1000  = v;
    else if ((v=(uint32_t)c["s_mva_x1k_per_lsb"]))    s_scale_mva_per_lsb_x1000  = v;
    if ((v=(uint32_t)c["e_ap_kWh_x1e5"]))            e_ap_kWh_per_cnt_x1e5 = v;
    if ((v=(uint32_t)c["e_an_kWh_x1e5"]))            e_an_kWh_per_cnt_x1e5 = v;
    if ((v=(uint32_t)c["e_rp_kvarh_x1e5"]))          e_rp_kvarh_per_cnt_x1e5 = v;
    if ((v=(uint32_t)c["e_rn_kWh_x1e5"]))            e_rn_kvarh_per_cnt_x1e5 = v;
    else if ((v=(uint32_t)c["e_rn_kvarh_x1e5"]))     e_rn_kvarh_per_cnt_x1e5 = v;
    if ((v=(uint32_t)c["e_s_kVAh_x1e5"]))            e_s_kVAh_per_cnt_x1e5 = v;

    changed = true;
    if (reinit) atm90_init();
    WebSerial.send("message", "Meter options updated");
  }
  else if (type == "leds") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
      ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 7);
    }
    WebSerial.send("message", "LEDs configuration updated");
    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    changed = true;
  }
  else if (type == "buttons") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      int a = (int)list[i]["action"];
      if (a < 0) a = 0; if (a > 6) a = 6;
      buttonCfg[i].action = (uint8_t)a;
    }
    WebSerial.send("message", "Buttons configuration updated");
    JSONVar btnCfg; for(int i=0;i<4;i++) btnCfg[i]=buttonCfg[i].action;
    WebSerial.send("ButtonConfigList", btnCfg);
    changed = true;
  }
  else if (type == "relays") { // legacy path
    JSONVar rcfg = obj["cfg"];
    relay_active_low = (uint8_t)constrain((int)rcfg["active_low"], 0, 1);
    relay_default[0] = (bool)rcfg["def0"];
    relay_default[1] = (bool)rcfg["def1"];
    setRelayPhys(0, relayState[0]); setRelayPhys(1, relayState[1]);
    WebSerial.send("message", "Relay configuration updated");
    changed = true;
  }
  else {
    WebSerial.send("message", String("Unknown Config type: ") + t);
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); }
}

// --- Direct relay control via WebSerial ---
void handleRelaysSet(JSONVar v){
  bool changed=false;

  if (v.hasOwnProperty("list") && v["list"].length() >= 2) {
    bool r0 = (bool)v["list"][0];
    bool r1 = (bool)v["list"][1];
    if (relayState[0] != r0) { setRelayPhys(0, r0); changed=true; }
    if (relayState[1] != r1) { setRelayPhys(1, r1); changed=true; }
  } else if (v.hasOwnProperty("r1") || v.hasOwnProperty("r2")) {
    if (v.hasOwnProperty("r1")) { bool r0=(bool)v["r1"]; if (relayState[0]!=r0){ setRelayPhys(0,r0); changed=true; } }
    if (v.hasOwnProperty("r2")) { bool r1=(bool)v["r2"]; if (relayState[1]!=r1){ setRelayPhys(1,r1); changed=true; } }
  } else if (v.hasOwnProperty("idx")) {
    int idx = constrain((int)v["idx"], 0, 1);
    if (v.hasOwnProperty("toggle") && (bool)v["toggle"]) {
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

// Configure relay polarity & power-on defaults (persisted)
void handleRelaysCfg(JSONVar v){
  if (v.hasOwnProperty("active_low")) {
    relay_active_low = (uint8_t)constrain((int)v["active_low"],0,1);
  }
  if (v.hasOwnProperty("def0")) relay_default[0] = (bool)v["def0"];
  if (v.hasOwnProperty("def1")) relay_default[1] = (bool)v["def1"];

  setRelayPhys(0, relayState[0]);
  setRelayPhys(1, relayState[1]);

  cfgDirty = true; lastCfgTouchMs = millis();
  WebSerial.send("message","RelaysCfg updated (persist)");
  echoRelayState();
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

  // LittleFS
  if (!LittleFS.begin()) { WebSerial.send("message", "LittleFS mount failed. Formatting…"); LittleFS.format(); LittleFS.begin(); }
  if (loadConfigFS()) WebSerial.send("message", "Config loaded from flash");
  else { WebSerial.send("message", "No valid config. Using defaults."); saveConfigFS(); }

  // Apply relay defaults
  setRelayPhys(0, relay_default[0]);
  setRelayPhys(1, relay_default[1]);

  // SPI1 + CS
  pinMode(PIN_ATM_CS, OUTPUT); csRelease();
  MCM_SPI.setSCK(PIN_SPI_SCK);
  MCM_SPI.setTX(PIN_SPI_MOSI);
  MCM_SPI.setRX(PIN_SPI_MISO);
  MCM_SPI.begin();

  // Meter init
  atm90_init();

  // Modbus RTU
  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud);
  mb.config(g_mb_baud);
  setSlaveIdIfAvailable(mb, g_mb_address);

  // ---- Register telemetry regs ----
  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_URMS_BASE + i);
  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_IRMS_BASE + i);

  for (uint16_t i=0;i<6;i++) mb.addIreg(IREG_P_BASE + i);
  for (uint16_t i=0;i<6;i++) mb.addIreg(IREG_Q_BASE + i);
  for (uint16_t i=0;i<6;i++) mb.addIreg(IREG_S_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_PTOT + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_QTOT + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_STOT + i);

  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_PF_BASE + i);
  mb.addIreg(IREG_PF_TOT);
  for (uint16_t i=0;i<3;i++) mb.addIreg(IREG_ANGLE_BASE + i);

  mb.addIreg(IREG_FREQ_X100);
  mb.addIreg(IREG_TEMP_C);

  for (uint16_t i=0;i<5;i++) mb.addIreg(IREG_E_AP_RAW + i);
  for (uint16_t i=0;i<10;i++) mb.addIreg(IREG_E_AP_MILLI + i);

  mb.addIreg(IREG_EMM0); mb.addIreg(IREG_EMM1); mb.addIreg(IREG_INT0); mb.addIreg(IREG_INT1);
  mb.addIreg(IREG_CRCERR); mb.addIreg(IREG_LASTSPI);

  // ---- Holding (RW) ----
  mb.addHreg(HREG_SMPLL_MS); mb.Hreg(HREG_SMPLL_MS, sample_ms);
  mb.addHreg(HREG_LFREQ_HZ); mb.Hreg(HREG_LFREQ_HZ, atm_lineFreqHz);
  mb.addHreg(HREG_SUMMODE);  mb.Hreg(HREG_SUMMODE,  atm_sumModeAbs);
  mb.addHreg(HREG_UCAL);     mb.Hreg(HREG_UCAL,     atm_ucal);

  auto setU32 = [&](uint16_t regL, uint32_t v){ mb.addHreg(regL); mb.addHreg(regL+1); mb.Hreg(regL, (uint16_t)(v & 0xFFFF)); mb.Hreg(regL+1, (uint16_t)(v>>16)); };
  setU32(HREG_P_SCALE_mW_L,    p_scale_mW_per_lsb);
  setU32(HREG_Q_SCALE_X1k_L,   q_scale_mvar_per_lsb_x1000);
  setU32(HREG_S_SCALE_X1k_L,   s_scale_mva_per_lsb_x1000);
  setU32(HREG_E_AP_kWh_x1e5_L, e_ap_kWh_per_cnt_x1e5);
  setU32(HREG_E_AN_kWh_x1e5_L, e_an_kWh_per_cnt_x1e5);
  setU32(HREG_E_RP_kvarh_x1e5_L, e_rp_kvarh_per_cnt_x1e5);
  setU32(HREG_E_RN_kvarh_x1e5_L, e_rn_kvarh_per_cnt_x1e5);
  setU32(HREG_E_S_kVAh_x1e5_L,  e_s_kVAh_per_cnt_x1e5);

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
  WebSerial.on("AlarmsCfg",  handleAlarmsCfg);      // full array (also forwards per‑channel)
  WebSerial.on("AlarmsCfgCh",handleAlarmsCfgCh);    // per‑channel
  WebSerial.on("AlarmsAck",  handleAlarmsAck);      // ACK

  // Initial echoes
  sendAllEchoesOnce();
  WebSerial.send("message", "Boot OK (ENM-223-R1 — alarms accept array & per‑channel payloads; persist on Apply)");
}

// ================== HREG write watcher ==================
static uint32_t readU32H(uint16_t regL){ return ((uint32_t)mb.Hreg(regL+1)<<16) | mb.Hreg(regL); }

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
  if (uc != prevUc) { prevUc=uc; atm_ucal = uc; reinit=true; }

  p_scale_mW_per_lsb            = readU32H(HREG_P_SCALE_mW_L);
  q_scale_mvar_per_lsb_x1000    = readU32H(HREG_Q_SCALE_X1k_L);
  s_scale_mva_per_lsb_x1000     = readU32H(HREG_S_SCALE_X1k_L);
  e_ap_kWh_per_cnt_x1e5         = readU32H(HREG_E_AP_kWh_x1e5_L);
  e_an_kWh_per_cnt_x1e5         = readU32H(HREG_E_AN_kWh_x1e5_L);
  e_rp_kvarh_per_cnt_x1e5       = readU32H(HREG_E_RP_kvarh_x1e5_L);
  e_rn_kvarh_per_cnt_x1e5       = readU32H(HREG_E_RN_kvarh_x1e5_L);
  e_s_kVAh_per_cnt_x1e5         = readU32H(HREG_E_S_kVAh_x1e5_L);

  if (reinit) { atm90_init(); cfgDirty=true; lastCfgTouchMs=millis(); }
}

// ================== Button actions ==================
// Actions: 0 None | 1 Toggle Relay1 | 2 Toggle Relay2 | 3..6 Toggle LED0..3
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

// ================== LED source evaluation ==================
bool evalLedSource(uint8_t src) {
  switch (src) {
    case 0: return false;                         // None
    case 1: return blinkPhase;                    // Heartbeat
    case 2: return buttonState[0];
    case 3: return buttonState[1];
    case 4: return buttonState[2];
    case 5: return buttonState[3];
    case 6: return buttonState[0]||buttonState[1]||buttonState[2]||buttonState[3];
    case 7: return samplingTick;                  // pulse on each poll
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

  // Modbus coils mirror to physical relays
  if (mb.Coil(COIL_RLY1) != relayState[0]) setRelayPhys(0, mb.Coil(COIL_RLY1));
  if (mb.Coil(COIL_RLY2) != relayState[1]) setRelayPhys(1, mb.Coil(COIL_RLY2));

  // Alarm ACK coils (edge)
  for (uint16_t ch=0; ch<CH_COUNT; ++ch) {
    uint16_t coil = COIL_ALARM_ACK_BASE + ch;
    if (mb.Coil(coil)) { alarms_ack_channel((uint8_t)ch); mb.Coil(coil, 0); }
  }

  // Buttons
  for (int i = 0; i < 4; i++) {
    bool pressed = readPressed(i);
    if (pressed != buttonState[i] && (now - btnChangeAt[i] >= BTN_DEBOUNCE_MS)) {
      btnChangeAt[i] = now; buttonPrev[i]  = buttonState[i]; buttonState[i] = pressed;
      if (buttonPrev[i] && !buttonState[i]) { doButtonAction(i); echoRelayState(); } // on release
    }
    mb.setIsts(ISTS_BTN_BASE + i, buttonState[i]);
  }

  // ===== Sampling =====
  samplingTick = false;
  if (now - lastSample >= sample_ms) {
    lastSample = now; samplingTick = true;

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

    // ---- Powers RAW ----
    int32_t p_raw[4], q_raw[4], s_raw[4];
    p_raw[0]=read_power32(PmeanA, PmeanALSB); p_raw[1]=read_power32(PmeanB, PmeanBLSB); p_raw[2]=read_power32(PmeanC, PmeanCLSB);
    q_raw[0]=read_power32(QmeanA, QmeanALSB); q_raw[1]=read_power32(QmeanB, QmeanBLSB); q_raw[2]=read_power32(QmeanC, QmeanCLSB);
    s_raw[0]=read_power32(SmeanA, SmeanALSB); s_raw[1]=read_power32(SmeanB, SmeanBLSB); s_raw[2]=read_power32(SmeanC, SmeanCLSB);
    p_raw[3]=read_power32(PmeanT, PmeanTLSB);
    q_raw[3]=read_power32(QmeanT, QmeanTLSB);
    s_raw[3]=read_power32(SmeanT, SAmeanTLSB);

    auto put_s32 = [&](uint16_t base, int32_t v){
      mb.Ireg(base+0, (uint16_t)((v>>16)&0xFFFF));
      mb.Ireg(base+1, (uint16_t)(v & 0xFFFF));
    };
    put_s32(IREG_P_BASE+0, p_raw[0]); put_s32(IREG_P_BASE+2, p_raw[1]); put_s32(IREG_P_BASE+4, p_raw[2]);
    put_s32(IREG_Q_BASE+0, q_raw[0]); put_s32(IREG_Q_BASE+2, q_raw[1]); put_s32(IREG_Q_BASE+4, q_raw[2]);
    put_s32(IREG_S_BASE+0, s_raw[0]); put_s32(IREG_S_BASE+2, s_raw[1]); put_s32(IREG_S_BASE+4, s_raw[2]);
    put_s32(IREG_PTOT, p_raw[3]); put_s32(IREG_QTOT, q_raw[3]); put_s32(IREG_STOT, s_raw[3]);

    // ---- PF & Angles ----
    int16_t pf_raw[4];
    pf_raw[0]=(int16_t)atm90_read16(PFmeanA);
    pf_raw[1]=(int16_t)atm90_read16(PFmeanB);
    pf_raw[2]=(int16_t)atm90_read16(PFmeanC);
    pf_raw[3]=(int16_t)atm90_read16(PFmeanT);
    for (int i=0;i<3;i++) mb.Ireg(IREG_PF_BASE + i, (uint16_t)pf_raw[i]);
    mb.Ireg(IREG_PF_TOT, (uint16_t)pf_raw[3]);

    int16_t ang_raw[3];
    ang_raw[0]=(int16_t)atm90_read16(PAngleA);
    ang_raw[1]=(int16_t)atm90_read16(PAngleB);
    ang_raw[2]=(int16_t)atm90_read16(PAngleC);
    for (int i=0;i<3;i++) mb.Ireg(IREG_ANGLE_BASE + i, (uint16_t)ang_raw[i]);

    // ---- Freq/Temp ----
    uint16_t fraw = atm90_read16(Freq);
    int16_t  tC   = (int16_t)atm90_read16(Temp);
    mb.Ireg(IREG_FREQ_X100, fraw);
    mb.Ireg(IREG_TEMP_C, (uint16_t)tC);

    // ---- Energies ----
    uint16_t e_ap = atm90_read16(APenergyT);
    uint16_t e_an = atm90_read16(ANenergyT);
    uint16_t e_rp = atm90_read16(RPenergyT);
    uint16_t e_rn = atm90_read16(RNenergyT);
    uint16_t e_s  = atm90_read16(SAenergyT);
    mb.Ireg(IREG_E_AP_RAW, e_ap);
    mb.Ireg(IREG_E_AN_RAW, e_an);
    mb.Ireg(IREG_E_RP_RAW, e_rp);
    mb.Ireg(IREG_E_RN_RAW, e_rn);
    mb.Ireg(IREG_E_S_RAW,  e_s);

    auto e_milli = [](uint16_t raw, uint32_t k_per_cnt_x1e5)->uint32_t{
      return (uint32_t)(( (uint64_t)raw * (uint64_t)k_per_cnt_x1e5 ) / 100ULL);
    };
    auto put_u32 = [&](uint16_t base, uint32_t v){
      mb.Ireg(base+0, (uint16_t)((v>>16)&0xFFFF));
      mb.Ireg(base+1, (uint16_t)(v & 0xFFFF));
    };
    put_u32(IREG_E_AP_MILLI, e_milli(e_ap, e_ap_kWh_per_cnt_x1e5));
    put_u32(IREG_E_AN_MILLI, e_milli(e_an, e_an_kWh_per_cnt_x1e5));
    put_u32(IREG_E_RP_MILLI, e_milli(e_rp, e_rp_kvarh_per_cnt_x1e5));
    put_u32(IREG_E_RN_MILLI, e_milli(e_rn, e_rn_kvarh_per_cnt_x1e5));
    put_u32(IREG_E_S_MILLI,  e_milli(e_s,  e_s_kVAh_per_cnt_x1e5));

    // ---- Build metrics snapshot for alarms ----
    MetricsSnapshot ms{};
    for (int i=0;i<3;i++){
      ms.Urms_cV[i] = (int32_t)lround(urms[i]*100.0);   // 0.01 V
      ms.Irms_mA[i] = (int32_t)lround(irms[i]*1000.0);  // 0.001 A
    }
    for (int i=0;i<4;i++){
      ms.P_W[i]   = p_raw_to_W(p_raw[i]);
      ms.Q_var[i] = q_raw_to_var(q_raw[i]);
      ms.S_VA[i]  = s_raw_to_VA(s_raw[i]);
    }
    ms.Freq_cHz = (int32_t)fraw; // already ×100

    // ---- Alarm evaluation ----
    eval_alarms_with_metrics(ms);

    // ---- WebSerial live echo ----
    sendMeterEcho(urms, irms, p_raw, q_raw, s_raw, pf_raw, ang_raw, fraw, tC, e_ap,e_an,e_rp,e_rn,e_s);
  }

  // LEDs
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
    echoRelayState();
    alarms_publish_state();
  }
}
