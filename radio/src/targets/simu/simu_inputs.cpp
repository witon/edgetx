/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#include "simu_inputs.h"

#include "dataconstants.h"
#include "edgetx.h"
#include "hal/adc_driver.h"
#include "input_mapping.h"
#include "simulib.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

namespace {

std::mutex g_mu;
uint16_t g_adc[MAX_ANALOG_INPUTS];

bool ieq(const char* a, const char* b)
{
  if (!a || !b) return false;
  while (*a && *b) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    ++a;
    ++b;
  }
  return *a == 0 && *b == 0;
}

#if defined(SURFACE_RADIO)
const uint8_t kThrottleLogical = 1;  // TH
struct StickName { const char* name; uint8_t logical; };
const StickName kSticks[] = {{"st", 0}, {"th", 1}, {"thr", 1}};
#else
const uint8_t kThrottleLogical = 2;  // THR, mode-1 order in input_mapping.cpp
struct StickName { const char* name; uint8_t logical; };
const StickName kSticks[] = {
    {"rud", 0}, {"ele", 1}, {"thr", 2}, {"ail", 3},
    {"rudder", 0}, {"elevator", 1}, {"throttle", 2}, {"aileron", 3},
};
#endif

uint16_t percentToAdc(int value)
{
  if (value < -100) value = -100;
  if (value > 100) value = 100;
  return (uint16_t)(((value + 100) * 4096) / 200);
}

}  // namespace

void simuInputsReset()
{
  std::lock_guard<std::mutex> lock(g_mu);
  for (unsigned i = 0; i < MAX_ANALOG_INPUTS; i++) g_adc[i] = 2048;
}

void simuInputsSetThrottleLow(int mode)
{
  simuInputsReset();
  if (mode < 0) mode = 0;
  if (mode > 3) mode = 3;
  uint8_t phys = inputMappingConvertMode((uint8_t)mode, kThrottleLogical);
  simuInputsSetAdc(phys, 0);
}

int simuInputsInferStickMode(const std::string& storagePath)
{
#if defined(DEFAULT_MODE)
  int mode = DEFAULT_MODE - 1;
#else
  int mode = 0;
#endif
  std::string path = storagePath.empty() ? std::string("RADIO/radio.yml")
                                         : storagePath + "/RADIO/radio.yml";
  std::ifstream file(path);
  if (!file) return mode;
  std::string line;
  while (std::getline(file, line)) {
    auto pos = line.find("stickMode:");
    if (pos == std::string::npos) continue;
    const char* rest = line.c_str() + pos + 10;
    while (*rest == ' ' || *rest == '\t') rest++;
    char* end = nullptr;
    long value = strtol(rest, &end, 10);
    if (end != rest && value >= 0 && value <= 3) return (int)value;
  }
  return mode;
}

uint16_t simuInputsGetAdc(uint8_t idx)
{
  std::lock_guard<std::mutex> lock(g_mu);
  if (idx >= MAX_ANALOG_INPUTS) return 0;
  return g_adc[idx];
}

void simuInputsSetAdc(uint8_t idx, uint16_t value)
{
  std::lock_guard<std::mutex> lock(g_mu);
  if (idx >= MAX_ANALOG_INPUTS) return;
  g_adc[idx] = value > 4096 ? 4096 : value;
}

bool simuInputsSetLogicalStick(const char* name, int value, const char** error)
{
  uint8_t logical = 0xFF;
  for (const auto& stick : kSticks) {
    if (ieq(stick.name, name)) {
      logical = stick.logical;
      break;
    }
  }
  if (logical == 0xFF) {
    if (error) *error = "unknown stick";
    return false;
  }
  uint8_t phys = inputMappingConvertMode(logical);
  simuInputsSetAdc(phys, percentToAdc(value));
  return true;
}

bool simuInputsSetPot(int idx, int value, const char** error)
{
  auto sticks = adcGetMaxInputs(ADC_INPUT_MAIN);
  auto pots = adcGetMaxInputs(ADC_INPUT_FLEX);
  if (idx < 0 || idx >= (int)pots) {
    if (error) *error = "pot index out of range";
    return false;
  }
  uint16_t adc;
  switch (getPotType(idx)) {
    case FLEX_MULTIPOS:
      if (value < 0) value = 0;
      if (value > 5) value = 5;
      adc = (uint16_t)((uint32_t)value * 4096 / 5);
      break;
    default:
      adc = percentToAdc(value);
      break;
  }
  simuInputsSetAdc((uint8_t)(sticks + idx), adc);
  return true;
}

uint16_t simuGetAnalog(uint8_t idx)
{
  return simuInputsGetAdc(idx);
}
