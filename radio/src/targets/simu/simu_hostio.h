/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <atomic>
#include <string>

// Socket and console IO for the headless host. Kept out of the firmware
// translation units so windows.h does not meet FatFs or the min/max helpers.

bool simuHostNetStartup(std::string& error);
void simuHostNetShutdown();

bool simuHostListen(const std::string& host, int port, std::string& error);
void simuHostCloseListen();

bool simuHostAccept(const std::atomic<bool>& stop);
void simuHostCloseClient();

bool simuHostSendLine(const std::string& line);
bool simuHostReadClientLine(std::string& line, bool& closed, const std::atomic<bool>& stop);
bool simuHostReadStdioLine(std::string& line, bool& closed, const std::atomic<bool>& stop);
