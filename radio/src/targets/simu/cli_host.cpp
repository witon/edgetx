/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Headless simulator host. Firmware tasks run as usual; this process
 * has no SDL window and speaks the JSON control protocol.
 */

#include "arg_parser.h"
#include "simu_control.h"
#include "simu_inputs.h"
#include "simulib.h"

#include <cstdio>
#include <filesystem>
#include <string>

void simuLcdNotify() {}

int main(int argc, char* argv[])
{
  auto progname = std::filesystem::path(argv[0]).filename();
  ArgumentParser args(progname.string());
  if (!args.parse(argc, argv)) return 1;
  if (args.isHelpRequested()) {
    args.printHelp();
    fprintf(stderr,
            "\nsimu-cli starts headless. --control defaults to stdio.\n");
    return 0;
  }

  SimuControlConfig cfg;
  std::string spec = args.hasControl() ? args.getControl() : "stdio";
  std::string error;
  if (!simuControlParse(spec, cfg, error)) {
    fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }

  simuControlUseStdout(cfg);
  int mode = simuInputsInferStickMode(args.getStoragePath());

  simuCreateDefaults();
  simuInit();
  simuInputsSetThrottleLow(mode);
  simuFatfsSetPaths(args.getStoragePath().c_str(),
                    args.getSettingsPath().c_str());
  simuStart(false);

  int rc = simuControlServe(cfg);
  simuStop();
  return rc;
}
