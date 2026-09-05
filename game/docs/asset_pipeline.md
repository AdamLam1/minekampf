# Asset pipeline: Blockbench models & textures (AI-friendly)

## Blockbench models (mobs)

The game loads models made in [Blockbench](https://blockbench.net) — no code
changes needed to swap a model **or to add a brand-new species**.

Blockbench (portable) is installed at:

    C:\Users\AdamLam\Tools\Blockbench.exe

### Two supported model formats

1. **Bedrock entity geometry** — `File -> Export -> Bedrock Geometry` from a
   Minecraft-entity project. Box UVs (or per-face UVs), texture as a separate
   PNG.
2. **Native Blockbench project** — `File -> Save As` the `.bbmodel` itself
   into the mobs folder. Per-face UVs, element rotations, groups (bones) and
   the **texture embedded as base64** are all parsed; no sidecar PNG needed.

Both go into:

    game/assets/models/mobs/

### Adding a custom species (no recompiling)

1. Drop `<name>.geo.json` (+ `<name>.png`) **or** `<name>.bbmodel` into
   `assets/models/mobs/`. The file stem becomes the species key (lowercase).
2. Optional sidecar `<name>.mob.json` overrides the defaults — every key is
   optional (see the shipped example `golem.mob.json`):

   ```json
   {
     "display_name": "Golem",
     "hostile": false,            // false = passive animal, true = monster AI
     "health": 40, "speed": 0.04, "attack_damage": 6,
     "follow_range": 16,
     "scale": 1.0,                // render scale
     "body_width": 0.9, "body_height": 1.7,   // hitbox in blocks
     "xp": 6,
     "drop_item": "iron_ingot",   // registry item name ("" = no drop)
     "drop_min": 1, "drop_max": 3,
     "quadruped": false,          // procedural fallback rig shape
     "zombie_arms": false
   }
   ```

3. The species is discovered at startup (sorted alphabetically after the four
   built-ins), spawns naturally alongside vanilla mobs (25% of spawn rolls
   pick a custom species of the matching hostility), and is immediately
   available to `/spawnmob <name>` and to quests (`mob_from_name`).
4. Broken model files are skipped with a log warning and never crash the game;
   a species without a usable model falls back to the procedural box rig with
   a stable name-derived color palette.

The shipped example: `assets/models/mobs/golem.bbmodel` (embedded texture,
per-face UVs) + `golem.mob.json`. Try it in-game with `/spawnmob 1 golem`.

### Built-in species

`zombie`, `skeleton`, `cow`, `pig` resolve from the same folder — replacing
their `.geo.json`/`.png` swaps the model without code changes. A missing file
falls back to the procedural box rig, so a broken file can never make a mob
invisible (it logs a warning).

### Bone naming (auto-animation)

Name bones so the runtime can auto-animate them (case-insensitive substrings):

- `head` — follows mob pitch
- `leftArm` / `rightArm` — walk swing + attack chop
- `leftLeg` / `rightLeg` — walk swing (biped)
- `leg_front_left`, `leg_front_right`, `leg_back_left`, `leg_back_right` —
  quadruped diagonal gait (also detected by leg pivot Z when the name has
  `front`/`back`/`hind`)
- anything else (e.g. `body`) — static, follows the parent chain

Bone pivots are absolute model-space coordinates (Bedrock convention); child
bones move with their parent's rotation. Cube-level rotations (Blockbench
"rotate element") are supported around the cube pivot.

### Textures of any size

Mob textures no longer need to be exactly 64x64 — the renderer normalizes all
species into one texture array (nearest-neighbor resampling to the largest
loaded texture). Keep the texture layout matching the model's UV convention.

### Offline preview (no game launch)

    cd game
    build\release\bin\minekampf_assets.exe %TEMP%\mk_assets

writes `model_<species>.png` (posed software render, including custom
species and embedded bbmodel textures) plus the texture-atlas and item-icon
contact sheets (`atlas_albedo.png`, `icons.png`, ...).

## Custom item icons

Drop a PNG into `game/assets/icons/<item_name>.png` (registry name,
case-insensitive — e.g. `iron_sword.png`, `diamond.png`) and rebuild; the icon
atlas picks it up instead of the procedural pixel art. Any resolution
(center-cropped and box-downsampled to the 32x32 icon cell).

## Texture overrides (block tiles)

Drop a 16x16 PNG into `game/assets/textures/<TileName>.png` (e.g.
`GrassTop.png`, `Stone.png`) and rebuild; the atlas picks it up instead of the
procedural tile. Tile names are the C++ `Tile` enum names
(`game/src/world/block.hpp`).

## texkit — programmatic texture drawing

`game/scripts/texkit.py` is a deterministic PIL-based painter for AI-driven
art iteration:

    python scripts/texkit.py mobs                     # regenerate entity skins
    python scripts/texkit.py new out.png 64 64 "#8040ff"
    python scripts/texkit.py noise out.png 64 64 "#7a7a7a" 12
    python scripts/texkit.py bright file.png 20       # lighten in place

It also contains the box-UV skin painters (`humanoid_skin`, `quadruped_skin`)
that generate the sample entity textures.

## Visual QA

    python scripts/visual_test.py     # scripted scenes + pixel assertions
    python scripts/perf_test.py       # mspt/FPS sampling across scenes
