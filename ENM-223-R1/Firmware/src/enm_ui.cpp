#include "enm_ui.h"
#include "enm_modbus.h"   // MB_* facade
#include "atm90e32.h"     // for enm223::M90DiagRegs

namespace enm223 {

static SimpleWebSerial* s_ws = nullptr;
static UiBindings* s_b = nullptr;

static inline void ws_send(const char* evt, const JSONVar& v){ if (s_ws) s_ws->send(evt, v); }
static inline void ws_msg(const char* m){ if (s_ws) s_ws->send("message", m); }

// ----- JSON helpers -----
static JSONVar LedConfigListFromCfg() {
  JSONVar arr;
  for (int i = 0; i < 4; i++) { JSONVar o; o["mode"]=s_b->ledCfg[i].mode; o["source"]=s_b->ledCfg[i].source; arr[i]=o; }
  return arr;
}
static JSONVar ButtonConfigListFromCfg() {
  JSONVar arr; for (int i=0;i<4;i++) arr[i] = s_b->buttonCfg[i].action; return arr;
}
static JSONVar RelayConfigFromCfg() {
  JSONVar r;
  r["active_low"] = *s_b->relay_active_low;
  r["def0"] = s_b->relay_default[0];
  r["def1"] = s_b->relay_default[1];
  JSONVar modes; modes[0]=s_b->relay_mode[0]; modes[1]=s_b->relay_mode[1];
  r["mode"] = modes;
  JSONVar alarms;
  for (int i=0;i<2;i++){ JSONVar a; a["ch"]=s_b->relay_alarm_src[i].ch; a["mask"]=s_b->relay_alarm_src[i].kindsMask; alarms[i]=a; }
  r["alarm"] = alarms;
  return r;
}
static void sendStatusEcho() { MB_fillStatus(s_b->modbusStatus); ws_send("status", *s_b->modbusStatus); }
static void alarms_publish_cfg(){
  JSONVar alCfg;
  for (int ch=0; ch<CH_COUNT; ++ch) {
    JSONVar chO; chO["ack"]= s_b->alarmCfg[ch].ackRequired;
    for (int k=0;k<AK_COUNT;++k) {
      JSONVar r; r["enabled"]= s_b->alarmCfg[ch].rule[k].enabled;
      r["min"]    = (int32_t)s_b->alarmCfg[ch].rule[k].min;
      r["max"]    = (int32_t)s_b->alarmCfg[ch].rule[k].max;
      r["metric"] = (uint8_t)s_b->alarmCfg[ch].rule[k].metric;
      chO[(int)k]=r;
    }
    alCfg[(int)ch]=chO;
  }
  ws_send("AlarmsCfg", alCfg);
}
static void alarms_publish_state(){
  JSONVar st;
  for (int ch=0; ch<CH_COUNT; ++ch) {
    JSONVar chO;
    for (int k=0;k<AK_COUNT;++k) {
      const AlarmRuntime& art = s_b->alarmRt[ch*AK_COUNT+k];
      JSONVar r; r["cond"]=art.conditionNow; r["active"]=art.active; r["latched"]=art.latched; chO[(int)k]=r;
    }
    st[(int)ch]=chO;
  }
  ws_send("AlarmsState", st);
}
static JSONVar calib_json_from_cfg(){
  JSONVar arr;
  for (int ph=0; ph<3; ++ph){
    JSONVar o;
    o["Ug"] = (uint32_t)s_b->cal_Ugain[ph];
    o["Ig"] = (uint32_t)s_b->cal_Igain[ph];
    o["Uo"] = (int32_t)s_b->cal_Uoffset[ph];
    o["Io"] = (int32_t)s_b->cal_Ioffset[ph];
    arr[ph]=o;
  }
  return arr;
}
static void calib_publish_cfg(){ ws_send("CalibCfg", calib_json_from_cfg()); }

// ================= Public API =================
void UI_begin(SimpleWebSerial* ws, UiBindings* b){
  s_ws = ws; s_b = b;
  s_ws->on("values",     UI_handleValues);
  s_ws->on("Config",     UI_handleUnifiedConfig);
  s_ws->on("RelaysSet",  UI_handleRelaysSet);
  s_ws->on("RelaysCfg",  UI_handleRelaysCfg);
  s_ws->on("AlarmsCfg",  UI_handleAlarmsCfg);
  s_ws->on("AlarmsAck",  UI_handleAlarmsAck);
  s_ws->on("CalibCfg",   UI_handleCalibCfg);
  ws_msg("UI ready");
}

JSONVar UI_MeterOptionsJSON() {
  JSONVar mo;
  mo["lineHz"] = *s_b->atm_lineFreqHz;
  mo["sumAbs"] = *s_b->atm_sumModeAbs;
  mo["ucal"]   = *s_b->atm_ucal;
  mo["sample_ms"] = *s_b->sample_ms;
  return mo;
}

void UI_handleValues(JSONVar values) {
  int addr = (int)values["mb_address"];
  int baud = (int)values["mb_baud"];
  addr = constrain(addr, 1, 255);
  baud = constrain(baud, 9600, 115200);
  if (s_b->cbApplyMB) s_b->cbApplyMB((uint8_t)addr, (uint32_t)baud);
  ws_msg("Modbus configuration updated");
}

void UI_handleUnifiedConfig(JSONVar obj) {
  const char* t = (const char*)obj["t"];
  if (!t) { ws_msg("Config: missing 't'"); return; }
  String type = String(t);
  bool changed = false, reinit = false;

  if (type == "meter") {
    JSONVar c = obj["cfg"];
    if (c.hasOwnProperty("lineHz")) {
      uint16_t lf = (uint16_t)constrain((int)c["lineHz"], 50, 60);
      if (lf != *s_b->atm_lineFreqHz) { *s_b->atm_lineFreqHz = lf; reinit = true; }
      changed = true;
    }
    if (c.hasOwnProperty("sumAbs")) {
      uint8_t  sm = (uint8_t)constrain((int)c["sumAbs"], 0, 1);
      if (sm != *s_b->atm_sumModeAbs) { *s_b->atm_sumModeAbs = sm; reinit = true; }
      changed = true;
    }
    if (c.hasOwnProperty("ucal")) {
      uint16_t uc = (uint16_t)constrain((int)c["ucal"], 1, 65535);
      *s_b->atm_ucal = uc;
      changed = true;
    }
    if (c.hasOwnProperty("sample_ms")) {
      int smp = (int)c["sample_ms"];
      *s_b->sample_ms = (uint16_t)constrain(smp, 10, 5000);
      changed = true;
    }
    if (reinit && s_b->cbMeterRebegin) s_b->cbMeterRebegin();
    if (changed) {
      MB_buildRegisterMap(*s_b->sample_ms, *s_b->atm_lineFreqHz, *s_b->atm_sumModeAbs, *s_b->atm_ucal);
      ws_msg("Meter options updated");
      if (s_b->cbSaveCfg) s_b->cbSaveCfg();
    }
  }
  else if (type == "leds") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      s_b->ledCfg[i].mode   = (uint8_t)constrain((int)list[i]["mode"],   0, 1);
      s_b->ledCfg[i].source = (uint8_t)constrain((int)list[i]["source"], 0, 25);
    }
    ws_msg("LEDs configuration updated");
    ws_send("LedConfigList", LedConfigListFromCfg());
    if (s_b->cbSaveCfg) s_b->cbSaveCfg();
  }
  else if (type == "buttons") {
    JSONVar list = obj["list"];
    for (int i=0;i<4 && i<list.length();i++) {
      int a = (int)list[i]["action"]; if (a < 0) a = 0; if (a > 8) a = 8;
      s_b->buttonCfg[i].action = (uint8_t)a;
    }
    ws_msg("Buttons configuration updated");
    ws_send("ButtonConfigList", ButtonConfigListFromCfg());
    if (s_b->cbSaveCfg) s_b->cbSaveCfg();
  }
  else {
    ws_msg((String("Unknown Config type: ") + t).c_str());
  }
}

void UI_handleRelaysSet(JSONVar v){
  bool changed=false;
  auto canWrite = [&](int idx)->bool{
    return (s_b->relay_mode[idx] != RM_ALARM) && !s_b->buttonOverrideMode[idx];
  };
  if (v.hasOwnProperty("list") && v["list"].length() >= 2) {
    bool r0 = (bool)v["list"][0];
    bool r1 = (bool)v["list"][1];
    if (canWrite(0) && s_b->relayState[0] != r0) { if (s_b->cbSetRelayPhys) s_b->cbSetRelayPhys(0, r0); changed=true; }
    if (canWrite(1) && s_b->relayState[1] != r1) { if (s_b->cbSetRelayPhys) s_b->cbSetRelayPhys(1, r1); changed=true; }
  } else if (v.hasOwnProperty("r1") || v.hasOwnProperty("r2")) {
    if (v.hasOwnProperty("r1")) { bool r0=(bool)v["r1"]; if (canWrite(0) && s_b->relayState[0]!=r0){ if (s_b->cbSetRelayPhys) s_b->cbSetRelayPhys(0,r0); changed=true; } }
    if (v.hasOwnProperty("r2")) { bool r1=(bool)v["r2"]; if (canWrite(1) && s_b->relayState[1]!=r1){ if (s_b->cbSetRelayPhys) s_b->cbSetRelayPhys(1,r1); changed=true; } }
  } else if (v.hasOwnProperty("idx")) {
    int idx = constrain((int)v["idx"], 0, 1);
    if (!canWrite(idx)) {
      ws_msg("RelaysSet: blocked (ALARM mode or Button Override)");
    } else if (v.hasOwnProperty("toggle") && (bool)v["toggle"]) {
      if (s_b->cbSetRelayPhys) s_b->cbSetRelayPhys(idx, !s_b->relayState[idx]); changed=true;
    } else if (v.hasOwnProperty("on")) {
      bool on = (bool)v["on"];
      if (s_b->relayState[idx] != on) { if (s_b->cbSetRelayPhys) s_b->cbSetRelayPhys(idx, on); changed=true; }
    }
  } else {
    ws_msg("RelaysSet: no-op (bad payload)");
  }
  if (changed) {
    if (s_b->cbEchoRelay) s_b->cbEchoRelay();
    ws_msg("Relays updated");
  }
}

void UI_handleRelaysCfg(JSONVar v){
  bool touched=false;
  if (v.hasOwnProperty("active_low")) { *s_b->relay_active_low = (uint8_t)constrain((int)v["active_low"],0,1); touched=true; }
  if (v.hasOwnProperty("def0")) { s_b->relay_default[0] = (bool)v["def0"]; touched=true; }
  if (v.hasOwnProperty("def1")) { s_b->relay_default[1] = (bool)v["def1"]; touched=true; }
  if (v.hasOwnProperty("mode") && v["mode"].length() >= 2) {
    for (int i=0;i<2;i++){ int m = (int)v["mode"][i]; if (m<RM_NONE) m=RM_NONE; if (m>RM_ALARM) m=RM_ALARM; s_b->relay_mode[i] = (uint8_t)m; }
    touched=true;
  }
  if (v.hasOwnProperty("alarm") && v["alarm"].length() >= 2) {
    for (int i=0;i<2;i++){
      JSONVar a = v["alarm"][i];
      if (a.hasOwnProperty("ch"))   { int ch = (int)a["ch"]; ch = constrain(ch, 0, CH_COUNT-1); s_b->relay_alarm_src[i].ch = (uint8_t)ch; }
      if (a.hasOwnProperty("mask")) { int mk = (int)a["mask"]; mk &= 0b111; s_b->relay_alarm_src[i].kindsMask = (uint8_t)mk; }
      else if (a.hasOwnProperty("kinds") && a["kinds"].length()>=3){
        uint8_t mk = ((bool)a["kinds"][0]?1:0) | ((bool)a["kinds"][1]?2:0) | ((bool)a["kinds"][2]?4:0);
        s_b->relay_alarm_src[i].kindsMask = mk;
      }
    }
    touched=true;
  }
  if (touched && s_b->cbSaveCfg) s_b->cbSaveCfg();
  ws_msg("RelaysCfg updated");
  if (s_b->cbEchoRelay) s_b->cbEchoRelay();
}

static void alarms_ack_channel(uint8_t ch){
  if (ch >= CH_COUNT) return;
  for (int k=0;k<AK_COUNT;++k) {
    AlarmRuntime& ar = s_b->alarmRt[ch*AK_COUNT+k];
    if (!ar.conditionNow) { ar.latched = false; ar.active  = false; }
  }
}

void UI_handleAlarmsCfg(JSONVar cfgArr){
  if (cfgArr.hasOwnProperty("ch") || cfgArr.hasOwnProperty("cfg")) {
    JSONVar v = cfgArr; if (!v.hasOwnProperty("ch")) return;
    int ch = constrain((int)v["ch"], 0, CH_COUNT-1);
    JSONVar payload = v.hasOwnProperty("cfg") ? v["cfg"] : v;

    bool changed=false;
    if (payload.hasOwnProperty("ack")) { s_b->alarmCfg[ch].ackRequired = (bool)payload["ack"]; changed = true; }
    for (int k=0;k<AK_COUNT;++k){
      String key = String(k); if (!payload.hasOwnProperty(key)) continue;
      JSONVar r = payload[key];
      if (r.hasOwnProperty("enabled")) s_b->alarmCfg[ch].rule[k].enabled = (bool)r["enabled"], changed=true;
      if (r.hasOwnProperty("min"))     s_b->alarmCfg[ch].rule[k].min     = (int32_t)r["min"], changed=true;
      if (r.hasOwnProperty("max"))     s_b->alarmCfg[ch].rule[k].max     = (int32_t)r["max"], changed=true;
      if (r.hasOwnProperty("metric"))  { int m=(int)r["metric"]; if(m<0)m=0; if(m>5)m=5; s_b->alarmCfg[ch].rule[k].metric=(uint8_t)m; changed=true; }
    }
    JSONVar chO; chO["ack"]=s_b->alarmCfg[ch].ackRequired;
    for (int k=0;k<AK_COUNT;++k){ JSONVar rr; rr["enabled"]=s_b->alarmCfg[ch].rule[k].enabled; rr["min"]=s_b->alarmCfg[ch].rule[k].min; rr["max"]=s_b->alarmCfg[ch].rule[k].max; rr["metric"]=s_b->alarmCfg[ch].rule[k].metric; chO[k]=rr; }
    JSONVar one; one[ch]=chO; ws_send("AlarmsCfg", one);
    alarms_publish_state();

    if (changed) {
      if (s_b->cbSaveCfg && s_b->cbSaveCfg()) ws_msg("AlarmsCfgCh saved");
      else                                     ws_msg("ERROR: saving AlarmsCfgCh failed");
    } else {
      ws_msg("AlarmsCfgCh: no changes");
    }
    return;
  }

  bool changed=false;
  for (int ch=0; ch<CH_COUNT && ch<cfgArr.length(); ++ch) {
    JSONVar c = cfgArr[ch];
    if (c.hasOwnProperty("ack")) { s_b->alarmCfg[ch].ackRequired = (bool)c["ack"]; changed = true; }
    for (int k=0; k<AK_COUNT; ++k) {
      if (!c.hasOwnProperty(String(k))) continue;
      JSONVar r = c[String(k)];
      if (r.hasOwnProperty("enabled")) s_b->alarmCfg[ch].rule[k].enabled = (bool)r["enabled"], changed=true;
      if (r.hasOwnProperty("min"))     s_b->alarmCfg[ch].rule[k].min     = (int32_t)r["min"], changed=true;
      if (r.hasOwnProperty("max"))     s_b->alarmCfg[ch].rule[k].max     = (int32_t)r["max"], changed=true;
      if (r.hasOwnProperty("metric"))  { int m = (int)r["metric"]; if (m < 0) m = 0; if (m > 5) m = 5; s_b->alarmCfg[ch].rule[k].metric = (uint8_t)m; changed=true; }
    }
  }
  alarms_publish_cfg();
  if (changed) {
    if (s_b->cbSaveCfg && s_b->cbSaveCfg()) ws_msg("AlarmsCfg saved");
    else                                     ws_msg("ERROR: saving AlarmsCfg failed");
  } else {
    ws_msg("AlarmsCfg: no changes");
  }
}

void UI_handleAlarmsAck(JSONVar v){
  bool any = false;
  if (v.hasOwnProperty("ch")) { int _ch = constrain((int)v["ch"], 0, CH_COUNT-1); alarms_ack_channel((uint8_t)_ch); any = true; }
  if (v.hasOwnProperty("list") && v["list"].length() >= CH_COUNT) {
    for (int ch = 0; ch < CH_COUNT; ++ch) if ((bool)v["list"][ch]) { alarms_ack_channel((uint8_t)ch); any = true; }
  }
  if (any) { ws_msg("Alarms acknowledged"); alarms_publish_state(); }
  else     { ws_msg("AlarmsAck: no-op"); }
}

// =============== Calibration cfg (WebSerial) ===============
static bool set_calib_phase_from_obj(int ph, JSONVar obj, bool &changed){
  if (ph < 0 || ph > 2) return false;
  bool any=false;
  if (obj.hasOwnProperty("Ug")) { uint32_t v=(uint32_t)obj["Ug"]; if (v>65535) v=65535; if (s_b->cal_Ugain[ph] != (uint16_t)v){ s_b->cal_Ugain[ph]=(uint16_t)v; any=true; } }
  if (obj.hasOwnProperty("Ig")) { uint32_t v=(uint32_t)obj["Ig"]; if (v>65535) v=65535; if (s_b->cal_Igain[ph] != (uint16_t)v){ s_b->cal_Igain[ph]=(uint16_t)v; any=true; } }
  if (obj.hasOwnProperty("Uo")) { int32_t v=(int32_t)obj["Uo"]; if (v<-32768) v=-32768; if (v>32767) v=32767; if (s_b->cal_Uoffset[ph] != (int16_t)v){ s_b->cal_Uoffset[ph]=(int16_t)v; any=true; } }
  if (obj.hasOwnProperty("Io")) { int32_t v=(int32_t)obj["Io"]; if (v<-32768) v=-32768; if (v>32767) v=32767; if (s_b->cal_Ioffset[ph] != (int16_t)v){ s_b->cal_Ioffset[ph]=(int16_t)v; any=true; } }
  changed |= any; return any;
}

void UI_handleCalibCfg(JSONVar payload){
  bool changed=false;
  if (String(JSON.typeof(payload)) == "array") {
    for (int ph=0; ph<3 && ph<payload.length(); ++ph){ JSONVar o = payload[ph]; set_calib_phase_from_obj(ph, o, changed); }
  } else if (payload.hasOwnProperty("ph")) {
    int ph = constrain((int)payload["ph"], 0, 2);
    if (payload.hasOwnProperty("cfg")) { JSONVar o = payload["cfg"]; set_calib_phase_from_obj(ph, o, changed); }
    else { set_calib_phase_from_obj(ph, payload, changed); }
  } else {
    ws_msg("CalibCfg: no-op (bad payload)"); calib_publish_cfg(); return;
  }
  if (changed) {
    if (s_b->cbSaveCfg && s_b->cbSaveCfg()) ws_msg("CalibCfg saved");
    else                                     ws_msg("ERROR: saving CalibCfg failed");
  } else ws_msg("CalibCfg: no changes");
  calib_publish_cfg();
}

// ================== 1 Hz push ==================
static void sendMeterEcho(const UiMeterCache& mc){
  JSONVar m;
  for (int i=0;i<3;i++){ m["Urms"][i]=mc.urms[i]; m["Irms"][i]=mc.irms[i]; }
  JSONVar pW, qVar, sVA;
  for (int i=0;i<4;i++){ pW[i]=mc.p_W[i]; qVar[i]=mc.q_var[i]; sVA[i]=mc.s_VA[i]; }
  m["P_W"]=pW; m["Q_var"]=qVar; m["S_VA"]=sVA;
  m["Ptot_W"]=mc.p_W[3]; m["Qtot_var"]=mc.q_var[3]; m["Stot_VA"]=mc.s_VA[3];
  JSONVar pfR; for(int i=0;i<4;i++) pfR[i]=((double)mc.pf_raw[i])/1000.0;
  m["PF"]=pfR; m["PFtot"]=((double)mc.pf_raw[3])/1000.0;
  JSONVar ang; for(int i=0;i<3;i++) ang[i]=((double)mc.ang_raw[i])/10.0;
  m["Angle_deg"]=ang;
  m["FreqHz"]=((double)mc.f_x100)/100.0; m["TempC"]=(int)mc.tempC;
  auto to_k = [](uint32_t Wh)->double{ return Wh/1000.0; };
  JSONVar E;
  for (int i=0;i<3;i++){ JSONVar ph; ph["AP_kWh"]=to_k(mc.e_ap_Wh[i]); ph["AN_kWh"]=to_k(mc.e_an_Wh[i]); ph["RP_kvarh"]=to_k(mc.e_rp_varh[i]); ph["RN_kvarh"]=to_k(mc.e_rn_varh[i]); ph["S_kVAh"]=to_k(mc.e_s_VAh[i]); E[i]=ph; }
  JSONVar tot; tot["AP_kWh"]=to_k(mc.e_ap_Wh[3]); tot["AN_kWh"]=to_k(mc.e_an_Wh[3]); tot["RP_kvarh"]=to_k(mc.e_rp_varh[3]); tot["RN_kvarh"]=to_k(mc.e_rn_varh[3]); tot["S_kVAh"]=to_k(mc.e_s_VAh[3]); E["tot"]=tot; m["E"]=E;
  JSONVar st;
  for (int ch=0; ch<CH_COUNT; ++ch) {
    JSONVar chO;
    for (int k=0;k<AK_COUNT;++k) {
      const AlarmRuntime& art = s_b->alarmRt[ch*AK_COUNT+k];
      JSONVar r; r["cond"]=art.conditionNow; r["active"]=art.active; r["latched"]=art.latched; chO[k]=r;
    }
    st[ch]=chO;
  }
  m["AlarmsState"]=st;
  ws_send("ENM_Meter", m);
}

void UI_onSecond(const UiMeterCache& mc,
                 const bool buttonState[4],
                 const bool ledStateList[4],
                 const bool /*relayState*/[2],
                 const M90DiagRegs& diag)
{
  if (!s_ws || !s_b) return;
  s_ws->check();
  sendStatusEcho();
  ws_send("LedConfigList", LedConfigListFromCfg());
  ws_send("ButtonConfigList", ButtonConfigListFromCfg());
  ws_send("RelaysCfg",       RelayConfigFromCfg());
  ws_send("MeterOptions",    UI_MeterOptionsJSON());
  alarms_publish_cfg(); calib_publish_cfg();
  if (mc.haveMeter) sendMeterEcho(mc);
  JSONVar btnState; for(int i=0;i<4;i++) btnState[i]=buttonState[i]; ws_send("ButtonStateList", btnState);
  JSONVar ledState; for(int i=0;i<4;i++) ledState[i]=ledStateList[i]; ws_send("LedStateList", ledState);
  if (s_b->cbEchoRelay) s_b->cbEchoRelay();
  alarms_publish_state();
  MB_setDiag(diag); // update Modbus diag regs 360..365
}

} // namespace enm223
