# Wave 3 — World Generator Overhaul & Feature Wave (2026-09-06)

Implementation report for the "maximum features" session goal. Work was split:
an exploratory subagent produced Stage A (biome blending) before hitting its
usage limit; everything below was finished and verified by studio-head.

## World size & biome reachability (headline change)

- World expanded **16x16 -> 32x32 chunks** (256² -> 512² blocks)
  (`core/config.hpp`, `WORLD_CHUNK_HALF 8 -> 16`). The old world was too small
  for the biome noise scales — half the new biomes could never generate.
- Climate noise wavelengths halved so biome variety fits the playable area:
  temperature/humidity 0.003 -> 0.006, continentalness 0.0035 -> 0.007,
  rivers 0.0022 -> 0.004, forest groves 0.012 -> 0.02
  (`generation/world_generator.cpp`).
- Humidity field stretched (fbm x0.75 + 0.5) — the raw fbm distribution
  bunched around 0.5, which starved every wet-biome band (diagnosed via the
  new `/gotobiome` biome-frequency printout: forest was down to 1%).
- Rebalanced selection bands (`generation/biomes.cpp`), measured distribution
  near spawn: beach 26%, plains 16%, forest 9%, taiga 19%, cherry 12%,
  swamp 4%, jungle 6%, savanna 4%, desert ~0% (rare, exotic).

## Biomes (Stage B)

- Four new biomes (`generation/biomes.hpp/.cpp`): **Swamp** (murky olive tint,
  short wide oaks), **Jungle** (lush green, 8-12 block tall trees, dense
  undergrowth), **Cherry Grove** (pink canopy, warm neutral grass),
  **Flower Forest** (meadow flowers x6).
- New blocks 86-89 (`world/block.hpp`): cherry_log/leaves, jungle_log/leaves
  with procedural tiles (`renderer/texture_atlas.cpp`). Cherry/jungle leaves
  are deliberately excluded from biome tinting so authored colors survive.
- New tree painters: `place_cherry`, `place_jungle`, `place_swamp_oak`
  (`generation/world_generator.cpp`).
- Axe speed class applies to cherry/jungle logs (`gameplay/mining.hpp`).

## Biome blending (Stage A, by subagent + review)

- Climate smoothing: temperature/humidity averaged over a ±4 block 5-tap
  kernel, continentalness 3-tap (`world_generator.cpp::terrain_height`) —
  biome borders now transition over ~8-12 blocks.
- Per-vertex foliage tint: the mesher bilinearly samples the biome-tint field
  at each vertex world position through the 3x3 chunk snapshot
  (`renderer/mesh_builder.cpp::tint_at_world`) — C0-continuous gradients
  across biome borders, greedy-merged quads included.

## Caves & ores (Stage C)

- **Perlin-worm tunnels**: 0-2 worms per origin chunk, radius 1.7-3.2, Y 8-52,
  simulated per origin chunk and carved only where they land in the generating
  chunk → seamless cross-chunk tunnels (`world_generator.cpp::carve_caves`).
- **Ravines**: ~10% of chunks, elliptical crack 14-21 deep, coastal anchors
  skipped, margin keeps them chunk-local.
- **New ores** (blocks 90-92): copper (Y28-92, common), redstone (Y<20),
  lapis (Y<32); anisotropic ore noise (0.09/0.22/0.09) stretches veins into
  flat lenses. New items raw_copper/redstone/lapis (ids 276-278) with icons,
  drops and pickaxe tier gates (`mining.hpp`).

## Structures (Stage D)

- Village square: 3x3 cobblestone **well** (water core, posts + plank roof),
  **gravel paths** house→well, second **barn** building (6x5, log roof)
  (`generation/structure_generator.cpp`). Verified by probes: well water at
  (40,71,95), gravel path at (40,74,85).
- **Desert pyramid**: one per 20x20 chunk region, desert-anchored, stepped
  9x9 sandstone tiers with a buried gold/diamond chamber. `/tppyramid`
  command added for reachability.

## Audio (was: game ships silent)

- `scripts/gen_sounds.py` synthesizes 18 WAVs (stdlib-only, deterministic):
  dig per material, steps, place, hurt, eat, click, level-up, splash,
  zombie growl, skeleton rattle, loopable rain bed, 16 s ambient music pad →
  `assets/sounds/`.
- New `audio/sound_events.{hpp,cpp}` facade owns SoundManager, registers the
  event set, no-ops safely when headless. Wired in `game.cpp`: dig/step/place
  per material family, hurt, eat, mob-species calls, level-up jingle, rain
  loop intensity.

## Survival mechanics

- `survival::tick_environment` (pure, unit-tested `tests/test_environment.cpp`):
  vanilla fall damage (1 HP/block past 3), drowning (15 s air, then 1 HP/s),
  lava burns (2 HP/s). Wired per-tick in `game.cpp` with hurt feedback and
  death handling. Player fields: fall_distance/breath/drown_timer/lava_timer.

## Quests

- `mods/quests.json` grew 3 -> 10 quests (progression arc: cobble → iron →
  diamonds; kill arcs vs zombies/skeletons) and now uses proper Polish
  diacritics (the font fix below makes them renderable).
- `scripts/auto_test.py` mod expectations updated to the new catalog.

## Font (renderer/ui.*)

- Silkscreen TTF ladder (10-48 px rasters, Regular + Bold) replaces the
  single 10 px raster that blurred at menu scales; glyphs draw from tight
  raster bboxes with per-raster scaling (`renderer/ui.cpp`).
- **Polish diacritics fixed**: Silkscreen's cmap covers Latin-1 only — every
  codepoint ≥ U+0100 used to render as the `.notdef` box. Diacritics are now
  composed (base glyph raster + stamped accents) in the atlas.
- Full UTF-8 decode (1-4 byte), Polish letters accepted in text inputs,
  backspace pops whole codepoints.

## Lighting & weather

- Rain: dedicated world-vertical alpha-faded streaks replace blue block-dust
  flashes (the old emitter fell at 15 blocks/tick); constant terminal
  velocity; die on ground contact.
- Storms gray the sky/fog toward overcast blue and dim brightness
  (`renderer.cpp`, `weather` hooks); `/weather clear|rain|thunder` command.
- Hytale-style tinted shadows: cast-shadow lit component pulls violet
  (`chunk.frag`).

## Fixes found on the way

- `block.hpp` properties table had acacia rows misaligned with the enum since
  the acacia feature landed — birch logs/leaves rendered with acacia tiles.
  Rows moved to their real ids (84/85).
- Cherry/log leaves now drop sticks; logs drop themselves.
- `/gotobiome` (new, by subagent + hardening): biome name search spiral,
  climbs out of canopies, never teleports outside world bounds, prints biome
  frequency histogram when a biome is not found nearby.

## Gates (all green at commit time)

| Gate | Result |
|---|---|
| dev.bat build | BUILD OK |
| dev.bat test (unit) | 210/210 (7 new environment tests) |
| auto_test.py | 84/84 |
| visual_test.py | 7/7 |
| shader_test.py | 6/6 |
| graphics_test.py | 5/5 |
| perf_test.py | mspt 0.3-1.0, no regression |

## Known limitations / queued

- Biome-aware mob variants (husk in desert, stray in snowy) need model
  assets — queued for technical-artist (`make_husk_stray.py` pattern exists).
- Desert ~0% near spawn (rare band) — reachable via /gotobiome in far worlds.
- Beach band still ~25% (continentalness range compressed); a deeper
  terrain-shape pass (3D density, oceans) is wave-4 material.
- Weather not synced in multiplayer (host-only state).
