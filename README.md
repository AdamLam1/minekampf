# Minekampf

Hybryda Minecraft + Hytale pisana w C++20 / OpenGL 4.6 — voxelowy świat z pipeline'em graficznym w stylu shaderpacków (Iris/BSL).

![Build](https://img.shields.io/badge/build-CMake%20%2B%20Ninja-blue) ![Engine](https://img.shields.io/badge/API-OpenGL%204.6-9cf) ![Std](https://img.shields.io/badge/C%2B%2B-20-00599c)

## Cechy

- **Świat voxelowy**: greedy meshing, chunki 16×256×16, biomy, jaskinie, struktury (dungeons, wioski), Nether, płyny, redstone, crafting/smelting/enchanting, questy, mody (`mods/`).
- **Pipeline graficzny shaderpack-style**: mapa cieni 4096 z soft-PCF (obracany dysk), POM na teksturach, SSR na wodzie, Fresnel + poświata słońca, cumulusy z samocieniowaniem, księżyc/gwiazdy, god rays, SSAO, bloom, ACES tonemapping, kolorowe światło (ciepłe pochodnie z flickerem, złota godzina, nocny ambient księżycowy).
- **Presety jakości**: `/quality low|medium|high` (rozmiar mapy cieni, zasięg, tapki PCF, POM, SSR, chmury) — także w panelu opcji.
- **Moby**: Blockbench (`.geo.json`) → proceduralne rigi z animacją chodu/ataku.
- **Narzędzia testowe**: E2E przez API automatyzacji TCP (`--auto-play --automation-port`), testy pikselowe (`scripts/shader_test.py`, `graphics_test.py`, `visual_test.py`), testy gameplay (`auto_test.py`, 84 asercje), 168 testów jednostkowych.

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
