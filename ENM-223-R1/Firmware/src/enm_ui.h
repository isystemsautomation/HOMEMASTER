#pragma once
#include <Arduino.h>
#include <Arduino_JSON.h>
#include <SimpleWebSerial.h>

namespace enm223 {
  struct M90DiagRegs; // defined in atm90e32.h
}

// ================= UI PUBLIC API =================
namespace enm223 {

// Mirror of small app structs
struct LedCfg   { uint8_t mode; uint8_t source; };
struct ButtonCfg{ uint8_t action; };

enum : uint8_t { CH_L1=0, CH_L2, CH_L3, CH_TOT, CH_COUNT };
enum : uint8_t { AK_ALARM=0, AK_WARNING, AK_EVENT, AK_COUNT };
enum AlarmMetric : uint8_t { AM_VOLTAGE=0, AM_CURRENT, AM_P_ACTIVE, AM_Q_REACTIVE, AM_S_APPARENT, AM_FREQ };

struct AlarmRule { bool enabled; int32_t min; int32_t max; uint8_t metric; };
struct AlarmRuntime { bool conditionNow; bool active; bool latched; };
struct ChannelAlarmCfg { bool ackRequired; AlarmRule rule[AK_COUNT]; };

enum : uint8_t { RM_NONE=0, RM_MODBUS=1, RM_ALARM=2 };
struct RelayAlarmSrc { uint8_t ch; uint8_t kindsMask; }; // bit0=Alarm,1=Warn,2=Event

// Callbacks into sketch
typedef void (*FnApplyModbusSettings)(uint8_t addr, uint32_t baud);
typedef void (*FnMeterRebegin)();
typedef void (*FnEchoRelayState)();
typedef bool (*FnSaveConfigFS)();
typedef void (*FnSetRelayPhys)(uint8_t idx, bool on);

// Bindings (addresses/pointers to your sketch data & callbacks)
struct UiBindings {
  // Modbus
  uint8_t*   mb_address;
  uint32_t*  mb_baud;

  // Timing
  uint16_t*  sample_ms;

  // Meter options
  uint16_t*  atm_lineFreqHz;
  uint8_t*   atm_sumModeAbs;
  uint16_t*  atm_ucal;

  // Calibration (3 phases)
  uint16_t*  cal_Ugain;
  uint16_t*  cal_Igain;
  int16_t*   cal_Uoffset;
  int16_t*   cal_Ioffset;

  // LEDs/Buttons
  LedCfg*    ledCfg;       // [4]
  ButtonCfg* buttonCfg;    // [4]
  bool*      ledManual;    // [4]
  bool*      buttonState;  // [4]

  // Relays
  uint8_t*      relay_active_low;
  bool*         relay_default;       // [2]
  uint8_t*      relay_mode;          // [2]
  RelayAlarmSrc* relay_alarm_src;    // [2]
  bool*         relayState;          // [2]
  bool*         buttonOverrideMode;  // [2]
  bool*         buttonOverrideState; // [2]

  // Alarms
  ChannelAlarmCfg* alarmCfg;         // [CH_COUNT]
  AlarmRuntime*    alarmRt;          // flattened [CH_COUNT*AK_COUNT]

  // WebSerial status payload owned by sketch
  JSONVar* modbusStatus;

  // Callbacks
  FnApplyModbusSettings cbApplyMB;
  FnMeterRebegin        cbMeterRebegin;
  FnEchoRelayState      cbEchoRelay;
  FnSaveConfigFS        cbSaveCfg;
  FnSetRelayPhys        cbSetRelayPhys;
};

// Meter snapshot passed once per second
struct UiMeterCache {
  bool      haveMeter;
  double    urms[3];
  double    irms[3];
  int32_t   p_W[4];
  int32_t   q_var[4];
  int32_t   s_VA[4];
  int16_t   pf_raw[4];
  int16_t   ang_raw[3];
  uint16_t  f_x100;
  int16_t   tempC;
  uint32_t  e_ap_Wh[4], e_an_Wh[4], e_rp_varh[4], e_rn_varh[4], e_s_VAh[4];
};

// Init UI layer + register handlers
void UI_begin(SimpleWebSerial* ws, UiBindings* b);

// Send the 1 Hz UI bundle and update diag regs
void UI_onSecond(const UiMeterCache& mc,
                 const bool buttonState[4],
                 const bool ledStateList[4],
                 const bool relayState[2],
                 const M90DiagRegs& diag);

// Handlers (auto-registered by UI_begin)
void UI_handleValues(JSONVar values);
void UI_handleUnifiedConfig(JSONVar obj);
void UI_handleRelaysSet(JSONVar v);
void UI_handleRelaysCfg(JSONVar v);
void UI_handleAlarmsCfg(JSONVar cfgArr);
void UI_handleAlarmsAck(JSONVar v);
void UI_handleCalibCfg(JSONVar payload);

// Utility
JSONVar UI_MeterOptionsJSON();

} // namespace enm223
