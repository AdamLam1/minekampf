---
name: minekampf-game-testing
description: Autonomous testing, visual inspection (screenshot vision), navigation, and rendering verification for the Minekampf C++ voxel game. Use whenever the user asks to test the game, capture screenshots, view rendering shaders, explore the world, check FPS, or verify gameplay features.
---

# Minekampf Game Testing & AI Vision Skill

Fully automated testing for Minekampf. The game never needs to be closed by
hand — every tool below launches, drives, and kills its own game process.

## Project layout

- Game source: `game/src/` (C++20, CMake + Ninja preset `release`)
- Build tool: `game/scripts/dev.bat` (build / test / shot / runbg / kill)
- Binary: `game/build/release/bin/minekampf.exe` (+ `shaders/` and `assets/` next to it)
- Automated suite: `game/scripts/auto_test.py`
- One-shot vision helper: `game/scripts/ai_shot.py`
- Visual QA suite (pixel assertions): `game/scripts/visual_test.py`
- Graphics regression suite (torch orientation, texture crispness, leaves,
  water coverage): `game/scripts/graphics_test.py`
- Performance harness (mspt report): `game/scripts/perf_test.py`
- Texture painter / skin generator: `game/scripts/texkit.py`
- Offline asset sheets (atlas, icons, model previews):
  `build/release/bin/minekampf_assets.exe <out_dir>` (writes `atlas_albedo.png`,
  `icons.png`, `model_<species>.png`, ...)

## 0. Graphics/asset pipeline (graphics work)

- Docs: `game/docs/asset_pipeline.md`.
- Blockbench models: `game/assets/models/mobs/<species>.geo.json` + `.png`
  (Bedrock geometry) or a native `.bbmodel` project (per-face UVs + embedded
  texture). Custom species are data-driven: any new model file (plus optional
  `<species>.mob.json` sidecar with stats/AI/drops) is discovered at startup
  and spawnable via `/spawnmob <name>` — see `game/docs/asset_pipeline.md`.
  Procedural fallback when a file is missing. Bone names drive auto-animation
  (`head`, `leftArm`, `leg_front_left`, ...). Blockbench portable:
  `C:\Users\AdamLam\Tools\Blockbench.exe`.
- Block texture overrides: `game/assets/textures/<TileName>.png`
  (names = `Tile` enum in `world/block.hpp`, see `TILE_NAMES`).
- UI icons + HUD sprites: procedural, `renderer/item_icons.cpp`
  (`Icon cell = icon_cell(id)`; HUD hearts at cells 112+).
- UI design tokens: `renderer/ui_theme.hpp` (classic-MC palette).
- Text: Silkscreen TTF from `game/assets/fonts/` (metrics-compatible with the
  old 8x8 font — same 8px advance; bitmap fallback when the file is missing).

## 1. Full automatic test suite (preferred)

Builds (unless `--skip-build`), launches the game with an in-memory world
(`--auto-play --automation-port <free port>`), runs a scripted scenario
(teleports, `/goto water`, time of day, torch placement), captures screenshots,
checks state invariants, then TERMINATES the game. Exit code 0 = all passed.

```bat
cd /d C:\Users\AdamLam\Desktop\Dev\Minekampf\game
python scripts\auto_test.py               & :: build + run + kill
python scripts\auto_test.py --skip-build  & :: when already built
python scripts\auto_test.py --keep        & :: leave the game running after
```

Screenshots land in `%TEMP%\minekampf_auto_test\*.bmp` and are converted to
`.png` for vision. Always inspect them (Read tool) after a run:
- `01_overview_noon.png` — spawn overview at noon
- `02_beach_water.png` — water + sand close-up
- `03_water_above.png` — water surface from above
- `04_torch_night.png` — dynamic torch light at night
- `05_night_survival_mobs.png` — survival mode at midnight; verifies new mob
  species spawn (`night_mobs_spawn` check reads the `mobs` invariant from
  get_state) and that hunger/XP systems stay inert for creative bots
- `06_rig_daytime.png` — daylight view of the procedural mob skeletal rig
  (joint-pivot limbs, zombie raised-arm pose, species colors)
- `07_furnace_burning.png` — furnace loaded via `/fillfurnace` (coal + iron
  ore); `furnace_ignites` reads the `burning` invariant from get_state
- `08_skeleton_archer.png` — forced skeletons (`/spawnmob 4 skeleton`);
  `skeleton_shoots` reads the lifetime `arrows_fired` counter (flake-proof)
- `09_bow_hit.png` — player bow E2E: `/give bow`, `/select bow`,
  `/spawnmob 1 pig 4` (nearly stationary), `/aimnearest pig` +
  `/shoot 1`; `player_bow_hits_mob` reads the `arrow_hits` counter
- `10_dungeon.png` — buried dungeon E2E: `/tpdungeon` then `/probe` asserts
  the diamond prize on the pedestal, cobble shell walls, dark interior
- `11_quest_village.png` — quest NPC E2E: `/tpvillage` + `/probe` finds the
  quest_npc block; `/quest accept|turnin` drives both quests end to end
  (collect via /give, kill via real bow shots at `/spawnmob` zombies)
- `12_nether.png` — portal E2E: `/setblock` portal under the bot, dimension
  switch to Nether and back; the loop drops a portal under the falling bot
  every 0.7 s (Nether free-fall lasts several seconds)

## AI debugging

`MINEKAMPF_AI_DIAG=1` (environment) enables gated diagnostics written to the
game log (`bin/minekampf.log`): per-tick `[melee]` distance/cooldown/callback
state, `[arrow]` spawn/wall-despawn/mob-hit with the closest-sample distance,
and A* failure dumps with start-column neighbor block names. Default: silent.
auto_test.py sets it automatically for every run.

## 2. One-shot screenshot probe (launch → capture → kill)

```bat
cd /d C:\Users\AdamLam\Desktop\Dev\Minekampf\game
scripts\dev.bat kill                          & :: clean any stray instance first
python scripts\ai_shot.py --port 25590 shot C:\path\out.bmp --x 6 --y 75 --z 6 --pitch 20
```

`ai_shot.py` actions: `status`, `exec "<command>"`, `shot <file> [--x --y --z --yaw --pitch]`.
Add `--no-wait` to capture the main menu instead of waiting for gameplay.
When launching manually, ALWAYS pass `--auto-play` (skips the menu) and kill
the process afterwards (`scripts\dev.bat kill` or `taskkill /f /im minekampf.exe`).

## 3. In-engine automation API (TCP JSON, localhost)

Start the game with `--automation-port <port>` (combine with `--auto-play`).
One JSON per line, newline-terminated responses:

| Command | Payload | Effect |
|---|---|---|
| get_state | `{"cmd":"get_state"}` | state, pos, yaw/pitch, health, chunks, meshes, mobs, mspt, time_of_day |
| screenshot | `{"cmd":"screenshot","filename":"C:/path/out.bmp"}` | captures the CURRENT frame (deferred a few frames) |
| exec | `{"cmd":"exec","command":"/tp 1 2 3"}` | runs a console command, returns its chat reply |
| look | `{"cmd":"look","yaw":270,"pitch":10}` | aim camera (angles in DEGREES) |

Console commands: `/tp x y z`, `/time 0..1`, `/give <block> [n]`, `/gamemode m`,
`/setblock x y z <block>`, `/goto <block>` (teleport to nearest surface block),
`/fly`, `/sethome`, `/home`, `/kill`, `/help`.

## 4. Unit tests

```bat
cd /d C:\Users\AdamLam\Desktop\Dev\Minekampf\game
scripts\dev.bat test        & :: GoogleTest suite (build\release\bin\minekampf_tests.exe)
```

## 5. Build

```bat
cd /d C:\Users\AdamLam\Desktop\Dev\Minekampf\game
scripts\dev.bat build       & :: cmake --build --preset release (MSVC x64 via vcvars)
```

Notes:
- Shaders are copied to `bin/shaders` by the `minekampf_shaders` CMake target
  on EVERY build — shader-only edits need a rebuild (any `dev.bat build`).
- `--screenshot <path> --ticks N` = headless bot mode (in-memory world,
  screenshot after N ticks, auto-exit). `--ticks` default 500.
- The bot world is in-memory: no saves, no pollution of the world list.
