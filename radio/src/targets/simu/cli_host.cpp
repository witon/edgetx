/*
 * Copyright (C) EdgeTX
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * Simulator host. Firmware tasks run as usual. The process speaks the
 * JSON control protocol and stays headless unless --screen is set.
 */

#include "arg_parser.h"
#include "simu_control.h"
#include "simu_inputs.h"
#include "simulib.h"

#if defined(SIMU_CLI_SCREEN)
#include "cli_screen.h"
#include <thread>
#endif

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
            "\nsimu-cli starts headless. --screen shows the LCD. "
            "--control defaults to stdio.\n");
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
  if (args.hasScreen()) {
#if defined(SIMU_CLI_SCREEN)
    if (!cliScreenInit()) return 1;
#else
    fprintf(stderr,
            "--screen requires SDL2, which was not found at build time\n");
    return 1;
#endif
  }

  int mode = simuInputsInferStickMode(args.getStoragePath());

  simuCreateDefaults();
  simuInit();
  simuInputsSetThrottleLow(mode);
  simuFatfsSetPaths(args.getStoragePath().c_str(),
                    args.getSettingsPath().c_str());
  simuStart(false);

  int rc = 0;
#if defined(SIMU_CLI_SCREEN)
  if (args.hasScreen()) {
    std::thread controlThread([cfg] { simuControlServe(cfg); });
    cliScreenRun();
    simuControlRequestStop();
    if (controlThread.joinable()) controlThread.join();
    cliScreenShutdown();
  } else
#endif
  {
    rc = simuControlServe(cfg);
  }
  simuStop();
  return rc;
}
