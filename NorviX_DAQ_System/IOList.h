/*  IOList.h  -  The node's I/O tag database. EDIT THIS TAB to match wiring.
 *  Each row maps a physical signal to a JSON key, unit and scaling.
 *  Runtime fields are left out of the initialisers (auto-zeroed).
 */
#ifndef IOLIST_H
#define IOLIST_H
#include "Config.h"

// ===========================================================================
//  LAYER 1 - ANALOGUE INPUTS   (AI4 / ADS1115)
//  { enabled, key, unit, module, ch, mode, engMin, engMax }
//  engMin/engMax map the 4-20 mA loop (or 0-10 V) to engineering units.
// ===========================================================================
AiChannel aiList[] = {
  { true, "spindle_load",  "%",   0, 0, AI_MODE_420MA,   0.0f, 100.0f },
  { true, "coolant_press", "bar", 0, 1, AI_MODE_420MA,   0.0f,  10.0f },
  { true, "hyd_press",     "bar", 0, 2, AI_MODE_420MA,   0.0f, 250.0f },
  { true, "ambient_temp",  "C",   0, 3, AI_MODE_420MA, -20.0f,  80.0f },
  // 2nd AI4 module (addr 0x49): set module = 1
  // { true, "vibration", "mm_s", 1, 0, AI_MODE_420MA, 0.0f, 50.0f },
};

// ===========================================================================
//  LAYER 2 - DIGITAL INPUTS   (DI16 / PCA9555)
//  { enabled, key, module, pin, invert }   invert = true for active-low
// ===========================================================================
DiChannel diList[] = {
  { true, "conveyor_run", 0, 0,  false },
  { true, "safety_gate",  0, 1,  false },
  { true, "part_present", 0, 2,  false },
  { true, "motor_ovld",   0, 3,  true  },   // active-low fault
  { true, "cycle_pulse",  0, 4,  false },   // rising edges -> "cnt"
  { true, "estop",        0, 5,  true  },
  { true, "di7",          0, 6,  false },
  { true, "di8",          0, 7,  false },
  { true, "di9",          0, 8,  false },
  { true, "di10",         0, 9,  false },
  { true, "di11",         0, 10, false },
  { true, "di12",         0, 11, false },
  { true, "di13",         0, 12, false },
  { true, "di14",         0, 13, false },
  { true, "di15",         0, 14, false },
  { true, "di16",         0, 15, false },
};

// ===========================================================================
//  LAYER 3 - RS-485 MODBUS RTU INSTRUMENTS
//  { enabled, key, unit, slaveId, fc, addr, dtype, wordSwap, scale, offset, pollMs }
//  Replace with your instrument's real register map.
// ===========================================================================
ModbusReg mbList[] = {
  { true, "active_power", "kW",  1, MB_FC_INPUT,   0x0034, MB_FLOAT32, false, 0.001f, 0, 1000 },
  { true, "power_factor", "",    1, MB_FC_INPUT,   0x003E, MB_FLOAT32, false, 1.0f,   0, 1000 },
  { true, "voltage_l1",   "V",   1, MB_FC_INPUT,   0x0000, MB_FLOAT32, false, 1.0f,   0, 1000 },
  { true, "flow_rate",    "m3h", 2, MB_FC_HOLDING, 0x0010, MB_U32,     false, 0.01f,  0, 2000 },
};

const uint8_t AI_COUNT = sizeof(aiList)/sizeof(aiList[0]);
const uint8_t DI_COUNT = sizeof(diList)/sizeof(diList[0]);
const uint8_t MB_COUNT = sizeof(mbList)/sizeof(mbList[0]);

#endif // IOLIST_H
