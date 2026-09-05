# Generates the shipped example custom mob: assets/models/mobs/golem.bbmodel
# (native Blockbench project, per-face UVs, embedded 64x64 texture) plus the
# sidecar golem.mob.json demonstrating every stat override.
#
# Run from game/:  python scripts/make_golem_example.py
import base64
import io
import json
import random
from pathlib import Path

from PIL import Image

OUT = Path("assets/models/mobs")
TEX = 64

IRONS = [(196, 196, 204), (186, 186, 196), (204, 204, 212), (176, 176, 188)]
DARK = (42, 42, 52)
VINE = (90, 122, 58)
VINE2 = (106, 142, 70)


def fill(img, x0, y0, w, h, seed_shift=0, palette=IRONS):
    rng = random.Random(1234 + seed_shift)
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            c = palette[rng.randrange(len(palette))]
            # subtle vertical shading
            k = 1.0 - 0.18 * (y - y0) / max(1, h - 1)
            img.putpixel((x, y), tuple(int(v * k) for v in c) + (255,))


def rect_outline(img, x0, y0, w, h, color):
    for x in range(x0, x0 + w):
        img.putpixel((x, y0), color + (255,))
        img.putpixel((x, y0 + h - 1), color + (255,))
    for y in range(y0, y0 + h):
        img.putpixel((x0, y), color + (255,))
        img.putpixel((x0 + w - 1, y), color + (255,))


def paint_texture():
    img = Image.new("RGBA", (TEX, TEX), (0, 0, 0, 0))
    # head: six 6x6 faces in a row
    for i, seed in enumerate(range(6)):
        fill(img, i * 6, 0, 6, 6, seed)
    rect_outline(img, 0, 0, 6, 6, (150, 150, 160))
    # eyes on the north (front) face
    for ex in (1, 4):
        for ey in (2, 3):
            img.putpixel((ex, ey), DARK + (255,))
    # nose ridge
    img.putpixel((2, 4), (120, 120, 130, 255))
    img.putpixel((3, 4), (120, 120, 130, 255))
    # body: south(0,8) north(8,8) east(16,8) west(20,8) up(24,8) down(24,12)
    fill(img, 0, 8, 8, 12, 10)
    fill(img, 8, 8, 8, 12, 11)
    fill(img, 16, 8, 4, 12, 12)
    fill(img, 20, 8, 4, 12, 13)
    fill(img, 24, 8, 8, 4, 14)
    fill(img, 24, 12, 8, 4, 15)
    # vines on the front (north) face
    rng = random.Random(77)
    for x in range(8, 16):
        if rng.randrange(3) == 0:
            y0 = 9 + rng.randrange(3)
            y1 = y0 + 4 + rng.randrange(5)
            for y in range(y0, min(y1, 20)):
                img.putpixel((x, y), (VINE if (x + y) % 2 else VINE2) + (255,))
    # arm: 3x12 column at (32,0); leg: 3x7 column at (52,0)
    fill(img, 32, 0, 3, 12, 20)
    fill(img, 35, 0, 3, 12, 21)
    fill(img, 38, 0, 3, 12, 22)
    fill(img, 41, 0, 3, 12, 23)
    fill(img, 44, 0, 3, 3, 24)
    fill(img, 44, 3, 3, 3, 25)
    fill(img, 52, 0, 3, 7, 26)
    fill(img, 55, 0, 3, 7, 27)
    fill(img, 58, 0, 3, 7, 28)
    fill(img, 61, 0, 3, 7, 29)
    fill(img, 52, 7, 3, 3, 30)
    fill(img, 55, 7, 3, 3, 31)
    return img


def face(u, v, w, h):
    return {"uv": [u, v, u + w, v + h], "texture": 0}


def cube(uuid, name, frm, to, faces):
    return {
        "name": name,
        "uuid": uuid,
        "from": frm,
        "to": to,
        "faces": faces,
        "type": "cube",
    }


def main():
    img = paint_texture()
    buf = io.BytesIO()
    img.save(buf, "PNG")
    png_b64 = base64.b64encode(buf.getvalue()).decode()

    elements = [
        # body 8x12x4
        cube("b0d1-0001", "body", [-4, 7, -2], [4, 19, 2], {
            "south": face(0, 8, 8, 12), "north": face(8, 8, 8, 12),
            "east": face(16, 8, 4, 12), "west": face(20, 8, 4, 12),
            "up": face(24, 8, 8, 4), "down": face(24, 12, 8, 4),
        }),
        # head 6x6x6
        cube("b0d1-0002", "head", [-3, 19, -3], [3, 25, 3], {
            "south": face(12, 0, 6, 6), "north": face(0, 0, 6, 6),
            "east": face(6, 0, 6, 6), "west": face(18, 0, 6, 6),
            "up": face(24, 0, 6, 6), "down": face(30, 0, 6, 6),
        }),
        # arms 3x12x3
        cube("b0d1-0003", "armL", [-7, 8, -1.5], [-4, 20, 1.5], {
            "south": face(38, 0, 3, 12), "north": face(32, 0, 3, 12),
            "east": face(35, 0, 3, 12), "west": face(41, 0, 3, 12),
            "up": face(44, 0, 3, 3), "down": face(44, 3, 3, 3),
        }),
        cube("b0d1-0004", "armR", [4, 8, -1.5], [7, 20, 1.5], {
            "south": face(38, 0, 3, 12), "north": face(32, 0, 3, 12),
            "east": face(35, 0, 3, 12), "west": face(41, 0, 3, 12),
            "up": face(44, 0, 3, 3), "down": face(44, 3, 3, 3),
        }),
        # legs 3x7x3
        cube("b0d1-0005", "legL", [-4, 0, -1.5], [-1, 7, 1.5], {
            "south": face(58, 0, 3, 7), "north": face(52, 0, 3, 7),
            "east": face(55, 0, 3, 7), "west": face(61, 0, 3, 7),
            "up": face(52, 7, 3, 3), "down": face(55, 7, 3, 3),
        }),
        cube("b0d1-0006", "legR", [1, 0, -1.5], [4, 7, 1.5], {
            "south": face(58, 0, 3, 7), "north": face(52, 0, 3, 7),
            "east": face(55, 0, 3, 7), "west": face(61, 0, 3, 7),
            "up": face(52, 7, 3, 3), "down": face(55, 7, 3, 3),
        }),
    ]

    outliner = [
        {"name": "body", "origin": [0, 7, 0], "uuid": "g-0001", "children": [
            "b0d1-0001",
            {"name": "head", "origin": [0, 19, 0], "uuid": "g-0002",
             "children": ["b0d1-0002"]},
            {"name": "armLeft", "origin": [-5.5, 19, 0], "uuid": "g-0003",
             "children": ["b0d1-0003"]},
            {"name": "armRight", "origin": [5.5, 19, 0], "uuid": "g-0004",
             "children": ["b0d1-0004"]},
            {"name": "legLeft", "origin": [-2.5, 7, 0], "uuid": "g-0005",
             "children": ["b0d1-0005"]},
            {"name": "legRight", "origin": [2.5, 7, 0], "uuid": "g-0006",
             "children": ["b0d1-0006"]},
        ]},
    ]

    bb = {
        "meta": {"format_version": "4.5", "model_format": "bedrock",
                 "box_uv": False},
        "name": "golem",
        "model_identifier": "golem",
        "resolution": {"width": TEX, "height": TEX},
        "elements": elements,
        "outliner": outliner,
        "textures": [{
            "name": "golem.png", "id": "0", "particle": False,
            "source": "data:image/png;base64," + png_b64,
            "width": TEX, "height": TEX,
        }],
    }
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "golem.bbmodel").write_text(json.dumps(bb, indent=1), encoding="utf-8")

    sidecar = {
        "display_name": "Golem",
        "hostile": False,
        "health": 40,
        "speed": 0.04,
        "attack_damage": 6,
        "body_width": 0.9,
        "body_height": 1.7,
        "xp": 6,
        "drop_item": "iron_ingot",
        "drop_min": 1,
        "drop_max": 3,
    }
    (OUT / "golem.mob.json").write_text(json.dumps(sidecar, indent=2),
                                        encoding="utf-8")
    print("wrote", OUT / "golem.bbmodel", "and golem.mob.json")


if __name__ == "__main__":
    main()
