#
# Copyright (C) EdgeTX
#
# License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
#
"""Keep one simulator open and choose the next input from the log."""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from simctl import Simulator


def main():
    sim = Simulator(sim=sys.argv[1], storage=sys.argv[2], kind="telemetry")
    sim.start()
    try:
        sim.show_telemetry("telog")
        sim.key("PAGE")
        lines = sim.log_since()
        print("\n".join(lines))
        if any("key" in line for line in lines):
            sim.stick("thr", -100)
            print("\n".join(sim.log_since()))
    finally:
        sim.close()


if __name__ == "__main__":
    main()
