#!/usr/bin/env python3
"""texkit — programmatic texture drawing toolkit for Minekampf.

Draws and edits PNG textures (block tile overrides, mob/entity skins) with a
small deterministic API. Also regenerates the sample Boxbench-model skins in
assets/models/mobs/.

Usage:
  python scripts/texkit.py mobs               # regenerate mob entity skins
  python scripts/texkit.py new out.png 64 64 "#8040ff"
  python scripts/texkit.py noise out.png 64 64 "#7a7a7a" 12
  python scripts/texkit.py bright file.png 20 # lighten in place
  python scripts/texkit.py sheet assets/textures/Stone.png  # preview helper
"""

import math
import random
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

GAME = Path(__file__).resolve().parent.parent
MOBS = GAME / "assets" / "models" / "mobs"


class Tex:
    """Thin PIL wrapper with pixel-art friendly helpers."""

    def __init__(self, w, h, color=(0, 0, 0, 0)):
        self.img = Image.new("RGBA", (w, h), color)
        self.draw = ImageDraw.Draw(self.img)

    @staticmethod
    def open(path):
        t = Tex.__new__(Tex)
        t.img = Image.open(path).convert("RGBA")
        t.draw = ImageDraw.Draw(t.img)
        return t

    def save(self, path):
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        self.img.save(path)

    # -- primitives ---------------------------------------------------------
    def rect(self, x, y, w, h, color):
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], fill=color)

    def px(self, x, y, color):
        if 0 <= x < self.img.width and 0 <= y < self.img.height:
            self.img.putpixel((int(x), int(y)), color)

    def outline(self, x, y, w, h, color):
        self.draw.rectangle([x, y, x + w - 1, y + h - 1], outline=color)

    # -- effects ------------------------------------------------------------
    def noise(self, amount=12, seed=1, region=None):
        rng = random.Random(seed)
        x0, y0, x1, y1 = region or (0, 0, self.img.width, self.img.height)
        for y in range(y0, y1):
            for x in range(x0, x1):
                r, g, b, a = self.img.getpixel((x, y))
                if a == 0:
                    continue
                d = rng.randint(-amount, amount)
                self.img.putpixel((x, y), (
                    max(0, min(255, r + d)),
                    max(0, min(255, g + d)),
                    max(0, min(255, b + d)), a))

    def brighten(self, delta):
        self.img = self.img.point(
            lambda v: max(0, min(255, v + delta)) if v < 250 or delta < 0 else v)
        self.draw = ImageDraw.Draw(self.img)


def parse_color(s):
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4)) + (255,)


# ---------------------------------------------------------------------------
# Box-UV skin painting for Blockbench entity models (mirrors the C++ unwrap:
# south, north, down, up, west, east).
# ---------------------------------------------------------------------------

def box_faces(u, v, w, h, d):
    """Texture rect per face for a cube, matching geo_model.cpp's unwrap."""
    return {
        "south": (u + d + w, v + d, w, h),
        "north": (u + d, v + d, w, h),
        "down":  (u + d + w, v, w, d),
        "up":    (u + d, v, w, d),
        "west":  (u + d + w + d, v + d, d, h),
        "east":  (u, v + d, d, h),
    }


def paint_cube(t, u, v, w, h, d, base, shade_sides=True, front=None):
    """Paints all 6 faces of a cube; `front` is an optional draw callback
    invoked with (Tex, rect) for the north face (the 'face' slot)."""
    rng = random.Random(u * 7 + v * 13)
    for name, (x, y, fw, fh) in box_faces(u, v, w, h, d).items():
        col = base
        if shade_sides:
            k = {"south": -14, "north": 6, "down": -28, "up": 14, "west": -10, "east": -20}[name]
            col = tuple(max(0, min(255, c + k)) for c in base[:3]) + (255,)
        t.rect(x, y, fw, fh, col)
        for _ in range(max(2, (fw * fh) // 6)):
            px = x + rng.randrange(fw)
            py = y + rng.randrange(fh)
            t.px(px, py, tuple(max(0, min(255, c + rng.randint(-10, 10))) for c in col))
    if front:
        front(t, box_faces(u, v, w, h, d)["north"])


def humanoid_skin(path, skin, shirt, pants, eyes=(60, 60, 120, 255), skeleton=False):
    """64x64 classic layout for zombie/skeleton proportions."""
    t = Tex(64, 64)

    # Head 8x8x8 at (0,0).
    def face(t, r):
        x, y, fw, fh = r
        if skeleton:
            for ex in range(fw):
                t.px(x + ex, y + 3, (40, 40, 40, 255))
                t.px(x + ex, y + 5, (30, 30, 30, 255))
        else:
            t.rect(x + 1, y + 3, 2, 1, eyes)      # eyes
            t.rect(x + 5, y + 3, 2, 1, eyes)
            t.rect(x + 3, y + 4, 2, 2, tuple(max(0, c - 30) for c in skin[:3]) + (255,))
    paint_cube(t, 0, 0, 8, 8, 8, skin, front=face)

    # Body 8x12x4 at (16,16).
    paint_cube(t, 16, 16, 8, 12, 4, shirt)
    if skeleton:  # ribs
        for i in range(4):
            t.rect(20, 20 + i * 2, 4, 1, (35, 35, 35, 255))

    # Right arm 4x12x4 at (40,16); left arm at (32,48).
    arm = skin if not skeleton else (206, 206, 202, 255)
    paint_cube(t, 40, 16, 4, 12, 4, arm)
    paint_cube(t, 32, 48, 4, 12, 4, arm)

    # Legs: right (0,16) uses the head row space; classic layout puts legs at
    # (0,16) and (0,32) — 4x12x4 each.
    paint_cube(t, 0, 16, 4, 12, 4, pants)
    paint_cube(t, 0, 32, 4, 12, 4, pants)

    t.noise(8, seed=hash(path) & 0xFF)
    t.save(path)
    print(f"[texkit] wrote {path}")


def quadruped_skin(path, hide, head_col, legs_col, snout=True, patches=None,
                   leg_h=12, head_h=8, head_d=6):
    """64x64 texture for cow/pig style models (see cow.geo.json layout)."""
    t = Tex(64, 64)

    # Body 12x10x18 at uv (18,4): faces + long sides.
    paint_cube(t, 18, 4, 12, 10, 18, hide)

    # Head 8xhead_hxhead_d at (0,0), face on north slot.
    def face(t, r):
        x, y, fw, fh = r
        if snout:
            t.rect(x + 2, y + 4, 4, 3, (232, 160, 170, 255))
            t.px(x + 2, y + 5, (120, 60, 70, 255))
            t.px(x + 5, y + 5, (120, 60, 70, 255))
        else:
            t.rect(x + 1, y + 3, 2, 1, (40, 30, 20, 255))
            t.rect(x + 5, y + 3, 2, 1, (40, 30, 20, 255))
        if patches:  # eyes above the snout/muzzle
            t.rect(x + 1, y + 2, 1, 1, (20, 20, 20, 255))
            t.rect(x + 6, y + 2, 1, 1, (20, 20, 20, 255))
    paint_cube(t, 0, 0, 8, head_h, head_d, head_col, front=face)

    # Cow patches on the body side.
    if patches:
        rng = random.Random(7)
        sx, sy, sw, sh = box_faces(18, 4, 12, 10, 18)["east"]
        for _ in range(6):
            t.rect(sx + rng.randrange(max(1, sw - 3)), sy + rng.randrange(max(1, sh - 3)),
                   rng.randrange(2, 4), rng.randrange(2, 4), (70, 40, 25, 255))

    # Four legs 4xleg_hx4 at the four 4-wide slots.
    for (u, v) in [(0, 16), (0, 32), (28, 48), (40, 48)]:
        paint_cube(t, u, v, 4, leg_h, 4, legs_col)

    t.noise(9, seed=hash(path) & 0xFF)
    t.save(path)
    print(f"[texkit] wrote {path}")


def gen_mob_skins():
    humanoid_skin(MOBS / "zombie.png",
                  skin=(96, 150, 90, 255), shirt=(52, 96, 160, 255),
                  pants=(70, 60, 130, 255))
    humanoid_skin(MOBS / "skeleton.png",
                  skin=(206, 206, 202, 255), shirt=(160, 160, 158, 255),
                  pants=(170, 170, 168, 255), skeleton=True)
    quadruped_skin(MOBS / "cow.png",
                   hide=(94, 62, 44, 255), head_col=(86, 56, 40, 255),
                   legs_col=(70, 46, 34, 255), snout=False, patches=True)
    quadruped_skin(MOBS / "pig.png",
                   hide=(238, 150, 158, 255), head_col=(244, 160, 168, 255),
                   legs_col=(214, 128, 138, 255), snout=True, leg_h=6, head_h=8,
                   head_d=8)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    if cmd == "mobs":
        gen_mob_skins()
        return 0
    if cmd == "new" and len(sys.argv) >= 6:
        Tex(int(sys.argv[3]), int(sys.argv[4]), parse_color(sys.argv[5])).save(sys.argv[2])
        return 0
    if cmd == "noise" and len(sys.argv) >= 6:
        t = Tex(int(sys.argv[3]), int(sys.argv[4]), parse_color(sys.argv[5]))
        t.noise(int(sys.argv[6]) if len(sys.argv) > 6 else 12)
        t.save(sys.argv[2])
        return 0
    if cmd == "bright" and len(sys.argv) >= 4:
        t = Tex.open(sys.argv[2])
        t.brighten(int(sys.argv[3]))
        t.save(sys.argv[2])
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
