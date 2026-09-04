#!/usr/bin/env python3
"""Minekampf performance benchmark.

Launches the game with an in-memory world + automation API, drives it through
performance-relevant scenarios (idle tick, block-edit storm, chunk reload via
teleport, mob pressure, fluid activity) and records MSPT + profiler section
timings + lighting stats from the `get_perf` automation command.

Usage:
  python perf_bench.py [--out results.json] [--label NAME] [--skip-scenarios]

The game process is always terminated at the end. Exit code 0 = all scenarios
completed (values themselves are for before/after comparison, not pass/fail).
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
        self.sock.settimeout(20)
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

    def perf(self, reset=False):
        return self.cmd({"cmd": "get_perf", "reset": 1 if reset else 0})

    def exec(self, line):
        return self.cmd({"cmd": "exec", "command": line})


def wait_playing(api, deadline_s=120):
    t0 = time.time()
    while time.time() - t0 < deadline_s:
        try:
            st = api.state()
        except (OSError, json.JSONDecodeError):
            time.sleep(0.5)
            continue
        if st.get("state") == "playing":
            return time.time() - t0
        time.sleep(0.25)
    return None


def sample_mspt(api, seconds, interval=0.5):
    samples = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        try:
            st = api.state()
            samples.append(float(st.get("mspt", 0.0)))
        except (OSError, json.JSONDecodeError):
            pass
        time.sleep(interval)
    return samples


def stats(samples):
    if not samples:
        return {"n": 0}
    s = sorted(samples)
    return {
        "n": len(s),
        "avg": round(sum(s) / len(s), 3),
        "p50": round(s[len(s) // 2], 3),
        "p95": round(s[int(len(s) * 0.95)], 3),
        "max": round(s[-1], 3),
    }


def collect(api, reset=True):
    p = api.perf(reset=reset)
    p.pop("status", None)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--out", default=os.path.join(HERE, "perf_results.json"))
    ap.add_argument("--label", default="run")
    ap.add_argument("--settle", type=float, default=3.0)
    args = ap.parse_args()

    if not os.path.exists(args.exe):
        print(f"EXE not found: {args.exe}")
        return 1

    port = free_port()
    print(f"[1/3] Launching game on port {port}...")
    stderr_fh = open(os.path.join(os.path.dirname(args.exe), "perf_bench_game.log"), "wb")
    t_launch = time.time()
    proc = subprocess.Popen([args.exe, "--auto-play", "--automation-port", str(port)],
                            cwd=os.path.dirname(args.exe), stdout=stderr_fh, stderr=stderr_fh)
    api = None
    try:
        for _ in range(60):
            if proc.poll() is not None:
                print("!!! game process died during launch")
                return 1
            try:
                api = Api(port)
                break
            except OSError:
                time.sleep(0.5)
        if api is None:
            print("FAILED to connect to automation API")
            return 1

        print("[2/3] Waiting for world load...")
        load_s = wait_playing(api)
        if load_s is None:
            print("FAILED: world never reached playing state")
            return 1
        print(f"      loaded in {load_s:.1f}s")

        results = {"label": args.label, "load_to_playing_s": round(load_s, 2)}

        # ---- Scenario 1: idle tick (baseline MSPT + section split) ----
        time.sleep(args.settle)
        collect(api, reset=True)  # zero the light counters
        ms = sample_mspt(api, 6.0)
        perf = collect(api, reset=False)
        results["idle"] = {"mspt": stats(ms), "perf": perf}
        print(f"  idle:      mspt avg={results['idle']['mspt']['avg']} p95={results['idle']['mspt']['p95']} "
              f"light={perf.get('light_computes')}c/{perf.get('light_total_ms')}ms")

        # ---- Scenario 2: block-edit storm (forces light recompute + remesh) ----
        time.sleep(1.0)
        t0 = time.time()
        st = api.state()
        px, py, pz = int(float(st["pos"][0])), int(float(st["pos"][1])), int(float(st["pos"][2]))
        edits = 0
        while time.time() - t0 < 6.0:
            for i in range(20):
                x = px + (i % 9) - 4
                z = pz + ((i * 3) % 9) - 4
                y = py + (i % 3) - 1
                blk = "stone" if (edits // 20) % 2 == 0 else "air"
                api.exec(f"/setblock {x} {y} {z} {blk}")
                edits += 1
        ms = sample_mspt(api, 2.0)
        perf = collect(api, reset=False)
        results["edits"] = {"edits_sent": edits, "mspt": stats(ms), "perf": perf}
        print(f"  edits:     {edits} cmds, mspt avg={results['edits']['mspt']['avg']} p95={results['edits']['mspt']['p95']} "
              f"light={perf.get('light_computes')}c/{perf.get('light_total_ms')}ms")

        # ---- Scenario 3: teleport to far corner (unload + gen + light + mesh burst) ----
        time.sleep(1.0)
        api.exec("/tp -110 90 -110")
        t_tp = time.time()
        ms = sample_mspt(api, 8.0, interval=0.25)
        perf = collect(api, reset=False)
        settle_s = None
        t0 = time.time()
        base_chunks = perf.get("chunks", 0)
        while time.time() - t0 < 30:
            st2 = api.state()
            if int(st2.get("chunks", 0)) >= base_chunks and time.time() - t0 > 2:
                # chunk count only grows while gen is pending; stable for 2s window
                settle_s = time.time() - t_tp
                break
            time.sleep(0.5)
        results["teleport"] = {"mspt": stats(ms), "perf": perf, "settle_s": settle_s}
        print(f"  teleport:  mspt avg={results['teleport']['mspt']['avg']} p95={results['teleport']['mspt']['p95']} "
              f"max={results['teleport']['mspt']['max']} chunks={perf.get('chunks')} "
              f"light={perf.get('light_computes')}c/{perf.get('light_total_ms')}ms")

        # ---- Scenario 4: mob pressure (AI + pathfinding + collision) ----
        time.sleep(1.0)
        for _ in range(12):
            api.exec("/spawnmob 2 zombie")
        time.sleep(1.0)
        collect(api, reset=True)
        ms = sample_mspt(api, 6.0)
        perf = collect(api, reset=False)
        results["mobs"] = {"mspt": stats(ms), "perf": perf}
        print(f"  mobs:      count={perf.get('mobs')} mspt avg={results['mobs']['mspt']['avg']} "
              f"p95={results['mobs']['mspt']['p95']}")

        results["ts"] = time.strftime("%Y-%m-%d %H:%M:%S")
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(results, f, indent=2)
        print(f"[3/3] Results written to {args.out}")
        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
        stderr_fh.close()
        print("Game process terminated.")


if __name__ == "__main__":
    sys.exit(main())
