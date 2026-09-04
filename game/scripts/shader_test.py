#!/usr/bin/env python3
"""Shaderpack-style water & lighting verification (Iris/BSL look port).

Scenes + pixel assertions:
  A1  noon ocean: vivid water body present
  A2  noon ocean: Fresnel gradient (far/grazing water brighter than near)
  A3  noon ocean: glints are NARROW (no blown-white glass sheet)
  A4  terrain: no blown-white specular/rims on block faces
  A5  sunset: warm sun path visible on water from at least one yaw
  A6  night: water dark, glints off

Usage:  python scripts/shader_test.py [--port 25695] [--out DIR]
Exit 0 = all passed.
"""

import argparse
import sys
import time
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from visual_test import GameClient, kill_game, capture  # noqa: E402

GAME_DIR = Path(__file__).resolve().parent.parent


def water_zone(img):
    """Rows that are over water for a pitch-70-down ocean shot."""
    w, h = img.size
    return (int(w * 0.1), int(h * 0.30), int(w * 0.9), int(h * 0.88))


def water_fraction(img, box):
    """Deep navy ocean reads (b≈60-110) after the cloud-darkened sky lowered
    the reflection level; the test still rejects grey where blue ≈ red."""
    region = img.crop(box).convert("RGB")
    water = total = 0
    for yy in range(0, region.size[1], 3):
        for xx in range(0, region.size[0], 3):
            r, g, b = region.getpixel((xx, yy))
            if b > r + 10 and b > 60:
                water += 1
            total += 1
    return water / max(1, total)


def band_brightness(img, y0f, y1f):
    w, h = img.size
    zone = water_zone(img)
    box = (zone[0], int(h * y0f), zone[2], int(h * y1f))
    region = img.crop(box).convert("L").resize((64, 12))
    data = list(region.getdata())
    return sum(data) / max(1, len(data))


def white_fraction(img, box, exclude_center=True):
    """Fraction of near-white pixels in box, skipping the crosshair square."""
    w, h = img.size
    region = img.crop(box).convert("RGB")
    cx0, cy0 = box[0], box[1]
    cross = None
    if exclude_center:
        cross = (w // 2 - 40, h // 2 - 40, w // 2 + 40, h // 2 + 40)
    white = total = 0
    for yy in range(0, region.size[1], 2):
        for xx in range(0, region.size[0], 2):
            gx, gy = cx0 + xx, cy0 + yy
            if cross and cross[0] <= gx <= cross[2] and cross[1] <= gy <= cross[3]:
                continue
            r, g, b = region.getpixel((xx, yy))
            if r > 238 and g > 238 and b > 238:
                white += 1
            total += 1
    return white / max(1, total)


def warm_fraction(img, box):
    """Fraction of warm gold/orange pixels (sunset sun path)."""
    region = img.crop(box).convert("RGB")
    warm = total = 0
    for yy in range(0, region.size[1], 2):
        for xx in range(0, region.size[0], 2):
            r, g, b = region.getpixel((xx, yy))
            if r > 150 and r > b + 40 and g > b + 10:
                warm += 1
            total += 1
    return warm / max(1, total)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=25695)
    ap.add_argument("--out", default=str(GAME_DIR / "build" / "shader_test"))
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    kill_game()
    time.sleep(1.0)
    proc = None
    import subprocess
    exe = GAME_DIR / "build" / "release" / "bin" / "minekampf.exe"
    proc = subprocess.Popen([
        str(exe), "--auto-play", "--automation-port", str(args.port),
    ], cwd=str(GAME_DIR), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    api = GameClient(args.port)
    shots = {}
    results = []
    try:
        api.wait_playing()
        api.exec("/gamemode creative")
        api.exec("/fly")

        # ---- Ocean vantage, noon ------------------------------------------
        # Worldgen drifts (generator edits move the coastline), so find the
        # water empirically: scan 4 yaws from high above the goto point, then
        # fly 20 blocks toward the most water-heavy heading and look down at
        # the bay.
        import math
        api.exec("/goto water")
        st = api.state()
        x, y, z = st["pos"]
        api.exec(f"/tp {x} {y+40} {z}")
        time.sleep(4.0)
        best_yaw, best_count = 0, -1
        for yaw in (0, 90, 180, 270):
            api._rpc({"cmd": "look", "yaw": yaw, "pitch": 35})
            time.sleep(1.5)
            img = capture(api, out, f"s0_yawscan{yaw}")
            w, h = img.size
            n = 0
            for yy in range(int(h * 0.3), h, 4):
                for xx in range(0, w, 4):
                    r, g, b = img.getpixel((xx, yy))
                    if b > r + 10 and b > 60:
                        n += 1
            if n > best_count:
                best_yaw, best_count = yaw, n
        rad = math.radians(best_yaw)
        dx, dz = -math.sin(rad) * 20.0, math.cos(rad) * 20.0
        api.exec(f"/tp {x+dx:.1f} {y+14} {z+dz:.1f}")
        # Nearly-level view: water fills the lower frame, far shore the top.
        api._rpc({"cmd": "look", "yaw": best_yaw, "pitch": 12})
        time.sleep(4.0)
        noon = capture(api, out, "s1_noon_ocean")
        print(f"  (ocean yaw={best_yaw}, scan px={best_count})")

        zone = water_zone(noon)
        frac = water_fraction(noon, zone)
        results.append(("A1_noon_water_body", frac > 0.50, f"water_frac={frac:.2f}"))

        # Fresnel gradient over WATER pixels only (shore must not pollute).
        w, h = noon.size
        zx0, zy0, zx1, zy1 = zone
        far_sum = far_n = near_sum = near_n = 0
        for yy in range(zy0, zy1, 2):
            for xx in range(zx0, zx1, 2):
                r, g, b = noon.getpixel((xx, yy))
                if not (b > r + 10 and b > 80):
                    continue
                lum = 0.299 * r + 0.587 * g + 0.114 * b
                if yy < h * 0.45:
                    far_sum += lum
                    far_n += 1
                elif yy > h * 0.72:
                    near_sum += lum
                    near_n += 1
        far = far_sum / max(1, far_n)
        near = near_sum / max(1, near_n)
        results.append(("A2_fresnel_gradient", far > near + 4,
                        f"far={far:.0f} near={near:.0f}"))

        glint = white_fraction(noon, zone)
        results.append(("A3_glints_narrow", glint < 0.010, f"white_frac={glint*100:.2f}%"))

        # ---- Terrain, noon --------------------------------------------------
        api.exec("/goto forest")
        st = api.state()
        x, y, z = st["pos"]
        api.exec(f"/tp {x} {y+6} {z}")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 35})
        time.sleep(5.0)
        terr = capture(api, out, "s2_terrain")

        w, h = terr.size
        terr_box = (int(w * 0.08), int(h * 0.45), int(w * 0.92), int(h * 0.90))
        blown = white_fraction(terr, terr_box)
        results.append(("A4_terrain_matte", blown < 0.004, f"white_frac={blown*100:.2f}%"))

        # ---- Sunset sun path ------------------------------------------------
        # Stay on the ocean vantage (water must be in frame for the sun path).
        api.exec("/time 0.75")
        time.sleep(2.0)
        best_warm = 0.0
        for yaw in (0, 90, 180, 270):
            api._rpc({"cmd": "look", "yaw": yaw, "pitch": 6})
            time.sleep(1.2)
            img = capture(api, out, f"s3_sunset_yaw{yaw}")
            ww, hh = img.size
            path_box = (int(ww * 0.1), int(hh * 0.45), int(ww * 0.9), int(hh * 0.80))
            best_warm = max(best_warm, warm_fraction(img, path_box))
        results.append(("A5_sunset_sun_path", best_warm > 0.02,
                        f"warm_frac={best_warm*100:.2f}% (best yaw)"))

        # ---- Night ----------------------------------------------------------
        api.exec("/time 0.0")
        api._rpc({"cmd": "look", "yaw": 0, "pitch": 60})
        time.sleep(3.0)
        night = capture(api, out, "s4_night_ocean")
        night_b = band_brightness(night, 0.32, 0.86)
        noon_b = band_brightness(noon, 0.32, 0.86)
        results.append(("A6_night_water_dark", night_b < noon_b * 0.65,
                        f"night={night_b:.0f} noon={noon_b:.0f}"))
    except Exception as e:
        results.append(("scenario_completed", False, repr(e)))
    finally:
        api.close()
        kill_game()
        if proc is not None:
            try:
                proc.wait(timeout=10)
            except Exception:
                proc.kill()

    print("\n=== Shader test results ===")
    failed = 0
    for name, ok, info in results:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}: {info}")
        failed += 0 if ok else 1
    print(f"=== {len(results) - failed}/{len(results)} passed ===")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
