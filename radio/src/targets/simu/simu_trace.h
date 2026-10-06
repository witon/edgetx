/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <stdint.h>
#include <string>
#include <vector>

// Line-oriented copy of the firmware debugPrintf() stream.
uint32_t simuTraceCursor();
void simuTraceRead(uint32_t since, std::vector<std::string>& lines, uint32_t& next);
