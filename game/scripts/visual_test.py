#!/usr/bin/env python3
"""Visual QA suite for Minekampf.

Launches the game with the automation API, drives a scripted scenario and
captures screenshots, then runs pixel-level assertions on the captured frames
(HUD sprite colors, icon diversity, day/night lighting, perf sanity).
Exit code 0 = all checks passed.

Usage:
    python scripts/visual_test.py [--out DIR] [--port PORT] [--keep]
"""

import argparse
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

GAME_DIR = Path(__file__).resolve().parent.parent
EXE = GAME_DIR / "build" / "release" / "bin" / "minekampf.exe"


class GameClient:
    """Automation API client over ONE persistent TCP connection (the game
    server accepts a single client; the first reply on connect is a banner)."""

    def __init__(self, port):
        self.port = port
        self.sock = None

    def _connect(self, timeout=5.0):
        if self.sock is not None:
            return
        s = socket.create_connection(("127.0.0.1", self.port), timeout=timeout)
        s.settimeout(timeout)
        self.sock = s
        self._readline()  # consume the "connected" banner

    def _readline(self):
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = self.sock.recv(65536)
            if not chunk:
                break
            buf += chunk
        return buf

    def _rpc(self, payload, timeout=10.0):
        self._connect(timeout)
        self.sock.settimeout(timeout)
        self.sock.sendall((json.dumps(payload) + "\n").encode())
        return json.loads(self._readline().decode(errors="replace"))

    def wait_playing(self, timeout=90.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                st = self._rpc({"cmd": "get_state"}, timeout=5.0)
                if st.get("state") == "playing":
                    return st
                self.sock.close()
                self.sock = None
            except (ConnectionRefusedError, socket.timeout, OSError,
                    json.JSONDecodeError):
                self.sock = None
            time.sleep(1.0)
        raise RuntimeError("game did not reach 'playing' state")

    def exec(self, command):
        return self._rpc({"cmd": "exec", "command": command})

    def shot(self, path):
        # Deferred a few frames by the engine; small wait keeps PNG order sane.
        resp = self._rpc({"cmd": "screenshot", "filename": str(path)}, timeout=30.0)
        time.sleep(0.5)
        return resp

    def state(self):
        return self._rpc({"cmd": "get_state"})

    def close(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


def kill_game():
    subprocess.run(["taskkill", "/f", "/im", "minekampf.exe"],
                   capture_output=True)


def count_colors(img, box):
    region = img.crop(box).convert("RGB")
    colors = set()
    for px in region.getdata():
        # quantize to reduce AA noise
        colors.add((px[0] // 16, px[1] // 16, px[2] // 16))
    return len(colors)


def count_matching(img, box, pred):
    region = img.crop(box).convert("RGB")
    return sum(1 for px in region.getdata() if pred(px))


def avg_brightness(img, box=None):
    region = img.convert("L")
    if box:
        region = region.crop(box)
    small = region.resize((64, 36))
    data = list(small.getdata())
    return sum(data) / len(data)


def capture(api, out, name):
    """Captures a screenshot (engine writes BMP) and returns a PIL image."""
    bmp = out / f"{name}.bmp"
    api.shot(bmp)
    img = Image.open(bmp)
    img.save(out / f"{name}.png")
    return img.convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "build" / "visual_test"))
    ap.add_argument("--port", type=int, default=25597)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    if not EXE.exists():
        print(f"[!] exe missing: {EXE} (run scripts\\dev.bat build first)")
        return 2

    kill_game()
    time.sleep(1.0)

    proc = subprocess.Popen(
        [str(EXE), "--auto-play", "--automation-port", str(args.port)],
        cwd=str(GAME_DIR), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    results = []
    screenshots = {}
    api = GameClient(args.port)
    try:
        state = api.wait_playing()
        results.append(("game_reached_playing", True, f"chunks={state.get('chunks')}"))

        # --- Scene 1: survival HUD with items -------------------------------
        r = api.exec("/gamemode s")
        results.append(("gamemode_survival", "set to s" in r.get("msg", ""), r.get("msg", "")))
        for item in ("diamond_sword 1", "apple 5", "torch 16", "iron_ingot 12"):
            api.exec(f"/give {item}")
        api.exec("/time 0.5")     # noon
        api.exec("/tp 6 75 6")
        time.sleep(2.5)
        screenshots["hud"] = capture(api, out, "01_survival_hud")

        # --- Scene 2: water --------------------------------------------------
        api.exec("/goto water")
        time.sleep(2.0)
        screenshots["water"] = capture(api, out, "02_water")

        # --- Scene 3: mobs ---------------------------------------------------
        api.exec("/spawnmob 3 zombie")
        api.exec("/spawnmob 2 pig")
        time.sleep(2.0)
        screenshots["mobs"] = capture(api, out, "03_mobs")

        # --- Scene 4: night (fixed vantage, sky-dominated frame) ------------
        api.exec("/gamemode creative")
        api.exec("/fly")
        api.exec("/tp 6 85 6")
        api.exec("/time 0.0")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 35})
        time.sleep(2.0)
        screenshots["night"] = capture(api, out, "04_night")

        state = api.state()
        mspt = state.get("mspt", 999.0)
        results.append(("mspt_below_50", mspt < 50.0, f"mspt={mspt:.1f}"))
    except Exception as e:
        results.append(("scenario_completed", False, repr(e)))
    finally:
        api.close()
        if not args.keep:
            kill_game()

    # --- Pixel assertions ----------------------------------------------------
    hud = screenshots.get("hud")
    if hud is not None:
        # Hearts: survival HUD shows 10 hearts above the hotbar (bottom-left
        # of center). Scan a generous strip for saturated heart red.
        w, h = hud.size
        strip = (w // 2 - 220, h - 80, w // 2 + 20, h - 48)
        reds = count_matching(hud, strip, lambda p: p[0] > 120 and p[1] < 90 and p[2] < 90)
        results.append(("hearts_red_pixels", reds >= 30, f"{reds} red px in HUD strip"))

        hotbar = (w // 2 - 190, h - 48, w // 2 + 190, h)
        colors = count_colors(hud, hotbar)
        results.append(("hotbar_icon_diversity", colors >= 12, f"{colors} quantized colors"))

        # Apple red somewhere in the hotbar.
        apple_red = count_matching(hud, hotbar, lambda p: p[0] > 140 and p[1] < 90 and p[2] < 90)
        results.append(("apple_red_in_hotbar", apple_red >= 8, f"{apple_red} red px"))

    day = screenshots.get("water")
    night = screenshots.get("night")
    if day is not None and night is not None:
        w, h = day.size
        sky = (0, 0, w, h // 3)
        day_b, night_b = avg_brightness(day, sky), avg_brightness(night, sky)
        results.append(("night_sky_darker_than_day", night_b < day_b * 0.7,
                        f"sky day={day_b:.0f} night={night_b:.0f}"))

    print("\n=== Visual test results ===")
    failed = 0
    for name, ok, info in results:
        mark = "PASS" if ok else "FAIL"
        if not ok:
            failed += 1
        print(f"[{mark}] {name}: {info}")
    print(f"=== {len(results) - failed}/{len(results)} passed; shots in {out} ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
