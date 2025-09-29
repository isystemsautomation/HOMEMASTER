// ---- FIX for Arduino auto-prototype: make PersistConfig visible to auto-prototypes 
struct PersistConfig;

// ==== AIO-422-R1 (RP2350 ONLY) ====
// ADS1115 via ADS1X15.h on Wire1 (SDA=6, SCL=7), 4ch analog
// + TWO MAX31865 RTD channels over SOFTWARE SPI (CS/DI/DO/CLK)
// + TWO MCP4725 12-bit DACs on Wire1 (0x61 and 0x60)
// WebSerial UI, Modbus RTU, LittleFS persistence
// Buttons: GPIO22..25, LEDs: GPIO18..21
// ============================================================================

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
#define LED_ACTIVE_LOW     0         // 1 if LEDs are active-LOW
#define BUTTON_USES_PULLUP 0         // 1 uses INPUT_PULLUP (pressed=LOW)

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

// RTD parameters (common setup)
static const float RTD_NOMINAL_OHMS = 100.0f;   // PT100 nominal
static const float RTD_REF_OHMS     = 430.0f;   // reference resistor on breakout
static const max31865_numwires_t RTD_WIRES = MAX31865_3WIRE;  // change to _2WIRE/_4WIRE if needed

// MCP4725 parameters
#define MCP4725_ADDR0  0x61   // U12, A0=+5V (AO1_DAC)
#define MCP4725_ADDR1  0x60   // U13, A0=GND  (AO0_DAC)
#define DAC_VREF_MV    5000   // both DACs powered at 5V

// ================== Modbus / RS-485 ==================
#define TX2 4
#define RX2 5
const int TxenPin = -1;     // -1 if not using TXEN
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== ADS1115 (ADS1X15) ==================
ADS1115 ads(0x48, &Wire1);  // bind to Wire1 explicitly
uint8_t  ads_gain = 0;      // 0..5 -> {2/3,1,2,4,8,16}×
uint8_t  ads_data_rate = 4; // 0..7 -> {8,16,32,64,128,250,475,860} SPS
uint16_t sample_ms = 50;    // polling interval

// ================== MAX31865 (2x RTD, SOFTWARE SPI) ==================
Adafruit_MAX31865 rtd1(RTD1_CS, RTD_DI, RTD_DO, RTD_CLK);
Adafruit_MAX31865 rtd2(RTD2_CS, RTD_DI, RTD_DO, RTD_CLK);

// ================== MCP4725 DACs ==================
Adafruit_MCP4725 dac0;  // 0x61
Adafruit_MCP4725 dac1;  // 0x60
uint16_t dac_raw[2] = {0, 0};  // 0..4095 current DAC codes

// ================== Buttons & LEDs config ==================
struct LedCfg   { uint8_t mode;   uint8_t source; };
struct ButtonCfg{ uint8_t action; };

LedCfg    ledCfg[4];
ButtonCfg buttonCfg[4];

bool      ledManual[4] = {false,false,false,false};   // manual toggles (actions 1..4)
bool      ledPhys[4]   = {false,false,false,false};   // physical pin state tracking

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
  uint16_t dac_raw[2];    // NEW: 2x MCP4725 code (0..4095)
  uint32_t crc32;
} __attribute__((packed));

static const uint32_t CFG_MAGIC   = 0x32323441UL; // 'A422'
static const uint16_t CFG_VERSION = 0x0009;       // bumped for dual MCP4725
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

inline bool readPressed(int i){
#if BUTTON_USES_PULLUP
  return digitalRead(BUTTON_PINS[i]) == LOW;   // pressed = LOW
#else
  return digitalRead(BUTTON_PINS[i]) == HIGH;  // pressed = HIGH
#endif
}

// ================== Defaults ==================
void setDefaults() {
  g_mb_address   = 3;
  g_mb_baud      = 19200;
  ads_gain       = 0;     // 2/3× FS ±6.144V
  ads_data_rate  = 4;     // 128 SPS
  sample_ms      = 50;
  dac_raw[0]     = 0;     // both start at 0V
  dac_raw[1]     = 0;

  for (int i=0;i<4;i++) {
    ledCfg[i].mode = 0;      // steady
    ledCfg[i].source = (i==0) ? 1 : 0; // LED0 shows heartbeat by default
    buttonCfg[i].action = 0; // None
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
  // runtime-only states
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
  IREG_MV_BASE      = 120,   // 120..123 : millivolts (uint16) ch0..ch3

  // MAX31865 RTD
  IREG_RTD_C_BASE     = 140,   // 140..141 : temperature °C x100 (int16)
  IREG_RTD_OHM_BASE   = 150,   // 150..151 : resistance ohms x100 (uint16)
  IREG_RTD_FAULT_BASE = 160,   // 160..161 : fault bitmask (uint16)

  // MCP4725 DACs
  IREG_DAC_MV_BASE  = 180,   // 180..181 : DAC outputs in mV (uint16)

  // Config (holding)
  HREG_GAIN         = 200,   // ADS gainIndex 0..5 (RW)
  HREG_DRATE        = 201,   // ADS dataRateIndex 0..7 (RW)
  HREG_SMPLL_MS     = 202,   // sample_ms 10..5000 (RW)
  HREG_DAC0_RAW     = 210,   // DAC0 raw 0..4095 (RW, 0x61)
  HREG_DAC1_RAW     = 211,   // DAC1 raw 0..4095 (RW, 0x60)

  // Discrete
  ISTS_LED_BASE     = 300,   // 300..303 : LED states
  ISTS_BTN_BASE     = 320,   // 320..323 : button states
  ISTS_RTD_BASE     = 360,   // 360..361 : RTD fault flag (1 = fault present)

  // Coils
  COIL_LEDTEST      = 400,   // pulse: LED test
  COIL_SAVE_CFG     = 401,   // pulse: save cfg
  COIL_REBOOT       = 402,   // pulse: reboot
  COIL_DAC0_EESAVE  = 403,   // pulse: write DAC0 code to EEPROM
  COIL_DAC1_EESAVE  = 404    // pulse: write DAC1 code to EEPROM
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

// ---- WebSerial echo helpers ----
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
  mvArr[0] = (uint16_t)lroundf((float)dac_raw[0] * (float)DAC_VREF_MV / 4095.0f);
  mvArr[1] = (uint16_t)lroundf((float)dac_raw[1] * (float)DAC_VREF_MV / 4095.0f);
  obj["raw"] = rawArr;
  obj["mv"]  = mvArr;
  WebSerial.send("DAC", obj);
}
JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) { JSONVar o; o["mode"]=ledCfg[i].mode; o["source"]=ledCfg[i].source; arr[i]=o; }
  return arr;
}
void sendAllEchoesOnce() {
  sendStatusEcho(); sendAdsEcho(); sendDacEcho();
  // Buttons config
  JSONVar btnCfg; for(int i=0;i<4;i++) btnCfg[i]=buttonCfg[i].action;
  WebSerial.send("ButtonConfigList", btnCfg);
  // LEDs config
  WebSerial.send("LedConfigList", LedConfigListFromCfg());
  // Current states
  JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i];
  WebSerial.send("ButtonStateList", btnState);
  JSONVar ledState; for(int i=0;i<4;i++) ledState[i]=ledPhys[i];
  WebSerial.send("LedStateList", ledState);
}

void sendSamplesEcho(const int16_t raw[4], const uint16_t mv[4]) {
  JSONVar smp; for (int i=0;i<4;i++){ smp["raw"][i] = raw[i]; smp["mv"][i] = mv[i]; }
  WebSerial.send("AIO_Samples", smp);
}

void sendRTDEcho(const int16_t c_x100[2], const uint16_t ohm_x100[2], const uint16_t fault[2]) {
  JSONVar obj;
  for (int i=0;i<2;i++) {
    obj["c"][i]    = c_x100[i];
    obj["ohm"][i]  = ohm_x100[i];
    obj["fault"][i]= fault[i];
  }
  WebSerial.send("AIO_RTD", obj);
}

// ---- WebSerial handlers ----
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
    int raw = (int)cfg["raw"];     // 0..4095
    dac_raw[idx] = (uint16_t)constrain(raw, 0, 4095);
    if (idx == 0) {
      dac0.setVoltage(dac_raw[0], false);
      mb.Hreg(HREG_DAC0_RAW, dac_raw[0]);
    } else {
      dac1.setVoltage(dac_raw[1], false);
      mb.Hreg(HREG_DAC1_RAW, dac_raw[1]);
    }
    sendDacEcho();
    WebSerial.send("message", "DAC updated");
    changed = true;

  } else if (type == "buttons") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      buttonCfg[i].action = (uint8_t)constrain((int)list[i]["action"], 0, 8);
    }
    WebSerial.send("message", "Buttons configuration updated");
    // echo
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

  } else {
    WebSerial.send("message", String("Unknown Config type: ") + t);
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); }
}

// Commands: { action:"reset" | "save" | "load" | "factory" | "ledtest" }
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
      // Restore DAC hardware
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

  } else {
    WebSerial.send("message", String("Unknown command: ") + actC);
  }
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
  if (loadConfigFS()) WebSerial.send("message", "Config loaded from flash");
  else { WebSerial.send("message", "No valid config. Using defaults."); saveConfigFS(); }

  // -------- I2C INIT on Wire1 (RP2350) --------
  Wire1.setSDA(SDA1);
  Wire1.setSCL(SCL1);
  Wire1.begin();
  Wire1.setClock(400000);

  // ADS1X15 on Wire1
  if (!ads.begin()) {
    WebSerial.send("message", "ERROR: ADS1115 not found at 0x48");
  }
  ads.setGain(ads_gain);            // 0..5 -> 2/3,1,2,4,8,16
  ads.setDataRate(ads_data_rate);   // 0..7 -> 8..860 SPS
  ads.setMode(1);                   // 1 = single-shot

  // -------- MCP4725 x2 on Wire1 --------
  if (!dac0.begin(MCP4725_ADDR0, &Wire1)) {
    WebSerial.send("message", "ERROR: MCP4725 #0 not found at 0x61");
  } else {
    dac0.setVoltage(dac_raw[0], false);
  }
  if (!dac1.begin(MCP4725_ADDR1, &Wire1)) {
    WebSerial.send("message", "ERROR: MCP4725 #1 not found at 0x60");
  } else {
    dac1.setVoltage(dac_raw[1], false);
  }

  // -------- MAX31865 SOFTWARE SPI init --------
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

  // RTD telemetry
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_RTD_C_BASE     + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_RTD_OHM_BASE   + i);
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_RTD_FAULT_BASE + i);

  // DAC telemetry
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_DAC_MV_BASE + i);

  // ---- Register config (holding) ----
  mb.addHreg(HREG_GAIN);         mb.Hreg(HREG_GAIN, ads_gain);
  mb.addHreg(HREG_DRATE);        mb.Hreg(HREG_DRATE, ads_data_rate);
  mb.addHreg(HREG_SMPLL_MS);     mb.Hreg(HREG_SMPLL_MS, sample_ms);
  mb.addHreg(HREG_DAC0_RAW);     mb.Hreg(HREG_DAC0_RAW, dac_raw[0]);
  mb.addHreg(HREG_DAC1_RAW);     mb.Hreg(HREG_DAC1_RAW, dac_raw[1]);

  // ---- Discrete stats ----
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_LED_BASE + i);
  for (uint16_t i=0;i<4;i++) mb.addIsts(ISTS_BTN_BASE + i);
  for (uint16_t i=0;i<2;i++) mb.addIsts(ISTS_RTD_BASE + i);

  // ---- Coils ----
  mb.addCoil(COIL_LEDTEST);
  mb.addCoil(COIL_SAVE_CFG);
  mb.addCoil(COIL_REBOOT);
  mb.addCoil(COIL_DAC0_EESAVE);
  mb.addCoil(COIL_DAC1_EESAVE);

  // WebSerial handlers
  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);

  // Initial echoes
  sendAllEchoesOnce();
  WebSerial.send("message", "Boot OK (AIO-422-R1, RP2350, ADS1X15 + 2x MAX31865 via SoftSPI + 2x MCP4725 DAC)");
}

// ================== HREG write watcher (optional Modbus control) ==================
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

  if (dv0 != prevDac0) {
    prevDac0 = dv0;
    dac_raw[0] = constrain((int)dv0, 0, 4095);
    dac0.setVoltage(dac_raw[0], false);   // fast write (not EEPROM)
    changed = true;
  }
  if (dv1 != prevDac1) {
    prevDac1 = dv1;
    dac_raw[1] = constrain((int)dv1, 0, 4095);
    dac1.setVoltage(dac_raw[1], false);   // fast write (not EEPROM)
    changed = true;
  }

  if (changed) { cfgDirty = true; lastCfgTouchMs = millis(); sendAdsEcho(); sendDacEcho(); }
}

// ================== Button actions ==================
void doButtonAction(uint8_t idx) {
  uint8_t act = buttonCfg[idx].action;
  switch (act) {
    case 0: /* None */ break;
    case 1: case 2: case 3: case 4: {
      uint8_t led = act - 1;                       // 0..3
      ledManual[led] = !ledManual[led];
      break;
    }
    case 5: { // Cycle ADS gain
      ads_gain = (ads_gain + 1) % 6;
      ads.setGain(ads_gain);
      mb.Hreg(HREG_GAIN, ads_gain);
      sendAdsEcho();
      WebSerial.send("message", String("ADS gain -> index ") + ads_gain);
      cfgDirty = true; lastCfgTouchMs = millis();
      break;
    }
    case 6: { // LED test
      for (int k=0;k<2;k++) {
        for (int i=0;i<4;i++) setLedPhys(i, true);
        delay(100);
        for (int i=0;i<4;i++) setLedPhys(i, false);
        delay(100);
      }
      break;
    }
    case 7: { // Save
      if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
      else                WebSerial.send("message", "ERROR: Save failed");
      break;
    }
    case 8: { // Reset
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
    case 0: return false;                         // None
    case 1: return blinkPhase;                    // Heartbeat signal
    case 2: return buttonState[0];
    case 3: return buttonState[1];
    case 4: return buttonState[2];
    case 5: return buttonState[3];
    case 6: return buttonState[0]||buttonState[1]||buttonState[2]||buttonState[3];
    case 7: return samplingTick;                  // goes true on each sample loop
    default: return false;
  }
}

// ================== Main loop ==================
void loop() {
  unsigned long now = millis();

  // Heartbeat phase
  if (now - lastBlinkToggle >= blinkPeriodMs) {
    lastBlinkToggle = now;
    blinkPhase = !blinkPhase;
  }

  // Auto-save after quiet period
  if (cfgDirty && (now - lastCfgTouchMs >= CFG_AUTOSAVE_MS)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    cfgDirty = false;
  }

  // Modbus
  mb.task();
  serviceHregWrites();

  // Coils
  if (mb.Coil(COIL_LEDTEST)) {
    for (int k=0;k<2;k++) {
      for (int i=0;i<4;i++) setLedPhys(i, true);
      delay(120);
      for (int i=0;i<4;i++) setLedPhys(i, false);
      delay(120);
    }
    mb.Coil(COIL_LEDTEST, false);
  }
  if (mb.Coil(COIL_SAVE_CFG)) {
    if (saveConfigFS()) WebSerial.send("message", "Configuration saved");
    else                WebSerial.send("message", "ERROR: Save failed");
    mb.Coil(COIL_SAVE_CFG, false);
  }
  if (mb.Coil(COIL_REBOOT)) {
    WebSerial.send("message", "Rebooting…");
    delay(120);
    performReset();
  }
  if (mb.Coil(COIL_DAC0_EESAVE)) {
    dac0.setVoltage(dac_raw[0], true); // write with EEPROM
    WebSerial.send("message", "DAC0: code written to EEPROM");
    mb.Coil(COIL_DAC0_EESAVE, false);
  }
  if (mb.Coil(COIL_DAC1_EESAVE)) {
    dac1.setVoltage(dac_raw[1], true); // write with EEPROM
    WebSerial.send("message", "DAC1: code written to EEPROM");
    mb.Coil(COIL_DAC1_EESAVE, false);
  }

  // Buttons: short press action
  for (int i = 0; i < 4; i++) {
    bool pressed = readPressed(i);
    if (pressed != buttonState[i] && (now - btnChangeAt[i] >= BTN_DEBOUNCE_MS)) {
      btnChangeAt[i] = now;
      buttonPrev[i]  = buttonState[i];
      buttonState[i] = pressed;

      if (buttonPrev[i] && !buttonState[i]) {    // release -> short press action
        doButtonAction(i);
      }
    }
    mb.setIsts(ISTS_BTN_BASE + i, buttonState[i]);
  }

  // Sampling
  static int16_t  rawArr[4] = {0,0,0,0};
  static uint16_t mvArr[4]  = {0,0,0,0};

  // RTD buffers
  static int16_t  rtdC_x100[2]   = {0,0};
  static uint16_t rtdOhm_x100[2] = {0,0};
  static uint16_t rtdFault[2]    = {0,0};

  samplingTick = false;

  if (now - lastSample >= sample_ms) {
    lastSample = now;
    samplingTick = true;

    // ---- ADS1115 4 channels ----
    for (int ch=0; ch<4; ch++) {
      int16_t raw = ads.readADC(ch);       // single-shot conversion
      float   v   = ads.toVoltage(raw);
      long    mv  = lroundf(v * 1000.0f);
      if (mv < 0) mv = 0; if (mv > 65535) mv = 65535;

      rawArr[ch] = raw;
      mvArr[ch]  = (uint16_t)mv;

      mb.Ireg(IREG_RAW_BASE + ch, (int16_t)rawArr[ch]);
      mb.Ireg(IREG_MV_BASE  + ch, (uint16_t)mvArr[ch]);
    }

    // ---- MAX31865: two RTDs (software SPI) ----
    // RTD1
    {
      uint16_t rtd = rtd1.readRTD();
      float ohms = (float)rtd * RTD_REF_OHMS / 32768.0f;
      float tC   = rtd1.temperature(RTD_NOMINAL_OHMS, RTD_REF_OHMS);
      uint8_t f  = rtd1.readFault();
      if (f) rtd1.clearFault();

      int32_t cx100 = lroundf(tC * 100.0f);
      if (cx100 < -32768) cx100 = -32768; if (cx100 > 32767) cx100 = 32767;
      int32_t ox100 = lroundf(ohms * 100.0f);
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
      uint8_t f  = rtd2.readFault();
      if (f) rtd2.clearFault();

      int32_t cx100 = lroundf(tC * 100.0f);
      if (cx100 < -32768) cx100 = -32768; if (cx100 > 32767) cx100 = 32767;
      int32_t ox100 = lroundf(ohms * 100.0f);
      if (ox100 < 0) ox100 = 0; if (ox100 > 65535) ox100 = 65535;

      rtdC_x100[1]   = (int16_t)cx100;
      rtdOhm_x100[1] = (uint16_t)ox100;
      rtdFault[1]    = (uint16_t)f;

      mb.Ireg(IREG_RTD_C_BASE + 1, (int16_t)rtdC_x100[1]);
      mb.Ireg(IREG_RTD_OHM_BASE + 1, (uint16_t)rtdOhm_x100[1]);
      mb.Ireg(IREG_RTD_FAULT_BASE + 1, (uint16_t)rtdFault[1]);
      mb.setIsts(ISTS_RTD_BASE + 1, (f != 0));
    }

    // ---- DAC derived telemetry ----
    uint16_t dac_mv0 = (uint16_t)lroundf((float)dac_raw[0] * (float)DAC_VREF_MV / 4095.0f);
    uint16_t dac_mv1 = (uint16_t)lroundf((float)dac_raw[1] * (float)DAC_VREF_MV / 4095.0f);
    mb.Ireg(IREG_DAC_MV_BASE + 0, dac_mv0);
    mb.Ireg(IREG_DAC_MV_BASE + 1, dac_mv1);

    // WebSerial live echoes for the WebConfig UI
    sendSamplesEcho(rawArr, mvArr);
    sendRTDEcho(rtdC_x100, rtdOhm_x100, rtdFault);
  }

  // Drive LEDs from (source OR manual) with optional blink
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

    // also push button/led states so UI dots stay live
    JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i];
    WebSerial.send("ButtonStateList", btnState);
    WebSerial.send("LedStateList", ledStateList);
  }
}
