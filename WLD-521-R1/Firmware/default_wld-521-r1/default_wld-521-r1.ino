// ================== WLD-521-R1 — Flow + Heat (pos-based sensor selection) ==================
#include <Arduino.h>
#include <ModbusSerial.h>
#include <SimpleWebSerial.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <utility>
#include "hardware/watchdog.h"

// ================== UART2 (RS-485 / Modbus) ==================
#define TX2 4
#define RX2 5
const int TxenPin = -1;
int SlaveId = 1;
ModbusSerial mb(Serial2, SlaveId, TxenPin);

// ================== GPIO MAP (WLD-521-R1) ==================
static const uint8_t DI_PINS[5]    = {3, 2, 8, 9, 15}; // DI1..DI5
static const uint8_t RELAY_PINS[2] = {0, 1};           // R1..R2
static const uint8_t ONEWIRE_PIN   = 16;
OneWire oneWire(ONEWIRE_PIN);

// ================== Sizes & types ==================
static const uint8_t NUM_DI  = 5;
static const uint8_t NUM_RLY = 2;
enum : uint8_t { IT_WATER=0, IT_HUMIDITY=1, IT_WCOUNTER=2 };

struct InCfg  { bool enabled; bool inverted; uint8_t action; uint8_t target; uint8_t type; };
struct RlyCfg { bool enabled; bool inverted; };

InCfg  diCfg[NUM_DI];
RlyCfg rlyCfg[NUM_RLY];

bool diState[NUM_DI] = {0};
bool diPrev[NUM_DI]  = {0};
uint32_t diCounter[NUM_DI]   = {0};
uint32_t diLastEdgeMs[NUM_DI]= {0};
const uint32_t CNT_DEBOUNCE_MS = 20;

bool desiredRelay[NUM_RLY] = {false,false};
uint32_t rlyPulseUntil[NUM_RLY] = {0,0};
const uint32_t PULSE_MS = 500;

// ===== Flow meter per-DI =====
uint32_t flowPulsesPerL[NUM_DI];
float    flowCalibRate[NUM_DI];
float    flowCalibAccum[NUM_DI];
uint32_t flowCounterBase[NUM_DI];

float    flowRateLmin[NUM_DI];
uint32_t lastPulseSnapshot[NUM_DI];
uint32_t nextRateTickMs = 0;

// ===== Heat energy per-DI =====
bool     heatEnabled[NUM_DI];
uint64_t heatAddrA[NUM_DI], heatAddrB[NUM_DI];
float    heatCp[NUM_DI], heatRho[NUM_DI], heatCalib[NUM_DI];
double   heatTA[NUM_DI], heatTB[NUM_DI], heatDT[NUM_DI];
double   heatPowerW[NUM_DI];
double   heatEnergyJ[NUM_DI];

uint32_t nextOneWireConvertMs = 0;
bool     oneWireBusy = false;

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 250;

// ================== Modbus persisted ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
struct PersistConfigV1 {
  uint32_t magic; uint16_t version; uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address; uint32_t mb_baud; uint32_t crc32;
} __attribute__((packed));

struct PersistConfigV2 {
  uint32_t magic; uint16_t version; uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address; uint32_t mb_baud; uint32_t crc32;
} __attribute__((packed));

struct PersistConfigV3 {
  uint32_t magic;  uint16_t version;  uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t  mb_address;
  uint32_t mb_baud;
  uint32_t flowPPL[NUM_DI];
  float    flowCalib[NUM_DI];
  float    flowAccumL[NUM_DI];
  uint32_t crc32;
} __attribute__((packed));

struct PersistConfigV4 {
  uint32_t magic;  uint16_t version;  uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t  mb_address;
  uint32_t mb_baud;
  uint32_t flowPPL[NUM_DI];
  float    flowCalibRate[NUM_DI];
  float    flowCalibAccum[NUM_DI];
  uint32_t flowCounterBase[NUM_DI];
  uint32_t crc32;
} __attribute__((packed));

// V5 current
struct PersistConfigV5 {
  uint32_t magic;  uint16_t version;  uint16_t size;

  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];

  uint8_t  mb_address;
  uint32_t mb_baud;

  uint32_t flowPPL[NUM_DI];
  float    flowCalibRate[NUM_DI];
  float    flowCalibAccum[NUM_DI];
  uint32_t flowCounterBase[NUM_DI];

  bool     heatEnabled[NUM_DI];
  uint64_t heatAddrA[NUM_DI];
  uint64_t heatAddrB[NUM_DI];
  float    heatCp[NUM_DI];
  float    heatRho[NUM_DI];
  float    heatCalib[NUM_DI];
  double   heatEnergyJ[NUM_DI];

  uint32_t crc32;
} __attribute__((packed));

using PersistConfig = PersistConfigV5;

static const uint32_t CFG_MAGIC       = 0x31524C57UL; // 'WLR1'
static const uint16_t CFG_VERSION_V1  = 0x0001;
static const uint16_t CFG_VERSION_V2  = 0x0002;
static const uint16_t CFG_VERSION_V3  = 0x0003;
static const uint16_t CFG_VERSION_V4  = 0x0004;
static const uint16_t CFG_VERSION     = 0x0005;
static const char*    CFG_PATH        = "/cfg.bin";

// ---- 1-Wire DB ----
static const char* ONEWIRE_DB_PATH = "/ow_sensors.json";
static const size_t MAX_OW_SENSORS = 32;
struct OwRec { uint64_t addr; String name; };
OwRec  g_owDb[MAX_OW_SENSORS];
size_t g_owCount = 0;

// === 1-Wire last-good cache & error tracking ===
double   owLastGoodTemp[MAX_OW_SENSORS];
uint32_t owLastGoodMs[MAX_OW_SENSORS];
uint32_t owErrCount[MAX_OW_SENSORS];
const uint32_t OW_FAIL_HIDE_MS = 15000;

volatile bool   cfgDirty       = false;
uint32_t        lastCfgTouchMs = 0;
const uint32_t  CFG_AUTOSAVE_MS = 1500;

// ================== Utils ==================
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  crc = ~crc;
  while (len--) {
    crc ^= *data++;
    for (uint8_t k=0;k<8;k++) crc = (crc >> 1) ^ (0xEDB88320UL & (-(int32_t)(crc & 1)));
  }
  return ~crc;
}
inline bool timeAfter32(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

String hex64(uint64_t v) {
  char buf[19]; buf[0]='0'; buf[1]='1'==0 ? 'x' : 'x'; // keep compiler happy
  buf[1]='x';
  for (int i=0;i<16;i++) { uint8_t nib=(v >> ((15-i)*4)) & 0xF; buf[2+i]=(nib<10)?('0'+nib):('A'+(nib-10)); }
  buf[18]=0; return String(buf);
}
uint64_t romBytesToU64(const uint8_t *addr) { uint64_t v=0; for (int i=7;i>=0;i--) { v = (v<<8) | addr[i]; } return v; }
bool parseHex64(const char* s, uint64_t &out) {
  if (!s) return false;
  String str(s); str.trim();
  if (str.startsWith("0x") || str.startsWith("0X")) str.remove(0,2);
  if (str.length()==0 || str.length()>16) return false;
  uint64_t v=0;
  for (uint16_t i=0;i<str.length();i++){
    char c=str[i]; uint8_t x;
    if (c>='0'&&c<='9') x=c-'0';
    else if (c>='a'&&c<='f') x=10+(c-'a');
    else if (c>='A'&&c<='F') x=10+(c-'A');
    else return false;
    v = (v<<4) | x;
  }
  out=v; return true;
}
bool parseDec64(const char* s, uint64_t &out) {
  if (!s) return false;
  while (*s==' '||*s=='\t') s++;
  if (!*s) return false;
  uint64_t v=0; bool any=false;
  for (const char* p=s; *p; ++p) {
    if (*p<'0'||*p>'9') return false;
    v = v*10 + uint64_t(*p-'0'); any=true;
  }
  if (!any) return false;
  out=v; return true;
}
static bool jsonGetStr(JSONVar obj, const char* key, String &out) {
  if (!obj.hasOwnProperty(key)) return false;
  JSONVar v = obj[key];
  String t = JSON.typeof(v);
  if (t=="string") {
    const char* s = (const char*)v;
    if (!s) return false;
    out = String(s); out.trim();
    return out.length()>0;
  }
  return false;
}
static bool jsonGetDouble(JSONVar obj, const char* key, double &out) {
  if (!obj.hasOwnProperty(key)) return false;
  JSONVar v=obj[key];
  String t=JSON.typeof(v);
  if (t=="number") { out = (double)v; return true; }
  if (t=="string") { const char* s=(const char*)v; if(!s) return false; char* e=nullptr; out = strtod(s,&e); return e && e!=s; }
  return false;
}

// ======= SAFE 64-bit address extraction =======
static bool jsonGetAddr64(JSONVar obj, const char* baseKey, uint64_t &out) {
  String s;
  if (jsonGetStr(obj,"rom_hex", s) || jsonGetStr(obj,"addr", s) ||
      jsonGetStr(obj,"address", s) || jsonGetStr(obj,"rom", s)) {
    if (parseHex64(s.c_str(), out)) return true;
  }
  if (obj.hasOwnProperty("addr_hi") && obj.hasOwnProperty("addr_lo")) {
    JSONVar vhi = obj["addr_hi"], vlo = obj["addr_lo"];
    uint32_t hi=0, lo=0;
    String th = JSON.typeof(vhi), tl = JSON.typeof(vlo);
    if (th=="number") hi = (uint32_t)(double)vhi; else if (th=="string") { uint64_t tmp; if (parseDec64((const char*)vhi, tmp)) hi=(uint32_t)tmp; }
    if (tl=="number") lo = (uint32_t)(double)vlo; else if (tl=="string") { uint64_t tmp; if (parseDec64((const char*)vlo, tmp)) lo=(uint32_t)tmp; }
    out = ( (uint64_t)hi << 32 ) | (uint64_t)lo;
    if (hi!=0 || lo!=0) return true;
  }
  if (jsonGetStr(obj,"addr_u64_str", s) || jsonGetStr(obj,"rom_u64_str", s)) {
    if (parseDec64(s.c_str(), out)) return true;
  }
  if (obj.hasOwnProperty("addr_u64")) {
    JSONVar v = obj["addr_u64"];
    if (JSON.typeof(v)=="number") {
      double d = (double)v;
      const double MAX_SAFE = 9007199254740991.0;
      if (d >= 0.0 && d <= MAX_SAFE && d == floor(d)) { out = (uint64_t)d; return true; }
      else { WebSerial.send("message","WARNING: addr_u64 not safely representable; ignoring numeric path."); }
    } else if (JSON.typeof(v)=="string") {
      if (parseDec64((const char*)v, out)) return true;
    }
  }
  if (baseKey && jsonGetStr(obj, baseKey, s)) {
    if (parseDec64(s.c_str(), out)) return true;
  }
  return false;
}

// ======= NEW: position helpers =======
// 1-based position -> ROM address (0 if invalid/out of range)
inline uint64_t owdbAddrAtPos(uint32_t pos){
  if (pos == 0) return 0;
  size_t i = (pos - 1);
  if (i < g_owCount) return g_owDb[i].addr;
  return 0;
}

// Try to read a numeric 'pos' (accept number or numeric string)
static bool jsonGetPos(JSONVar obj, const char* key, uint32_t &out){
  if (!obj.hasOwnProperty(key)) return false;
  JSONVar v = obj[key];
  String t = JSON.typeof(v);
  if (t=="number") {
    double d = (double)v;
    if (d >= 0.0 && d == floor(d)) { out = (uint32_t)d; return true; }
  } else if (t=="string") {
    const char* s = (const char*)v;
    if (!s) return false;
    uint64_t tmp;
    if (parseDec64(s, tmp)) { out = (uint32_t)tmp; return true; }
  }
  return false;
}

// ================== Defaults / persist ==================
void setDefaults() {
  for (int i=0;i<NUM_DI;i++)  diCfg[i]  = (InCfg){ true, false, 0, 0, IT_WATER };
  for (int i=0;i<NUM_RLY;i++) rlyCfg[i] = (RlyCfg){ true, false };
  for (int i=0;i<NUM_RLY;i++){ desiredRelay[i]=false; rlyPulseUntil[i]=0; }
  for (int i=0;i<NUM_DI;i++){
    diCounter[i]=0; diLastEdgeMs[i]=0;
    flowPulsesPerL[i] = 450;
    flowCalibRate[i]  = 1.0f;
    flowCalibAccum[i] = 1.0f;
    flowCounterBase[i]= 0;
    flowRateLmin[i]   = 0.0f;
    lastPulseSnapshot[i] = 0;

    heatEnabled[i]=false;
    heatAddrA[i]=0; heatAddrB[i]=0;
    heatCp[i]=4186.0f; heatRho[i]=1.0f; heatCalib[i]=1.0f;
    heatTA[i]=NAN; heatTB[i]=NAN; heatDT[i]=0.0;
    heatPowerW[i]=0.0; heatEnergyJ[i]=0.0;
  }
  for (size_t i=0;i<MAX_OW_SENSORS;i++){ owLastGoodTemp[i]=NAN; owLastGoodMs[i]=0; owErrCount[i]=0; }

  oneWireBusy=false; nextOneWireConvertMs=millis();
  nextRateTickMs = millis() + 1000;
  g_mb_address=3; g_mb_baud=19200;
}

void captureToPersist(PersistConfig &pc){
  pc.magic=CFG_MAGIC; pc.version=CFG_VERSION; pc.size=sizeof(PersistConfig);
  memcpy(pc.diCfg,diCfg,sizeof(diCfg)); memcpy(pc.rlyCfg,rlyCfg,sizeof(rlyCfg));
  memcpy(pc.desiredRelay,desiredRelay,sizeof(desiredRelay));
  pc.mb_address=g_mb_address; pc.mb_baud=g_mb_baud;
  for (int i=0;i<NUM_DI;i++){
    pc.flowPPL[i]        = flowPulsesPerL[i];
    pc.flowCalibRate[i]  = flowCalibRate[i];
    pc.flowCalibAccum[i] = flowCalibAccum[i];
    pc.flowCounterBase[i]= flowCounterBase[i];

    pc.heatEnabled[i] = heatEnabled[i];
    pc.heatAddrA[i]   = heatAddrA[i];
    pc.heatAddrB[i]   = heatAddrB[i];
    pc.heatCp[i]      = heatCp[i];
    pc.heatRho[i]     = heatRho[i];
    pc.heatCalib[i]   = heatCalib[i];
    pc.heatEnergyJ[i] = heatEnergyJ[i];
  }
  pc.crc32=0;
  pc.crc32=crc32_update(0,(const uint8_t*)&pc,sizeof(PersistConfig));
}

// ----- legacy apply -----
bool applyFromPersistV1(const PersistConfigV1 &v1){
  if (v1.magic!=CFG_MAGIC || v1.size!=sizeof(PersistConfigV1)) return false;
  PersistConfigV1 tmp=v1; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc) return false;
  if (v1.version!=CFG_VERSION_V1) return false;
  memcpy(diCfg, v1.diCfg, sizeof(diCfg));
  for (int i=0;i<NUM_DI;i++) if (diCfg[i].type>IT_WCOUNTER) diCfg[i].type=IT_WATER;
  memcpy(rlyCfg, v1.rlyCfg, sizeof(rlyCfg));
  memcpy(desiredRelay, v1.desiredRelay, sizeof(desiredRelay));
  g_mb_address=v1.mb_address; g_mb_baud=v1.mb_baud;

  for (int i=0;i<NUM_DI;i++){
    flowPulsesPerL[i]=450; flowCalibRate[i]=1.0f; flowCalibAccum[i]=1.0f; flowCounterBase[i]=diCounter[i];
    flowRateLmin[i]=0.0f; lastPulseSnapshot[i]=0;
    heatEnabled[i]=false; heatAddrA[i]=0; heatAddrB[i]=0;
    heatCp[i]=4186.0f; heatRho[i]=1.0f; heatCalib[i]=1.0f; heatEnergyJ[i]=0.0;
  }
  nextRateTickMs = millis() + 1000;
  return true;
}
bool applyFromPersistV2(const PersistConfigV2 &v2){
  if (v2.magic!=CFG_MAGIC || v2.size!=sizeof(PersistConfigV2)) return false;
  PersistConfigV2 tmp=v2; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc) return false;
  if (v2.version!=CFG_VERSION_V2) return false;
  memcpy(diCfg, v2.diCfg, sizeof(diCfg));
  for (int i=0;i<NUM_DI;i++) if (diCfg[i].type>IT_WCOUNTER) diCfg[i].type=IT_WATER;
  memcpy(rlyCfg, v2.rlyCfg, sizeof(rlyCfg));
  memcpy(desiredRelay, v2.desiredRelay, sizeof(desiredRelay));
  g_mb_address=v2.mb_address; g_mb_baud=v2.mb_baud;

  for (int i=0;i<NUM_DI;i++){
    flowPulsesPerL[i]=450; flowCalibRate[i]=1.0f; flowCalibAccum[i]=1.0f; flowCounterBase[i]=diCounter[i];
    flowRateLmin[i]=0.0f; lastPulseSnapshot[i]=0;
    heatEnabled[i]=false; heatAddrA[i]=0; heatAddrB[i]=0;
    heatCp[i]=4186.0f; heatRho[i]=1.0f; heatCalib[i]=1.0f; heatEnergyJ[i]=0.0;
  }
  nextRateTickMs = millis() + 1000;
  return true;
}
bool applyFromPersistV3(const PersistConfigV3 &pc){
  if (pc.magic!=CFG_MAGIC || pc.size!=sizeof(PersistConfigV3)) return false;
  PersistConfigV3 tmp=pc; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc) return false;
  if (pc.version!=CFG_VERSION_V3) return false;

  memcpy(diCfg, pc.diCfg, sizeof(diCfg));
  memcpy(rlyCfg, pc.rlyCfg, sizeof(rlyCfg));
  memcpy(desiredRelay, pc.desiredRelay, sizeof(desiredRelay));
  g_mb_address=pc.mb_address; g_mb_baud=pc.mb_baud;

  for (int i=0;i<NUM_DI;i++){
    flowPulsesPerL[i]= pc.flowPPL[i] ? pc.flowPPL[i] : 450;
    float seedCal = (isnan(pc.flowCalib[i])||pc.flowCalib[i]<=0) ? 1.0f : pc.flowCalib[i];
    flowCalibRate[i]  = seedCal;
    flowCalibAccum[i] = seedCal;
    flowCounterBase[i]= diCounter[i];
    flowRateLmin[i]   = 0.0f;
    lastPulseSnapshot[i] = 0;

    heatEnabled[i]=false; heatAddrA[i]=0; heatAddrB[i]=0;
    heatCp[i]=4186.0f; heatRho[i]=1.0f; heatCalib[i]=1.0f; heatEnergyJ[i]=0.0;
  }
  nextRateTickMs = millis() + 1000;
  return true;
}
bool applyFromPersist(const PersistConfig &pc){
  if (pc.magic!=CFG_MAGIC || pc.size!=sizeof(PersistConfig)) return false;
  PersistConfig tmp=pc; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc) return false;
  if (pc.version!=CFG_VERSION) return false;

  memcpy(diCfg, pc.diCfg, sizeof(diCfg));
  memcpy(rlyCfg, pc.rlyCfg, sizeof(rlyCfg));
  memcpy(desiredRelay, pc.desiredRelay, sizeof(desiredRelay));
  g_mb_address=pc.mb_address; g_mb_baud=pc.mb_baud;

  for (int i=0;i<NUM_DI;i++){
    flowPulsesPerL[i] = pc.flowPPL[i] ? pc.flowPPL[i] : 1;
    flowCalibRate[i]  = (isnan(pc.flowCalibRate[i]) || pc.flowCalibRate[i]<=0) ? 1.0f : pc.flowCalibRate[i];
    flowCalibAccum[i] = (isnan(pc.flowCalibAccum[i])|| pc.flowCalibAccum[i]<=0)? 1.0f : pc.flowCalibAccum[i];
    flowCounterBase[i]= pc.flowCounterBase[i];
    flowRateLmin[i]   = 0.0f;
    lastPulseSnapshot[i] = 0;

    heatEnabled[i] = pc.heatEnabled[i];
    heatAddrA[i]   = pc.heatAddrA[i];
    heatAddrB[i]   = pc.heatAddrB[i];
    heatCp[i]      = (isnan(pc.heatCp[i]) || pc.heatCp[i]<=0) ? 4186.0f : pc.heatCp[i];
    heatRho[i]     = (isnan(pc.heatRho[i])|| pc.heatRho[i]<=0)? 1.0f    : pc.heatRho[i];
    heatCalib[i]   = (isnan(pc.heatCalib[i])||pc.heatCalib[i]<=0)?1.0f  : pc.heatCalib[i];
    heatEnergyJ[i] = isfinite(pc.heatEnergyJ[i]) ? pc.heatEnergyJ[i] : 0.0;
  }
  nextRateTickMs = millis() + 1000;
  return true;
}
bool saveConfigFS(){
  PersistConfig pc{}; captureToPersist(pc);
  File f=LittleFS.open(CFG_PATH,"w");
  if(!f){ WebSerial.send("message","save: open failed"); return false; }
  size_t n=f.write((const uint8_t*)&pc,sizeof(pc)); f.flush(); f.close();
  if(n!=sizeof(pc)){ WebSerial.send("message","save: short write "); return false; }
  File r=LittleFS.open(CFG_PATH,"r"); if(!r){ WebSerial.send("message","save: reopen failed"); return false; }
  if((size_t)r.size()!=sizeof(PersistConfig)){ WebSerial.send("message","save: size mismatch after write"); r.close(); return false; }
  PersistConfig back{}; size_t nr=r.read((uint8_t*)&back,sizeof(back)); r.close();
  if(nr!=sizeof(back)){ WebSerial.send("message","save: short readback"); return false; }
  PersistConfig tmp=back; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc){ WebSerial.send("message","save: CRC verify failed"); return false; }
  return true;
}
bool loadConfigFS(){
  File f=LittleFS.open(CFG_PATH,"r"); if(!f){ WebSerial.send("message","load: open failed"); return false; }
  size_t sz=f.size();
  if (sz==sizeof(PersistConfigV1)){
    PersistConfigV1 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v1)"); return false; }
    if(!applyFromPersistV1(pc)){ WebSerial.send("message","load: v1 magic/version/crc mismatch"); return false; }
    WebSerial.send("message","Loaded legacy config v1 → migrated to v5 defaults for flow/heat.");
    return true;
  } else if (sz==sizeof(PersistConfigV2)){
    PersistConfigV2 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v2)"); return false; }
    if(!applyFromPersistV2(pc)){ WebSerial.send("message","load: v2 magic/version/crc mismatch"); return false; }
    WebSerial.send("message","Loaded legacy config v2 → migrated to v5 defaults for flow/heat.");
    return true;
  } else if (sz==sizeof(PersistConfigV3)){
    PersistConfigV3 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v3)"); return false; }
    if(!applyFromPersistV3(pc)){ WebSerial.send("message","load: v3 magic/version/crc mismatch"); return false; }
    WebSerial.send("message","Loaded legacy config v3 → migrated to v5 (split calibs, derived total, heat defaults).");
    return true;
  } else if (sz==sizeof(PersistConfigV4)){
    PersistConfigV4 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v4)"); return false; }
    memcpy(diCfg, pc.diCfg, sizeof(diCfg));
    memcpy(rlyCfg, pc.rlyCfg, sizeof(rlyCfg));
    memcpy(desiredRelay, pc.desiredRelay, sizeof(desiredRelay));
    g_mb_address=pc.mb_address; g_mb_baud=pc.mb_baud;
    for (int i=0;i<NUM_DI;i++){
      flowPulsesPerL[i]= pc.flowPPL[i] ? pc.flowPPL[i] : 1;
      flowCalibRate[i]= (isnan(pc.flowCalibRate[i])||pc.flowCalibRate[i]<=0)?1.0f:pc.flowCalibRate[i];
      flowCalibAccum[i]=(isnan(pc.flowCalibAccum[i])||pc.flowCalibAccum[i]<=0)?1.0f:pc.flowCalibAccum[i];
      flowCounterBase[i]=pc.flowCounterBase[i];
      flowRateLmin[i]=0.0f; lastPulseSnapshot[i]=0;
      heatEnabled[i]=false; heatAddrA[i]=0; heatAddrB[i]=0;
      heatCp[i]=4186.0f; heatRho[i]=1.0f; heatCalib[i]=1.0f; heatEnergyJ[i]=0.0;
    }
    nextRateTickMs = millis() + 1000;
    WebSerial.send("message","Loaded legacy config v4 → migrated to v5 (heat defaults).");
    return true;
  } else if (sz==sizeof(PersistConfig)){
    PersistConfig pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v5)"); return false; }
    if(!applyFromPersist(pc)){ WebSerial.send("message","load: v5 magic/version/crc mismatch"); return false; }
    return true;
  } else {
    WebSerial.send("message",String("load: unexpected size ")+sz); f.close(); return false;
  }
}
bool initFilesystemAndConfig(){
  if(!LittleFS.begin()){
    WebSerial.send("message","LittleFS mount failed. Formatting…");
    if(!LittleFS.format() || !LittleFS.begin()){ WebSerial.send("message","FATAL: FS mount/format failed"); return false; }
  }
  if (loadConfigFS()){ WebSerial.send("message","Config loaded from flash"); return true; }
  WebSerial.send("message","No valid config. Using defaults."); setDefaults();
  if (saveConfigFS()){ WebSerial.send("message","Defaults saved"); return true; }
  WebSerial.send("message","First save failed. Formatting FS…");
  if(!LittleFS.format() || !LittleFS.begin()){ WebSerial.send("message","FATAL: FS format failed"); return false; }
  setDefaults();
  if (saveConfigFS()){ WebSerial.send("message","FS formatted and config saved"); return true; }
  WebSerial.send("message","FATAL: save still failing after format"); return false;
}

// ================== SFINAE helper ==================
template <class M>
inline auto setSlaveIdIfAvailable(M& m, uint8_t id)
  -> decltype(std::declval<M&>().setSlaveId(uint8_t{}), void()) { m.setSlaveId(id); }
inline void setSlaveIdIfAvailable(...){}

// ================== Modbus maps ==================
enum : uint16_t { ISTS_DI_BASE=1, ISTS_RLY_BASE=60 };
enum : uint16_t { HREG_DI_COUNT_BASE=1000 };
enum : uint16_t {
  CMD_RLY_ON_BASE=200, CMD_RLY_OFF_BASE=210,
  CMD_DI_EN_BASE=300,  CMD_DI_DIS_BASE=320, CMD_CNT_RST_BASE=340
};

// ================== 1-Wire DB helpers ==================
int owdbIndexOf(uint64_t addr){ for(size_t i=0;i<g_owCount;i++) if(g_owDb[i].addr==addr) return (int)i; return -1; }

// === NEW: 1-based position of an address (0 if not found)
inline uint32_t owdbPosOf(uint64_t addr){
  int idx = owdbIndexOf(addr);
  return (idx >= 0) ? (uint32_t)(idx + 1) : 0;
}

// === UPDATED: include position in each record
JSONVar owdbBuildArray(){
  JSONVar arr;
  for(size_t i=0;i<g_owCount;i++){
    JSONVar o;
    o["pos"]  = (double)(i + 1);
    o["addr"] = hex64(g_owDb[i].addr);
    o["name"] = g_owDb[i].name.c_str();
    arr[i] = o;
  }
  return arr;
}

// === NEW: map of addrHex -> position
JSONVar owdbBuildIndexMap(){
  JSONVar map;
  for(size_t i=0;i<g_owCount;i++){
    map[ hex64(g_owDb[i].addr) ] = (double)(i + 1);
  }
  return map;
}

bool owdbSave(){
  String json=JSON.stringify(owdbBuildArray());
  File f=LittleFS.open(ONEWIRE_DB_PATH,"w");
  if(!f){ WebSerial.send("message","owdb: save open failed"); return false; }
  size_t n=f.print(json); f.flush(); f.close();
  if(n!=json.length()){ WebSerial.send("message","owdb: short write"); return false; }
  return true;
}
bool owdbLoad(){
  g_owCount=0;
  File f=LittleFS.open(ONEWIRE_DB_PATH,"r"); if(!f){ return false; }
  String s; while(f.available()) s+=(char)f.read(); f.close();
  JSONVar arr=JSON.parse(s); if(JSON.typeof(arr)!="array"){ WebSerial.send("message","owdb: invalid JSON, ignoring"); return false; }
  for (unsigned i=0; i<arr.length() && g_owCount<MAX_OW_SENSORS; i++){
    const char* a=(const char*)arr[i]["addr"]; const char* n=(const char*)arr[i]["name"];
    if(!a||!n) continue; uint64_t v; if(!parseHex64(a,v)) continue;
    g_owDb[g_owCount].addr=v; g_owDb[g_owCount].name=String(n); g_owCount++;
  }
  for (size_t i=0;i<MAX_OW_SENSORS;i++){ owLastGoodTemp[i]=NAN; owLastGoodMs[i]=0; owErrCount[i]=0; }
  return true;
}
// Saves to flash and re-sends the DB if successful.
bool owdbAddOrUpdate(uint64_t addr, const char* name){
  if (!name) return false;
  int idx = owdbIndexOf(addr);
  if (idx >= 0) {
    g_owDb[idx].name = String(name);
    bool ok = owdbSave();
    if (ok) owdbSendList();
    return ok;
  }
  if (g_owCount >= MAX_OW_SENSORS) return false;
  g_owDb[g_owCount].addr = addr;
  g_owDb[g_owCount].name = String(name);
  g_owCount++;
  bool ok = owdbSave();
  if (ok) owdbSendList();
  return ok;
}

// Remove a sensor by ROM from the DB.
// Saves to flash and re-sends the DB if successful.
bool owdbRemove(uint64_t addr){
  int idx = owdbIndexOf(addr);
  if (idx < 0) return false;
  for (size_t i = idx + 1; i < g_owCount; i++) g_owDb[i - 1] = g_owDb[i];
  g_owCount--;
  bool ok = owdbSave();
  if (ok) owdbSendList();
  return ok;
}
// === UPDATED: send both the array (with pos) and the index map
void owdbSendList(){
  WebSerial.send("onewireDb",    owdbBuildArray());
  WebSerial.send("onewireIndex", owdbBuildIndexMap());
}

// ================== Decls ==================
void applyModbusSettings(uint8_t addr, uint32_t baud);
void handleValues(JSONVar values);
void handleUnifiedConfig(JSONVar obj);
void handleCommand(JSONVar obj);
void handleOneWire(JSONVar obj);
void performReset();
void sendAllEchoesOnce();
void processModbusCommandPulses();
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now);
void doOneWireScan();
bool applyHeatCfgObjectToIndex(int idx, JSONVar o);

// ---- DS18B20 helpers ----
bool ds18b20StartConvertAll(){
  oneWire.reset();
  oneWire.skip();
  oneWire.write(0x44, 1);
  return true;
}
bool ds18b20ReadTempC(uint64_t rom, double &outC){
  if (!rom) return false;
  uint8_t addr[8]; uint64_t v=rom;
  for (int i=0;i<8;i++){ addr[i]=(uint8_t)(v & 0xFF); v >>= 8; }
  auto try_once = [&](double &tOut)->bool{
    if (!oneWire.reset()) return false;
    oneWire.select(addr);
    oneWire.write(0xBE);
    uint8_t data[9];
    for (int i=0;i<9;i++) data[i]=oneWire.read();
    if (OneWire::crc8(data,8) != data[8]) return false;
    int16_t raw = ((int16_t)data[1] << 8) | data[0];
    if (addr[0] == 0x10) {
      raw <<= 3;
      if (data[7] == 0x10) raw = (raw & 0xFFF0) + 12 - data[6];
    } else {
      uint8_t cfg = (data[4] & 0x60);
      if (cfg == 0x00)      raw &= ~0x7;
      else if (cfg == 0x20) raw &= ~0x3;
      else if (cfg == 0x40) raw &= ~0x1;
    }
    double t = (double)raw / 16.0;
    if (t == 85.0 || t < -55.0 || t > 125.0) return false;
    tOut = t;
    return true;
  };
  double t;
  if (try_once(t)) { outC=t; return true; }
  delayMicroseconds(200);
  if (try_once(t)) { outC=t; return true; }
  return false;
}
void publishOneWireTemps(){
  JSONVar owTemps, owErrs, owTempsList; // include ordered list with pos
  uint32_t now = millis();
  for (size_t i=0; i<g_owCount; i++){
    bool fresh = (owLastGoodMs[i] != 0) && (now - owLastGoodMs[i] <= OW_FAIL_HIDE_MS);
    double t = fresh ? owLastGoodTemp[i] : NAN;
    String addrHex = hex64(g_owDb[i].addr);

    owTemps[addrHex] = isfinite(t) ? t : NAN;
    owErrs[addrHex]  = (double)owErrCount[i];

    JSONVar row;
    row["pos"]  = (double)(i+1);
    row["addr"] = addrHex;
    row["name"] = g_owDb[i].name.c_str();
    row["temp"] = isfinite(t) ? t : NAN;
    row["errs"] = (double)owErrCount[i];
    owTempsList[i] = row;
  }
  WebSerial.send("onewireTemps", owTemps);
  WebSerial.send("onewireErrs",  owErrs);
  WebSerial.send("onewireTempsList", owTempsList); // NEW: ordered table
}
void buildCachedOwTemps(JSONVar &owTemps, JSONVar &owErrs){
  uint32_t now = millis();
  for (size_t i=0; i<g_owCount; i++){
    bool fresh = (owLastGoodMs[i] != 0) && (now - owLastGoodMs[i] <= OW_FAIL_HIDE_MS);
    double t = fresh ? owLastGoodTemp[i] : NAN;
    owTemps[hex64(g_owDb[i].addr)] = isfinite(t) ? t : NAN;
    owErrs[hex64(g_owDb[i].addr)]  = (double)owErrCount[i];
  }
}

// ================== Setup ==================
void setup(){
  Serial.begin(57600);

  for(uint8_t i=0;i<NUM_DI;i++)   pinMode(DI_PINS[i], INPUT);
  for(uint8_t i=0;i<NUM_RLY;i++){ pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], LOW); }
  pinMode(ONEWIRE_PIN, INPUT_PULLUP);

  setDefaults();

  if(!initFilesystemAndConfig()){ WebSerial.send("message","FATAL: Filesystem/config init failed"); }
  if(owdbLoad()) WebSerial.send("message","1-Wire DB loaded from flash");
  else           WebSerial.send("message","1-Wire DB missing/invalid (will create on first save)");
  publishOneWireTemps();

  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud); mb.config(g_mb_baud); setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("WLD-521-R1");

  for(uint16_t i=0;i<NUM_DI;i++)  mb.addIsts(ISTS_DI_BASE + i);
  for(uint16_t i=0;i<NUM_RLY;i++) mb.addIsts(ISTS_RLY_BASE + i);
  for(uint16_t i=0;i<NUM_DI;i++){ mb.addHreg(HREG_DI_COUNT_BASE+(i*2)+0,0); mb.addHreg(HREG_DI_COUNT_BASE+(i*2)+1,0); }
  for(uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_ON_BASE+i);  mb.setCoil(CMD_RLY_ON_BASE+i,false); }
  for (uint16_t i=0; i<NUM_RLY; i++) { mb.addCoil(CMD_RLY_OFF_BASE+i); mb.setCoil(CMD_RLY_OFF_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_DI_EN_BASE+i);   mb.setCoil(CMD_DI_EN_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_DI_DIS_BASE+i);  mb.setCoil(CMD_DI_DIS_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_CNT_RST_BASE+i); mb.setCoil(CMD_CNT_RST_BASE+i,false); }

  modbusStatus["address"]=g_mb_address; modbusStatus["baud"]=g_mb_baud; modbusStatus["state"]=0;

  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);
  WebSerial.on("onewire", handleOneWire);

  WebSerial.send("message","Boot OK (Flow + Heat). DS18B20 hardened (retry, cache).");
  sendAllEchoesOnce();
}

// ================== Command handlers ==================
void performReset(){ if(Serial) Serial.flush(); delay(50); watchdog_reboot(0,0,0); while(true){ __asm__("wfi"); } }
void applyModbusSettings(uint8_t addr, uint32_t baud){
  if ((uint32_t)modbusStatus["baud"]!=baud){ Serial2.end(); Serial2.begin(baud); mb.config(baud); }
  setSlaveIdIfAvailable(mb, addr); g_mb_address=addr; g_mb_baud=baud;
  modbusStatus["address"]=g_mb_address; modbusStatus["baud"]=g_mb_baud;
}
void handleValues(JSONVar values){
  int addr=(int)values["mb_address"]; int baud=(int)values["mb_baud"];
  addr=constrain(addr,1,255); baud=constrain(baud,9600,115200);
  applyModbusSettings((uint8_t)addr,(uint32_t)baud);
  WebSerial.send("message","Modbus configuration updated");
  cfgDirty=true; lastCfgTouchMs=millis();
}

// ===== NEW: apply heat config that may contain posA/posB or full addresses =====
bool applyHeatCfgObjectToIndex(int idx, JSONVar o){
  if (idx < 0 || idx >= (int)NUM_DI) return false;

  if (o.hasOwnProperty("enabled"))
    heatEnabled[idx] = (bool)o["enabled"];

  // Prefer positions if provided
  bool setA = false, setB = false;
  uint32_t posA = 0, posB = 0;

  // Accept keys: posA / posB (primary), and addrA_pos / addrB_pos (compat)
  if (jsonGetPos(o, "posA", posA) || jsonGetPos(o, "addrA_pos", posA)) {
    uint64_t a = owdbAddrAtPos(posA);
    heatAddrA[idx] = a;
    setA = true;
    WebSerial.send("message", String("Heat: DI")+String(idx+1)+
                               " A set by posA="+String(posA)+" -> "+(a?hex64(a):String("''")));
  }
  if (jsonGetPos(o, "posB", posB) || jsonGetPos(o, "addrB_pos", posB)) {
    uint64_t b = owdbAddrAtPos(posB);
    heatAddrB[idx] = b;
    setB = true;
    WebSerial.send("message", String("Heat: DI")+String(idx+1)+
                               " B set by posB="+String(posB)+" -> "+(b?hex64(b):String("''")));
  }

  // Fallback: accept explicit addresses (old UI)
  if (!setA) {
    uint64_t a=0;
    if (jsonGetAddr64(o,"addrA_u64_str", a)) heatAddrA[idx]=a;
    else if (jsonGetAddr64(o,"addrA", a))    heatAddrA[idx]=a;
  }
  if (!setB) {
    uint64_t b=0;
    if (jsonGetAddr64(o,"addrB_u64_str", b)) heatAddrB[idx]=b;
    else if (jsonGetAddr64(o,"addrB", b))    heatAddrB[idx]=b;
  }

  // Scalars
  double dv;
  if (jsonGetDouble(o,"cp",dv)  && dv>0) heatCp[idx]=(float)dv;
  if (jsonGetDouble(o,"rho",dv) && dv>0) heatRho[idx]=(float)dv;
  if (jsonGetDouble(o,"calib",dv) && dv>0) heatCalib[idx]=(float)dv;

  return true;
}

void handleUnifiedConfig(JSONVar obj){
  const char* t = (const char*)obj["t"];
  JSONVar list = obj["list"];
  if (!t) return;

  String type = String(t);
  bool changed = false;

  if (type=="inputEnable"){
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].enabled = (bool)list[i];
    WebSerial.send("message","Input Enabled list updated");
    changed = true;
  }
  else if (type=="inputInvert"){
    for (int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].inverted = (bool)list[i];
    WebSerial.send("message","Input Invert list updated");
    changed = true;
  }
  else if (type=="inputAction"){
    for (int i=0;i<NUM_DI && i<list.length();i++){
      int a=(int)list[i];
      diCfg[i].action = (uint8_t)constrain(a,0,2);
    }
    WebSerial.send("message","Input Action list updated");
    changed = true;
  }
  else if (type=="inputTarget"){
    for (int i=0;i<NUM_DI && i<list.length();i++){
      int tgt=(int)list[i];
      diCfg[i].target = (uint8_t)((tgt==4||tgt==0||(tgt>=1&&tgt<=2))?tgt:0);
    }
    WebSerial.send("message","Input Control Target list updated");
    changed = true;
  }
  else if (type=="inputType"){
    for (int i=0;i<NUM_DI && i<list.length();i++){
      int tp=(int)list[i];
      diCfg[i].type = (uint8_t)constrain(tp,IT_WATER,IT_WCOUNTER);
    }
    WebSerial.send("message","Input Type list updated");
    changed = true;
  }
  else if (type=="counterResetList"){
    uint32_t now = millis();
    for (int i=0;i<NUM_DI && i<list.length();i++){
      if ((bool)list[i]){
        diCounter[i]=0;
        diLastEdgeMs[i]=now;
        lastPulseSnapshot[i]=0;
        flowCounterBase[i]=0;
      }
    }
    WebSerial.send("message","Counters reset");
  }
  else if (type=="relays"){
    for (int i=0;i<NUM_RLY && i<list.length();i++){
      rlyCfg[i].enabled = (bool)list[i]["enabled"];
      rlyCfg[i].inverted= (bool)list[i]["inverted"];
    }
    WebSerial.send("message","Relay Configuration updated");
    changed = true;
  }
  else if (type=="flowPPL"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      long v = (long)(double)list[i];
      if (v <= 0) v = 1;
      flowPulsesPerL[i] = (uint32_t)v;
    }
    WebSerial.send("message","Flow: pulsesPerLiter updated");
    changed = true;
  }
  else if (type=="flowCalib"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      double v = (double)list[i];
      if (v <= 0.0) v = 1.0;
      flowCalibRate[i]  = (float)v;
      flowCalibAccum[i] = (float)v;
    }
    WebSerial.send("message","Flow: calibration updated (applied to rate & accum)");
    changed = true;
  }
  else if (type=="flowCalibRate"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      double v = (double)list[i];
      if (v <= 0.0) v = 1.0;
      flowCalibRate[i] = (float)v;
    }
    WebSerial.send("message","Flow: rate calibration updated");
    changed = true;
  }
  else if (type=="flowCalibAccum"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      double v = (double)list[i];
      if (v <= 0.0) v = 1.0;
      flowCalibAccum[i] = (float)v;
    }
    WebSerial.send("message","Flow: accumulation calibration updated");
    changed = true;
  }
  else if (type=="flowResetAccumList"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      if ((bool)list[i]) flowCounterBase[i] = diCounter[i];
    }
    WebSerial.send("message","Flow: accumulation windows reset (baseline moved)");
    changed = true;
  }
  else if (type=="heatConfig"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      JSONVar o = list[i];
      if (JSON.typeof(o)!="object") continue;
      bool ok = applyHeatCfgObjectToIndex(i, o);
      if (ok){
        String line; line.reserve(200);
        line += "Heat: DI"; line += String(i+1); line += " updated — ";
        line += "enabled="; line += (heatEnabled[i] ? "true" : "false");
        line += " A=";      line += (heatAddrA[i] ? hex64(heatAddrA[i]) : "''");
        line += " B=";      line += (heatAddrB[i] ? hex64(heatAddrB[i]) : "''");
        line += " posA=";   line += String(owdbPosOf(heatAddrA[i]));
        line += " posB=";   line += String(owdbPosOf(heatAddrB[i]));
        line += " cp=";     line += String(heatCp[i], 0);
        line += " rho=";    line += String(heatRho[i], 3);
        line += " calib=";  line += String(heatCalib[i], 4);
        WebSerial.send("message", line);
        changed = true;
      }
    }
  }
  else if (type=="heatResetEnergyList"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      if ((bool)list[i]) heatEnergyJ[i]=0.0;
    }
    WebSerial.send("message","Heat: energy counters reset");
    changed = true;
  }
  else if (type=="heatConfigSingle"){
    int di = -1;
    if (obj.hasOwnProperty("di")) di = (int)obj["di"];
    if (di >= 1 && di <= (int)NUM_DI) di -= 1;
    if (di < 0 || di >= (int)NUM_DI) { WebSerial.send("message","heatConfigSingle: invalid 'di'"); return; }
    if (!obj.hasOwnProperty("cfg")) { WebSerial.send("message","heatConfigSingle: missing 'cfg'");  return; }
    JSONVar cfg = obj["cfg"];
    if (JSON.typeof(cfg)!="object"){ WebSerial.send("message","heatConfigSingle: 'cfg' must be an object"); return; }
    if (!applyHeatCfgObjectToIndex(di, cfg)){
      WebSerial.send("message","heatConfigSingle: failed to apply");
      return;
    }
    String line; line.reserve(200);
    line += "Heat: DI"; line += String(di+1); line += " updated — ";
    line += "enabled="; line += (heatEnabled[di] ? "true" : "false");
    line += " A=";      line += (heatAddrA[di] ? hex64(heatAddrA[di]) : "''");
    line += " B=";      line += (heatAddrB[di] ? hex64(heatAddrB[di]) : "''");
    line += " posA=";   line += String(owdbPosOf(heatAddrA[di]));
    line += " posB=";   line += String(owdbPosOf(heatAddrB[di]));
    line += " cp=";     line += String(heatCp[di], 0);
    line += " rho=";    line += String(heatRho[di], 3);
    line += " calib=";  line += String(heatCalib[di], 4);
    WebSerial.send("message", line);
    changed = true;
  }
  else {
    WebSerial.send("message","Unknown Config type");
  }

  if (changed){
    cfgDirty = true;
    lastCfgTouchMs = millis();
  }
}

void handleCommand(JSONVar obj){
  const char* actC=(const char*)obj["action"]; if(!actC){ WebSerial.send("message","command: missing 'action'"); return; }
  String act=String(actC); act.toLowerCase();

  if (act=="reset"||act=="reboot"){
    bool ok=saveConfigFS();
    WebSerial.send("message", ok ? "Saved. Rebooting…" : "WARNING: Save verify FAILED. Rebooting anyway…");
    delay(400); performReset(); return;
  }
  if (act=="save"){ if (saveConfigFS()) WebSerial.send("message","Configuration saved"); else WebSerial.send("message","ERROR: Save failed"); return; }
  if (act=="load"){ if (loadConfigFS()){ WebSerial.send("message","Configuration loaded"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address,g_mb_baud); }
                    else WebSerial.send("message","ERROR: Load failed/invalid"); return; }
  if (act=="factory"){
    setDefaults(); if (saveConfigFS()){ WebSerial.send("message","Factory defaults restored & saved"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address,g_mb_baud); }
    else WebSerial.send("message","ERROR: Save after factory reset failed"); return;
  }
  if (act=="scan"||act=="scan1wire"||act=="scan_1wire"||act=="scan1w"){ doOneWireScan(); return; }

  if (act=="flow_calculate"){
    int di = -1;
    if (obj.hasOwnProperty("di")) di = (int)obj["di"];
    else if (obj.hasOwnProperty("input")) di = (int)obj["input"];
    else if (obj.hasOwnProperty("index")) di = (int)obj["index"];
    if (di >= 1 && di <= (int)NUM_DI) di -= 1;
    if (di < 0 || di >= (int)NUM_DI) { WebSerial.send("message","flow_calculate: invalid 'di'"); return; }

    double extLit = 0.0;
    if (!jsonGetDouble(obj,"external", extLit) &&
        !jsonGetDouble(obj,"external_liters", extLit) &&
        !jsonGetDouble(obj,"externalAccum", extLit)) {
      WebSerial.send("message","flow_calculate: missing 'external' liters"); return;
    }
    if (extLit <= 0.0) { WebSerial.send("message","flow_calculate: external must be > 0"); return; }

    uint32_t ppl = flowPulsesPerL[di] ? flowPulsesPerL[di] : 1;
    uint32_t pulses_since = (diCounter[di] >= flowCounterBase[di]) ? (diCounter[di] - flowCounterBase[di]) : 0;
    if (pulses_since == 0) { WebSerial.send("message","flow_calculate: no pulses in current window (reset total, run flow)"); return; }

    double liters_no_cal = (double)pulses_since / (double)ppl;
    double newCal = extLit / liters_no_cal;

    flowCalibAccum[di] = (float)((newCal>0.0)?newCal:1.0);
    cfgDirty = true; lastCfgTouchMs = millis();

    String msg; msg.reserve(128);
    msg += "flow_calculate: DI"; msg += String(di+1);
    msg += " pulses_since="; msg += String(pulses_since);
    msg += " ppl=";          msg += String((uint32_t)ppl);
    msg += " external=";     msg += String(extLit, 6);
    msg += " => calibAccum=";msg += String(newCal, 6);
    WebSerial.send("message", msg);
    return;
  }

  if (act=="flow_reset"){
    int di = -1;
    if (obj.hasOwnProperty("di")) di = (int)obj["di"];
    if (di >= 1 && di <= (int)NUM_DI) di -= 1;
    if (di >= 0 && di < (int)NUM_DI) {
      flowCounterBase[di] = diCounter[di];
      cfgDirty = true; lastCfgTouchMs = millis();
      String msg; msg.reserve(32);
      msg += "flow_reset: DI"; msg += String(di+1);
      WebSerial.send("message", msg);
    } else {
      WebSerial.send("message","flow_reset: invalid 'di'");
    }
    return;
  }

  if (act=="heat_reset"){
    int di=-1; if (obj.hasOwnProperty("di")) di=(int)obj["di"];
    if (di>=1 && di<=NUM_DI) di-=1;
    if (di>=0 && di<NUM_DI){ heatEnergyJ[di]=0.0; WebSerial.send("message", String("heat_reset: DI")+String(di+1)); }
    else WebSerial.send("message","heat_reset: invalid 'di'");
    return;
  }

  WebSerial.send("message", String("Unknown command: ")+actC);
}

void handleOneWire(JSONVar obj){
  const char* actC=(const char*)obj["action"]; if(!actC){ WebSerial.send("message","onewire: missing action"); return; }
  String act=String(actC); act.toLowerCase();

  if (act=="list"){ owdbSendList(); return; }
  if (act=="clear"){ g_owCount=0; if(owdbSave()) WebSerial.send("message","onewire: cleared"); else WebSerial.send("message","onewire: clear save failed"); owdbSendList(); return; }

  uint64_t addr=0;
  if (!jsonGetAddr64(obj, "addr_u64_str", addr)){ WebSerial.send("message","onewire: invalid or missing address (expect rom_hex or addr_hi/addr_lo)"); return; }

  if (act=="add"||act=="save"||act=="rename"){
    String nm; if (!jsonGetStr(obj,"name",nm) || nm.length()==0){ WebSerial.send("message","onewire: missing 'name'"); return; }
    if (owdbAddOrUpdate(addr, nm.c_str())) {
      String msg; msg.reserve(64); msg += "onewire: saved "; msg += hex64(addr);
      WebSerial.send("message", msg);
    } else {
      WebSerial.send("message","onewire: save failed (maybe full?)"); owdbSendList();
    }
  } else if (act=="remove"||act=="delete"){
    if (owdbRemove(addr)) {
      String msg; msg.reserve(64); msg += "onewire: removed "; msg += hex64(addr);
      WebSerial.send("message", msg);
    } else {
      WebSerial.send("message","onewire: address not found"); owdbSendList();
    }
  } else {
    WebSerial.send("message", String("onewire: unknown action '")+act+"'");
  }
}

// ================== Modbus pulses ==================
void processModbusCommandPulses(){
  for(int r=0;r<NUM_RLY;r++){
    if (mb.Coil(CMD_RLY_ON_BASE+r))  { mb.setCoil(CMD_RLY_ON_BASE+r,false); desiredRelay[r]=true;  rlyPulseUntil[r]=0; cfgDirty=true; lastCfgTouchMs=millis(); }
    if (mb.Coil(CMD_RLY_OFF_BASE+r)) { mb.setCoil(CMD_RLY_OFF_BASE+r,false); desiredRelay[r]=false; rlyPulseUntil[r]=0; cfgDirty=true; lastCfgTouchMs=millis(); }
  }
  for(int i=0;i<NUM_DI;i++){
    if (mb.Coil(CMD_DI_EN_BASE+i))  { mb.setCoil(CMD_DI_EN_BASE+i,false); if(!diCfg[i].enabled){ diCfg[i].enabled=true; cfgDirty=true; lastCfgTouchMs=millis(); } }
    if (mb.Coil(CMD_DI_DIS_BASE+i)) { mb.setCoil(CMD_DI_DIS_BASE+i,false); if( diCfg[i].enabled){ diCfg[i].enabled=false; cfgDirty=true; lastCfgTouchMs=millis(); } }
  }
  for(int i=0;i<NUM_DI;i++){
    if (mb.Coil(CMD_CNT_RST_BASE+i)) { mb.setCoil(CMD_CNT_RST_BASE+i,false);
      diCounter[i]=0; diLastEdgeMs[i]=millis(); lastPulseSnapshot[i]=0; flowCounterBase[i]=0;
    }
  }
}
void applyActionToTarget(uint8_t tgt, uint8_t action, uint32_t now){
  auto doRelay = [&](int rIdx){ if(rIdx<0||rIdx>=NUM_RLY) return; if(action==1){ desiredRelay[rIdx]=!desiredRelay[rIdx]; rlyPulseUntil[rIdx]=0; } else if(action==2){ desiredRelay[rIdx]=true; rlyPulseUntil[rIdx]=now+PULSE_MS; } };
  if(action==0 || tgt==4) return;
  if(tgt==0){ for(int r=0;r<NUM_RLY;r++) doRelay(r); }
  else if(tgt>=1 && tgt<=2) doRelay(tgt-1);
  cfgDirty=true; lastCfgTouchMs=now;
}
inline void setHreg32(uint16_t base, uint32_t v){ mb.setHreg(base+0,(uint16_t)(v & 0xFFFF)); mb.setHreg(base+1,(uint16_t)((v>>16)&0xFFFF)); }

// ================== Loop ==================
void loop(){
  unsigned long now=millis();
  mb.task(); processModbusCommandPulses();

  if (cfgDirty && (now-lastCfgTouchMs>=CFG_AUTOSAVE_MS)){
    if (saveConfigFS()) WebSerial.send("message","Configuration saved");
    else WebSerial.send("message","ERROR: Save failed");
    cfgDirty=false;
  }

  JSONVar inputs; JSONVar counters;
  for (int i=0;i<NUM_DI;i++){
    bool raw=(digitalRead(DI_PINS[i])==HIGH); if (diCfg[i].inverted) raw=!raw;
    bool val=false;

    if(!diCfg[i].enabled){ val=false; }
    else {
      switch(diCfg[i].type){
        case IT_WATER: case IT_HUMIDITY: val=raw; break;
        case IT_WCOUNTER: val=raw; break;
        default: val=raw; break;
      }
    }

    bool prev=diState[i]; diPrev[i]=prev; diState[i]=val;
    inputs[i]=val; mb.setIsts(ISTS_DI_BASE+i, val);

    bool rising=(!prev && val);

    if (diCfg[i].type==IT_WCOUNTER){
      if (rising && (now - diLastEdgeMs[i] >= CNT_DEBOUNCE_MS)){
        diCounter[i]++; diLastEdgeMs[i]=now; setHreg32(HREG_DI_COUNT_BASE + (i*2), diCounter[i]);
      }
    } else {
      uint8_t act=diCfg[i].action;
      if (act==1){ if (rising || (prev && !val)) applyActionToTarget(diCfg[i].target,1,now); }
      else if (act==2){ if (rising) applyActionToTarget(diCfg[i].target,2,now); }
    }

    counters[i]=(double)diCounter[i];
  }

  JSONVar relayStateList;
  for (int i=0;i<NUM_RLY;i++){
    bool outVal=desiredRelay[i]; if(!rlyCfg[i].enabled) outVal=false; if(rlyCfg[i].inverted) outVal=!outVal;
    digitalWrite(RELAY_PINS[i], outVal?HIGH:LOW);
    relayStateList[i]=outVal; mb.setIsts(ISTS_RLY_BASE+i,outVal);
    if (rlyPulseUntil[i] && timeAfter32(now, rlyPulseUntil[i])){ desiredRelay[i]=false; rlyPulseUntil[i]=0; cfgDirty=true; lastCfgTouchMs=now; }
  }

  // ===== 1-Wire schedule: Convert then Read+Cache =====
  if (timeAfter32(now, nextRateTickMs)) {
    uint32_t dt_ms = 1000;
    nextRateTickMs = now + 1000;

    if (!oneWireBusy) {
      ds18b20StartConvertAll();
      oneWireBusy = true;
      nextOneWireConvertMs = now + 1500;
    } else if (timeAfter32(now, nextOneWireConvertMs)) {
      for (size_t i=0; i<g_owCount; i++){
        double t;
        if (ds18b20ReadTempC(g_owDb[i].addr, t)) {
          owLastGoodTemp[i] = t;
          owLastGoodMs[i]   = now;
          owErrCount[i]     = 0;
        } else {
          if (owErrCount[i] < 0xFFFFFFFF) owErrCount[i]++;
        }
      }
      for (int i=0;i<NUM_DI;i++){
        double ta=NAN, tb=NAN;
        int ia = owdbIndexOf(heatAddrA[i]);
        int ib = owdbIndexOf(heatAddrB[i]);
        if (ia>=0 && ia<(int)g_owCount && owLastGoodMs[ia] && (now-owLastGoodMs[ia] <= OW_FAIL_HIDE_MS)) ta = owLastGoodTemp[ia];
        if (ib>=0 && ib<(int)g_owCount && owLastGoodMs[ib] && (now-owLastGoodMs[ib] <= OW_FAIL_HIDE_MS)) tb = owLastGoodTemp[ib];
        heatTA[i]=ta; heatTB[i]=tb;
      }
      publishOneWireTemps();
      oneWireBusy=false;
    }

    // Flow: update rate from counters
    for (int i=0;i<NUM_DI;i++){
      uint32_t ppl   = flowPulsesPerL[i] ? flowPulsesPerL[i] : 1;
      uint32_t delta = diCounter[i] - lastPulseSnapshot[i];
      lastPulseSnapshot[i] = diCounter[i];
      float liters_last_sec = ((float)delta / (float)ppl) * flowCalibRate[i];
      flowRateLmin[i] = liters_last_sec * 60.0f;
    }

    // Heat power & energy integration
    for (int i=0;i<NUM_DI;i++){
      double P=0.0; double dT=0.0;
      if (diCfg[i].type==IT_WCOUNTER && heatEnabled[i] &&
          isfinite(heatTA[i]) && isfinite(heatTB[i])) {
        dT = (heatTA[i] - heatTB[i]);
        double m_dot = ((double)flowRateLmin[i] / 60.0) * (double)heatRho[i]; // kg/s
        P = m_dot * (double)heatCp[i] * dT;     // W
        P *= (double)heatCalib[i];
        heatEnergyJ[i] += P * (dt_ms/1000.0);   // J
      }
      heatDT[i]=dT; heatPowerW[i]=P;
    }
  }

  if (millis()-lastSend>=sendInterval){
    lastSend=millis();
    WebSerial.check();
    WebSerial.send("status", modbusStatus);

    JSONVar invertList, enableList, actionList, targetList, typeList;
    for (int i=0;i<NUM_DI;i++){
      invertList[i]=diCfg[i].inverted; enableList[i]=diCfg[i].enabled;
      actionList[i]=diCfg[i].action;   targetList[i]=diCfg[i].target; typeList[i]=diCfg[i].type;
    }
    JSONVar relayEnableList, relayInvertList;
    for (int i=0;i<NUM_RLY;i++){ relayEnableList[i]=rlyCfg[i].enabled; relayInvertList[i]=rlyCfg[i].inverted; }

    JSONVar flowPPLList, flowCalibList, flowAccumList, flowRateList, flowCalibRateList, flowCalibAccumList;
    for (int i=0;i<NUM_DI;i++){
      uint32_t ppl = flowPulsesPerL[i] ? flowPulsesPerL[i] : 1;
      uint32_t pulses_since = (diCounter[i] >= flowCounterBase[i]) ? (diCounter[i] - flowCounterBase[i]) : 0;
      double accumL = ((double)pulses_since / (double)ppl) * (double)flowCalibAccum[i];

      flowPPLList[i]         = (double)flowPulsesPerL[i];
      flowCalibList[i]       = (double)flowCalibAccum[i];
      flowCalibRateList[i]   = (double)flowCalibRate[i];
      flowCalibAccumList[i]  = (double)flowCalibAccum[i];
      flowAccumList[i]       = accumL;
      flowRateList[i]        = (double)flowRateLmin[i];
    }

    JSONVar heatEnabledList, heatAddrAList, heatAddrBList, heatAddrAPosList, heatAddrBPosList;
    JSONVar heatCpList, heatRhoList, heatCalibList;
    JSONVar heatTAList, heatTBList, heatDTList, heatPowerList, heatEnergyJList, heatEnergyKWhList;

    for (int i=0;i<NUM_DI;i++){
      heatEnabledList[i]=heatEnabled[i];
      heatAddrAList[i]= (heatAddrA[i] ? hex64(heatAddrA[i]) : "");
      heatAddrBList[i]= (heatAddrB[i] ? hex64(heatAddrB[i]) : "");

      // Positions (1-based; 0 if not found)
      heatAddrAPosList[i] = (double)owdbPosOf(heatAddrA[i]);
      heatAddrBPosList[i] = (double)owdbPosOf(heatAddrB[i]);

      heatCpList[i]=(double)heatCp[i];
      heatRhoList[i]=(double)heatRho[i];
      heatCalibList[i]=(double)heatCalib[i];

      heatTAList[i]=isfinite(heatTA[i])?heatTA[i]:NAN;
      heatTBList[i]=isfinite(heatTB[i])?heatTB[i]:NAN;
      heatDTList[i]=heatDT[i];
      heatPowerList[i]=heatPowerW[i];
      heatEnergyJList[i]=heatEnergyJ[i];
      heatEnergyKWhList[i]=heatEnergyJ[i]/3.6e6;
    }

    // cached temps/errors
    {
      JSONVar owTemps, owErrs;
      buildCachedOwTemps(owTemps, owErrs);
      WebSerial.send("onewireTemps", owTemps);
      WebSerial.send("onewireErrs",  owErrs);
    }

    WebSerial.send("inputs", inputs);
    WebSerial.send("invertList", invertList);
    WebSerial.send("enableList", enableList);
    WebSerial.send("inputActionList", actionList);
    WebSerial.send("inputTargetList", targetList);
    WebSerial.send("inputTypeList", typeList);
    WebSerial.send("counterList", counters);

    WebSerial.send("flowPPLList",   flowPPLList);
    WebSerial.send("flowCalibList", flowCalibList);
    WebSerial.send("flowCalibRateList",  flowCalibRateList);
    WebSerial.send("flowCalibAccumList", flowCalibAccumList);
    WebSerial.send("flowAccumList", flowAccumList);
    WebSerial.send("flowRateList",  flowRateList);

    WebSerial.send("heatEnabledList",   heatEnabledList);
    WebSerial.send("heatAddrAList",     heatAddrAList);
    WebSerial.send("heatAddrBList",     heatAddrBList);
    WebSerial.send("heatAddrAPosList",  heatAddrAPosList);  // NEW
    WebSerial.send("heatAddrBPosList",  heatAddrBPosList);  // NEW
    WebSerial.send("heatCpList",        heatCpList);
    WebSerial.send("heatRhoList",       heatRhoList);
    WebSerial.send("heatCalibList",     heatCalibList);
    WebSerial.send("heatTAList",        heatTAList);
    WebSerial.send("heatTBList",        heatTBList);
    WebSerial.send("heatDTList",        heatDTList);
    WebSerial.send("heatPowerList",     heatPowerList);
    WebSerial.send("heatEnergyJList",   heatEnergyJList);
    WebSerial.send("heatEnergyKWhList", heatEnergyKWhList);

    WebSerial.send("relayStateList", relayStateList);
    WebSerial.send("relayEnableList", relayEnableList);
    WebSerial.send("relayInvertList", relayInvertList);

    owdbSendList();
  }
}

void sendAllEchoesOnce(){
  JSONVar enableList, invertList, actionList, targetList, typeList, counters;
  for (int i=0;i<NUM_DI;i++){
    enableList[i]=diCfg[i].enabled; invertList[i]=diCfg[i].inverted;
    actionList[i]=diCfg[i].action;  targetList[i]=diCfg[i].target;
    typeList[i]=diCfg[i].type;      counters[i]=(double)diCounter[i];
  }
  WebSerial.send("enableList", enableList);
  WebSerial.send("invertList", invertList);
  WebSerial.send("inputActionList", actionList);
  WebSerial.send("inputTargetList", targetList);
  WebSerial.send("inputTypeList", typeList);
  WebSerial.send("counterList", counters);

  JSONVar relayEnableList, relayInvertList;
  for (int i=0;i<NUM_RLY;i++){ relayEnableList[i]=rlyCfg[i].enabled; relayInvertList[i]=rlyCfg[i].inverted; }
  WebSerial.send("relayEnableList", relayEnableList);
  WebSerial.send("relayInvertList", relayInvertList);

  JSONVar flowPPLList, flowCalibList, flowAccumList, flowRateList, flowCalibRateList, flowCalibAccumList;
  for (int i=0;i<NUM_DI;i++){
    uint32_t ppl = flowPulsesPerL[i] ? flowPulsesPerL[i] : 1;
    uint32_t pulses_since = (diCounter[i] >= flowCounterBase[i]) ? (diCounter[i] - flowCounterBase[i]) : 0;
    double accumL = ((double)pulses_since / (double)ppl) * (double)flowCalibAccum[i];

    flowPPLList[i]         = (double)flowPulsesPerL[i];
    flowCalibList[i]       = (double)flowCalibAccum[i];
    flowCalibRateList[i]   = (double)flowCalibRate[i];
    flowCalibAccumList[i]  = (double)flowCalibAccum[i];
    flowAccumList[i]       = accumL;
    flowRateList[i]        = (double)flowRateLmin[i];
  }
  WebSerial.send("flowPPLList",   flowPPLList);
  WebSerial.send("flowCalibList", flowCalibList);
  WebSerial.send("flowCalibRateList",  flowCalibRateList);
  WebSerial.send("flowCalibAccumList", flowCalibAccumList);
  WebSerial.send("flowAccumList", flowAccumList);
  WebSerial.send("flowRateList",  flowRateList);

  JSONVar heatEnabledList, heatAddrAList, heatAddrBList, heatAddrAPosList, heatAddrBPosList;
  JSONVar heatCpList, heatRhoList, heatCalibList;
  JSONVar heatTAList, heatTBList, heatDTList, heatPowerList, heatEnergyJList, heatEnergyKWhList;
  for (int i=0;i<NUM_DI;i++){
    heatEnabledList[i]=heatEnabled[i];
    heatAddrAList[i]= (heatAddrA[i] ? hex64(heatAddrA[i]) : "");
    heatAddrBList[i]= (heatAddrB[i] ? hex64(heatAddrB[i]) : "");

    // Positions (1-based; 0 if not found)
    heatAddrAPosList[i] = (double)owdbPosOf(heatAddrA[i]);
    heatAddrBPosList[i] = (double)owdbPosOf(heatAddrB[i]);

    heatCpList[i]=(double)heatCp[i];
    heatRhoList[i]=(double)heatRho[i];
    heatCalibList[i]=(double)heatCalib[i];

    heatTAList[i]=isfinite(heatTA[i])?heatTA[i]:NAN;
    heatTBList[i]=isfinite(heatTB[i])?heatTB[i]:NAN;
    heatDTList[i]=heatDT[i];
    heatPowerList[i]=heatPowerW[i];
    heatEnergyJList[i]=heatEnergyJ[i];
    heatEnergyKWhList[i]=heatEnergyJ[i]/3.6e6;
  }
  WebSerial.send("heatEnabledList",   heatEnabledList);
  WebSerial.send("heatAddrAList",     heatAddrAList);
  WebSerial.send("heatAddrBList",     heatAddrBList);
  WebSerial.send("heatAddrAPosList",  heatAddrAPosList); // NEW
  WebSerial.send("heatAddrBPosList",  heatAddrBPosList); // NEW
  WebSerial.send("heatCpList", heatCpList);
  WebSerial.send("heatRhoList", heatRhoList);
  WebSerial.send("heatCalibList", heatCalibList);
  WebSerial.send("heatTAList", heatTAList);
  WebSerial.send("heatTBList", heatTBList);
  WebSerial.send("heatDTList", heatDTList);
  WebSerial.send("heatPowerList", heatPowerList);
  WebSerial.send("heatEnergyJList", heatEnergyJList);
  WebSerial.send("heatEnergyKWhList", heatEnergyKWhList);

  {
    JSONVar owTemps, owErrs;
    buildCachedOwTemps(owTemps, owErrs);
    WebSerial.send("onewireTemps", owTemps);
    WebSerial.send("onewireErrs",  owErrs);
  }

  owdbSendList();
  modbusStatus["address"]=g_mb_address; modbusStatus["baud"]=g_mb_baud;
  WebSerial.send("status", modbusStatus);
}

void doOneWireScan(){
  JSONVar romList; uint8_t addr[8]; int idx=0; int found=0;
  oneWire.reset_search();
  while(oneWire.search(addr)){
    if (OneWire::crc8(addr,7)!=addr[7]) continue;
    uint64_t v=romBytesToU64(addr);
    romList[idx++]=hex64(v); found++;
  }
  WebSerial.send("onewireScan", romList);
  WebSerial.send("message", String("1-Wire scan: ")+found+ " device(s)");
}
