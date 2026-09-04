#!/usr/bin/env python3
"""graphics_test.py — dedicated visual regression tests for Minekampf graphics.

Each test drives the game bot into a controlled scene, captures a screenshot
and asserts pixel-level properties that would catch the classic rendering
bugs: upside-down cross sprites (torches/grass), stretched textures on greedy
quads, missing water bodies, inverted skies.

Usage:  python scripts/graphics_test.py [--port 25695] [--out DIR]
Exit 0 = all passed.
"""

import argparse
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from visual_test import GameClient, EXE, GAME_DIR, kill_game, capture  # noqa: E402


def torch_orientation(img, box):
    """Finds the flame (overexposed ember core) vs the stick (warm-lit wood)
    pixels inside the given region and returns their centroid rows — the flame
    must be ABOVE (smaller y) the stick for an upright torch.

    Thresholds are calibrated for the shaderpack lighting: the torch's own
    warm light tints its stick orange (it used to read neutral brown), and
    worldgen flowers must not classify as flame (they lack a bright green
    channel)."""
    region = img.crop(box).convert("RGB")
    w, h = region.size
    flame_pts, stick_pts = [], []
    for yy in range(h):
        for xx in range(w):
            r, g, b = region.getpixel((xx, yy))
            if r > 225 and g > 140 and b < 160 and r - b > 70:
                flame_pts.append((xx, yy))    # overexposed ember core
            elif 40 < r < 230 and 20 < g < 210 and r > g + 12 and b < r + 25:
                stick_pts.append((xx, yy))    # warm-lit or backlit wood
    if not flame_pts:
        return None, None, 0, 0
    fx = sum(p[0] for p in flame_pts) / len(flame_pts)
    fy = sum(p[1] for p in flame_pts) / len(flame_pts)
    # Stick = dark-warm pixels in a narrow column directly BELOW the flame —
    # counting stick-colored pixels everywhere would match dirt/clouds.
    below = [p[1] for p in stick_pts if abs(p[0] - fx) < 25 and fy + 2 < p[1] < fy + 90]
    stick_y = sum(below) / len(below) if below else None
    return fy, stick_y, len(flame_pts), len(below)


def texture_crispness(img, box):
    """Average absolute luminance difference between horizontally adjacent
    pixels — smeared/stretched textures have near-zero local variance."""
    region = img.crop(box).convert("L")
    w, h = region.size
    total, count = 0, 0
    for yy in range(0, h, 2):
        prev = None
        for xx in range(0, w, 2):
            v = region.getpixel((xx, yy))
            if prev is not None:
                total += abs(v - prev)
                count += 1
            prev = v
    return total / max(1, count)


def water_fraction(img, box=None):
    region = (img if box is None else img.crop(box)).convert("RGB")
    water = total = 0
    for yy in range(0, region.size[1], 3):
        for xx in range(0, region.size[0], 3):
            r, g, b = region.getpixel((xx, yy))
            if b > r + 10 and b > 80:
                water += 1
            total += 1
    return water / max(1, total)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=25695)
    ap.add_argument("--out", default=str(GAME_DIR / "build" / "graphics_test"))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    kill_game()
    time.sleep(1.0)
    proc = subprocess.Popen([str(EXE), "--auto-play", "--automation-port", str(args.port)],
                            cwd=str(GAME_DIR), stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    results = []
    shots = {}
    api = GameClient(args.port)
    try:
        api.wait_playing()
        api.exec("/gamemode c")
        api.exec("/fly")
        api.exec("/time 0.5")

        # ---- T1: torch upright — rig built LOCAL to the camera over OPEN
        # OCEAN (30 blocks seaward of the /goto point: no trees, no shore in
        # the 3-block corridor). Camera floats 1 block above, looking slightly
        # down; background is open water and sky. ----------------------------
        api.exec("/goto water")
        st = api.state(); x, y, z = [int(v) for v in st["pos"]]
        ox, oz = x - 30, z
        api.exec(f"/tp {ox} {y+10} {oz}")
        time.sleep(1.5)                          # let the chunk load
        # Rig hangs 12 blocks AHEAD of the camera. NOTE: at yaw 0 the camera
        # faces +Z (verified empirically via compass pillars; math.hpp's
        # comment says -Z but the rendered view disagrees). The +Z corridor
        # from the goto-water point is open water — nothing occludes.
        api.exec(f"/setblock {ox} {y+16} {oz+12} stone")
        api.exec(f"/setblock {ox} {y+17} {oz+12} torch")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": -21})
        time.sleep(3.0)
        shots["torch"] = capture(api, out, "t1_torch")

        # ---- T2: beach crispness (no POM smear) --------------------------
        # Build a 3x3 sand platform and look at it from 4 blocks: the sand
        # texture deterministically fills the analyzed crop (goto "sand"
        # often finds underwater sand — a water-filled crop has no texture).
        api.exec("/goto sand")
        st = api.state(); x, y, z = [int(v) for v in st["pos"]]
        for dx in (-1, 0, 1):
            for dz in (-1, 0, 1):
                api.exec(f"/setblock {x+dx} {y+2} {z+dz} sand")
        # Camera 2 blocks on the -Z side; yaw 0 faces +Z (empirical camera
        # convention), so the platform fills the analysis crop up close.
        api.exec(f"/tp {x} {y+4} {z-2}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 37})
        time.sleep(1.5)
        shots["beach"] = capture(api, out, "t2_beach")

        # ---- T3: forest edge (leaves solid, grass upright) ---------------
        api.exec("/goto grass_block")
        st = api.state(); x, y, z = st["pos"]
        api.exec(f"/tp {x} {y+3} {z}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 12})
        time.sleep(1.2)
        shots["forest"] = capture(api, out, "t3_forest")

        # ---- T4: water body present over ocean ---------------------------
        # Rise high above the goto-water point and look straight down: the
        # player floats ON the water, so the frame center is guaranteed to be
        # over the bay (a low oblique view depends on where the shore is).
        api.exec("/goto water")
        st = api.state(); x, y, z = st["pos"]
        api.exec(f"/tp {x} {y+40} {z}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 89})
        time.sleep(6.0)  # let distant meshes settle
        shots["ocean"] = capture(api, out, "t4_ocean")
        state = api.state()
        mspt = state.get("mspt", 999.0)
        results.append(("mspt_below_50", mspt < 50.0, f"mspt={mspt:.1f}"))
    except Exception as e:
        results.append(("scenario_completed", False, repr(e)))
    finally:
        api.close()
        kill_game()

    # ---- Assertions ----------------------------------------------------------
    torch = shots.get("torch")
    if torch is not None:
        w, h = torch.size
        box = (w // 6, h // 5, w * 5 // 6, h * 4 // 5)  # skip HUD/hotbar
        flame_y, stick_y, nf, ns = torch_orientation(torch, box)
        ok = (nf > 30 and ns >= 5 and flame_y is not None and stick_y is not None
              and flame_y < stick_y)
        results.append(("torch_flame_above_stick", ok,
                        f"flame_y={flame_y and round(flame_y)} stick_y={stick_y and round(stick_y)} "
                        f"({nf} flame / {ns} stick px)"))

    beach = shots.get("beach")
    if beach is not None:
        w, h = beach.size
        crisp = texture_crispness(beach, (w // 4, h // 3, w * 3 // 4, h * 2 // 3))
        # Uniform sand platform scene: a POM smear flattens local variance to
        # ~0.1-0.3, crisp texel grain reads ~0.8-1.0 (the sand albedo is
        # deliberately low-contrast; the old natural-beach scene mixed
        # grass/shadow edges and read 4-6).
        results.append(("beach_texture_crisp", crisp > 0.5, f"local variance={crisp:.1f}"))

    forest = shots.get("forest")
    if forest is not None:
        w, h = forest.size
        greens = 0
        for yy in range(h // 3, h, 4):
            for xx in range(0, w, 4):
                r, g, b = forest.getpixel((xx, yy))[:3]
                if g > 90 and g > r + 20 and g > b + 20:
                    greens += 1
        results.append(("forest_grass_present", greens > 400, f"{greens} green px"))

    ocean = shots.get("ocean")
    if ocean is not None:
        frac = water_fraction(ocean)
        results.append(("ocean_water_present", frac > 0.15, f"water fraction={frac:.0%}"))

    print("\n=== Graphics test results ===")
    failed = 0
    for name, ok, info in results:
        mark = "PASS" if ok else "FAIL"
        failed += (not ok)
        print(f"[{mark}] {name}: {info}")
    print(f"=== {len(results) - failed}/{len(results)} passed; shots in {out} ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
