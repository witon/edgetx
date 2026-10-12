/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Optional LCD window for simu-cli. Compiled only when SDL2 is available.
 */

#pragma once

// Creates the LCD window. On failure, prints to stderr and returns false.
bool cliScreenInit();

// Presents frames until the window closes or the control server stops.
void cliScreenRun();

void cliScreenShutdown();
