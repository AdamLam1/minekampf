# Asset pipeline: Blockbench models & textures (AI-friendly)

## Blockbench models (mobs / animals / future player rig)

The game loads Bedrock entity geometry exported from
[Blockbench](https://blockbench.net) — no code changes needed to swap a model.

Blockbench (portable) is installed at:

    C:\Users\AdamLam\Tools\Blockbench.exe

### Workflow

1. Open Blockbench -> **File -> New -> Minecraft Entity** (or "Modded Entity").
2. Model the mob with cubes grouped into bones. Name bones so the runtime can
   auto-animate them (case-insensitive substrings):
   - `head` — follows mob pitch
   - `leftArm` / `rightArm` — walk swing + attack chop
   - `leftLeg` / `rightLeg` — walk swing (biped)
   - `leg_front_left`, `leg_front_right`, `leg_back_left`, `leg_back_right` —
     quadruped diagonal gait (also detected by leg pivot Z when the name has
     `front`/`back`/`hind`)
   - anything else (e.g. `body`) — static, follows the parent chain
3. UV map with the standard **box UV** (Template: box UV layout per cube).
4. **File -> Export -> Bedrock Geometry** ->
   `game/assets/models/mobs/<species>.geo.json`
5. Export the texture PNG (same box layout) ->
   `game/assets/models/mobs/<species>.png`

Species resolved at startup: `zombie`, `skeleton`, `cow`, `pig`.
A missing `.geo.json` falls back to the procedural box rig, so a broken file
can never make a mob invisible (it logs a warning).

### Offline preview (no game launch)

    cd game
    build\release\bin\minekampf_assets.exe %TEMP%\mk_assets

writes `model_<species>.png` (posed software render) plus the texture-atlas
and item-icon contact sheets (`atlas_albedo.png`, `icons.png`, ...).

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
