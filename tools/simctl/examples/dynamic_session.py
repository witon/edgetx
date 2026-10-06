"""Keep one simulator open and choose the next input from the log.

Run from the tools directory after simu-cli has been built:

    python simctl/examples/dynamic_session.py --sim path/to/simu-cli --storage ./sd
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from simctl.client import Simulator, log_has_error


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", required=True)
    parser.add_argument("--storage", required=True)
    parser.add_argument("--script", default="telem")
    args = parser.parse_args()

    with Simulator(sim=args.sim, storage=args.storage) as sim:
        sim.start()
        sim.show_telemetry(args.script)
        lines = sim.log_since()
        if log_has_error(lines):
            print("\n".join(lines))
            return 1
        if any("telem ready" in line for line in lines):
            sim.key("PAGE")
            lines = sim.log_since()
        print("\n".join(lines))
        return 1 if log_has_error(lines) else 0


if __name__ == "__main__":
    sys.exit(main())
