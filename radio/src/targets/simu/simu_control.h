/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 */

#pragma once

#include <string>

struct SimuControlConfig {
  enum class Mode { None, Stdio, Tcp } mode = Mode::None;
  std::string host = "127.0.0.1";
  int port = 8378;

  std::string describe() const;
};

// spec: "stdio" or "tcp://host:port" (host may be empty).
bool simuControlParse(const std::string& spec, SimuControlConfig& cfg,
                      std::string& error);

// Call before simuInit(). stdio mode stops debugPrintf() writing stdout.
void simuControlUseStdout(const SimuControlConfig& cfg);

// Blocks until stop, EOF, or the client disconnects. Returns 0.
int simuControlServe(const SimuControlConfig& cfg);

void simuControlRequestStop();
bool simuControlStopRequested();
