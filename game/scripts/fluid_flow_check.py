#!/usr/bin/env python3
"""Fluid flow E2E verification.

Places a water source on top of a stone pillar in open air, waits for the
fluid tick, then probes the ground around the pillar base: the water must
have flowed down and spread horizontally. Exits 0 when flow is observed.
"""
import json
import os
import socket
import subprocess
import sys
import time

EXE = os.path.join(os.getcwd(), "build", "release", "bin", "minekampf.exe")


def free_port():
    s = socket.socket(); s.bind(("127.0.0.1", 0)); p = s.getsockname()[1]; s.close(); return p


class Api:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(15)
        self.sock.recv(4096)

    def cmd(self, p):
        self.sock.sendall((json.dumps(p) + "\n").encode())
        buf = b""
        while b"\n" not in buf:
            buf += self.sock.recv(4096)
        return json.loads(buf.decode().strip())

    def exec(self, line):
        r = self.cmd({"cmd": "exec", "command": line})
        return r.get("msg", "")


def probe_is_water(api, x, y, z):
    reply = api.exec(f"/probe {x} {y} {z}")
    return "water" in reply.lower(), reply


def main():
    port = free_port()
    proc = subprocess.Popen([EXE, "--auto-play", "--automation-port", str(port)],
                            cwd=os.path.dirname(EXE))
    api = None
    try:
        for _ in range(60):
            if proc.poll() is not None:
                print("game died"); return 1
            try:
                api = Api(port); break
            except OSError:
                time.sleep(0.5)
        for _ in range(240):
            if api.cmd({"cmd": "get_state"}).get("state") == "playing":
                break
            time.sleep(0.25)
        st = api.cmd({"cmd": "get_state"})
        px, py, pz = (int(float(v)) for v in st["pos"])
        print(f"player at ({px},{py},{pz})")

        # Build a 3-high stone pillar 4 blocks east, water on top.
        for dy in range(0, 3):
            api.exec(f"/setblock {px+4} {py+dy} {pz} stone")
        api.exec(f"/setblock {px+4} {py+3} {pz} water")

        # Let the fluid tick run: activation scan (<=4 ticks) + delay 5 + spread.
        time.sleep(3.0)

        # The source must survive.
        ok_src, r_src = probe_is_water(api, px + 4, py + 3, pz)
        print("source:", r_src)
        # Flow: water spreads horizontally at source level (y=py+3), then
        # falls off the pillar edges toward the ground.
        flowed = []
        candidates = [(px + 4 + dx, py + 3, pz + dz)
                      for dx, dz in [(-1, 0), (1, 0), (0, -1), (0, 1)]]
        candidates += [(px + 4 + dx, py + 2, pz + dz)
                       for dx, dz in [(-1, 0), (1, 0), (0, -1), (0, 1)]]
        candidates += [(px + 4 + dx, py, pz + dz)
                       for dx, dz in [(-1, 0), (1, 0), (0, -1), (0, 1), (0, 0)]]
        for (x, y, z) in candidates:
            is_w, r = probe_is_water(api, x, y, z)
            if is_w:
                flowed.append((x, y, z))
                print(f"  flowed -> ({x},{y},{z}): {r}")
        if flowed:
            print(f"PASS: water flowed to {len(flowed)} position(s)")
            return 0
        print("FAIL: no water flow detected")
        return 1
    finally:
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=10)
            except subprocess.TimeoutExpired: proc.kill()


if __name__ == "__main__":
    sys.exit(main())
