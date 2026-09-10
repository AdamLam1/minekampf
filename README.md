# Minekampf

Hybryda Minecraft + Hytale pisana w C++20 / OpenGL 4.6 — voxelowy świat z pipeline'em graficznym w stylu shaderpacków (Iris/BSL).

![Build](https://img.shields.io/badge/build-CMake%20%2B%20Ninja-blue) ![Engine](https://img.shields.io/badge/API-OpenGL%204.6-9cf) ![Std](https://img.shields.io/badge/C%2B%2B-20-00599c)

## Cechy

- **Świat voxelowy**: greedy meshing, chunki 16×256×16, **świat 32×32 chunków**, **13 biomów** (m.in. Jungle, Swamp, Cherry Grove, Flower Forest) z płynnymi granicami i tintami per-vertex, **jaskinie-karvery + ravines**, rudy (węgiel, żelazo, **miedź**, złoto, **lapis**, **redstone**, diament), struktury (dungeons, wioski ze studnią/ścieżkami/stodołą, **pustynne piramidy**), Nether, płyny, redstone, crafting/smelting/enchanting, **10 questów** z NPC w wioskach, mody (`mods/`).
- **Dźwięk**: proceduralny zestaw startowy (kopanie per materiał, kroki, place, hurt, jedzenie, level-up, deszcz, ambient music) — `scripts/gen_sounds.py` → `assets/sounds/`.
- **Przetrwanie**: głód/XP/regen + **obrażenia od upadku, topienia i lawy**; komendy `/weather`, `/gotobiome`, `/tppyramid`.
- **Pipeline graficzny shaderpack-style**: mapa cieni 4096 z soft-PCF (obracany dysk), POM na teksturach, SSR na wodzie, Fresnel + poświata słońca, cumulusy z samocieniowaniem, księżyc/gwiazdy, god rays, SSAO, bloom, ACES tonemapping, kolorowe światło (ciepłe pochodnie z flickerem, złota godzina, nocny ambient księżycowy).
- **Presety jakości**: `/quality low|medium|high` (rozmiar mapy cieni, zasięg, tapki PCF, POM, SSR, chmury) — także w panelu opcji. **Render scale** `/render_scale 0.5-1.0` (świat w obniżonej rozdzielczości, UI natywny) + **cienie chmur** na terenie. F3 = overlay diagnostyczny (FPS, klatka, jitter, chunky).
- **Moby**: Blockbench (`.geo.json`) → proceduralne rigi z animacją chodu/ataku.
- **Multiplayer (co-op 2–8 graczy, w toku — gałąź `feature/multiplayer`)**: listen server (host autorytatywny), streaming chunków (sekcje paletowe + zlib), zdalni gracze z interpolacją (proceduralny humanoid), synchronizacja bloków (batch per tick + interest management), czat i sync czasu. Menu *Multiplayer → Hostuj/Dołącz*; CLI: `--host [port]`, `--join <ip> <port> <nick>`.
- **Narzędzia testowe**: E2E przez API automatyzacji TCP (`--auto-play --automation-port`), testy pikselowe (`scripts/shader_test.py`, `graphics_test.py`, `visual_test.py`), testy gameplay (`auto_test.py`, 84 asercje), testy jednostkowe (210).

## Budowanie

Wymagane: Visual Studio 2022+ (MSVC, vcvars64), CMake ≥ 3.25, Ninja, Python 3 + Pillow (skrypty testowe).

```bat
cd game
scripts\dev.bat build      # konfiguracja + kompilacja (preset `release`)
scripts\dev.bat run        # uruchomienie gry
scripts\dev.bat test       # testy jednostkowe
```

Binarka ląduje w `game/build/release/bin/minekampf.exe` (shadery/assety kopiowane obok).

## Testy

```bash
cd game
python scripts/auto_test.py      # 84 asercje gameplay przez API automatyzacji
python scripts/mp_test.py        # E2E multiplayer: dwie instancje (host + klient)
python scripts/mp_sync_test.py   # głęboki sync multiplayer: pozycje graczy, akcje bloków, czat
python scripts/shader_test.py    # woda/Fresnel/glinty/cienie — asercje pikselowe
python scripts/graphics_test.py  # pochodnia/tekstury/woda
python scripts/visual_test.py    # HUD, dzień/noc, wydajność
```

## Struktura

```
game/
  src/           # engine + gameplay (C++20)
    renderer/    # OpenGL 4.6: chunky, shadery, moby, UI
    world/       # chunki, lighting (BFS sky/block), generacja
    gameplay/    # gracz, moby, crafting, questy, redstone
    core/        # pętla gry, settings, automation server
    shaders/     # GLSL: chunk, sky, post, mob, hand, ui
  assets/        # tekstury (atlas 4-warstwowy), fonty, modele Blockbench
  scripts/       # dev.bat, testy E2E, texkit (malarz tekstur)
  docs/          # ADR-y, pipeline assetów
```

## Licencja

Wszelkie prawa zastrzeżone © 2026 AdamLam1.
