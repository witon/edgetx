/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <stdint.h>

// Write an RGB888 image (row-major, 3 bytes per pixel) as a PNG file.
bool simuWriteRgbPng(const char* path, const uint8_t* rgb, int width, int height);
