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
static const uint8_t DI_PINS[5]    = {3, 2, 8, 9, 15};   // DI1..DI5
static const uint8_t RELAY_PINS[2] = {0, 1};            // R1..R2

// 1-Wire bus on GPIO16
static const uint8_t ONEWIRE_PIN = 16;
OneWire oneWire(ONEWIRE_PIN);

// ================== Sizes ==================
static const uint8_t NUM_DI  = 5;
static const uint8_t NUM_RLY = 2;

// ================== Input Types ==================
enum : uint8_t { IT_WATER=0, IT_HUMIDITY=1, IT_WCOUNTER=2 };

// ================== Config & runtime ==================
struct InCfg  { bool enabled; bool inverted; uint8_t action; uint8_t target; uint8_t type; };
struct RlyCfg { bool enabled; bool inverted; };

InCfg  diCfg[NUM_DI];
RlyCfg rlyCfg[NUM_RLY];

bool diState[NUM_DI]        = {0};
bool diPrev[NUM_DI]         = {0};

uint32_t diCounter[NUM_DI]    = {0};
uint32_t diLastEdgeMs[NUM_DI] = {0};
const uint32_t CNT_DEBOUNCE_MS = 20;

bool     desiredRelay[NUM_RLY] = {0};
uint32_t rlyPulseUntil[NUM_RLY]= {0};
const uint32_t PULSE_MS = 500;

// ================== Web Serial ==================
SimpleWebSerial WebSerial;
JSONVar modbusStatus;

// ================== Timing ==================
unsigned long lastSend = 0;
const unsigned long sendInterval = 250;

// ================== Persisted Modbus settings ==================
uint8_t  g_mb_address = 3;
uint32_t g_mb_baud    = 19200;

// ================== Persistence (LittleFS) ==================
struct InCfgV1 { bool enabled; bool inverted; uint8_t action; uint8_t target; /* no type */ };
struct PersistConfigV1 {
  uint32_t magic;  uint16_t version;  uint16_t size;
  InCfgV1 diCfg[NUM_DI];
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address;
  uint32_t mb_baud;
  uint32_t crc32;
} __attribute__((packed));

struct PersistConfigV2 {
  uint32_t magic;  uint16_t version;  uint16_t size;
  InCfg   diCfg[NUM_DI];   // includes .type
  RlyCfg  rlyCfg[NUM_RLY];
  bool    desiredRelay[NUM_RLY];
  uint8_t mb_address;
  uint32_t mb_baud;
  uint32_t crc32;
} __attribute__((packed));
using PersistConfig = PersistConfigV2;

static const uint32_t CFG_MAGIC      = 0x31524C57UL; // 'WLR1'
static const uint16_t CFG_VERSION_V1 = 0x0001;
static const uint16_t CFG_VERSION    = 0x0002;
static const char*    CFG_PATH       = "/cfg.bin";

// ---- 1-Wire DB (separate) ----
static const char* ONEWIRE_DB_PATH = "/ow_sensors.json";
static const size_t MAX_OW_SENSORS = 32;
struct OwRec { uint64_t addr; String name; };
OwRec  g_owDb[MAX_OW_SENSORS];
size_t g_owCount = 0;

volatile bool   cfgDirty        = false;
uint32_t        lastCfgTouchMs  = 0;
const uint32_t  CFG_AUTOSAVE_MS = 1500;

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
inline bool timeAfter32(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

// ---- HEX helpers for 1-Wire ROM formatting ----
String hex64(uint64_t v) {
  char buf[19]; buf[0]='0'; buf[1]='x';
  for (int i=0;i<16;i++) { uint8_t nib=(v >> ((15-i)*4)) & 0xF; buf[2+i]=(nib<10)?('0'+nib):('A'+(nib-10)); }
  buf[18]=0; return String(buf);
}
uint64_t romBytesToU64(const uint8_t *addr) {
  uint64_t v=0; for (int i=7;i>=0;i--) v=(v<<8)|addr[i]; return v;
}

// ---- robust parsers ----
static bool parseHexOrDec(String s, uint64_t& out) {
  s.trim();
  if (s.length()==0) return false;

  // strip surrounding quotes (if JSON.stringify gave quotes)
  if (s.length()>=2 && s[0]=='"' && s[s.length()-1]=='"') { s.remove(0,1); s.remove(s.length()-1); s.trim(); }

  if (s.startsWith("0x") || s.startsWith("0X")) {
    String h=s; h.remove(0,2);
    if (h.length()==0 || h.length()>16) return false;
    uint64_t v=0;
    for (uint16_t i=0;i<h.length();i++){
      char c=h[i]; uint8_t x;
      if(c>='0'&&c<='9') x=c-'0';
      else if(c>='a'&&c<='f') x=10+(c-'a');
      else if(c>='A'&&c<='F') x=10+(c-'A');
      else return false;
      v=(v<<4)|x;
    }
    out=v; return true;
  }

  // decimal
  uint64_t v=0;
  for (uint16_t i=0;i<s.length();i++){
    char c=s[i]; if(c<'0'||c>'9') return false;
    v = v*10 + (uint8_t)(c-'0');
  }
  out=v; return true;
}

static bool jsonToString(JSONVar v, String& out) {
  String t = JSON.typeof(v);     // "string" | "number" | ...
  if (t=="string") {
    const char* s = (const char*)v;
    if (!s) return false;
    out = String(s); out.trim(); return out.length()>0;
  }
  if (t=="number") {             // represent the number as a string
    out = JSON.stringify(v);     // e.g. 1 → "1"
    out.trim(); return out.length()>0;
  }
  return false;
}

// STRICT reader: prefer rom_hex → addr_hex → rom → addr → address
static bool jsonGetAddrStrict(JSONVar obj, uint64_t &out) {
  const char* KEYS[] = { "rom_hex", "addr_hex", "rom", "addr", "address" };
  String      srcS, srcKey, srcType;
  bool        gotSomething=false, parsed=false;
  uint64_t    val=0;

  for (auto key : KEYS) {
    if (!obj.hasOwnProperty(key)) continue;
    JSONVar v = obj[key];
    srcType = JSON.typeof(v);
    String s;
    if (!jsonToString(v, s)) s = "";      // collect anyway for debugging
    if (!gotSomething) { srcS = s; srcKey = key; gotSomething = true; }
    if (parseHexOrDec(s, val)) { parsed=true; break; }
  }

  if (!parsed) return false;

  // refuse saving zero unless the original string actually meant zero
  if (val==0) {
    String z = srcS; z.trim();
    String zl = z; zl.toLowerCase();
    bool explicitlyZero = (z=="0" || zl=="0x0" || zl=="0x");
    if (!explicitlyZero) {
      WebSerial.send("message", String("onewire: REFUSED to save 0. key=")+srcKey+
                                 " typeof="+srcType+" val="+srcS);
      return false;
    }
  }

  out = val;
  return true;
}

// ================== Defaults / persist ==================
void setDefaults() {
  for (int i=0;i<NUM_DI;i++) diCfg[i] = {true,false,0,0,IT_WATER};
  for (int i=0;i<NUM_RLY;i++) rlyCfg[i]= {true,false};
  for (int i=0;i<NUM_RLY;i++){ desiredRelay[i]=false; rlyPulseUntil[i]=0; }
  for (int i=0;i<NUM_DI;i++)  { diCounter[i]=0; diLastEdgeMs[i]=0; }
  g_mb_address = 3; g_mb_baud = 19200;
}

void captureToPersist(PersistConfig &pc) {
  pc.magic=CFG_MAGIC; pc.version=CFG_VERSION; pc.size=sizeof(PersistConfig);
  memcpy(pc.diCfg,diCfg,sizeof(diCfg));
  memcpy(pc.rlyCfg,rlyCfg,sizeof(rlyCfg));
  memcpy(pc.desiredRelay,desiredRelay,sizeof(desiredRelay));
  pc.mb_address=g_mb_address; pc.mb_baud=g_mb_baud;
  pc.crc32=0; pc.crc32=crc32_update(0,(const uint8_t*)&pc,sizeof(PersistConfig));
}

bool applyFromPersistV1(const PersistConfigV1 &v1) {
  if (v1.magic!=CFG_MAGIC || v1.size!=sizeof(PersistConfigV1)) return false;
  PersistConfigV1 tmp=v1; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc) return false;
  if (v1.version!=CFG_VERSION_V1) return false;

  for (int i=0;i<NUM_DI;i++){
    diCfg[i].enabled = v1.diCfg[i].enabled;
    diCfg[i].inverted= v1.diCfg[i].inverted;
    diCfg[i].action  = v1.diCfg[i].action;
    diCfg[i].target  = v1.diCfg[i].target;
    diCfg[i].type    = IT_WATER;
  }
  memcpy(rlyCfg, v1.rlyCfg, sizeof(rlyCfg));
  memcpy(desiredRelay, v1.desiredRelay, sizeof(desiredRelay));
  g_mb_address=v1.mb_address; g_mb_baud=v1.mb_baud;
  return true;
}

bool applyFromPersist(const PersistConfig &pc) {
  if (pc.magic!=CFG_MAGIC || pc.size!=sizeof(PersistConfig)) return false;
  PersistConfig tmp=pc; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if (crc32_update(0,(const uint8_t*)&tmp,sizeof(PersistConfig))!=crc) return false;
  if (pc.version!=CFG_VERSION) return false;

  memcpy(diCfg, pc.diCfg, sizeof(diCfg));
  memcpy(rlyCfg, pc.rlyCfg, sizeof(rlyCfg));
  memcpy(desiredRelay, pc.desiredRelay, sizeof(desiredRelay));
  g_mb_address=pc.mb_address; g_mb_baud=pc.mb_baud;
  return true;
}

bool saveConfigFS() {
  PersistConfig pc{}; captureToPersist(pc);
  File f=LittleFS.open(CFG_PATH,"w");
  if(!f){ WebSerial.send("message","save: open failed"); return false; }
  size_t n=f.write((const uint8_t*)&pc,sizeof(pc));
  f.flush(); f.close();
  if(n!=sizeof(pc)){ WebSerial.send("message",String("save: short write ")+n); return false; }
  File r=LittleFS.open(CFG_PATH,"r");
  if(!r){ WebSerial.send("message","save: reopen failed"); return false; }
  if((size_t)r.size()!=sizeof(PersistConfig)){ WebSerial.send("message","save: size mismatch"); r.close(); return false; }
  PersistConfig back{}; size_t nr=r.read((uint8_t*)&back,sizeof(back)); r.close();
  if(nr!=sizeof(back)){ WebSerial.send("message","save: short readback"); return false; }
  PersistConfig tmp=back; uint32_t crc=tmp.crc32; tmp.crc32=0;
  if(crc32_update(0,(const uint8_t*)&tmp,sizeof(tmp))!=crc){ WebSerial.send("message","save: CRC verify failed"); return false; }
  return true;
}

bool loadConfigFS() {
  File f=LittleFS.open(CFG_PATH,"r"); if(!f){ WebSerial.send("message","load: open failed"); return false; }
  size_t sz=f.size();
  if(sz==sizeof(PersistConfig)){
    PersistConfig pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v2)"); return false; }
    if(!applyFromPersist(pc)){ WebSerial.send("message","load: v2 hdr/crc mismatch"); return false; }
    return true;
  }else if(sz==sizeof(PersistConfigV1)){
    PersistConfigV1 pc{}; size_t n=f.read((uint8_t*)&pc,sizeof(pc)); f.close();
    if(n!=sizeof(pc)){ WebSerial.send("message","load: short read (v1)"); return false; }
    if(!applyFromPersistV1(pc)){ WebSerial.send("message","load: v1 hdr/crc mismatch"); return false; }
    WebSerial.send("message","Loaded legacy config v1 and migrated");
    return true;
  }else{
    WebSerial.send("message",String("load: unexpected size ")+sz);
    f.close(); return false;
  }
}

// ================== FS init ==================
bool initFilesystemAndConfig() {
  if(!LittleFS.begin()){
    WebSerial.send("message","LittleFS mount failed. Formatting…");
    if(!LittleFS.format() || !LittleFS.begin()){
      WebSerial.send("message","FATAL: FS mount/format failed"); return false;
    }
  }
  if(loadConfigFS()){ WebSerial.send("message","Config loaded from flash"); return true; }

  WebSerial.send("message","No valid config. Using defaults.");
  setDefaults();
  if(saveConfigFS()){ WebSerial.send("message","Defaults saved"); return true; }

  WebSerial.send("message","First save failed. Formatting FS…");
  if(!LittleFS.format() || !LittleFS.begin()){ WebSerial.send("message","FATAL: FS format failed"); return false; }
  setDefaults();
  if(saveConfigFS()){ WebSerial.send("message","FS formatted and config saved"); return true; }
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
  CMD_DI_EN_BASE=300, CMD_DI_DIS_BASE=320,
  CMD_CNT_RST_BASE=340
};

// ================== 1-Wire DB helpers ==================
int owdbIndexOf(uint64_t addr){ for(size_t i=0;i<g_owCount;i++) if(g_owDb[i].addr==addr) return (int)i; return -1; }
JSONVar owdbBuildArray(){
  JSONVar arr; for(size_t i=0;i<g_owCount;i++){ JSONVar o; o["addr"]=hex64(g_owDb[i].addr); o["name"]=g_owDb[i].name.c_str(); arr[i]=o; }
  return arr;
}
bool owdbSave(){
  String json = JSON.stringify(owdbBuildArray());
  File f=LittleFS.open(ONEWIRE_DB_PATH,"w");
  if(!f){ WebSerial.send("message","owdb: save open failed"); return false; }
  size_t n=f.print(json); f.flush(); f.close();
  if(n!=json.length()){ WebSerial.send("message","owdb: short write"); return false; }
  return true;
}
bool owdbLoad(){
  g_owCount=0;
  File f=LittleFS.open(ONEWIRE_DB_PATH,"r"); if(!f) return false;
  String s; while(f.available()) s+=(char)f.read(); f.close();
  JSONVar arr=JSON.parse(s); if(JSON.typeof(arr)!="array"){ WebSerial.send("message","owdb: invalid JSON"); return false; }
  for(unsigned i=0; i<arr.length() && g_owCount<MAX_OW_SENSORS; i++){
    const char* a=(const char*)arr[i]["addr"]; const char* n=(const char*)arr[i]["name"];
    if(!a||!n) continue; uint64_t v; if(!parseHexOrDec(String(a),v)) continue;
    g_owDb[g_owCount].addr=v; g_owDb[g_owCount].name=String(n); g_owCount++;
  }
  return true;
}
void owdbSendList(){ WebSerial.send("onewireDb", owdbBuildArray()); }
bool owdbAddOrUpdate(uint64_t addr, const char* name){
  if(!name) return false;
  int idx=owdbIndexOf(addr);
  if(idx>=0){ g_owDb[idx].name=String(name); if(!owdbSave()) return false; owdbSendList(); return true; }
  if(g_owCount>=MAX_OW_SENSORS) return false;
  g_owDb[g_owCount].addr=addr; g_owDb[g_owCount].name=String(name); g_owCount++;
  if(!owdbSave()) return false; owdbSendList(); return true;
}
bool owdbRemove(uint64_t addr){
  int idx=owdbIndexOf(addr); if(idx<0) return false;
  for(size_t i=idx+1;i<g_owCount;i++) g_owDb[i-1]=g_owDb[i];
  g_owCount--; if(!owdbSave()) return false; owdbSendList(); return true;
}

// ================== Fw decls ==================
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
void setup() {
  Serial.begin(57600);

  for(uint8_t i=0;i<NUM_DI;i++) pinMode(DI_PINS[i], INPUT);
  for(uint8_t i=0;i<NUM_RLY;i++){ pinMode(RELAY_PINS[i],OUTPUT); digitalWrite(RELAY_PINS[i],LOW); }

  setDefaults();
  if(!initFilesystemAndConfig()) WebSerial.send("message","FATAL: FS/config init failed");

  if(owdbLoad()) WebSerial.send("message","1-Wire DB loaded from flash");
  else           WebSerial.send("message","1-Wire DB missing/invalid (created on first save)");

  Serial2.setTX(TX2); Serial2.setRX(RX2);
  Serial2.begin(g_mb_baud); mb.config(g_mb_baud); setSlaveIdIfAvailable(mb, g_mb_address);
  mb.setAdditionalServerData("WLD-521-R1");

  for(uint16_t i=0;i<NUM_DI;i++)  mb.addIsts(ISTS_DI_BASE + i);
  for(uint16_t i=0;i<NUM_RLY;i++) mb.addIsts(ISTS_RLY_BASE + i);

  for(uint16_t i=0;i<NUM_DI;i++){ mb.addHreg(HREG_DI_COUNT_BASE + (i*2) + 0, 0); mb.addHreg(HREG_DI_COUNT_BASE + (i*2) + 1, 0); }

  for(uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_ON_BASE+i);  mb.setCoil(CMD_RLY_ON_BASE+i,false); }
  for(uint16_t i=0;i<NUM_RLY;i++){ mb.addCoil(CMD_RLY_OFF_BASE+i); mb.setCoil(CMD_RLY_OFF_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_DI_EN_BASE+i);   mb.setCoil(CMD_DI_EN_BASE+i,false);  }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_DI_DIS_BASE+i);  mb.setCoil(CMD_DI_DIS_BASE+i,false); }
  for(uint16_t i=0;i<NUM_DI;i++){  mb.addCoil(CMD_CNT_RST_BASE+i); mb.setCoil(CMD_CNT_RST_BASE+i,false);}

  modbusStatus["address"]=g_mb_address;
  modbusStatus["baud"]=g_mb_baud;
  modbusStatus["state"]=0;

  WebSerial.on("values",  handleValues);
  WebSerial.on("Config",  handleUnifiedConfig);
  WebSerial.on("command", handleCommand);
  WebSerial.on("onewire", handleOneWire);

  WebSerial.send("message","Boot OK");
  sendAllEchoesOnce();  // pushes onewireDb once
}

// ================== Command handler / reset / scan ==================
void handleCommand(JSONVar obj) {
  const char* actC = (const char*)obj["action"];
  if(!actC){ WebSerial.send("message","command: missing 'action'"); return; }
  String act=String(actC); act.toLowerCase();

  if (act=="reset" || act=="reboot") {
    bool ok=saveConfigFS();
    WebSerial.send("message", ok ? "Saved. Rebooting…" : "WARNING: Save verify FAILED. Rebooting anyway…");
    delay(400); performReset();
  } else if (act=="save") {
    WebSerial.send("message", saveConfigFS() ? "Configuration saved" : "ERROR: Save failed");
  } else if (act=="load") {
    if (loadConfigFS()) { WebSerial.send("message","Configuration loaded"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address,g_mb_baud); }
    else WebSerial.send("message","ERROR: Load failed/invalid");
  } else if (act=="factory") {
    setDefaults();
    if (saveConfigFS()) { WebSerial.send("message","Factory defaults restored & saved"); sendAllEchoesOnce(); applyModbusSettings(g_mb_address,g_mb_baud); }
    else WebSerial.send("message","ERROR: Save after factory reset failed");
  } else if (act=="scan" || act=="scan1wire" || act=="scan_1wire" || act=="scan1w") {
    doOneWireScan();
  } else {
    WebSerial.send("message", String("Unknown command: ")+actC);
  }
}

void performReset() {
  if(Serial) Serial.flush(); delay(50);
  watchdog_reboot(0,0,0);
  while(true){ __asm__("wfi"); }
}

void applyModbusSettings(uint8_t addr, uint32_t baud) {
  if ((uint32_t)modbusStatus["baud"] != baud) { Serial2.end(); Serial2.begin(baud); mb.config(baud); }
  setSlaveIdIfAvailable(mb, addr);
  g_mb_address=addr; g_mb_baud=baud;
  modbusStatus["address"]=g_mb_address; modbusStatus["baud"]=g_mb_baud;
}

// ================== WebSerial config handlers ==================
void handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr,1,255); baud=constrain(baud,9600,115200);
  applyModbusSettings((uint8_t)addr,(uint32_t)baud);
  WebSerial.send("message","Modbus configuration updated");
  cfgDirty=true; lastCfgTouchMs=millis();
}

void handleUnifiedConfig(JSONVar obj) {
  const char* t=(const char*)obj["t"]; JSONVar list=obj["list"]; if(!t) return;
  String type=String(t); bool changed=false;

  if(type=="inputEnable"){ for(int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].enabled=(bool)list[i]; WebSerial.send("message","Input Enabled list updated"); changed=true; }
  else if(type=="inputInvert"){ for(int i=0;i<NUM_DI && i<list.length();i++) diCfg[i].inverted=(bool)list[i]; WebSerial.send("message","Input Invert list updated"); changed=true; }
  else if(type=="inputAction"){ for(int i=0;i<NUM_DI && i<list.length();i++){ int a=(int)list[i]; diCfg[i].action=(uint8_t)constrain(a,0,2);} WebSerial.send("message","Input Action list updated"); changed=true; }
  else if(type=="inputTarget"){ for(int i=0;i<NUM_DI && i<list.length();i++){ int tgt=(int)list[i]; diCfg[i].target=(uint8_t)((tgt==4||tgt==0||(tgt>=1&&tgt<=2))?tgt:0);} WebSerial.send("message","Input Control Target list updated"); changed=true; }
  else if(type=="inputType"){ for(int i=0;i<NUM_DI && i<list.length();i++){ int tp=(int)list[i]; diCfg[i].type=(uint8_t)constrain(tp,IT_WATER,IT_WCOUNTER);} WebSerial.send("message","Input Type list updated"); changed=true; }
  else if(type=="counterResetList"){ uint32_t now=millis(); for(int i=0;i<NUM_DI && i<list.length();i++){ if((bool)list[i]){ diCounter[i]=0; diLastEdgeMs[i]=now; }} WebSerial.send("message","Counters reset"); }
  else if(type=="relays"){ for(int i=0;i<NUM_RLY && i<list.length();i++){ rlyCfg[i].enabled=(bool)list[i]["enabled"]; rlyCfg[i].inverted=(bool)list[i]["inverted"]; } WebSerial.send("message","Relay Configuration updated"); changed=true; }
  else { WebSerial.send("message","Unknown Config type"); }

  if(changed){ cfgDirty=true; lastCfgTouchMs=millis(); }
}

// ================== OneWire DB handler ==================
void handleOneWire(JSONVar obj) {
  const char* actC=(const char*)obj["action"];
  if(!actC){ WebSerial.send("message","onewire: missing action"); return; }
  String act=String(actC); act.toLowerCase();

  if (act=="list") { owdbSendList(); return; }

  if (act=="clear") {
    g_owCount=0;
    if(owdbSave()) WebSerial.send("message","onewire: cleared");
    else WebSerial.send("message","onewire: clear save failed");
    owdbSendList(); return;
  }

  uint64_t addr;
  if (!jsonGetAddrStrict(obj, addr)) {
    // dump a short diagnostic of what we received (keys + typeof)
    String diag="onewire: bad/ambiguous address. keys:[";
    const char* KEYS[]={"rom_hex","addr_hex","rom","addr","address"};
    for (int i=0;i<5;i++){
      const char* k=KEYS[i]; if(!obj.hasOwnProperty(k)) continue;
      diag += String(k) + ":" + JSON.typeof(obj[k]) + "=" + JSON.stringify(obj[k]) + " ";
    }
    diag += "]";
    WebSerial.send("message", diag);
    return;
  }

  if (act=="add" || act=="save" || act=="rename") {
    String nm;
    if (!jsonToString(obj["name"], nm) || nm.length()==0) { WebSerial.send("message","onewire: missing 'name'"); return; }
    if (owdbAddOrUpdate(addr, nm.c_str())) WebSerial.send("message", String("onewire: saved ")+hex64(addr));
    else { WebSerial.send("message","onewire: save failed (maybe full?)"); owdbSendList(); }
  } else if (act=="remove" || act=="delete") {
    if (owdbRemove(addr)) WebSerial.send("message", String("onewire: removed ")+hex64(addr));
    else { WebSerial.send("message","onewire: address not found"); owdbSendList(); }
  } else {
    WebSerial.send("message", String("onewire: unknown action '")+act+"'");
  }
}

// ================== Modbus command pulses ==================
void processModbusCommandPulses() {
  for(int r=0;r<NUM_RLY;r++){
    if(mb.Coil(CMD_RLY_ON_BASE+r)){  mb.setCoil(CMD_RLY_ON_BASE+r,false);  desiredRelay[r]=true;  rlyPulseUntil[r]=0; cfgDirty=true; lastCfgTouchMs=millis(); }
    if(mb.Coil(CMD_RLY_OFF_BASE+r)){ mb.setCoil(CMD_RLY_OFF_BASE+r,false); desiredRelay[r]=false; rlyPulseUntil[r]=0; cfgDirty=true; lastCfgTouchMs=millis(); }
  }
  for(int i=0;i<NUM_DI;i++){
    if(mb.Coil(CMD_DI_EN_BASE+i)){  mb.setCoil(CMD_DI_EN_BASE+i,false);  if(!diCfg[i].enabled){ diCfg[i].enabled=true; cfgDirty=true; lastCfgTouchMs=millis(); } }
    if(mb.Coil(CMD_DI_DIS_BASE+i)){ mb.setCoil(CMD_DI_DIS_BASE+i,false); if(diCfg[i].enabled){  diCfg[i].enabled=false; cfgDirty=true; lastCfgTouchMs=millis(); } }
  }
  for(int i=0;i<NUM_DI;i++){
    if(mb.Coil(CMD_CNT_RST_BASE+i)){ mb.setCoil(CMD_CNT_RST_BASE+i,false); diCounter[i]=0; diLastEdgeMs[i]=millis(); }
  }
}

// ================== Apply DI action to a target ==================
void applyActionToTarget(uint8_t target, uint8_t action, uint32_t now) {
  auto doRelay = [&](int rIdx){ if(rIdx<0||rIdx>=NUM_RLY) return;
    if(action==1){ desiredRelay[rIdx]=!desiredRelay[rIdx]; rlyPulseUntil[rIdx]=0; }
    else if(action==2){ desiredRelay[rIdx]=true; rlyPulseUntil[rIdx]=now+PULSE_MS; }
  };
  if(action==0 || target==4) return;
  if(target==0){ for(int r=0;r<NUM_RLY;r++) doRelay(r); }
  else if(target>=1 && target<=2) doRelay(target-1);
  cfgDirty=true; lastCfgTouchMs=now;
}

// ================== Helpers (Modbus 32-bit write) ==================
inline void setHreg32(uint16_t base, uint32_t v){ mb.setHreg(base+0,(uint16_t)(v&0xFFFF)); mb.setHreg(base+1,(uint16_t)((v>>16)&0xFFFF)); }

// ================== Main loop ==================
void loop() {
  unsigned long now=millis();
  mb.task();
  processModbusCommandPulses();

  if(cfgDirty && (now-lastCfgTouchMs>=CFG_AUTOSAVE_MS)){
    WebSerial.send("message", saveConfigFS() ? "Configuration saved" : "ERROR: Save failed");
    cfgDirty=false;
  }

  JSONVar inputs, counters;
  for(int i=0;i<NUM_DI;i++){
    bool raw=(digitalRead(DI_PINS[i])==HIGH);
    if(diCfg[i].inverted) raw=!raw;

    bool val=false;
    if(!diCfg[i].enabled){ val=false; }
    else{
      switch(diCfg[i].type){
        case IT_WATER:
        case IT_HUMIDITY: val=raw; break;
        case IT_WCOUNTER: val=raw; break;
        default: val=raw; break;
      }
    }

    bool prev=diState[i]; diPrev[i]=prev; diState[i]=val;
    inputs[i]=val; mb.setIsts(ISTS_DI_BASE+i,val);

    bool rising=(!prev && val);
    bool falling=(prev && !val);

    if(diCfg[i].type==IT_WCOUNTER){
      if(rising && (now-diLastEdgeMs[i]>=CNT_DEBOUNCE_MS)){
        diCounter[i]++; diLastEdgeMs[i]=now; setHreg32(HREG_DI_COUNT_BASE+(i*2), diCounter[i]);
      }
    }else{
      uint8_t act=diCfg[i].action;
      if(act==1){ if(rising||falling) applyActionToTarget(diCfg[i].target,1,now); }
      else if(act==2){ if(rising) applyActionToTarget(diCfg[i].target,1,now); }
    }
    counters[i]=(double)diCounter[i];
  }

  JSONVar relayStateList;
  for(int i=0;i<NUM_RLY;i++){
    bool outVal=desiredRelay[i];
    if(!rlyCfg[i].enabled) outVal=false;
    if(rlyCfg[i].inverted) outVal=!outVal;
    digitalWrite(RELAY_PINS[i], outVal?HIGH:LOW);
    relayStateList[i]=outVal; mb.setIsts(ISTS_RLY_BASE+i,outVal);

    if(rlyPulseUntil[i] && timeAfter32(now,rlyPulseUntil[i])){
      desiredRelay[i]=false; rlyPulseUntil[i]=0; cfgDirty=true; lastCfgTouchMs=now;
    }
  }

  if(millis()-lastSend>=sendInterval){
    lastSend=millis(); WebSerial.check();
    WebSerial.send("status",modbusStatus);

    JSONVar invertList, enableList, actionList, targetList, typeList;
    for(int i=0;i<NUM_DI;i++){
      invertList[i]=diCfg[i].inverted; enableList[i]=diCfg[i].enabled;
      actionList[i]=diCfg[i].action; targetList[i]=diCfg[i].target; typeList[i]=diCfg[i].type;
    }
    JSONVar relayEnableList, relayInvertList;
    for(int i=0;i<NUM_RLY;i++){ relayEnableList[i]=rlyCfg[i].enabled; relayInvertList[i]=rlyCfg[i].inverted; }

    WebSerial.send("inputs",inputs);
    WebSerial.send("invertList",invertList);
    WebSerial.send("enableList",enableList);
    WebSerial.send("inputActionList",actionList);
    WebSerial.send("inputTargetList",targetList);
    WebSerial.send("inputTypeList",typeList);
    WebSerial.send("counterList",counters);

    WebSerial.send("relayStateList",relayStateList);
    WebSerial.send("relayEnableList",relayEnableList);
    WebSerial.send("relayInvertList",relayInvertList);

    // IMPORTANT: do not spam onewireDb here
  }
}

// ================== helpers ==================
void sendAllEchoesOnce() {
  JSONVar enableList, invertList, actionList, targetList, typeList, counters;
  for (int i=0;i<NUM_DI;i++){
    enableList[i]=diCfg[i].enabled; invertList[i]=diCfg[i].inverted;
    actionList[i]=diCfg[i].action;  targetList[i]=diCfg[i].target;
    typeList[i]=diCfg[i].type;      counters[i]=(double)diCounter[i];
  }
  WebSerial.send("enableList",enableList);
  WebSerial.send("invertList",invertList);
  WebSerial.send("inputActionList",actionList);
  WebSerial.send("inputTargetList",targetList);
  WebSerial.send("inputTypeList",typeList);
  WebSerial.send("counterList",counters);

  JSONVar relayEnableList, relayInvertList;
  for(int i=0;i<NUM_RLY;i++){ relayEnableList[i]=rlyCfg[i].enabled; relayInvertList[i]=rlyCfg[i].inverted; }
  WebSerial.send("relayEnableList",relayEnableList);
  WebSerial.send("relayInvertList",relayInvertList);

  owdbSendList(); // push once
  modbusStatus["address"]=g_mb_address; modbusStatus["baud"]=g_mb_baud;
  WebSerial.send("status",modbusStatus);
}

// ================== 1-Wire scan implementation ==================
void doOneWireScan() {
  JSONVar romList; uint8_t addr[8]; int idx=0; int found=0;
  oneWire.reset_search();
  while(oneWire.search(addr)){
    if(OneWire::crc8(addr,7)!=addr[7]) continue;
    uint64_t v=romBytesToU64(addr);
    romList[idx++]=hex64(v); found++;
  }
  // include two test ROMs for UI dev
  romList[idx++]="0x0000000000000001";
  romList[idx++]="0x0000000000000002";
  WebSerial.send("onewireScan",romList);
  WebSerial.send("message", String("1-Wire scan: ")+found+" device(s) + 2 test ROMs");
}
