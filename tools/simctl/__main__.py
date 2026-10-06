"""Command line for the simulator control client.

Run from the tools directory:

    python -m simctl test --kind telemetry --sim path/to/simu-cli --storage ./sd --script telem --expect "telem ready"
"""

import argparse
import json
import sys
import time

if __package__ in (None, ""):
    import os

    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from simctl.client import Simulator, log_has_error
else:
    from .client import Simulator, log_has_error


def _build_parser():
    parser = argparse.ArgumentParser(prog="simctl")
    sub = parser.add_subparsers(dest="command", required=True)

    test = sub.add_parser("test", help="open a telemetry script or widget and judge the simulator log")
    test.add_argument("--kind", required=True, choices=("telemetry", "widget"))
    test.add_argument("--sim", required=True, help="simu-cli executable for this firmware")
    test.add_argument("--storage", default="", help="SD card directory")
    test.add_argument("--settings", default="", help="settings directory")
    test.add_argument("--script", required=True, help="telemetry filename or widget name")
    test.add_argument("--expect", action="append", default=[], help="text that must appear in the simulator log")
    test.add_argument("--key", action="append", default=[], help="key click, in order")
    test.add_argument("--stick", action="append", default=[], help="name=value, in order")
    test.add_argument("--timeout", type=float, default=5.0, help="seconds to wait for the expected log text")
    return parser


def _apply_inputs(sim, args):
    for spec in args.stick:
        if "=" not in spec:
            raise SystemExit("stick must be name=value")
        name, value = spec.split("=", 1)
        sim.stick(name, int(value))
    for key in args.key:
        sim.key(key)


def _run_test(args):
    sim = Simulator(sim=args.sim, storage=args.storage, settings=args.settings, kind=args.kind)
    lines = []
    try:
        sim.start()
        if args.kind == "telemetry":
            sim.show_telemetry(args.script, timeout=int(args.timeout * 1000))
        else:
            sim.show_widget(args.script, timeout=int(args.timeout * 1000))
        _apply_inputs(sim, args)
        deadline = time.monotonic() + args.timeout
        while True:
            lines.extend(sim.log_since())
            error = log_has_error(lines)
            if error:
                _emit(False, error, lines)
                return 1
            if all(any(text in line for line in lines) for text in args.expect):
                _emit(True, "", lines)
                return 0
            if time.monotonic() >= deadline:
                _emit(False, "timed out waiting for expected log text", lines)
                return 1
            time.sleep(0.05)
    except Exception as exc:
        response = getattr(exc, "response", {}) or {}
        lines.extend(response.get("lines") or [])
        try:
            if sim._proc or sim._sock:
                lines.extend(sim.log_since())
        except Exception:
            pass
        _emit(False, str(exc), lines)
        return 1
    finally:
        sim.close()


def _emit(ok, error, lines):
    payload = {"ok": ok, "log": lines}
    if error:
        payload["error"] = error
    json.dump(payload, sys.stdout, ensure_ascii=False)
    sys.stdout.write("\n")


def main(argv=None):
    parser = _build_parser()
    args = parser.parse_args(argv)
    if args.command == "test":
        return _run_test(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
