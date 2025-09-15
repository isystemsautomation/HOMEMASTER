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
enum : uint8_t {
  IT_WATER    = 0,
  IT_HUMIDITY = 1,
  IT_WCOUNTER = 2            // Use this type for flow meters (pulse)
};

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
// Persisted parameters:
uint32_t flowPulsesPerL[NUM_DI];     // default 450
float    flowCalibRate[NUM_DI];      // correction for instantaneous rate
float    flowCalibAccum[NUM_DI];     // correction for accumulated total
uint32_t flowCounterBase[NUM_DI];    // pulses at the moment the total window started

// Derived / runtime:
float    flowRateLmin[NUM_DI];       // computed every second
uint32_t lastPulseSnapshot[NUM_DI];
uint32_t nextRateTickMs = 0;

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 250;   // periodic UI update

// ================== Modbus persisted ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
// V1 legacy
struct PersistConfigV1 {
  uint32_t magic; uint16_t version; uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address; uint32_t mb_baud; uint32_t crc32;
} __attribute__((packed));

// V2 legacy (same as V1 here)
struct PersistConfigV2 {
  uint32_t magic; uint16_t version; uint16_t size;
  InCfg   diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address; uint32_t mb_baud; uint32_t crc32;
} __attribute__((packed));

// V3 legacy (had flowCalib[] and flowAccumL[] which we now drop)
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

// V4 current (separate calibs + baseline; no stored liters)
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

using PersistConfig = PersistConfigV4;

static const uint32_t CFG_MAGIC       = 0x31524C57UL; // 'WLR1'
static const uint16_t CFG_VERSION_V1  = 0x0001;
static const uint16_t CFG_VERSION_V2  = 0x0002;
static const uint16_t CFG_VERSION_V3  = 0x0003;
static const uint16_t CFG_VERSION     = 0x0004;       // V4: separate calibs + baseline, no stored liters
static const char*    CFG_PATH        = "/cfg.bin";

// ---- 1-Wire DB ----
static const char* ONEWIRE_DB_PATH = "/ow_sensors.json";
static const size_t MAX_OW_SENSORS = 32;
struct OwRec { uint64_t addr; String name; };
OwRec  g_owDb[MAX_OW_SENSORS];
size_t g_owCount = 0;

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
  char buf[19]; buf[0]='0'; buf[1]='x';
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
static bool jsonGetU64(JSONVar obj, const char* key, uint64_t &out) {
  if (!obj.hasOwnProperty(key)) return false;
  JSONVar v=obj[key];
  String t=JSON.typeof(v);
  if (t=="number") { out=(uint64_t)(double)v; return true; }
  if (t=="string") { const char* s=(const char*)v; return parseDec64(s,out); }
  return false;
}
static bool jsonGetAddr(JSONVar obj, uint64_t &out) {
  if (jsonGetU64(obj,"addr_u64", out)) return true;
  if (jsonGetU64(obj,"rom_u64",  out)) return true;
  String s;
  if (jsonGetStr(obj,"rom_hex", s) || jsonGetStr(obj,"addr", s) ||
      jsonGetStr(obj,"address", s) || jsonGetStr(obj,"rom", s)) {
    return parseHex64(s.c_str(), out);
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

// ================== Defaults / persist ==================
void setDefaults() {
  for (int i=0;i<NUM_DI;i++)  diCfg[i]  = { true, false, 0, 0, IT_WATER };
  for (int i=0;i<NUM_RLY;i++) rlyCfg[i] = { true, false };
  for (int i=0;i<NUM_RLY;i++){ desiredRelay[i]=false; rlyPulseUntil[i]=0; }
  for (int i=0;i<NUM_DI;i++){
    diCounter[i]=0; diLastEdgeMs[i]=0;
    flowPulsesPerL[i] = 450;
    flowCalibRate[i]  = 1.0f;
    flowCalibAccum[i] = 1.0f;
    flowCounterBase[i]= 0;      // accumulation window starts from power-on
    flowRateLmin[i]   = 0.0f;
    lastPulseSnapshot[i] = 0;
  }
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
  }
  pc.crc32=0;
  pc.crc32=crc32_update(0,(const uint8_t*)&pc,sizeof(PersistConfig));
}

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

  // Initialize flow to defaults on migration
  for (int i=0;i<NUM_DI;i++){
    flowPulsesPerL[i]=450; flowCalibRate[i]=1.0f; flowCalibAccum[i]=1.0f; flowCounterBase[i]=diCounter[i];
    flowRateLmin[i]=0.0f; lastPulseSnapshot[i]=0;
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
    flowCounterBase[i]= diCounter[i]; // start window from current pulses
    flowRateLmin[i]   = 0.0f;
    lastPulseSnapshot[i] = 0;
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
  }
  nextRateTickMs = millis() + 1000;
  return true;
}

bool saveConfigFS(){
  PersistConfig pc{}; captureToPersist(pc);
  File f=LittleFS.open(CFG_PATH,"w");
  if(!f){ WebSerial.send("message","save: open failed"); return false; }
  size_t n=f.write((const uint8_t*)&pc,sizeof(pc)); f.flush(); f.close();
  if(n!=sizeof(pc)){ WebSerial.send("message",String("save: short write ")+n); return false; }
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
    WebSerial.send("message","Loaded legacy config v1 → migrated to v4 defaults for flow.");
    return true;
  } else if (sz==sizeof(PersistConfigV2)){
    PersistConfigV2 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v2)"); return false; }
    if(!applyFromPersistV2(pc)){ WebSerial.send("message","load: v2 magic/version/crc mismatch"); return false; }
    WebSerial.send("message","Loaded legacy config v2 → migrated to v4 defaults for flow.");
    return true;
  } else if (sz==sizeof(PersistConfigV3)){
    PersistConfigV3 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v3)"); return false; }
    if(!applyFromPersistV3(pc)){ WebSerial.send("message","load: v3 magic/version/crc mismatch"); return false; }
    WebSerial.send("message","Loaded legacy config v3 → migrated to v4 (split calibs, derived total).");
    return true;
  } else if (sz==sizeof(PersistConfig)){
    PersistConfig pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v4)"); return false; }
    if(!applyFromPersist(pc)){ WebSerial.send("message","load: v4 magic/version/crc mismatch"); return false; }
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
JSONVar owdbBuildArray(){
  JSONVar arr; for(size_t i=0;i<g_owCount;i++){ JSONVar o; o["addr"]=hex64(g_owDb[i].addr); o["name"]=g_owDb[i].name.c_str(); arr[i]=o; } return arr;
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
  return true;
}
void owdbSendList(){ WebSerial.send("onewireDb", owdbBuildArray()); }
bool owdbAddOrUpdate(uint64_t addr, const char* name){
  if (!name) return false;
  int idx=owdbIndexOf(addr);
  if (idx>=0){ g_owDb[idx].name=String(name); if(!owdbSave()) return false; owdbSendList(); return true; }
  if (g_owCount>=MAX_OW_SENSORS) return false;
  g_owDb[g_owCount].addr=addr; g_owDb[g_owCount].name=String(name); g_owCount++;
  if (!owdbSave()) return false; owdbSendList(); return true;
}
bool owdbRemove(uint64_t addr){
  int idx=owdbIndexOf(addr); if(idx<0) return false;
  for(size_t i=idx+1;i<g_owCount;i++) g_owDb[i-1]=g_owDb[i];
  g_owCount--; if(!owdbSave()) return false; owdbSendList(); return true;
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

// ================== Setup ==================
void setup(){
  Serial.begin(57600);
  for(uint8_t i=0;i<NUM_DI;i++)   pinMode(DI_PINS[i], INPUT);
  for(uint8_t i=0;i<NUM_RLY;i++){ pinMode(RELAY_PINS[i], OUTPUT); digitalWrite(RELAY_PINS[i], LOW); }
  setDefaults();

  if(!initFilesystemAndConfig()){ WebSerial.send("message","FATAL: Filesystem/config init failed"); }
  if(owdbLoad()) WebSerial.send("message","1-Wire DB loaded from flash");
  else           WebSerial.send("message","1-Wire DB missing/invalid (will create on first save)");

  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud); mb.config(g_mb_baud); setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("WLD-521-R1");

  for(uint16_t i=0;i<NUM_DI;i++)  mb.addIsts(ISTS_DI_BASE + i);
  for(uint16_t i=0;i<NUM_RLY;i++) mb.addIsts(ISTS_RLY_BASE + i);
  for(uint16_t i=0;i<NUM_DI;i++){ mb.addHreg(HREG_DI_COUNT_BASE+(i*2)+0,0); mb.addHreg(HREG_DI_COUNT_BASE+(i*2)+1,0); }
  for(uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_ON_BASE+i);  mb.setCoil(CMD_RLY_ON_BASE+i,false); }
  for(uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_OFF_BASE+i); mb.setCoil(CMD_RLY_OFF_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_DI_EN_BASE+i);   mb.setCoil(CMD_DI_EN_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_DI_DIS_BASE+i);  mb.setCoil(CMD_DI_DIS_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_CNT_RST_BASE+i); mb.setCoil(CMD_CNT_RST_BASE+i,false); }

  modbusStatus["address"]=g_mb_address; modbusStatus["baud"]=g_mb_baud; modbusStatus["state"]=0;

  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);
  WebSerial.on("onewire", handleOneWire);

  WebSerial.send("message","Boot OK (Flow on 'Water counter' type). Separate rate/accum calibration; accumulated derived from pulses.");
  sendAllEchoesOnce();   // also pushes onewireDb once
}

// ================== Command handlers ==================
void handleCommand(JSONVar obj){
  const char* actC=(const char*)obj["action"]; if(!actC){ WebSerial.send("message","command: missing 'action'"); return; }
  String act=String(actC); act.toLowerCase();

  if (act=="reset"||act=="reboot"){
    bool ok=saveConfigFS();
    WebSerial.send("message", ok ? "Saved. Rebooting…" : "WARNING: Save verify FAILED. Rebooting anyway…");
    delay(400); performReset();
    return;
  }
  if (act=="save"){
    if (saveConfigFS()) WebSerial.send("message","Configuration saved"); else WebSerial.send("message","ERROR: Save failed");
    return;
  }
  if (act=="load"){
    if (loadConfigFS()){ WebSerial.send("message","Configuration loaded"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address,g_mb_baud); }
    else WebSerial.send("message","ERROR: Load failed/invalid");
    return;
  }
  if (act=="factory"){
    setDefaults(); if (saveConfigFS()){ WebSerial.send("message","Factory defaults restored & saved"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address,g_mb_baud); }
    else WebSerial.send("message","ERROR: Save after factory reset failed");
    return;
  }
  if (act=="scan"||act=="scan1wire"||act=="scan_1wire"||act=="scan1w"){
    doOneWireScan();
    return;
  }

  // ===== Flow: calculate accumulation calibration from external liters =====
  if (act=="flow_calculate"){
    int di = -1;
    if (obj.hasOwnProperty("di")) di = (int)obj["di"];
    else if (obj.hasOwnProperty("input")) di = (int)obj["input"];
    else if (obj.hasOwnProperty("index")) di = (int)obj["index"];
    if (di >= 1 && di <= (int)NUM_DI) di -= 1; // convert to 0-based
    if (di < 0 || di >= (int)NUM_DI) { WebSerial.send("message","flow_calculate: invalid 'di'"); return; }

    double extLit = 0.0;
    if (!jsonGetDouble(obj,"external", extLit) &&
        !jsonGetDouble(obj,"external_liters", extLit) &&
        !jsonGetDouble(obj,"externalAccum", extLit)) {
      WebSerial.send("message","flow_calculate: missing 'external' liters");
      return;
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

  // Optional: reset accumulation window (does not clear pulses)
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

  WebSerial.send("message", String("Unknown command: ")+actC);
}

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

void handleUnifiedConfig(JSONVar obj){
  const char* t=(const char*)obj["t"]; JSONVar list=obj["list"]; if(!t) return;
  String type=String(t); bool changed=false;

  if (type=="inputEnable"){ for(int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].enabled=(bool)list[i]; WebSerial.send("message","Input Enabled list updated"); changed=true; }
  else if(type=="inputInvert"){ for(int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].inverted=(bool)list[i]; WebSerial.send("message","Input Invert list updated"); changed=true; }
  else if(type=="inputAction"){ for(int i=0;i<NUM_DI && i<list.length();i++){ int a=(int)list[i]; diCfg[i].action=(uint8_t)constrain(a,0,2);} WebSerial.send("message","Input Action list updated"); changed=true; }
  else if(type=="inputTarget"){ for(int i=0;i<NUM_DI && i<list.length();i++){ int tgt=(int)list[i]; diCfg[i].target=(uint8_t)((tgt==4||tgt==0||(tgt>=1&&tgt<=2))?tgt:0);} WebSerial.send("message","Input Control Target list updated"); changed=true; }
  else if(type=="inputType"){ for(int i=0;i<NUM_DI && i<list.length();i++){ int tp=(int)list[i]; diCfg[i].type=(uint8_t)constrain(tp,IT_WATER,IT_WCOUNTER);} WebSerial.send("message","Input Type list updated"); changed=true; }

  else if(type=="counterResetList"){
    uint32_t now=millis();
    for(int i=0;i<NUM_DI && i<list.length();i++){
      if((bool)list[i]){
        diCounter[i]=0;
        diLastEdgeMs[i]=now;
        lastPulseSnapshot[i]=0;
        flowCounterBase[i]=0; // keep accumulation at 0 after pulse reset
      }
    }
    WebSerial.send("message","Counters reset");
  }

  else if(type=="relays"){
    for(int i=0;i<NUM_RLY && i<list.length();i++){ rlyCfg[i].enabled=(bool)list[i]["enabled"]; rlyCfg[i].inverted=(bool)list[i]["inverted"]; }
    WebSerial.send("message","Relay Configuration updated"); changed=true;
  }

  // ===== Flow configs from UI =====
  else if (type=="flowPPL"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      long v = (long)(double)list[i];
      if (v <= 0) v = 1;
      flowPulsesPerL[i] = (uint32_t)v;
    }
    WebSerial.send("message","Flow: pulsesPerLiter updated");
    changed=true;
  }
  else if (type=="flowCalib"){
    // Back-compat: set BOTH rate & accum to provided values
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      double v = (double)list[i];
      if (v <= 0.0) v = 1.0;
      flowCalibRate[i]  = (float)v;
      flowCalibAccum[i] = (float)v;
    }
    WebSerial.send("message","Flow: calibration updated (applied to rate & accum)");
    changed=true;
  }
  else if (type=="flowCalibRate"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      double v = (double)list[i];
      if (v <= 0.0) v = 1.0;
      flowCalibRate[i]  = (float)v;
    }
    WebSerial.send("message","Flow: rate calibration updated");
    changed=true;
  }
  else if (type=="flowCalibAccum"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      double v = (double)list[i];
      if (v <= 0.0) v = 1.0;
      flowCalibAccum[i]  = (float)v;
    }
    WebSerial.send("message","Flow: accumulation calibration updated");
    changed=true;
  }
  else if (type=="flowResetAccumList"){
    for (int i=0;i<NUM_DI && i<list.length(); i++){
      if ((bool)list[i]) flowCounterBase[i] = diCounter[i];
    }
    WebSerial.send("message","Flow: accumulation windows reset (baseline moved)");
    changed=true;
  }

  else { WebSerial.send("message","Unknown Config type"); }

  if (changed){ cfgDirty=true; lastCfgTouchMs=millis(); }
}

// ===== OneWire handler =====
void handleOneWire(JSONVar obj){
  const char* actC=(const char*)obj["action"]; if(!actC){ WebSerial.send("message","onewire: missing action"); return; }
  String act=String(actC); act.toLowerCase();

  if (act=="list"){ owdbSendList(); return; }
  if (act=="clear"){ g_owCount=0; if(owdbSave()) WebSerial.send("message","onewire: cleared"); else WebSerial.send("message","onewire: clear save failed"); owdbSendList(); return; }

  uint64_t addr;
  if (!jsonGetAddr(obj, addr)){ WebSerial.send("message","onewire: invalid or missing address"); return; }

  if (act=="add"||act=="save"||act=="rename"){
    String nm; if (!jsonGetStr(obj,"name",nm) || nm.length()==0){ WebSerial.send("message","onewire: missing 'name'"); return; }
    if (owdbAddOrUpdate(addr, nm.c_str())) {
      String msg; msg.reserve(48); msg += "onewire: saved "; msg += hex64(addr);
      WebSerial.send("message", msg);
    } else {
      WebSerial.send("message","onewire: save failed (maybe full?)"); owdbSendList();
    }
  } else if (act=="remove"||act=="delete"){
    if (owdbRemove(addr)) {
      String msg; msg.reserve(48); msg += "onewire: removed "; msg += hex64(addr);
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

  // autosave
  if (cfgDirty && (now-lastCfgTouchMs>=CFG_AUTOSAVE_MS)){
    if (saveConfigFS()) WebSerial.send("message","Configuration saved");
    else WebSerial.send("message","ERROR: Save failed");
    cfgDirty=false;
  }

  // Inputs read + pulse handling
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

  // Relays
  JSONVar relayStateList;
  for (int i=0;i<NUM_RLY;i++){
    bool outVal=desiredRelay[i]; if(!rlyCfg[i].enabled) outVal=false; if(rlyCfg[i].inverted) outVal=!outVal;
    digitalWrite(RELAY_PINS[i], outVal?HIGH:LOW);
    relayStateList[i]=outVal; mb.setIsts(ISTS_RLY_BASE+i,outVal);
    if (rlyPulseUntil[i] && timeAfter32(now, rlyPulseUntil[i])){ desiredRelay[i]=false; rlyPulseUntil[i]=0; cfgDirty=true; lastCfgTouchMs=now; }
  }

  // Flow: compute rate every second (L/min) using flowCalibRate
  if (timeAfter32(now, nextRateTickMs)) {
    nextRateTickMs = now + 1000;
    for (int i=0;i<NUM_DI;i++){
      uint32_t ppl   = flowPulsesPerL[i] ? flowPulsesPerL[i] : 1;
      uint32_t delta = diCounter[i] - lastPulseSnapshot[i];
      lastPulseSnapshot[i] = diCounter[i];
      float liters_last_sec = ((float)delta / (float)ppl) * flowCalibRate[i];
      flowRateLmin[i] = liters_last_sec * 60.0f;
    }
  }

  // periodic UI publish
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

    // Flow lists to UI:
    JSONVar flowPPLList, flowCalibList, flowAccumList, flowRateList, flowCalibRateList, flowCalibAccumList;
    for (int i=0;i<NUM_DI;i++){
      // derived accumulated liters since baseline using flowCalibAccum
      uint32_t ppl = flowPulsesPerL[i] ? flowPulsesPerL[i] : 1;
      uint32_t pulses_since = (diCounter[i] >= flowCounterBase[i]) ? (diCounter[i] - flowCounterBase[i]) : 0;
      double accumL = ((double)pulses_since / (double)ppl) * (double)flowCalibAccum[i];

      flowPPLList[i]         = (double)flowPulsesPerL[i];
      flowCalibList[i]       = (double)flowCalibAccum[i];  // back-compat: single field shows ACCUM calib
      flowCalibRateList[i]   = (double)flowCalibRate[i];
      flowCalibAccumList[i]  = (double)flowCalibAccum[i];
      flowAccumList[i]       = accumL;
      flowRateList[i]        = (double)flowRateLmin[i];
    }

    WebSerial.send("inputs", inputs);
    WebSerial.send("invertList", invertList);
    WebSerial.send("enableList", enableList);
    WebSerial.send("inputActionList", actionList);
    WebSerial.send("inputTargetList", targetList);
    WebSerial.send("inputTypeList", typeList);
    WebSerial.send("counterList", counters);

    WebSerial.send("flowPPLList",   flowPPLList);
    WebSerial.send("flowCalibList", flowCalibList);           // legacy UI
    WebSerial.send("flowCalibRateList",  flowCalibRateList);  // new (optional)
    WebSerial.send("flowCalibAccumList", flowCalibAccumList); // new (optional)
    WebSerial.send("flowAccumList", flowAccumList);
    WebSerial.send("flowRateList",  flowRateList);

    WebSerial.send("relayStateList", relayStateList);
    WebSerial.send("relayEnableList", relayEnableList);
    WebSerial.send("relayInvertList", relayInvertList);

    // Also include stored 1-Wire DB
    owdbSendList();
  }
}

// ================== helpers ==================
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

  // Flow lists
  JSONVar flowPPLList, flowCalibList, flowAccumList, flowRateList, flowCalibRateList, flowCalibAccumList;
  for (int i=0;i<NUM_DI;i++){
    uint32_t ppl = flowPulsesPerL[i] ? flowPulsesPerL[i] : 1;
    uint32_t pulses_since = (diCounter[i] >= flowCounterBase[i]) ? (diCounter[i] - flowCounterBase[i]) : 0;
    double accumL = ((double)pulses_since / (double)ppl) * (double)flowCalibAccum[i];

    flowPPLList[i]         = (double)flowPulsesPerL[i];
    flowCalibList[i]       = (double)flowCalibAccum[i];  // legacy single
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
  // add two test ROMs
  romList[idx++]="0x0000000000000001";
  romList[idx++]="0x0000000000000002";
  WebSerial.send("onewireScan", romList);
  WebSerial.send("message", String("1-Wire scan: ")+found+ " device(s) + 2 test ROMs");
}
