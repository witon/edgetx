/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <stdint.h>
#include <string>

// Shared stick and pot state. The SDL widgets and the control protocol both
// write it; simuGetAnalog() is the only thing the firmware reads.

void simuInputsReset();
// mode is g_eeGeneral.stickMode (0..3). Call after simuInit().
void simuInputsSetThrottleLow(int mode);
int simuInputsInferStickMode(const std::string& storagePath);

uint16_t simuInputsGetAdc(uint8_t idx);
void simuInputsSetAdc(uint8_t idx, uint16_t value);

// Logical stick name (ail/ele/thr/rud, or st/th) and value -100..100.
bool simuInputsSetLogicalStick(const char* name, int value, const char** error);
bool simuInputsSetPot(int idx, int value, const char** error);
