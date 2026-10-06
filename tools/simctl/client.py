"""JSON-line client for the EdgeTX simulator control protocol."""

import json
import socket
import subprocess
import threading
import time


ERROR_MARKERS = ("-E- ", "Error loading script", "Error parsing script")


class SimulatorError(RuntimeError):
    def __init__(self, message, response=None):
        super().__init__(message)
        self.response = response or {}


class Simulator:
    """One running simulator.

    ``start()`` launches simu-cli and keeps it open. Further calls send one
    command each and return that command's response. ``log_since()`` returns
    only the debug lines printed after the previous read.
    """

    def __init__(self, sim=None, storage="", settings="", kind=None):
        self.sim = sim
        self.storage = storage
        self.settings = settings
        self.kind = kind
        self._proc = None
        self._sock = None
        self._stderr = []
        self._next_id = 1
        self._cursor = 0

    def start(self):
        if not self.sim:
            raise SimulatorError("sim executable is required")
        cmd = [self.sim, "--control", "stdio"]
        if self.storage:
            cmd.extend(["--storage", self.storage])
        if self.settings:
            cmd.extend(["--settings", self.settings])
        self._proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        threading.Thread(target=self._drain_stderr, daemon=True).start()
        ready = self._readline_proc()
        if not ready:
            try:
                self._proc.wait(timeout=2)
            except Exception:
                pass
            time.sleep(0.05)
            err = "".join(self._stderr)
            raise SimulatorError("simulator exited before ready: " + err)
        message = json.loads(ready)
        if message.get("event") != "ready":
            raise SimulatorError("unexpected simulator greeting", message)
        self._sync_log_cursor()
        return self

    def connect(self, url):
        host, port = _parse_tcp(url)
        self._sock = socket.create_connection((host, port))
        self._sync_log_cursor()
        return self

    def close(self):
        try:
            if self._proc or self._sock:
                self.request("stop")
        except Exception:
            pass
        if self._proc:
            try:
                self._proc.stdin.close()
            except Exception:
                pass
            try:
                self._proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self._proc.kill()
            self._proc = None
        if self._sock:
            try:
                self._sock.close()
            except Exception:
                pass
            self._sock = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False

    def request(self, cmd, **fields):
        message = {"id": self._next_id, "cmd": cmd}
        self._next_id += 1
        message.update(fields)
        line = json.dumps(message, ensure_ascii=False) + "\n"
        if self._proc:
            self._proc.stdin.write(line.encode("utf-8"))
            self._proc.stdin.flush()
            raw = self._readline_proc()
        elif self._sock:
            self._sock.sendall(line.encode("utf-8"))
            raw = self._readline_sock()
        else:
            raise SimulatorError("simulator is not started")
        if not raw:
            raise SimulatorError("simulator closed the control channel")
        response = json.loads(raw)
        if response.get("ok") is False:
            raise SimulatorError(response.get("error") or response.get("code") or "command failed", response)
        return response

    def key(self, name, action="click"):
        return self.request("key", name=name, action=action)

    def switch(self, name, state):
        return self.request("switch", name=name, state=state)

    def stick(self, name, value):
        return self.request("stick", name=name, value=int(value))

    def pot(self, index, value):
        return self.request("pot", index=int(index), value=int(value))

    def analog(self, index, value):
        return self.request("analog", index=int(index), value=int(value))

    def trim(self, index, action="click", value=None):
        if value is not None:
            return self.request("trim", index=int(index), value=int(value))
        return self.request("trim", index=int(index), action=action)

    def encoder(self, steps):
        return self.request("encoder", steps=int(steps))

    def touch(self, action, x=None, y=None):
        fields = {"action": action}
        if x is not None:
            fields["x"] = int(x)
        if y is not None:
            fields["y"] = int(y)
        return self.request("touch", **fields)

    def voltage(self, value):
        return self.request("voltage", value=int(value))

    def wait(self, ms):
        return self.request("wait", ms=int(ms))

    def screenshot(self, path):
        return self.request("screenshot", path=path)

    def channels(self):
        return self.request("channels").get("channels", [])

    def show_telemetry(self, name, timeout=5000):
        return self.request("show_telemetry", name=name, timeout=int(timeout))

    def show_widget(self, name, timeout=5000):
        return self.request("show_widget", name=name, timeout=int(timeout))

    def reload_lua(self):
        return self.request("reload_lua")

    def log(self, since=0):
        response = self.request("log", since=int(since))
        self._cursor = int(response.get("next", self._cursor))
        return response.get("lines", [])

    def log_since(self):
        return self.log(self._cursor)

    def wait_log(self, text, timeout=5.0):
        """Return new lines once one of them contains ``text``."""
        deadline = time.monotonic() + timeout
        seen = []
        while True:
            seen.extend(self.log_since())
            if any(text in line for line in seen):
                return seen
            if time.monotonic() >= deadline:
                raise SimulatorError("timed out waiting for log text: " + text, {"lines": seen})
            time.sleep(0.05)

    def _readline_proc(self):
        raw = self._proc.stdout.readline()
        if not raw:
            return ""
        return raw.decode("utf-8", errors="replace")

    def _drain_stderr(self):
        if not self._proc or not self._proc.stderr:
            return
        for raw in self._proc.stderr:
            self._stderr.append(raw.decode("utf-8", errors="replace"))

    def _sync_log_cursor(self):
        response = self.request("log", since=0)
        self._cursor = int(response.get("next", 0))

    def _readline_sock(self):
        chunks = []
        while True:
            byte = self._sock.recv(1)
            if not byte:
                break
            if byte == b"\n":
                break
            chunks.append(byte)
        return b"".join(chunks).decode("utf-8")


def _parse_tcp(url):
    text = url
    if text.startswith("tcp://"):
        text = text[6:]
    if ":" not in text:
        raise SimulatorError("control url must be tcp://host:port")
    host, port = text.rsplit(":", 1)
    if not host:
        host = "127.0.0.1"
    return host, int(port)


def log_has_error(lines):
    for line in lines:
        if any(marker in line for marker in ERROR_MARKERS):
            return line
    return None
