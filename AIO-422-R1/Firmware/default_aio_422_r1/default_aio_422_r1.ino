// ==== AIO-422-R1 (RP2350) SIMPLE FW ====
// 4x AI via ADS1115 (ADS1X15.h) on Wire1 (SDA=6, SCL=7)
// 2x AO via 2x MCP4725 on Wire1 (0x60,0x61)
// 2x RTD via 2x MAX31865 over SOFTWARE SPI (CS1=13, CS2=14, DI=11, DO=12, CLK=10)
// 4x Buttons (GPIO22..25), 4x LEDs (GPIO18..21)
// WebSerial + Modbus RTU + LittleFS persistence of Modbus/DAC

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
const float RTD_RREF      = 400.0f;
const float RTD_RNOMINAL  = 100.0f;
const max31865_numwires_t RTD_WIRES = MAX31865_3WIRE;  // same as working FW

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
enum : uint16_t {
  ISTS_BTN_BASE   = 1,     // 1..4  : buttons
  ISTS_LED_BASE   = 20,    // 20..23: LED states

  IREG_AI_BASE     = 100,  // 100..103: ADS1115 raw counts (int16)
  IREG_TEMP_BASE   = 120,  // 120..121: RTD temps *10°C (signed)
  IREG_AI_MV_BASE  = 140,  // 140..143: AI field mV (0..10000)

  HREG_DAC_BASE    = 200   // 200..201: DAC raw value 0..4095
};

// ================== Fw decls ==================
void handleValues(JSONVar values);
void handleCommand(JSONVar obj);
void handleDac(JSONVar obj);
void performReset();
void sendAllEchoesOnce();
void writeDac(int idx, uint16_t value);
void readSensors();

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

      // Modbus: raw counts and field mV
      mb.Ireg(IREG_AI_BASE    + ch, (uint16_t)raw);
      mb.Ireg(IREG_AI_MV_BASE + ch, aiMv[ch]);
    }
  } else {
    for (int ch=0; ch<4; ch++) {
      aiRaw[ch] = 0;
      aiMv[ch]  = 0;
      mb.Ireg(IREG_AI_BASE    + ch, 0);
      mb.Ireg(IREG_AI_MV_BASE + ch, 0);
    }
  }

  // --- RTDs via MAX31865 (software SPI) ---
  Adafruit_MAX31865* rtds[2] = { &rtd1, &rtd2 };
  for (int i=0;i<2;i++) {
    if (!rtd_ok[i]) {
      rtdTemp_x10[i] = 0;
      mb.Ireg(IREG_TEMP_BASE + i, 0);
      continue;
    }
    float temp = rtds[i]->temperature(RTD_RNOMINAL, RTD_RREF);
    int16_t t10 = (int16_t)roundf(temp * 10.0f);
    rtdTemp_x10[i] = t10;
    mb.Ireg(IREG_TEMP_BASE + i, (uint16_t)t10);
  }
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
    // Same as working firmware: BUTTON_USES_PULLUP == 0 → INPUT, HIGH = pressed
    pinMode(BTN_PINS[i], INPUT);
    buttonState[i] = buttonPrev[i] = false;
  }

  setDefaults();

  // WebSerial hooks
  WebSerial.on("values",  handleValues);
  WebSerial.on("command", handleCommand);
  WebSerial.on("dac",     handleDac);

  // Guarded FS init
  if (!initFilesystemAndConfig()) {
    WebSerial.send("message", "FATAL: Filesystem/config init failed");
  }

  // I2C setup (Wire1 on SDA1/SCL1, exactly like working firmware)
  Wire1.setSDA(SDA1);
  Wire1.setSCL(SCL1);
  Wire1.begin();
  Wire1.setClock(400000);

  // --- ADS1115 ---
  ads_ok = ads.begin();
  if (ads_ok) {
    // gain & data rate similar to working FW defaults
    ads.setGain(1);        // GAIN_ONE (±4.096V) in ADS1X15 lib
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
  // Discrete inputs
  for (uint16_t i=0;i<NUM_BTN;i++) mb.addIsts(ISTS_BTN_BASE + i);
  for (uint16_t i=0;i<NUM_LED;i++) mb.addIsts(ISTS_LED_BASE + i);

  // Input registers (analog in / temps / mV)
  for (uint16_t i=0;i<4;i++) mb.addIreg(IREG_AI_BASE    + i);   // raw
  for (uint16_t i=0;i<2;i++) mb.addIreg(IREG_TEMP_BASE  + i);   // RTD temps
  for (uint16_t i=0;i<4;i++) mb.addIreg(IREG_AI_MV_BASE + i);   // field mV

  // Holding registers (DACs)
  for (uint16_t i=0;i<2;i++) {
    mb.addHreg(HREG_DAC_BASE + i, dacRaw[i]);
  }

  WebSerial.send("message",
    "Boot OK (AIO-422-R1 RP2350: ADS1115@Wire1, 2xMCP4725@Wire1, 2xMAX31865 softSPI, 4 BTN, 4 LED)");
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
    bool pressed = (digitalRead(BTN_PINS[i]) == HIGH);  // same as working FW
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

  // --- DACs from Modbus holding registers ---
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
  }
}
