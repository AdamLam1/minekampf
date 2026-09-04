#!/usr/bin/env python3
"""Visual audit: captures a fixed set of scenes from the current build so
weak points vs Hytale/Iris-shader look can be ranked (shadows, clouds, cave
lighting, night sky, water).

Usage:  python scripts/visual_audit.py [--port 25695] [--out DIR]
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from visual_test import GameClient, kill_game, capture  # noqa: E402

GAME_DIR = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=25695)
    ap.add_argument("--out", default=str(GAME_DIR / "build" / "visual_audit"))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    kill_game()
    time.sleep(1.0)
    exe = GAME_DIR / "build" / "release" / "bin" / "minekampf.exe"
    proc = subprocess.Popen([str(exe), "--auto-play", "--automation-port", str(args.port)],
                            cwd=str(GAME_DIR), stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    api = GameClient(args.port)
    try:
        api.wait_playing()
        api.exec("/gamemode creative")
        api.exec("/fly")

        # ---- S1: noon terrain + water + clouds (the bay vantage) ----------
        api.exec("/time 0.5")
        api.exec("/goto water")
        st = api.state(); x, y, z = st["pos"]
        ox, oz = int(x) - 30, int(z)
        api.exec(f"/tp {ox} {y+14} {oz}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 12})
        time.sleep(5.0)
        capture(api, out, "s1_noon_bay")

        # ---- S2: noon shadows closeup (terrain, sun overhead) -------------
        api.exec("/goto grass_block")
        st = api.state(); x, y, z = st["pos"]
        api.exec(f"/tp {x} {y+6} {z}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 30})
        time.sleep(3.0)
        capture(api, out, "s2_noon_shadows")

        # ---- S3: cave lighting (carve a room, torch inside) ---------------
        api.exec("/goto grass_block")
        st = api.state(); x, y, z = [int(v) for v in st["pos"]]
        # carve a 3x3x3 room into the hillside at ground level
        for dx in (0, 1, 2):
            for dy in (0, 1, 2):
                for dz in (0, 1, 2):
                    api.exec(f"/setblock {x+dx} {y+dy} {z+dz} air")
        api.exec(f"/setblock {x+2} {y+1} {z+1} torch")
        api.exec(f"/tp {x-3} {y+1} {z+1}")
        api._rpc({"cmd": "look", "yaw": 90, "pitch": 5})
        time.sleep(3.0)
        capture(api, out, "s3_cave_torch")

        # ---- S4: sunset wide ----------------------------------------------
        api.exec("/time 0.72")
        api.exec(f"/tp {ox} {y+14} {oz}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 5})
        time.sleep(3.0)
        capture(api, out, "s4_sunset")

        # ---- S5: night sky + terrain --------------------------------------
        api.exec("/time 0.0")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 35})
        time.sleep(3.0)
        capture(api, out, "s5_night_sky")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": -10})
        time.sleep(2.0)
        capture(api, out, "s6_night_terrain")
    except Exception as e:
        print("scenario error:", repr(e))
    finally:
        api.close()
        kill_game()
        try:
            proc.wait(timeout=10)
        except Exception:
            proc.kill()

    print("audit shots in", out)


if __name__ == "__main__":
    main()
