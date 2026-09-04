#!/usr/bin/env python3
"""Performance sampling harness for Minekampf.

Launches the game, teleports through representative scenes and samples the
automation API (mspt, plus per-second frame cadence) into a markdown report.
Flags scenes whose mspt exceeds the 50 ms tick budget.

Usage: python scripts/perf_test.py [--out report.md] [--port 25598]
"""

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

GAME_DIR = Path(__file__).resolve().parent.parent
EXE = GAME_DIR / "build" / "release" / "bin" / "minekampf.exe"


class Client:
    def __init__(self, port):
        self.port = port
        self.sock = None

    def _connect(self, timeout=5.0):
        if self.sock is not None:
            return
        s = socket.create_connection(("127.0.0.1", self.port), timeout=timeout)
        s.settimeout(timeout)
        self.sock = s
        self._readline()  # banner

    def _readline(self):
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = self.sock.recv(65536)
            if not chunk:
                break
            buf += chunk
        return buf

    def rpc(self, payload, timeout=15.0):
        self._connect(timeout)
        self.sock.settimeout(timeout)
        self.sock.sendall((json.dumps(payload) + "\n").encode())
        return json.loads(self._readline().decode(errors="replace"))

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


def wait_playing(api, timeout=90.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            st = api.rpc({"cmd": "get_state"}, timeout=5.0)
            if st.get("state") == "playing":
                return st
            api.close()
        except (ConnectionRefusedError, socket.timeout, OSError, json.JSONDecodeError):
            api.close()
        time.sleep(1.0)
    raise RuntimeError("game did not reach playing state")


SCENES = [
    ("spawn_noon", ["/time 0.5", "/gamemode creative"], 8.0),
    ("water", ["/goto water", "/fly"], 8.0),
    ("underground", ["/tpdungeon"], 8.0),
    ("nether", ["/tpnether"] if False else ["/setblock 6 70 6 nether_portal", "/tp 6 71 6"], 10.0),
    ("night_surface", ["/gamemode creative", "/tp 6 75 6", "/time 0.0"], 8.0),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(GAME_DIR / "build" / "perf_report.md"))
    ap.add_argument("--port", type=int, default=25598)
    args = ap.parse_args()

    subprocess.run(["taskkill", "/f", "/im", "minekampf.exe"], capture_output=True)
    time.sleep(1.0)
    proc = subprocess.Popen([str(EXE), "--auto-play", "--automation-port", str(args.port)],
                            cwd=str(GAME_DIR), stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    rows = []
    api = Client(args.port)
    try:
        wait_playing(api)
        for name, setup, settle in SCENES:
            for cmd in setup:
                try:
                    api.rpc({"cmd": "exec", "command": cmd})
                except (OSError, json.JSONDecodeError):
                    pass
            time.sleep(settle)
            samples = []
            for _ in range(10):
                try:
                    st = api.rpc({"cmd": "get_state"}, timeout=5.0)
                    samples.append((st.get("mspt", 999.0), st.get("chunks", 0),
                                    st.get("meshes", 0)))
                except (OSError, json.JSONDecodeError):
                    pass
                time.sleep(0.5)
            if samples:
                mspts = [s[0] for s in samples]
                rows.append((name, min(mspts), sum(mspts) / len(mspts), max(mspts),
                             samples[-1][1], samples[-1][2]))
            else:
                rows.append((name, -1, -1, -1, 0, 0))
    finally:
        api.close()
        subprocess.run(["taskkill", "/f", "/im", "minekampf.exe"], capture_output=True)

    lines = [
        "# Minekampf performance report",
        "",
        f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        "| Scene | mspt min | mspt avg | mspt max | chunks | meshes | Status |",
        "|---|---|---|---|---|---|---|",
    ]
    failed = 0
    for name, mn, avg, mx, chunks, meshes in rows:
        ok = 0 <= avg < 50.0
        if not ok:
            failed += 1
        lines.append(f"| {name} | {mn:.1f} | {avg:.1f} | {mx:.1f} | {chunks} | {meshes} "
                     f"| {'OK' if ok else 'OVER BUDGET'} |")
    report = "\n".join(lines) + "\n"
    Path(args.out).write_text(report, encoding="utf-8")
    print(report)
    print(f"report: {args.out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
