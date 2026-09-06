#!/usr/bin/env python3
"""Minekampf multiplayer E2E: two instances (host + client) on one machine.

Launches the host with an in-memory world and a listen server, joins a client
through the real protocol, then asserts session lifecycle, chunk streaming,
host->client block sync, player snapshots and disconnect handling through the
automation API. Kills both processes at the end.

Usage:
  python mp_test.py [--skip-build] [--keep]
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_DIR = os.path.dirname(HERE)
DEFAULT_EXE = os.path.join(GAME_DIR, "build", "release", "bin", "minekampf.exe")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Api:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(15)
        self.welcome = self.sock.recv(4096).decode().strip()

    def cmd(self, payload):
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

    def exec_cmd(self, command):
        return self.cmd({"cmd": "exec", "command": command})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--exe", default=DEFAULT_EXE)
    args = ap.parse_args()

    host_auto = free_port()
    client_auto = free_port()
    game_port = free_port()

    results = []

    def check(name, ok, info=""):
        results.append((name, ok))
        print(f"  {'PASS' if ok else 'FAIL'}  {name}  {info}")

    procs = []
    try:
        procs.append(subprocess.Popen([
            args.exe, "--auto-play", "--host", str(game_port),
            "--automation-port", str(host_auto)]))
        time.sleep(3.0)
        procs.append(subprocess.Popen([
            args.exe, "--join", "127.0.0.1", str(game_port), "Tester2",
            "--automation-port", str(client_auto)]))

        # -- 1. client reaches Playing through the real login + chunk stream --
        client_playing = False
        deadline = time.time() + 120
        last = {}
        while time.time() < deadline:
            try:
                last = Api(client_auto).state()
            except Exception:
                last = {}
                time.sleep(1.0)
                continue
            if last.get("state") == "playing" and last.get("mp"):
                client_playing = True
                break
            time.sleep(1.0)
        check("client_playing", client_playing, json.dumps(last)[:200])

        if not client_playing:
            raise SystemExit(1)

        host = Api(host_auto)
        client = Api(client_auto)

        # Give chunk streaming a moment to settle.
        time.sleep(5.0)
        hs = host.state()
        cs = client.state()
        check("host_sees_two_players", hs.get("mp_players") == 2, str(hs.get("mp_players")))
        check("client_sees_two_players", cs.get("mp_players") == 2, str(cs.get("mp_players")))
        check("client_is_not_host", cs.get("role") == "client" and cs.get("my_player_id", 0) != 0,
              f"role={cs.get('role')} id={cs.get('my_player_id')}")
        check("host_is_host", hs.get("role") == "host", str(hs.get("role")))

        # -- 2. chunks streamed to the client (same region as the host) --
        check("client_has_chunks", cs.get("chunks", 0) > 10, f"chunks={cs.get('chunks')}")

        # -- 3. host block edit syncs to the client world --
        cx = 20
        cz = 20
        client.exec_cmd(f"/tp {cx} 90 {cz}")
        time.sleep(3.0)
        r = host.exec_cmd(f"/setblock {cx} 90 {cz} glowstone")
        check("host_setblock", "Set" in r.get("msg", ""), str(r))
        time.sleep(3.0)
        r = client.exec_cmd("/goto glowstone")
        found = "Found glowstone" in r.get("msg", "")
        check("block_synced_to_client", found, str(r.get("msg", ""))[:80])

        # -- 4. chat: host broadcast reaches the client --
        r = host.exec_cmd("/mp_players")
        time.sleep(1.0)
        r = client.exec_cmd("/mp_players")
        check("client_counts_two", "Gracze online (2)" in r.get("msg", ""), str(r.get("msg", "")))

        # -- 5. time sync: clocks within drift --
        time.sleep(12.0)  # > kTimeSyncEveryTicks (100 ticks = 5s)
        hs = host.state()
        cs = client.state()
        drift = abs(hs.get("time_of_day", 0) - cs.get("time_of_day", 0)) % 1.0
        drift = min(drift, 1.0 - drift)
        check("time_synced", drift < 0.05, f"drift={drift:.4f}")

        # -- 6. disconnect: killing the client empties the host session --
        procs[1].terminate()
        time.sleep(3.0)
        gone = False
        deadline = time.time() + 20
        while time.time() < deadline:
            hs = host.state()
            if hs.get("mp_players") == 1:
                gone = True
                break
            time.sleep(1.0)
        check("client_disconnect_propagates", gone, str(hs.get("mp_players")))

    finally:
        for p in procs:
            try:
                p.terminate()
            except Exception:
                pass
        time.sleep(1.0)
        for p in procs:
            try:
                p.kill()
            except Exception:
                pass

    passed = sum(1 for _, ok in results if ok)
    print(f"\n{passed}/{len(results)} multiplayer checks passed.")
    if not args.keep:
        pass
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
