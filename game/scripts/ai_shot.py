#!/usr/bin/env python3
"""Minekampf AI vision helper: connect to the in-game automation API,
teleport, aim the camera, capture a frame, and print game state.

Usage:
  ai_shot.py status
  ai_shot.py shot <out.bmp> [--x X --y Y --z Z --yaw DEG --pitch DEG]
"""
import argparse
import json
import socket
import sys
import time

PORT = 25568


class Api:
    def __init__(self, port=PORT):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(10)
        welcome = self.sock.recv(4096).decode()
        print(f"[api] {welcome.strip()}")

    def cmd(self, payload: dict) -> dict:
        self.sock.sendall((json.dumps(payload) + "\n").encode())
        buf = b""
        while b"\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            buf += chunk
        return json.loads(buf.decode().strip())

    def state(self):
        return self.cmd({"cmd": "get_state"})

    def exec(self, line: str):
        return self.cmd({"cmd": "exec", "command": line})

    def look(self, yaw=None, pitch=None):
        p = {"cmd": "look"}
        if yaw is not None:
            p["yaw"] = yaw
        if pitch is not None:
            p["pitch"] = pitch
        return self.cmd(p)

    def shot(self, filename):
        return self.cmd({"cmd": "screenshot", "filename": filename})

    def open_settings(self):
        return self.cmd({"cmd": "ui", "panel": "settings"})


def wait_playing(api, timeout=60):
    t0 = time.time()
    while time.time() - t0 < timeout:
        st = api.state()
        if st.get("state") == "playing":
            return st
        time.sleep(0.5)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("action", choices=["status", "shot", "exec", "settings"])
    ap.add_argument("outfile", nargs="?")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--x", type=float)
    ap.add_argument("--y", type=float)
    ap.add_argument("--z", type=float)
    ap.add_argument("--yaw", type=float)
    ap.add_argument("--pitch", type=float)
    ap.add_argument("--no-wait", action="store_true", help="don't wait for playing state (menu shots)")
    args = ap.parse_args()

    api = Api(args.port)
    if args.no_wait:
        time.sleep(1.5)
        st = api.state()
    else:
        st = wait_playing(api)
    if not st:
        print("[-] game not in playing state", file=sys.stderr)
        return 1
    print("[state]", json.dumps(st))

    if args.action == "status":
        return 0

    if args.action == "exec":
        if not args.outfile:
            print("[-] exec needs the command string", file=sys.stderr)
            return 1
        print("[exec]", api.exec(args.outfile))
        return 0

    if args.action == "settings":
        print("[ui]", api.open_settings())
        return 0

    if args.x is not None:
        print("[tp]", api.exec(f"/tp {args.x} {args.y} {args.z}"))
        time.sleep(1.0)  # let chunks stream in
    if args.yaw is not None or args.pitch is not None:
        print("[look]", api.look(args.yaw, args.pitch))
        time.sleep(0.3)
    print("[shot]", api.shot(args.outfile))
    return 0


if __name__ == "__main__":
    sys.exit(main())
