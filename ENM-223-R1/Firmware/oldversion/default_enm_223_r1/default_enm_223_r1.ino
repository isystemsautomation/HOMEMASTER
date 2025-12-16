// ==== ENM-223-R1 (RP2350/RP2040) ============================================
// SINGLE-FILE BUILD:
// - ATM90E32 driver (SPI1) inline (NO external driver files)
// - Modbus RTU inline (NO external enm_modbus.* files)
// - WebSerial UI (REAL VALUES ONLY), LittleFS persistence
// - Buttons GPIO22..25, LEDs GPIO18..21, Relays GPIO0/1
// - Per-relay Button Override (R1/R2), LED alarm sources
//
// RAW ATM (UPDATED):
// - Shows REAL chip-stored config by READING ATM registers (Ugain/Igain/U/I offsets, modes, thresholds, etc.)
// - DOES NOT show "attempted write" values anymore.
//
// FIXED (8) kept + NEW ENERGY FIX:
// 1) MetricsSnapshot defined BEFORE use
// 2) WebSerial.check() runs continuously
// 3) Change-only GPIO updates for LEDs/Relays (+ FORCE re-drive on polarity change)
// 4) REMOVED UCAL from config/UI/Modbus/persist; chip internal ucalRef derived from Phase-A Ugain
// 5) SumMode/LineHz re-init does NOT wipe calibration (cal is re-applied after begin())
// 6) FIX compile error with MB_setDiag(): M90DiagRegs COMPLETE and matches usage
// 7) Web/UI message handlers completed: CalibCfg, AlarmsCfg, AlarmsAck + LedStateList push
// 8) FIX readback going 0x8000 after phase-A change:
//    - Calibration writes now use cfgAccessBegin/End + a small commit delay
//    - Readback always done after commit delay; no “back-to-back” enable/disable race
//
// NEW ENERGY FIX (REAL COUNTER FROM ATM):
// - Energy regs are 0.01 CF units (read-to-clear). Convert using MC read from PLconstH/L.
// - MC (imp/kWh) is derived from PLconst: MC = 450,000,000,000 / PLconst (32-bit).
// - kWh = ticks_0p01CF / (MC * 100), Wh = ticks_0p01CF * 10 / MC.
// - Removes the old fake "kWh_per_cnt_x1e5" scaling.
// ============================================================================

#include <Arduino.h>

struct RawRow {
  uint16_t reg;
  const char* name;
  uint16_t val;
};


#include <SPI.h>
#include <math.h>
#include <limits>
#include <utility>
#include <cstring>

#include <LittleFS.h>
#include <Arduino_JSON.h>
#include <SimpleWebSerial.h>
#include <ModbusSerial.h>

// ===== Arduino .ino auto-prototype fix =====
struct PersistConfig;
void captureToPersist(PersistConfig &pc);
bool applyFromPersist(const PersistConfig &pc);

// ================== Shared alarm dimensions (must match Modbus map) ==================
enum : uint8_t { CH_L1=0, CH_L2, CH_L3, CH_TOT, CH_COUNT };
enum : uint8_t { AK_ALARM=0, AK_WARNING, AK_EVENT, AK_COUNT };

// ================== Hardware ==================
constexpr uint8_t LED_PINS[4]    = {18,19,20,21};
constexpr uint8_t BUTTON_PINS[4] = {22,23,24,25};
constexpr uint8_t RELAY_PINS[2]  = {0,1};

// IMPORTANT: these are used in preprocessor #if, so keep as macros (0/1)
#define LED_ACTIVE_LOW        0
#define BUTTON_USES_PULLUP    0   // pressed == HIGH if 0
constexpr bool RELAY_ACTIVE_LOW_DFLT = false;

// ================== RS-485 / Modbus UART2 ==================
constexpr int  TX2_PIN   = 4;
constexpr int  RX2_PIN   = 5;
constexpr int  TXEN_PIN  = -1;     // not used
constexpr int  SLAVE_ID  = 30;

// ===== ATM90E32 on SPI1 =====
#define MCM_SPI SPI1
constexpr uint8_t PIN_SPI_SCK  = 10;
constexpr uint8_t PIN_SPI_MOSI = 11;
constexpr uint8_t PIN_SPI_MISO = 12;
constexpr uint8_t PIN_ATM_CS   = 13;
constexpr uint8_t PIN_PM1      = 2;
constexpr uint8_t PIN_PM0      = 3;

constexpr bool     CS_ACTIVE_HIGH = false;
constexpr uint8_t  ATM_SPI_MODE   = SPI_MODE0;
constexpr uint32_t SPI_HZ         = 200000;

// ============================================================================
// ATM90E32 inline driver (+ raw read hooks + PUBLIC readReg16 for REAL chip config readback)
// ============================================================================
namespace enm223 {

struct M90PhaseCal {
  uint16_t Ugain;
  uint16_t Igain;
  int16_t  Uoffset;
  int16_t  Ioffset;
};

struct M90DiagRegs {
  uint16_t EMMState0;
  uint16_t EMMState1;
  uint16_t EMMIntState0;
  uint16_t EMMIntState1;
  uint16_t CRCErrStatus;
  uint16_t LastSPIData;
};

class ATM90E32_Inline {
public:
  using HookFn = void(*)(uint16_t reg, uint16_t val);

  ATM90E32_Inline(SPIClass& spi, uint8_t pinCS, uint8_t pinPM0, uint8_t pinPM1,
                  uint32_t spiHz = 200000, uint8_t spiMode = SPI_MODE0,
                  bool csActiveHigh = false)
  : spi_(spi), cs_(pinCS), pm0_(pinPM0), pm1_(pinPM1),
    spiHz_(spiHz), spiMode_(spiMode), csActiveHigh_(csActiveHigh) {}

  void setHooks(HookFn onRead, HookFn onWrite) { onRead_ = onRead; onWrite_ = onWrite; }

  // ---- Register map (subset) ----
  static constexpr uint16_t MeterEn       = 0x00;
  static constexpr uint16_t SagPeakDetCfg = 0x05;
  static constexpr uint16_t ZXConfig      = 0x07;
  static constexpr uint16_t SagTh         = 0x08;
  static constexpr uint16_t FreqLoTh      = 0x0C;
  static constexpr uint16_t FreqHiTh      = 0x0D;

  static constexpr uint16_t SoftReset     = 0x70;
  static constexpr uint16_t EMMState0     = 0x71;
  static constexpr uint16_t EMMState1     = 0x72;
  static constexpr uint16_t EMMIntState0  = 0x73;
  static constexpr uint16_t EMMIntState1  = 0x74;
  static constexpr uint16_t EMMIntEn0     = 0x75;
  static constexpr uint16_t EMMIntEn1     = 0x76;
  static constexpr uint16_t LastSPIData   = 0x78;
  static constexpr uint16_t CRCErrStatus  = 0x79;
  static constexpr uint16_t CfgRegAccEn   = 0x7F;

  static constexpr uint16_t PLconstH      = 0x31;
  static constexpr uint16_t PLconstL      = 0x32;
  static constexpr uint16_t MMode0        = 0x33;
  static constexpr uint16_t MMode1        = 0x34;
  static constexpr uint16_t PStartTh      = 0x35;
  static constexpr uint16_t QStartTh      = 0x36;
  static constexpr uint16_t SStartTh      = 0x37;
  static constexpr uint16_t PPhaseTh      = 0x38;
  static constexpr uint16_t QPhaseTh      = 0x39;
  static constexpr uint16_t SPhaseTh      = 0x3A;

  // Cal
  static constexpr uint16_t UgainA   = 0x61, IgainA   = 0x62, UoffsetA = 0x63, IoffsetA = 0x64;
  static constexpr uint16_t UgainB   = 0x65, IgainB   = 0x66, UoffsetB = 0x67, IoffsetB = 0x68;
  static constexpr uint16_t UgainC   = 0x69, IgainC   = 0x6A, UoffsetC = 0x6B, IoffsetC = 0x6C;

  // Energies (0.01CF units, read-to-clear)
  static constexpr uint16_t APenergyT = 0x80;
  static constexpr uint16_t APenergyA = 0x81;
  static constexpr uint16_t APenergyB = 0x82;
  static constexpr uint16_t APenergyC = 0x83;
  static constexpr uint16_t ANenergyT = 0x84;
  static constexpr uint16_t ANenergyA = 0x85;
  static constexpr uint16_t ANenergyB = 0x86;
  static constexpr uint16_t ANenergyC = 0x87;
  static constexpr uint16_t RPenergyT = 0x88;
  static constexpr uint16_t RPenergyA = 0x89;
  static constexpr uint16_t RPenergyB = 0x8A;
  static constexpr uint16_t RPenergyC = 0x8B;
  static constexpr uint16_t RNenergyT = 0x8C;
  static constexpr uint16_t RNenergyA = 0x8D;
  static constexpr uint16_t RNenergyB = 0x8E;
  static constexpr uint16_t RNenergyC = 0x8F;
  static constexpr uint16_t SAenergyT = 0x90;
  static constexpr uint16_t SAenergyA = 0x91;
  static constexpr uint16_t SAenergyB = 0x92;
  static constexpr uint16_t SAenergyC = 0x93;

  // Live
  static constexpr uint16_t PFmeanT  = 0xBC;
  static constexpr uint16_t PFmeanA  = 0xBD;
  static constexpr uint16_t PFmeanB  = 0xBE;
  static constexpr uint16_t PFmeanC  = 0xBF;
  static constexpr uint16_t Freq     = 0xF8;
  static constexpr uint16_t PAngleA  = 0xF9;
  static constexpr uint16_t PAngleB  = 0xFA;
  static constexpr uint16_t PAngleC  = 0xFB;
  static constexpr uint16_t Temp     = 0xFC;

  // RMS
  static constexpr uint16_t REG_UrmsA    = 0xD9;
  static constexpr uint16_t REG_UrmsB    = 0xDA;
  static constexpr uint16_t REG_UrmsC    = 0xDB;
  static constexpr uint16_t REG_IrmsA    = 0xDD;
  static constexpr uint16_t REG_IrmsB    = 0xDE;
  static constexpr uint16_t REG_IrmsC    = 0xDF;
  static constexpr uint16_t REG_UrmsALSB = 0xE9;
  static constexpr uint16_t REG_UrmsBLSB = 0xEA;
  static constexpr uint16_t REG_UrmsCLSB = 0xEB;
  static constexpr uint16_t REG_IrmsALSB = 0xED;
  static constexpr uint16_t REG_IrmsBLSB = 0xEE;
  static constexpr uint16_t REG_IrmsCLSB = 0xEF;

  // --- config access gate (with settle delays) ---
  void cfgAccessBegin() {
    write16(CfgRegAccEn, 0x55AA);
    delayMicroseconds(80);
  }
  void cfgAccessEnd() {
    write16(CfgRegAccEn, 0x0000);
    delayMicroseconds(80);
  }

  // Public raw access (READBACK from chip config!)
  uint16_t readReg16(uint16_t reg) { return read16(reg); }
  void     writeReg16(uint16_t reg, uint16_t val) { write16(reg, val); }

  // NOTE: ucalRef is ONLY used internally for sag threshold math (not user-config anymore).
  void begin(uint16_t lineHz, uint8_t sumAbs, uint16_t ucalRef, const M90PhaseCal cal[3]) {
    lineHz_ = (lineHz == 60) ? 60 : 50;
    sumAbs_ = sumAbs ? 1 : 0;
    ucalRef_ = (ucalRef == 0) ? 1 : ucalRef;

    pinMode(pm0_, OUTPUT);
    pinMode(pm1_, OUTPUT);
    digitalWrite(pm0_, HIGH);
    digitalWrite(pm1_, HIGH);
    delay(5);

    write16(SoftReset, 0x789A);
    delay(110);

    cfgAccessBegin();
    write16(MeterEn, 0xFFFF);

    // Sag/freq thresholds
    uint16_t sagV, FreqHiThresh, FreqLoThresh;
    if (lineHz_ == 60){ sagV=90;  FreqHiThresh=6100; FreqLoThresh=5900; }
    else              { sagV=190; FreqHiThresh=5100; FreqLoThresh=4900; }

    const double ucalRatio = ucalRef_ / 32768.0;
    const uint16_t vSagTh = (uint16_t)((sagV * 100.0 * sqrt(2.0)) / (2.0 * ucalRatio));

    write16(SagPeakDetCfg, 0x143F);
    write16(SagTh,        vSagTh);
    write16(FreqHiTh,     FreqHiThresh);
    write16(FreqLoTh,     FreqLoThresh);

    write16(EMMIntEn0, 0xB76F);
    write16(EMMIntEn1, 0xDDFD);
    write16(EMMIntState0, 0x0001);
    write16(EMMIntState1, 0x0001);
    write16(ZXConfig, 0xD654);

    // Modes
    uint16_t m0 = 0x019D;
    if (lineHz_ == 60) m0 |= (1u<<12); else m0 &= ~(1u<<12);
    m0 &= ~(0b11u<<3);
    m0 |= ((sumAbs_ ? 0b11u : 0b00u) << 3);
    m0 &= ~0b111u; m0 |= 0b101u;

    auto gainCode = [](uint8_t g)->uint8_t{ if(g==1)return 0; if(g==2)return 1; if(g==4)return 2; return 1; };
    const uint8_t gIA=1,gIB=1,gIC=1;
    uint8_t m1=0; m1|=(gainCode(gIA)<<0); m1|=(gainCode(gIB)<<2); m1|=(gainCode(gIC)<<4);

    // Your PLconst values (kept). MC will be READ FROM THESE regs for real energy conversion.
    write16(PLconstH, 0x0861);
    write16(PLconstL, 0xC468);

    write16(MMode0, m0);
    write16(MMode1, m1);
    write16(PStartTh, 0x1D4C);
    write16(QStartTh, 0x1D4C);
    write16(SStartTh, 0x1D4C);
    write16(PPhaseTh, 0x02EE);
    write16(QPhaseTh, 0x02EE);
    write16(SPhaseTh, 0x02EE);

    // (Fix #5) calibration is re-applied after begin() changes
    applyCalibration(cal);

    cfgAccessEnd();
    delay(2); // commit settle
  }

  // ---- NEW: write cal with gated access + commit settle (fixes 0x8000 “immediate readback” glitch) ----
  void applyCalibration(const M90PhaseCal cal[3]) {
    cfgAccessBegin();

    write16(UgainA,   cal[0].Ugain);   write16(IgainA,   cal[0].Igain);
    write16(UoffsetA, (uint16_t)cal[0].Uoffset); write16(IoffsetA, (uint16_t)cal[0].Ioffset);

    write16(UgainB,   cal[1].Ugain);   write16(IgainB,   cal[1].Igain);
    write16(UoffsetB, (uint16_t)cal[1].Uoffset); write16(IoffsetB, (uint16_t)cal[1].Ioffset);

    write16(UgainC,   cal[2].Ugain);   write16(IgainC,   cal[2].Igain);
    write16(UoffsetC, (uint16_t)cal[2].Uoffset); write16(IoffsetC, (uint16_t)cal[2].Ioffset);

    cfgAccessEnd();
    delay(2); // allow internal latch before any readback
    (void)read16(LastSPIData); // harmless dummy read to “flush” SPI path
  }

  double readRmsLike(uint16_t regH, uint16_t regLSB, double sH, double sLb){
    const uint16_t h = read16(regH);
    const uint16_t l = read16(regLSB);
    return (h * sH) + (((l >> 8) & 0xFF) * sLb);
  }

  int16_t  readPFmeanA(){ return (int16_t)read16(PFmeanA); }
  int16_t  readPFmeanB(){ return (int16_t)read16(PFmeanB); }
  int16_t  readPFmeanC(){ return (int16_t)read16(PFmeanC); }
  int16_t  readPFmeanT(){ return (int16_t)read16(PFmeanT); }

  int16_t  readPAngleA(){ return (int16_t)read16(PAngleA); }
  int16_t  readPAngleB(){ return (int16_t)read16(PAngleB); }
  int16_t  readPAngleC(){ return (int16_t)read16(PAngleC); }

  uint16_t readFreq_x100(){ return read16(Freq); }
  int16_t  readTempC(){ return (int16_t)read16(Temp); }

  uint16_t rdAP_A(){ return read16(APenergyA); }
  uint16_t rdAP_B(){ return read16(APenergyB); }
  uint16_t rdAP_C(){ return read16(APenergyC); }
  uint16_t rdAP_T(){ return read16(APenergyT); }

  uint16_t rdAN_A(){ return read16(ANenergyA); }
  uint16_t rdAN_B(){ return read16(ANenergyB); }
  uint16_t rdAN_C(){ return read16(ANenergyC); }
  uint16_t rdAN_T(){ return read16(ANenergyT); }

  uint16_t rdRP_A(){ return read16(RPenergyA); }
  uint16_t rdRP_B(){ return read16(RPenergyB); }
  uint16_t rdRP_C(){ return read16(RPenergyC); }
  uint16_t rdRP_T(){ return read16(RPenergyT); }

  uint16_t rdRN_A(){ return read16(RNenergyA); }
  uint16_t rdRN_B(){ return read16(RNenergyB); }
  uint16_t rdRN_C(){ return read16(RNenergyC); }
  uint16_t rdRN_T(){ return read16(RNenergyT); }

  uint16_t rdSA_A(){ return read16(SAenergyA); }
  uint16_t rdSA_B(){ return read16(SAenergyB); }
  uint16_t rdSA_C(){ return read16(SAenergyC); }
  uint16_t rdSA_T(){ return read16(SAenergyT); }

  M90DiagRegs readDiag() {
    M90DiagRegs d{};
    d.EMMState0    = read16(EMMState0);
    d.EMMState1    = read16(EMMState1);
    d.EMMIntState0 = read16(EMMIntState0);
    d.EMMIntState1 = read16(EMMIntState1);
    d.CRCErrStatus = read16(CRCErrStatus);
    d.LastSPIData  = read16(LastSPIData);
    return d;
  }

private:
  uint16_t xfer(uint8_t rw, uint16_t reg, uint16_t val) {
    const uint16_t data_swapped = (uint16_t)((val >> 8) | (val << 8));
    const uint16_t addr = reg | (rw ? 0x8000 : 0x0000);
    const uint16_t addr_swapped = (uint16_t)((addr >> 8) | (addr << 8));

    SPISettings settings(spiHz_, MSBFIRST, spiMode_);
    spi_.beginTransaction(settings);
    csAssert();
    delayMicroseconds(10);

    const uint8_t *pa = reinterpret_cast<const uint8_t*>(&addr_swapped);
    spi_.transfer(pa[0]);
    spi_.transfer(pa[1]);

    delayMicroseconds(4);

    uint16_t out = 0;
    if (rw) {
      uint8_t b0 = spi_.transfer(0x00);
      uint8_t b1 = spi_.transfer(0x00);
      const uint16_t raw = (uint16_t)(b0 | (uint16_t(b1) << 8));
      out = (uint16_t)((raw >> 8) | (raw << 8));
    } else {
      const uint8_t *pd = reinterpret_cast<const uint8_t*>(&data_swapped);
      spi_.transfer(pd[0]);
      spi_.transfer(pd[1]);
      out = val;
    }

    csRelease();
    delayMicroseconds(10);
    spi_.endTransaction();
    return out;
  }

  inline void csAssert()  { digitalWrite(cs_, csActiveHigh_ ? HIGH : LOW);  }
  inline void csRelease() { digitalWrite(cs_, csActiveHigh_ ? LOW  : HIGH); }

  inline uint16_t read16(uint16_t reg) {
    uint16_t v = xfer(1, reg, 0xFFFF);
    if (onRead_) onRead_(reg, v);
    return v;
  }

  inline void write16(uint16_t reg, uint16_t val) {
    (void)xfer(0, reg, val);
    if (onWrite_) onWrite_(reg, val);
  }

  uint16_t lineHz_ = 50;
  uint8_t  sumAbs_ = 1;
  uint16_t ucalRef_ = 25256;

  SPIClass &spi_;
  uint8_t cs_, pm0_, pm1_;
  uint32_t spiHz_;
  uint8_t  spiMode_;
  bool     csActiveHigh_;

  HookFn onRead_  = nullptr;
  HookFn onWrite_ = nullptr;
};

} // namespace enm223

static enm223::ATM90E32_Inline g_atm(MCM_SPI, PIN_ATM_CS, PIN_PM0, PIN_PM1, SPI_HZ, ATM_SPI_MODE, CS_ACTIVE_HIGH);

// ============================================================================
// Modbus inline
// ============================================================================
namespace enm223 {

static ModbusSerial mb(Serial2, SLAVE_ID, TXEN_PIN);
static uint8_t  g_addr  = SLAVE_ID;
static uint32_t g_baud  = 19200;

// SFINAE: call setSlaveId only if library provides it
template<typename M>
auto _trySetSlaveId(M& m, uint8_t id, int) -> decltype(m.setSlaveId(id), void()) { m.setSlaveId(id); }
template<typename M>
void _trySetSlaveId(M&, uint8_t, long) {}

static inline void _put_u32(uint16_t reg, uint32_t v){
  mb.Ireg(reg+0, (uint16_t)((v>>16)&0xFFFF));
  mb.Ireg(reg+1, (uint16_t)(v & 0xFFFF));
}
static inline void _put_s32(uint16_t reg, int32_t v){
  mb.Ireg(reg+0, (uint16_t)((v>>16)&0xFFFF));
  mb.Ireg(reg+1, (uint16_t)(v & 0xFFFF));
}

// (Fix #2 / #7 side-effects) Build map ONCE, never re-add registers repeatedly.
void MB_buildRegisterMap_once(uint16_t sample_ms, uint16_t lineHz, uint8_t sumAbs)
{
  static bool built=false;
  if (built) {

    return;
  }
  built = true;

  for (uint16_t i=0;i<3;i++) mb.addIreg(100 + i);
  for (uint16_t i=0;i<3;i++) mb.addIreg(110 + i);

  for (uint16_t i=0;i<8;i++) mb.addIreg(200 + i);
  for (uint16_t i=0;i<8;i++) mb.addIreg(210 + i);
  for (uint16_t i=0;i<8;i++) mb.addIreg(220 + i);

  for (uint16_t i=0;i<3;i++) mb.addIreg(240 + i);
  mb.addIreg(243);
  for (uint16_t i=0;i<3;i++) mb.addIreg(244 + i);

  mb.addIreg(250);
  mb.addIreg(251);

  for (uint16_t r=300; r<=338+1; ++r) mb.addIreg(r);

  for (uint16_t i=0;i<4;i++) mb.addIsts(500 + i);
  for (uint16_t i=0;i<4;i++) mb.addIsts(520 + i);
  for (uint16_t i=0;i<2;i++) mb.addIsts(540 + i);
  for (uint16_t i=0;i<CH_COUNT*AK_COUNT;i++) mb.addIsts(560 + i);

  mb.addCoil(600); mb.Coil(600, 0);
  mb.addCoil(601); mb.Coil(601, 0);
  for (uint16_t i=0;i<CH_COUNT;i++){ mb.addCoil(610 + i); mb.Coil(610 + i, 0); }
}

void MB_begin(uint8_t slaveId, uint32_t baud, uint16_t sample_ms, uint16_t lineHz, uint8_t sumAbs)
{
  g_addr = slaveId; g_baud = baud;

  Serial2.setTX(TX2_PIN);
  Serial2.setRX(RX2_PIN);
  Serial2.begin(g_baud);

  mb.config(g_baud);
  _trySetSlaveId(mb, g_addr, 0);

  MB_buildRegisterMap_once(sample_ms, lineHz, sumAbs);
}

void MB_applySettings(uint8_t addr, uint32_t baud, JSONVar *status)
{
  if (g_baud != baud) {
    Serial2.end();
    Serial2.begin(baud);
    mb.config(baud);
    g_baud = baud;
  }
  if (g_addr != addr) {
    _trySetSlaveId(mb, addr, 0);
    g_addr = addr;
  }
  if (status) {
    (*status)["address"] = g_addr;
    (*status)["baud"]    = g_baud;
  }
}

void MB_task(){ mb.task(); }

void MB_setURMS(const uint16_t urms_cV[3]){ for (int i=0;i<3;i++) mb.Ireg(100+i, urms_cV[i]); }
void MB_setIRMS(const uint16_t irms_mA[3]){ for (int i=0;i<3;i++) mb.Ireg(110+i, irms_mA[i]); }

void MB_setPFraw(const int16_t pf_raw[4]){
  for (int i=0;i<3;i++) mb.Ireg(240+i, (uint16_t)pf_raw[i]);
  mb.Ireg(243, (uint16_t)pf_raw[3]);
}
void MB_setAngles(const int16_t ang_raw[3]){ for (int i=0;i<3;i++) mb.Ireg(244+i, (uint16_t)ang_raw[i]); }

void MB_setFreqTemp(uint16_t f_x100, int16_t tempC){
  mb.Ireg(250, f_x100);
  mb.Ireg(251, (uint16_t)tempC);
}

void MB_setPQS(const int32_t P_W[4], const int32_t Q_var[4], const int32_t S_VA[4]){
  _put_s32(200+0, P_W[0]); _put_s32(200+2, P_W[1]); _put_s32(200+4, P_W[2]); _put_s32(200+6, P_W[3]);
  _put_s32(210+0, Q_var[0]); _put_s32(210+2, Q_var[1]); _put_s32(210+4, Q_var[2]); _put_s32(210+6, Q_var[3]);
  _put_s32(220+0, S_VA[0]); _put_s32(220+2, S_VA[1]); _put_s32(220+4, S_VA[2]); _put_s32(220+6, S_VA[3]);
}

void MB_setEnergies(
  const uint32_t ap_Wh[4], const uint32_t an_Wh[4],
  const uint32_t rp_varh[4], const uint32_t rn_varh[4],
  const uint32_t s_VAh[4]
){
  _put_u32(300, ap_Wh[0]); _put_u32(302, ap_Wh[1]); _put_u32(304, ap_Wh[2]); _put_u32(306, ap_Wh[3]);
  _put_u32(308, an_Wh[0]); _put_u32(310, an_Wh[1]); _put_u32(312, an_Wh[2]); _put_u32(314, an_Wh[3]);
  _put_u32(316, rp_varh[0]); _put_u32(318, rp_varh[1]); _put_u32(320, rp_varh[2]); _put_u32(322, rp_varh[3]);
  _put_u32(324, rn_varh[0]); _put_u32(326, rn_varh[1]); _put_u32(328, rn_varh[2]); _put_u32(330, rn_varh[3]);
  _put_u32(332, s_VAh[0]);  _put_u32(334, s_VAh[1]);  _put_u32(336, s_VAh[2]);  _put_u32(338, s_VAh[3]);
}

void MB_setLedIsts(uint8_t idx, bool on){       if (idx<4) mb.setIsts(500 + idx, on); }
void MB_setButtonIsts(uint8_t idx, bool on){    if (idx<4) mb.setIsts(520 + idx, on); }
void MB_setRelayIsts(uint8_t idx, bool on){     if (idx<2) mb.setIsts(540 + idx, on); }
void MB_setAlarmIsts(uint8_t ch, uint8_t kind, bool on){
  if (ch < CH_COUNT && kind < AK_COUNT) mb.setIsts(560 + (ch*AK_COUNT + kind), on);
}

bool MB_serviceCoils(
  bool canExternalWrite[2],
  bool desiredRelay[2],
  void (*ack_cb)(uint8_t ch)
){
  bool wantsChange=false;

  if (canExternalWrite[0]) {
    bool coil = mb.Coil(600);
    if (coil != desiredRelay[0]) { desiredRelay[0] = coil; wantsChange=true; }
  } else {
    if (mb.Coil(600) != desiredRelay[0]) mb.Coil(600, desiredRelay[0]);
  }

  if (canExternalWrite[1]) {
    bool coil = mb.Coil(601);
    if (coil != desiredRelay[1]) { desiredRelay[1] = coil; wantsChange=true; }
  } else {
    if (mb.Coil(601) != desiredRelay[1]) mb.Coil(601, desiredRelay[1]);
  }

  for (uint16_t ch=0; ch<CH_COUNT; ++ch) {
    uint16_t addr = 610 + ch;
    if (mb.Coil(addr)) {
      if (ack_cb) ack_cb((uint8_t)ch);
      mb.Coil(addr, 0);
    }
  }

  return wantsChange;
}

void MB_fillStatus(JSONVar *status){
  if (!status) return;
  (*status)["address"] = g_addr;
  (*status)["baud"]    = g_baud;
}

} // namespace enm223

// ============================================================================
// Application
// ============================================================================
SimpleWebSerial WebSerial;

// ================== Modbus / RS-485 ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;
JSONVar  modbusStatus;

// ================== Buttons & LEDs ==================
struct LedCfg    { uint8_t mode; uint8_t source; }; // mode: 0 steady, 1 blink
struct ButtonCfg { uint8_t action; };

LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool ledManual[4] = {false,false,false,false};
bool ledPhys[4]   = {false,false,false,false};

// Buttons runtime
constexpr unsigned long BTN_DEBOUNCE_MS = 30;
constexpr unsigned long BTN_LONG_MS     = 3000;

bool buttonState[4]          = {false,false,false,false};
bool buttonPrev[4]           = {false,false,false,false};
unsigned long btnChangeAt[4] = {0,0,0,0};
unsigned long btnPressAt[4]  = {0,0,0,0};
bool btnLongDone[4]          = {false,false,false,false};

// ---- Button override (per-relay) ----
bool buttonOverrideMode[2]  = {false,false};
bool buttonOverrideState[2] = {false,false};

// Relays (logical state; true = ON)
bool relayState[2] = {false,false};

// ================== Timing ==================
unsigned long lastSend = 0;
constexpr unsigned long sendInterval   = 1000; // 1 Hz UI push
unsigned long lastBlinkToggle = 0;
constexpr unsigned long blinkPeriodMs  = 500;
bool blinkPhase = false;
unsigned long lastSample = 0;

// Energy read/accumulate every 60s (ATM energy regs are read-to-clear pulses)
static unsigned long lastEnergySample = 0;
constexpr unsigned long ENERGY_SAMPLE_MS = 60000;

// ================== Persisted settings ==================
uint16_t sample_ms    = 200;

// ATM90E32 options (persisted) (UCAL REMOVED)
uint16_t atm_lineFreqHz = 50;    // 50/60
uint8_t  atm_sumModeAbs = 1;     // affects MMode0

// ===== Per-phase calibration (persisted) =====
uint16_t cal_Ugain[3]   = {25256,25256,25256};
uint16_t cal_Igain[3]   = {0,0,0};
int16_t  cal_Uoffset[3] = {0,0,0};
int16_t  cal_Ioffset[3] = {0,0,0};

// Persisted relay polarity & defaults
uint8_t  relay_active_low = RELAY_ACTIVE_LOW_DFLT;
bool     relay_default[2] = {false,false};

// ================== ALARMS ==================
enum AlarmMetric : uint8_t {
  AM_VOLTAGE=0,   // Urms (0.01 V)
  AM_CURRENT,     // Irms (0.001 A)
  AM_P_ACTIVE,    // W
  AM_Q_REACTIVE,  // var
  AM_S_APPARENT,  // VA
  AM_FREQ         // (0.01 Hz)
};

struct AlarmRule { bool enabled; int32_t min; int32_t max; uint8_t metric; };
struct AlarmRuntime { bool conditionNow; bool active; bool latched; };
struct ChannelAlarmCfg { bool ackRequired; AlarmRule rule[AK_COUNT]; };

ChannelAlarmCfg g_alarmCfg[CH_COUNT];
AlarmRuntime    g_alarmRt [CH_COUNT][AK_COUNT];

// (Fix #1) Snapshot used for alarms (defined BEFORE any use)
struct MetricsSnapshot {
  int32_t Urms_cV[3];   // 0.01 V
  int32_t Irms_mA[3];   // 0.001 A
  int32_t P_W[4];       // W L1..L3, total
  int32_t Q_var[4];     // var
  int32_t S_VA[4];      // VA
  int32_t Freq_cHz;     // 0.01 Hz
};

// ====== Relay mode / source ======
enum : uint8_t { RM_NONE=0, RM_MODBUS=1, RM_ALARM=2 };
struct RelayAlarmSrc { uint8_t ch; uint8_t kindsMask; }; // bit0=Alarm,1=Warn,2=Event
uint8_t       relay_mode[2]      = { RM_MODBUS, RM_MODBUS };
RelayAlarmSrc relay_alarm_src[2] = { {CH_TOT, 0b001}, {CH_TOT, 0b001} };

// ============================================================================
// ENERGY CONSTANT (MC) READ FROM ATM (PLconstH/PLconstL)
// ============================================================================
static uint32_t g_plconst32 = 0;
static uint32_t g_MC_imp_per_kWh = 3200; // derived from PLconst; fallback safe default
static unsigned long lastMCReadMs = 0;
constexpr unsigned long MC_REFRESH_MS = 30000; // refresh occasionally (optional but safe)

static inline uint32_t atm_read_plconst32() {
  // PLconst is config space: use access gate
  g_atm.cfgAccessBegin();
  uint16_t h = g_atm.readReg16(enm223::ATM90E32_Inline::PLconstH);
  uint16_t l = g_atm.readReg16(enm223::ATM90E32_Inline::PLconstL);
  g_atm.cfgAccessEnd();
  return (uint32_t(h) << 16) | uint32_t(l);
}

static inline void atm_update_MC_from_PLconst(bool force=false) {
  unsigned long now = millis();
  if (!force && (now - lastMCReadMs) < MC_REFRESH_MS) return;
  lastMCReadMs = now;

  uint32_t pl = atm_read_plconst32();
  g_plconst32 = pl;

  // MC = 450,000,000,000 / PLconst
  if (pl != 0) {
    uint64_t mc = 450000000000ULL / (uint64_t)pl;
    if (mc >= 1 && mc <= 10000000ULL) g_MC_imp_per_kWh = (uint32_t)mc;
  }
}

// Convert 0.01CF ticks to Wh using MC
static inline uint32_t ticks0p01CF_to_Wh(uint64_t ticks) {
  if (g_MC_imp_per_kWh == 0) return 0;
  // Wh = ticks * 10 / MC   (because 0.01 CF => 10/MC Wh)
  double Wh = (double)ticks * (10.0 / (double)g_MC_imp_per_kWh);
  if (Wh < 0) Wh = 0;
  if (Wh > 4294967295.0) Wh = 4294967295.0;
  return (uint32_t)lround(Wh);
}

// ============================================================================
// REAL ATM CHIP CONFIG READBACK
// ============================================================================

static const RawRow RAW_READ_TEMPLATE[] = {
  { enm223::ATM90E32_Inline::UgainA,   "UgainA",   0 },
  { enm223::ATM90E32_Inline::IgainA,   "IgainA",   0 },
  { enm223::ATM90E32_Inline::UoffsetA, "UoffsetA", 0 },
  { enm223::ATM90E32_Inline::IoffsetA, "IoffsetA", 0 },

  { enm223::ATM90E32_Inline::UgainB,   "UgainB",   0 },
  { enm223::ATM90E32_Inline::IgainB,   "IgainB",   0 },
  { enm223::ATM90E32_Inline::UoffsetB, "UoffsetB", 0 },
  { enm223::ATM90E32_Inline::IoffsetB, "IoffsetB", 0 },

  { enm223::ATM90E32_Inline::UgainC,   "UgainC",   0 },
  { enm223::ATM90E32_Inline::IgainC,   "IgainC",   0 },
  { enm223::ATM90E32_Inline::UoffsetC, "UoffsetC", 0 },
  { enm223::ATM90E32_Inline::IoffsetC, "IoffsetC", 0 },

  { enm223::ATM90E32_Inline::MeterEn, "MeterEn", 0 },

  { enm223::ATM90E32_Inline::MMode0,        "MMode0",        0 },
  { enm223::ATM90E32_Inline::MMode1,        "MMode1",        0 },
  { enm223::ATM90E32_Inline::PLconstH,      "PLconstH",      0 },
  { enm223::ATM90E32_Inline::PLconstL,      "PLconstL",      0 },
  { enm223::ATM90E32_Inline::SagPeakDetCfg, "SagPeakDetCfg", 0 },
  { enm223::ATM90E32_Inline::SagTh,         "SagTh",         0 },
  { enm223::ATM90E32_Inline::FreqHiTh,      "FreqHiTh",      0 },
  { enm223::ATM90E32_Inline::FreqLoTh,      "FreqLoTh",      0 },
  { enm223::ATM90E32_Inline::ZXConfig,      "ZXConfig",      0 },

  { enm223::ATM90E32_Inline::REG_UrmsA,    "UrmsA(H)", 0 },
  { enm223::ATM90E32_Inline::REG_UrmsALSB, "UrmsA(L)", 0 },
  { enm223::ATM90E32_Inline::REG_IrmsA,    "IrmsA(H)", 0 },
  { enm223::ATM90E32_Inline::REG_IrmsALSB, "IrmsA(L)", 0 },

  { enm223::ATM90E32_Inline::Freq, "Freq(x100)", 0 },
  { enm223::ATM90E32_Inline::Temp, "Temp", 0 },

  { enm223::ATM90E32_Inline::EMMState0,     "EMMState0",     0 },
  { enm223::ATM90E32_Inline::EMMState1,     "EMMState1",     0 },
  { enm223::ATM90E32_Inline::EMMIntState0,  "EMMIntState0",  0 },
  { enm223::ATM90E32_Inline::EMMIntState1,  "EMMIntState1",  0 },
  { enm223::ATM90E32_Inline::CRCErrStatus,  "CRCErrStatus",  0 },
  { enm223::ATM90E32_Inline::LastSPIData,   "LastSPIData",   0 },
};

static RawRow g_rawReads[sizeof(RAW_READ_TEMPLATE)/sizeof(RAW_READ_TEMPLATE[0])];

static void raw_init_tables() {
  memcpy(g_rawReads, RAW_READ_TEMPLATE, sizeof(g_rawReads));
}

static inline void raw_set_read(uint16_t reg, uint16_t val) {
  for (size_t i=0;i<sizeof(g_rawReads)/sizeof(g_rawReads[0]);++i) {
    if (g_rawReads[i].reg == reg) { g_rawReads[i].val = val; return; }
  }
}

static void atm_onRead(uint16_t reg, uint16_t val)  { raw_set_read(reg, val); }
static void atm_onWrite(uint16_t, uint16_t) {} // ignored


static JSONVar RawTableJSON(const RawRow* rows, size_t n) {
  JSONVar arr;
  for (size_t i=0;i<n;i++) {
    JSONVar o;
    char rbuf[8];
    snprintf(rbuf, sizeof(rbuf), "0x%04X", (unsigned)rows[i].reg);
    o["reg"]  = String(rbuf);
    o["name"] = rows[i].name;
    o["val"]  = (int)rows[i].val;
    arr[(int)i] = o;
  }
  return arr;
}

static void atm_readback_config_into_table() {
  g_atm.cfgAccessBegin();
  for (size_t i=0;i<sizeof(g_rawReads)/sizeof(g_rawReads[0]);++i) {
    (void)g_atm.readReg16(g_rawReads[i].reg);
  }
  g_atm.cfgAccessEnd();
}

static void pushAtmChipConfigJSON() {
  g_atm.cfgAccessBegin();

  auto rU16 = [](uint16_t reg)->uint16_t { return g_atm.readReg16(reg); };
  auto rS16 = [](uint16_t reg)->int16_t  { return (int16_t)g_atm.readReg16(reg); };

  JSONVar cfg;

  JSONVar ph;
  for (int i=0;i<3;i++) {
    JSONVar p;
    if (i==0) { p["Ugain"]= (int)rU16(enm223::ATM90E32_Inline::UgainA); p["Igain"]=(int)rU16(enm223::ATM90E32_Inline::IgainA);
                p["Uoffset"]=(int)rS16(enm223::ATM90E32_Inline::UoffsetA); p["Ioffset"]=(int)rS16(enm223::ATM90E32_Inline::IoffsetA); }
    if (i==1) { p["Ugain"]= (int)rU16(enm223::ATM90E32_Inline::UgainB); p["Igain"]=(int)rU16(enm223::ATM90E32_Inline::IgainB);
                p["Uoffset"]=(int)rS16(enm223::ATM90E32_Inline::UoffsetB); p["Ioffset"]=(int)rS16(enm223::ATM90E32_Inline::IoffsetB); }
    if (i==2) { p["Ugain"]= (int)rU16(enm223::ATM90E32_Inline::UgainC); p["Igain"]=(int)rU16(enm223::ATM90E32_Inline::IgainC);
                p["Uoffset"]=(int)rS16(enm223::ATM90E32_Inline::UoffsetC); p["Ioffset"]=(int)rS16(enm223::ATM90E32_Inline::IoffsetC); }
    ph[i]=p;
  }
  cfg["phase"]=ph;

  cfg["MMode0"]        = (int)rU16(enm223::ATM90E32_Inline::MMode0);
  cfg["MMode1"]        = (int)rU16(enm223::ATM90E32_Inline::MMode1);
  cfg["PLconstH"]      = (int)rU16(enm223::ATM90E32_Inline::PLconstH);
  cfg["PLconstL"]      = (int)rU16(enm223::ATM90E32_Inline::PLconstL);
  cfg["SagPeakDetCfg"] = (int)rU16(enm223::ATM90E32_Inline::SagPeakDetCfg);
  cfg["SagTh"]         = (int)rU16(enm223::ATM90E32_Inline::SagTh);
  cfg["FreqHiTh"]      = (int)rU16(enm223::ATM90E32_Inline::FreqHiTh);
  cfg["FreqLoTh"]      = (int)rU16(enm223::ATM90E32_Inline::FreqLoTh);
  cfg["ZXConfig"]      = (int)rU16(enm223::ATM90E32_Inline::ZXConfig);

  g_atm.cfgAccessEnd();

  // also publish computed MC for sanity
  cfg["PLconst32"] = (int)g_plconst32;
  cfg["MC_imp_per_kWh"] = (int)g_MC_imp_per_kWh;

  WebSerial.send("ATM_ChipCfg", cfg);
}

static void pushAtmRawReadsTable() {
  atm_readback_config_into_table();
  WebSerial.send("ATM_RawReads", RawTableJSON(g_rawReads, sizeof(g_rawReads)/sizeof(g_rawReads[0])));
}

// ================== Persistence (config + energy counters) ==================
static uint64_t g_ap_cnt[4]={0,0,0,0};
static uint64_t g_an_cnt[4]={0,0,0,0};
static uint64_t g_rp_cnt[4]={0,0,0,0};
static uint64_t g_rn_cnt[4]={0,0,0,0};
static uint64_t g_s_cnt [4]={0,0,0,0};

struct PersistConfig {
  uint32_t magic;       // 'ENM2'
  uint16_t version;
  uint16_t size;

  uint8_t  mb_address;
  uint32_t mb_baud;

  uint16_t sample_ms;

  LedCfg    ledCfg[4];
  ButtonCfg buttonCfg[4];

  uint8_t  relay_active_low;
  uint8_t  relay_default0;
  uint8_t  relay_default1;

  uint16_t atm_lineFreqHz;
  uint8_t  atm_sumModeAbs;

  uint16_t cal_Ugain[3];
  uint16_t cal_Igain[3];
  int16_t  cal_Uoffset[3];
  int16_t  cal_Ioffset[3];

  uint8_t  alarm_ack_required[CH_COUNT];
  struct PackedRule { uint8_t enabled; int32_t min; int32_t max; uint8_t metric; } alarm_rules[CH_COUNT][AK_COUNT];

  uint8_t  relay_mode0;
  uint8_t  relay_mode1;
  uint8_t  relay_alarm_ch0;
  uint8_t  relay_alarm_ch1;
  uint8_t  relay_alarm_mask0;
  uint8_t  relay_alarm_mask1;

  uint64_t ap_cnt[4], an_cnt[4], rp_cnt[4], rn_cnt[4], s_cnt[4];

  uint32_t crc32;
} __attribute__((packed));

constexpr uint32_t CFG_MAGIC   = 0x324D4E45UL; // 'ENM2'
constexpr uint16_t CFG_VERSION = 0x000B;       // NEW: MC-based energy conversion; removed fake energy scale fields
static const char* CFG_PATH    = "/enm223.bin";

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
constexpr uint32_t  CFG_AUTOSAVE_MS = 1200;

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
  return digitalRead(BUTTON_PINS[i]) == LOW;
#else
  return digitalRead(BUTTON_PINS[i]) == HIGH;
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

  for (int i=0;i<3;i++){
    cal_Ugain[i]   = 25256;
    cal_Igain[i]   = 0;
    cal_Uoffset[i] = 0;
    cal_Ioffset[i] = 0;
  }

  setAlarmDefaults();

  buttonOverrideMode[0] = buttonOverrideMode[1] = false;
  buttonOverrideState[0] = buttonOverrideState[1] = false;
}

// ================== Persistence ==================
void captureToPersist(PersistConfig &pc) {
  pc = {};
  pc.magic   = CFG_MAGIC;
  pc.version = CFG_VERSION;
  pc.size    = sizeof(PersistConfig);

  pc.mb_address   = g_mb_address;
  pc.mb_baud      = g_mb_baud;
  pc.sample_ms    = sample_ms;

  memcpy(pc.ledCfg,    ledCfg,    sizeof(ledCfg));
  memcpy(pc.buttonCfg, buttonCfg, sizeof(buttonCfg));

  pc.relay_active_low = relay_active_low;
  pc.relay_default0   = relay_default[0];
  pc.relay_default1   = relay_default[1];

  pc.atm_lineFreqHz = atm_lineFreqHz;
  pc.atm_sumModeAbs = atm_sumModeAbs;

  memcpy(pc.cal_Ugain,   cal_Ugain,   sizeof(cal_Ugain));
  memcpy(pc.cal_Igain,   cal_Igain,   sizeof(cal_Igain));
  memcpy(pc.cal_Uoffset, cal_Uoffset, sizeof(cal_Uoffset));
  memcpy(pc.cal_Ioffset, cal_Ioffset, sizeof(cal_Ioffset));

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

  memcpy(pc.ap_cnt, g_ap_cnt, sizeof(g_ap_cnt));
  memcpy(pc.an_cnt, g_an_cnt, sizeof(g_an_cnt));
  memcpy(pc.rp_cnt, g_rp_cnt, sizeof(g_rp_cnt));
  memcpy(pc.rn_cnt, g_rn_cnt, sizeof(g_rn_cnt));
  memcpy(pc.s_cnt,  g_s_cnt,  sizeof(g_s_cnt));

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

  memcpy(cal_Ugain,   pc.cal_Ugain,   sizeof(cal_Ugain));
  memcpy(cal_Igain,   pc.cal_Igain,   sizeof(cal_Igain));
  memcpy(cal_Uoffset, pc.cal_Uoffset, sizeof(cal_Uoffset));
  memcpy(cal_Ioffset, pc.cal_Ioffset, sizeof(cal_Ioffset));

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

  memcpy(g_ap_cnt, pc.ap_cnt, sizeof(g_ap_cnt));
  memcpy(g_an_cnt, pc.an_cnt, sizeof(g_an_cnt));
  memcpy(g_rp_cnt, pc.rp_cnt, sizeof(g_rp_cnt));
  memcpy(g_rn_cnt, pc.rn_cnt, sizeof(g_rn_cnt));
  memcpy(g_s_cnt,  pc.s_cnt,  sizeof(g_s_cnt));

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

// ================== GPIO helpers (change-only + FORCE) ==================
static inline void setLedPhys(uint8_t idx, bool on){
  if (ledPhys[idx] == on) return;
#if LED_ACTIVE_LOW
  digitalWrite(LED_PINS[idx], on ? LOW : HIGH);
#else
  digitalWrite(LED_PINS[idx], on ? HIGH : LOW);
#endif
  ledPhys[idx] = on;
  enm223::MB_setLedIsts(idx, on);
}

static inline void driveRelayPin(uint8_t idx, bool logicalOn){
  bool drive = logicalOn ^ (relay_active_low != 0);
  digitalWrite(RELAY_PINS[idx], drive ? HIGH : LOW);
}

static inline void setRelayPhys(uint8_t idx, bool on, bool force=false){
  if (!force && relayState[idx] == on) return;
  driveRelayPin(idx, on);
  relayState[idx] = on;
  enm223::MB_setRelayIsts(idx, on);
}

static inline void applyModbusSettings(uint8_t addr, uint32_t baud) {
  enm223::MB_applySettings(addr, baud, &modbusStatus);
  g_mb_address = addr; g_mb_baud = baud;
}

// ================== WebSerial helpers ==================
static inline void sendStatusEcho() {
  enm223::MB_fillStatus(&modbusStatus);
  WebSerial.send("status", modbusStatus);
}

static JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) { JSONVar o; o["mode"]=ledCfg[i].mode; o["source"]=ledCfg[i].source; arr[i]=o; }
  return arr;
}
static JSONVar ButtonConfigListFromCfg() {
  JSONVar arr;
  for (int i=0;i<4;i++) arr[i] = buttonCfg[i].action;
  return arr;
}
static JSONVar RelayConfigFromCfg() {
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
static inline void echoRelayState() {
  JSONVar rly; rly[0]=relayState[0]; rly[1]=relayState[1];
  WebSerial.send("RelayState", rly);
}

// ===== Alarms helpers =====
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

      enm223::MB_setAlarmIsts(ch, k, g_alarmRt[ch][k].active);
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

// ================== Meter helpers ==================
static inline uint16_t atm_ucalRef_from_cal() {
  uint16_t r = cal_Ugain[0];
  return (r == 0) ? 1 : r;
}

static inline void meter_begin_from_current_cfg() {
  enm223::M90PhaseCal cal[3] = {
    {cal_Ugain[0], cal_Igain[0], cal_Uoffset[0], cal_Ioffset[0]},
    {cal_Ugain[1], cal_Igain[1], cal_Uoffset[1], cal_Ioffset[1]},
    {cal_Ugain[2], cal_Igain[2], cal_Uoffset[2], cal_Ioffset[2]},
  };
  g_atm.begin(atm_lineFreqHz, atm_sumModeAbs, atm_ucalRef_from_cal(), cal);

  // IMPORTANT: MC is tied to PLconst, which we just wrote in begin(). Read it back and compute MC.
  atm_update_MC_from_PLconst(true);
}

static inline void meter_apply_cal_only() {
  enm223::M90PhaseCal cal[3] = {
    {cal_Ugain[0], cal_Igain[0], cal_Uoffset[0], cal_Ioffset[0]},
    {cal_Ugain[1], cal_Igain[1], cal_Uoffset[1], cal_Ioffset[1]},
    {cal_Ugain[2], cal_Igain[2], cal_Uoffset[2], cal_Ioffset[2]},
  };
  g_atm.applyCalibration(cal); // now includes commit settle
}

static JSONVar MeterOptionsJSON() {
  JSONVar mo;
  mo["lineHz"]    = atm_lineFreqHz;
  mo["sumAbs"]    = atm_sumModeAbs;
  mo["sample_ms"] = sample_ms;
  // expose MC readback for sanity (REAL, from PLconst)
  mo["MC_imp_per_kWh"] = (int)g_MC_imp_per_kWh;
  mo["PLconst32"]      = (int)g_plconst32;
  return mo;
}

static JSONVar CalibJSON_All() {
  JSONVar arr;
  for (int ph=0; ph<3; ++ph) {
    JSONVar o;
    o["Ugain"]   = cal_Ugain[ph];
    o["Igain"]   = cal_Igain[ph];
    o["Uoffset"] = (int32_t)cal_Uoffset[ph];
    o["Ioffset"] = (int32_t)cal_Ioffset[ph];
    arr[ph] = o;
  }
  return arr;
}

// ================== WebSerial handlers ==================
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

  bool changed=false, reinit=false;

  if (type == "meter") {
    JSONVar c = obj["cfg"];

    if (c.hasOwnProperty("lineHz")) {
      uint16_t lf = (uint16_t)((int)c["lineHz"] == 60 ? 60 : 50);
      if (lf != atm_lineFreqHz) { atm_lineFreqHz = lf; reinit = true; }
      changed = true;
    }
    if (c.hasOwnProperty("sumAbs")) {
      uint8_t sm = (uint8_t)constrain((int)c["sumAbs"], 0, 1);
      if (sm != atm_sumModeAbs) { atm_sumModeAbs = sm; reinit = true; }
      changed = true;
    }
    if (c.hasOwnProperty("sample_ms")) {
      int smp = (int)c["sample_ms"];
      sample_ms = (uint16_t)constrain(smp, 10, 5000);
      changed = true;
    }

    if (reinit) meter_begin_from_current_cfg();

    if (changed) {
      cfgDirty = true; lastCfgTouchMs = millis();
      WebSerial.send("message", "Meter options updated");
    }
  }
  else if (type == "leds") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<(int)list.length();i++) {
      ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
      ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 25);
    }
    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    WebSerial.send("message", "LEDs configuration updated");
    cfgDirty = true; lastCfgTouchMs = millis();
  }
  else if (type == "buttons") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<(int)list.length();i++) {
      int a = (int)list[i]["action"]; a = constrain(a, 0, 8);
      buttonCfg[i].action = (uint8_t)a;
    }
    WebSerial.send("ButtonConfigList", ButtonConfigListFromCfg());
    WebSerial.send("message", "Buttons configuration updated");
    cfgDirty = true; lastCfgTouchMs = millis();
  }
  else {
    WebSerial.send("message", String("Unknown Config type: ") + t);
  }
}

void handleRelaysSet(JSONVar v){
  bool changed=false;
  auto canWrite = [&](int idx)->bool{
    return (relay_mode[idx] != RM_ALARM) && !buttonOverrideMode[idx];
  };

  if (v.hasOwnProperty("list") && v["list"].length() >= 2) {
    bool r0 = (bool)v["list"][0];
    bool r1 = (bool)v["list"][1];
    if (canWrite(0)) { setRelayPhys(0, r0); changed=true; }
    if (canWrite(1)) { setRelayPhys(1, r1); changed=true; }
  } else if (v.hasOwnProperty("r1") || v.hasOwnProperty("r2")) {
    if (v.hasOwnProperty("r1") && canWrite(0)) { setRelayPhys(0, (bool)v["r1"]); changed=true; }
    if (v.hasOwnProperty("r2") && canWrite(1)) { setRelayPhys(1, (bool)v["r2"]); changed=true; }
  } else if (v.hasOwnProperty("idx")) {
    int idx = constrain((int)v["idx"], 0, 1);
    if (!canWrite(idx)) WebSerial.send("message","RelaysSet: blocked (ALARM mode or Button Override)");
    else if (v.hasOwnProperty("toggle") && (bool)v["toggle"]) { setRelayPhys(idx, !relayState[idx]); changed=true; }
    else if (v.hasOwnProperty("on")) { setRelayPhys(idx, (bool)v["on"]); changed=true; }
  } else {
    WebSerial.send("message","RelaysSet: no-op (bad payload)");
  }

  if (changed) { echoRelayState(); WebSerial.send("message","Relays updated"); }
}

void handleRelaysCfg(JSONVar v){
  bool touched=false;
  uint8_t prevPol = relay_active_low;

  if (v.hasOwnProperty("active_low")) { relay_active_low = (uint8_t)constrain((int)v["active_low"],0,1); touched=true; }
  if (v.hasOwnProperty("def0")) { relay_default[0] = (bool)v["def0"]; touched=true; }
  if (v.hasOwnProperty("def1")) { relay_default[1] = (bool)v["def1"]; touched=true; }

  if (v.hasOwnProperty("mode") && v["mode"].length() >= 2) {
    for (int i=0;i<2;i++){ int m = constrain((int)v["mode"][i], RM_NONE, RM_ALARM); relay_mode[i] = (uint8_t)m; }
    touched=true;
  }

  if (v.hasOwnProperty("alarm") && v["alarm"].length() >= 2) {
    for (int i=0;i<2;i++){
      JSONVar a = v["alarm"][i];
      if (a.hasOwnProperty("ch"))   relay_alarm_src[i].ch = (uint8_t)constrain((int)a["ch"], 0, CH_COUNT-1);
      if (a.hasOwnProperty("mask")) relay_alarm_src[i].kindsMask = ((uint8_t)((int)a["mask"])) & 0b111;
    }
    touched=true;
  }

  const bool force = (prevPol != relay_active_low);
  setRelayPhys(0, relayState[0], force);
  setRelayPhys(1, relayState[1], force);

  if (touched) { cfgDirty = true; lastCfgTouchMs = millis(); }
  WebSerial.send("message","RelaysCfg updated");
  echoRelayState();
}

void handleCalibCfg(JSONVar v){
  int ph = -1;
  if (v.hasOwnProperty("ph")) ph = (int)v["ph"];
  else if (v.hasOwnProperty("ch")) ph = (int)v["ch"];
  if (ph < 0 || ph > 2) { WebSerial.send("message","CalibCfg: bad phase"); return; }

  JSONVar c = v.hasOwnProperty("cfg") ? v["cfg"] : v;

  auto getI32 = [&](const char* k, int32_t def)->int32_t{
    if (!c.hasOwnProperty(k)) return def;
    return (int32_t)(int)c[k];
  };
  auto getU16 = [&](const char* k, uint16_t def)->uint16_t{
    if (!c.hasOwnProperty(k)) return def;
    int32_t x = (int32_t)(int)c[k];
    if (x < 0) x = 0;
    if (x > 65535) x = 65535;
    return (uint16_t)x;
  };

  uint16_t Ug = getU16("Ug", cal_Ugain[ph]);     if (c.hasOwnProperty("Ugain"))   Ug = getU16("Ugain", Ug);
  uint16_t Ig = getU16("Ig", cal_Igain[ph]);     if (c.hasOwnProperty("Igain"))   Ig = getU16("Igain", Ig);
  int32_t  Uo32 = getI32("Uo", cal_Uoffset[ph]); if (c.hasOwnProperty("Uoffset")) Uo32 = getI32("Uoffset", Uo32);
  int32_t  Io32 = getI32("Io", cal_Ioffset[ph]); if (c.hasOwnProperty("Ioffset")) Io32 = getI32("Ioffset", Io32);

  int32_t Uo = constrain(Uo32, -32768, 32767);
  int32_t Io = constrain(Io32, -32768, 32767);

  cal_Ugain[ph]   = Ug;
  cal_Igain[ph]   = Ig;
  cal_Uoffset[ph] = (int16_t)Uo;
  cal_Ioffset[ph] = (int16_t)Io;

  meter_apply_cal_only(); // includes settle now

  cfgDirty = true; lastCfgTouchMs = millis();
  WebSerial.send("message", String("Calibration updated for phase ") + ph);
  WebSerial.send("CalibCfg", CalibJSON_All());

  // MC from PLconst (doesn't change by cal, but keep it fresh)
  atm_update_MC_from_PLconst(true);

  pushAtmChipConfigJSON();
  pushAtmRawReadsTable();
}

void handleAlarmsCfg(JSONVar v){
  int ch = -1;
  JSONVar cfg;

  if (v.hasOwnProperty("ch") && v.hasOwnProperty("cfg")) {
    ch = (int)v["ch"];
    cfg = v["cfg"];
  } else if (v.hasOwnProperty("cfg")) {
    cfg = v["cfg"];
    ch = v.hasOwnProperty("channel") ? (int)v["channel"] : -1;
  } else {
    if (v.hasOwnProperty("ch")) ch = (int)v["ch"];
    cfg = v;
  }

  if (ch < 0 || ch >= CH_COUNT) { WebSerial.send("message","AlarmsCfg: bad channel"); return; }

  if (cfg.hasOwnProperty("ack")) g_alarmCfg[ch].ackRequired = (bool)cfg["ack"];

  for (int k=0;k<AK_COUNT;++k) {
    if (!cfg.hasOwnProperty(String(k))) continue;
    JSONVar r = cfg[String(k)];
    if (r.hasOwnProperty("enabled")) g_alarmCfg[ch].rule[k].enabled = (bool)r["enabled"];
    if (r.hasOwnProperty("min"))     g_alarmCfg[ch].rule[k].min     = (int32_t)(int)r["min"];
    if (r.hasOwnProperty("max"))     g_alarmCfg[ch].rule[k].max     = (int32_t)(int)r["max"];
    if (r.hasOwnProperty("metric"))  g_alarmCfg[ch].rule[k].metric  = (uint8_t)constrain((int)r["metric"], 0, 5);
  }

  cfgDirty = true; lastCfgTouchMs = millis();
  WebSerial.send("message", "Alarms configuration updated");
  alarms_publish_cfg();
}

void handleAlarmsAck(JSONVar v){
  if (v.hasOwnProperty("ch")) {
    int ch = (int)v["ch"];
    if (ch>=0 && ch<CH_COUNT) alarms_ack_channel((uint8_t)ch);
  } else if (v.hasOwnProperty("list")) {
    JSONVar list = v["list"];
    for (int ch=0; ch<CH_COUNT && ch<(int)list.length(); ++ch) {
      if ((bool)list[ch]) alarms_ack_channel((uint8_t)ch);
    }
  } else {
    for (int ch=0; ch<CH_COUNT; ++ch) alarms_ack_channel((uint8_t)ch);
  }
  WebSerial.send("message","Alarms acknowledged");
  alarms_publish_state();
}

// Buttons
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

// LED source evaluation
bool evalLedSource(uint8_t src) {
  auto anyKinds = [&](uint8_t ch)->bool {
    if (ch >= CH_COUNT) return false;
    return g_alarmRt[ch][AK_ALARM].active || g_alarmRt[ch][AK_WARNING].active || g_alarmRt[ch][AK_EVENT].active;
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

// ================== Cached meter values ==================
static double   g_urms[3] = {0,0,0};
static double   g_irms[3] = {0,0,0};
static int32_t  g_p_W[4]  = {0,0,0,0};
static int32_t  g_q_var[4]= {0,0,0,0};
static int32_t  g_s_VA[4] = {0,0,0,0};
static int16_t  g_pf_raw[4]= {0,0,0,0};
static int16_t  g_ang_raw[3]= {0,0,0};
static uint16_t g_f_x100 = 0;
static int16_t  g_tempC = 0;

static uint32_t g_e_ap_Wh[4]={0,0,0,0}, g_e_an_Wh[4]={0,0,0,0}, g_e_rp_varh[4]={0,0,0,0}, g_e_rn_varh[4]={0,0,0,0}, g_e_s_VAh[4]={0,0,0,0};
static bool     g_haveMeter = false;

// UI meter echo (real values only)
void sendMeterEcho(){
  JSONVar m;

  for (int i=0;i<3;i++){
    m["Urms"][i] = g_urms[i];
    m["Irms"][i] = g_irms[i];
  }

  JSONVar pW, qVar, sVA;
  for (int i=0;i<4;i++){ pW[i]=g_p_W[i]; qVar[i]=g_q_var[i]; sVA[i]=g_s_VA[i]; }
  m["P_W"]=pW; m["Q_var"]=qVar; m["S_VA"]=sVA;

  JSONVar pfR; for(int i=0;i<4;i++) pfR[i] = ((double)g_pf_raw[i])/1000.0;
  m["PF"]=pfR;

  JSONVar ang; for(int i=0;i<3;i++) ang[i]=((double)g_ang_raw[i])/10.0;
  m["Angle_deg"]=ang;

  m["FreqHz"]=((double)g_f_x100)/100.0;
  m["TempC"]=(int)g_tempC;

  // show MC for sanity in live meter
  m["MC_imp_per_kWh"] = (int)g_MC_imp_per_kWh;

  auto to_k = [](uint32_t Wh)->double{ return Wh/1000.0; };

  JSONVar Ephase;
  for (int i=0;i<3;i++){
    JSONVar ph;
    ph["AP_kWh"]=to_k(g_e_ap_Wh[i]);
    ph["AN_kWh"]=to_k(g_e_an_Wh[i]);
    ph["RP_kvarh"]=to_k(g_e_rp_varh[i]);
    ph["RN_kvarh"]=to_k(g_e_rn_varh[i]);
    ph["S_kVAh"]=to_k(g_e_s_VAh[i]);
    Ephase[i]=ph;
  }

  JSONVar Etot;
  Etot["AP_kWh"]=to_k(g_e_ap_Wh[3]);
  Etot["AN_kWh"]=to_k(g_e_an_Wh[3]);
  Etot["RP_kvarh"]=to_k(g_e_rp_varh[3]);
  Etot["RN_kvarh"]=to_k(g_e_rn_varh[3]);
  Etot["S_kVAh"]=to_k(g_e_s_VAh[3]);

  m["E_phase"] = Ephase;
  m["E_tot"]   = Etot;

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

  if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }
  (void)loadConfigFS();

  setRelayPhys(0, relay_default[0], true);
  setRelayPhys(1, relay_default[1], true);

  pinMode(PIN_ATM_CS, OUTPUT);
  digitalWrite(PIN_ATM_CS, CS_ACTIVE_HIGH ? LOW : HIGH);

  MCM_SPI.setSCK(PIN_SPI_SCK);
  MCM_SPI.setTX(PIN_SPI_MOSI);
  MCM_SPI.setRX(PIN_SPI_MISO);
  MCM_SPI.begin();

  raw_init_tables();
  g_atm.setHooks(atm_onRead, atm_onWrite);

  meter_begin_from_current_cfg();
  delay(120);
  meter_apply_cal_only();

  // read MC once more after cal settle (safe)
  atm_update_MC_from_PLconst(true);

  enm223::MB_begin(g_mb_address, g_mb_baud, sample_ms, atm_lineFreqHz, atm_sumModeAbs);

  WebSerial.on("values",     handleValues);
  WebSerial.on("Config",     handleUnifiedConfig);
  WebSerial.on("RelaysSet",  handleRelaysSet);
  WebSerial.on("RelaysCfg",  handleRelaysCfg);
  WebSerial.on("CalibCfg",   handleCalibCfg);
  WebSerial.on("AlarmsCfg",  handleAlarmsCfg);
  WebSerial.on("AlarmsAck",  handleAlarmsAck);

  WebSerial.send("message", "Boot OK (ENERGY uses REAL MC from PLconst; RAW shows REAL ATM chip config via readback)");

  WebSerial.send("LedConfigList", LedConfigListFromCfg());
  WebSerial.send("ButtonConfigList", ButtonConfigListFromCfg());
  WebSerial.send("RelaysCfg", RelayConfigFromCfg());
  WebSerial.send("MeterOptions", MeterOptionsJSON());
  WebSerial.send("CalibCfg", CalibJSON_All());
  alarms_publish_cfg();

  pushAtmChipConfigJSON();
  pushAtmRawReadsTable();
}

// ================== Main loop ==================
void loop() {
  const unsigned long now = millis();

  WebSerial.check();

  if (now - lastBlinkToggle >= blinkPeriodMs) { lastBlinkToggle = now; blinkPhase = !blinkPhase; }

  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    saveConfigFS();
    cfgDirty = false;
  }

  enm223::MB_task();

  // keep MC fresh in case something rewrites PLconst (safe)
  atm_update_MC_from_PLconst(false);

  // Buttons
  for (int i = 0; i < 4; i++) {
    const bool pressed = readPressed(i);

    if (pressed != buttonState[i] && (now - btnChangeAt[i] >= BTN_DEBOUNCE_MS)) {
      btnChangeAt[i] = now;
      buttonPrev[i]  = buttonState[i];
      buttonState[i] = pressed;

      const uint8_t act = buttonCfg[i].action;

      if (!buttonPrev[i] && buttonState[i]) {
        btnPressAt[i] = now;
        btnLongDone[i] = false;
      }

      if (buttonPrev[i] && !buttonState[i]) {
        if (!btnLongDone[i]) {
          if (act == 7 || act == 8) {
            const int r = (act == 7) ? 0 : 1;
            if (buttonOverrideMode[r]) buttonOverrideState[r] = !buttonOverrideState[r];
          } else {
            doButtonAction(i);
            echoRelayState();
          }
        }
      }
    }

    if (buttonState[i] && !btnLongDone[i] && (now - btnPressAt[i] >= BTN_LONG_MS)) {
      const uint8_t act = buttonCfg[i].action;
      const int r = (act == 7) ? 0 : (act == 8 ? 1 : -1);
      if (r >= 0) {
        if (!buttonOverrideMode[r]) { buttonOverrideMode[r]  = true; buttonOverrideState[r] = relayState[r]; }
        else { buttonOverrideMode[r]  = false; }
      }
      btnLongDone[i] = true;
    }

    enm223::MB_setButtonIsts(i, buttonState[i]);
  }

  // Modbus coils control
  bool canExternalWrite[2] = {
    (relay_mode[0] == RM_MODBUS) && !buttonOverrideMode[0],
    (relay_mode[1] == RM_MODBUS) && !buttonOverrideMode[1]
  };
  bool desiredRelay[2] = { relayState[0], relayState[1] };

  bool wantsChange = enm223::MB_serviceCoils(
    canExternalWrite, desiredRelay,
    [](uint8_t ch){ alarms_ack_channel(ch); }
  );

  if (wantsChange) {
    if (!buttonOverrideMode[0]) setRelayPhys(0, desiredRelay[0]);
    if (!buttonOverrideMode[1]) setRelayPhys(1, desiredRelay[1]);
    echoRelayState();
  }

  // ===== Meter sampling =====
  if (now - lastSample >= sample_ms) {
    lastSample = now;

    double urms[3];
    urms[0] = g_atm.readRmsLike(enm223::ATM90E32_Inline::REG_UrmsA, enm223::ATM90E32_Inline::REG_UrmsALSB, 0.01, 0.01/256.0);
    urms[1] = g_atm.readRmsLike(enm223::ATM90E32_Inline::REG_UrmsB, enm223::ATM90E32_Inline::REG_UrmsBLSB, 0.01, 0.01/256.0);
    urms[2] = g_atm.readRmsLike(enm223::ATM90E32_Inline::REG_UrmsC, enm223::ATM90E32_Inline::REG_UrmsCLSB, 0.01, 0.01/256.0);

    double irms[3];
    irms[0] = g_atm.readRmsLike(enm223::ATM90E32_Inline::REG_IrmsA, enm223::ATM90E32_Inline::REG_IrmsALSB, 0.001, 0.001/256.0);
    irms[1] = g_atm.readRmsLike(enm223::ATM90E32_Inline::REG_IrmsB, enm223::ATM90E32_Inline::REG_IrmsBLSB, 0.001, 0.001/256.0);
    irms[2] = g_atm.readRmsLike(enm223::ATM90E32_Inline::REG_IrmsC, enm223::ATM90E32_Inline::REG_IrmsCLSB, 0.001, 0.001/256.0);

    auto clampU = [](double v){ long x = lround(v*100.0);  return (uint16_t)constrain(x, 0L, 65535L); };
    auto clampI = [](double a){ long x = lround(a*1000.0); return (uint16_t)constrain(x, 0L, 65535L); };

    uint16_t urms_cV[3], irms_mA[3];
    for (int i=0;i<3;i++){ urms_cV[i]=clampU(urms[i]); irms_mA[i]=clampI(irms[i]); }

    enm223::MB_setURMS(urms_cV);
    enm223::MB_setIRMS(irms_mA);

    g_pf_raw[0]=g_atm.readPFmeanA();
    g_pf_raw[1]=g_atm.readPFmeanB();
    g_pf_raw[2]=g_atm.readPFmeanC();
    g_pf_raw[3]=g_atm.readPFmeanT();
    enm223::MB_setPFraw(g_pf_raw);

    g_ang_raw[0]=g_atm.readPAngleA();
    g_ang_raw[1]=g_atm.readPAngleB();
    g_ang_raw[2]=g_atm.readPAngleC();
    enm223::MB_setAngles(g_ang_raw);

    g_f_x100 = g_atm.readFreq_x100();
    g_tempC  = g_atm.readTempC();
    enm223::MB_setFreqTemp(g_f_x100, g_tempC);

    auto pf01 = [](int16_t pf_raw){ return constrain(pf_raw/1000.0, -1.0, 1.0); };

    double S_va_d[3], P_w_d[3], Q_var_d[3];
    for (int i=0;i<3;i++) {
      const double S = urms[i] * irms[i];
      const double PF = pf01(g_pf_raw[i]);
      const double P  = S * PF;

      const double ang_deg = g_ang_raw[i] / 10.0;
      const double ang_rad = ang_deg * (M_PI/180.0);
      const double Qmag = sqrt(fmax(0.0, (S*S) - (P*P)));
      const double Q = (sin(ang_rad) >= 0.0) ? Qmag : -Qmag;

      S_va_d[i] = S; P_w_d[i] = P; Q_var_d[i] = Q;
    }

    double S_tot_d = S_va_d[0] + S_va_d[1] + S_va_d[2];
    double P_tot_d = P_w_d[0]  + P_w_d[1]  + P_w_d[2];
    double Q_tot_d = Q_var_d[0]+ Q_var_d[1]+ Q_var_d[2];

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

    enm223::MB_setPQS(g_p_W, g_q_var, g_s_VA);

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

    auto alarm_any_selected = [&](uint8_t ch, uint8_t mask)->bool{
      bool any = false;
      if (mask & 0b001) any |= g_alarmRt[ch][AK_ALARM].active;
      if (mask & 0b010) any |= g_alarmRt[ch][AK_WARNING].active;
      if (mask & 0b100) any |= g_alarmRt[ch][AK_EVENT].active;
      return any;
    };

    if (relay_mode[0] == RM_ALARM && !buttonOverrideMode[0]) setRelayPhys(0, alarm_any_selected(relay_alarm_src[0].ch, relay_alarm_src[0].kindsMask));
    if (relay_mode[1] == RM_ALARM && !buttonOverrideMode[1]) setRelayPhys(1, alarm_any_selected(relay_alarm_src[1].ch, relay_alarm_src[1].kindsMask));

    for (int r=0; r<2; ++r) if (buttonOverrideMode[r]) setRelayPhys(r, buttonOverrideState[r]);

    for (int i=0;i<3;i++){ g_urms[i]=urms[i]; g_irms[i]=irms[i]; }
    g_haveMeter = true;
  }

  // ===== Energy accumulate (0.01CF read-to-clear) =====
  if (now - lastEnergySample >= ENERGY_SAMPLE_MS) {
    lastEnergySample = now;

    // ensure MC is up to date right before converting
    atm_update_MC_from_PLconst(true);

    uint16_t apA=g_atm.rdAP_A(), apB=g_atm.rdAP_B(), apC=g_atm.rdAP_C(), apT=g_atm.rdAP_T();
    uint16_t anA=g_atm.rdAN_A(), anB=g_atm.rdAN_B(), anC=g_atm.rdAN_C(), anT=g_atm.rdAN_T();
    uint16_t rpA=g_atm.rdRP_A(), rpB=g_atm.rdRP_B(), rpC=g_atm.rdRP_C(), rpT=g_atm.rdRP_T();
    uint16_t rnA=g_atm.rdRN_A(), rnB=g_atm.rdRN_B(), rnC=g_atm.rdRN_C(), rnT=g_atm.rdRN_T();
    uint16_t sA =g_atm.rdSA_A(), sB =g_atm.rdSA_B(), sC =g_atm.rdSA_C(), sT =g_atm.rdSA_T();

    g_ap_cnt[0]+=apA; g_ap_cnt[1]+=apB; g_ap_cnt[2]+=apC; g_ap_cnt[3]+=apT;
    g_an_cnt[0]+=anA; g_an_cnt[1]+=anB; g_an_cnt[2]+=anC; g_an_cnt[3]+=anT;
    g_rp_cnt[0]+=rpA; g_rp_cnt[1]+=rpB; g_rp_cnt[2]+=rpC; g_rp_cnt[3]+=rpT;
    g_rn_cnt[0]+=rnA; g_rn_cnt[1]+=rnB; g_rn_cnt[2]+=rnC; g_rn_cnt[3]+=rnT;
    g_s_cnt [0]+=sA;  g_s_cnt [1]+=sB;  g_s_cnt [2]+=sC;  g_s_cnt [3]+=sT;

    // Convert using REAL MC from PLconst (0.01CF ticks)
    for (int i=0;i<4;i++){
      g_e_ap_Wh[i]   = ticks0p01CF_to_Wh(g_ap_cnt[i]);
      g_e_an_Wh[i]   = ticks0p01CF_to_Wh(g_an_cnt[i]);
      g_e_rp_varh[i] = ticks0p01CF_to_Wh(g_rp_cnt[i]);
      g_e_rn_varh[i] = ticks0p01CF_to_Wh(g_rn_cnt[i]);
      g_e_s_VAh[i]   = ticks0p01CF_to_Wh(g_s_cnt [i]);
    }

    enm223::MB_setEnergies(g_e_ap_Wh, g_e_an_Wh, g_e_rp_varh, g_e_rn_varh, g_e_s_VAh);

    cfgDirty = true;
    lastCfgTouchMs = millis();
  }

  // LEDs
  for (int i=0;i<4;i++) {
    bool activeFromSource = evalLedSource(ledCfg[i].source);
    bool active = (activeFromSource || ledManual[i]);
    bool phys = (ledCfg[i].mode == 0) ? active : (active && blinkPhase);
    setLedPhys(i, phys);
  }

  // ===== 1 Hz UI push =====
  if (now - lastSend >= sendInterval) {
    lastSend = now;

    sendStatusEcho();

    WebSerial.send("LedConfigList", LedConfigListFromCfg());
    WebSerial.send("ButtonConfigList", ButtonConfigListFromCfg());
    WebSerial.send("RelaysCfg", RelayConfigFromCfg());
    WebSerial.send("MeterOptions", MeterOptionsJSON());
    WebSerial.send("CalibCfg", CalibJSON_All());

    alarms_publish_cfg();

    if (g_haveMeter) sendMeterEcho();

    JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i];
    WebSerial.send("ButtonStateList", btnState);

    JSONVar ledState; for(int i=0;i<4;i++) ledState[i]=ledPhys[i];
    WebSerial.send("LedStateList", ledState);

    echoRelayState();
    alarms_publish_state();

    pushAtmChipConfigJSON();
    pushAtmRawReadsTable();
  }
}
