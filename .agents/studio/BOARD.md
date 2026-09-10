# Tablica zadań studia Minekampf

Zasady użycia: `.agents/studio/PROTOCOL.md` §3. Zajmuj wiersz PRZED większą
pracą, aktualizuj status, kasuj `gotowe` starsze niż 7 dni.

## Aktywne prace

| Kto | Zadanie | Gałąź / pliki | Status | Notatka |
|---|---|---|---|---|
| network-dev (sesja trwała, patrz `.zcode/plans/`) | Multiplayer co-op MVP — Faza 0–1 **ukończona** (Faza 0 + MVP Fazy 1); w toku: Faza 2 (moby/dropy) | `feature/multiplayer`: `network/*`, `game.cpp`, `main.cpp` | w-toku | 203/203 testów, 11/11 mp_test.py, auto_test 78/84 (6 faili pre-istniejących na main). Praca niekomitowana — **rekomendacja: commit na `feature/multiplayer`** |
| producer (sesja 2026-09-06) | Rozpoznanie projektu + start PM/koordynacji studia (bez zmian w kodzie) | tylko `.agents/studio/*` | gotowe | Bramki: BUILD OK, 203/203 unit, auto_test 77/84 (7 faili pre-istniejących) |
| qa-lead (sesja 2026-09-06) | Głębokie testy multiplayer + narzędzie `scripts/mp_sync_test.py` (17 asercji) + instrumentacja QA w game.cpp (`mp_remote`/`chat` w get_state, `/say` `/mine` `/place`) | `feature/multiplayer`: game.cpp (get_state+komendy), scripts/mp_sync_test.py, README, ta tablica | gotowe | mp_sync 17/17, mp_test 11/11, unit 203/203, auto_test 78/84 (6 pre-istniejących, bez regresji). 2 bugi → `[OPEN]` chat → network-dev: re-request chunków po dalekim tp, klient /setblock omija hosta |
| studio-head/engine-dev (sesja 2026-09-06) | Overhaul look&feel fala 1: shaderpack anty-„szklaność" + fix missing-texture stone + research MC/Hytale (`.agents/studio/research_look_and_feel.md`) | `src/shaders/chunk.frag`, `post.frag`, `renderer.cpp`, `dev/asset_dump.cpp`, usunięty `bin/assets/textures/Stone.png`, `scripts/shader_test.py` (A5 time 0.62), `scripts/graphics_test.py` (matcher sticka) | gotowe | Bramki: shader 6/6, graphics 5/5, visual 7/7, unit 203/203, auto_test 78/84 (6 pre-istniejących, zero regresji). Zrzuty przed/po w `.agents/studio/`. Fala 2 (tinted shadows, deszcz, player model) w kolejce — patrz research §4 |
| studio-head + role (sesja 2026-09-06, fala 3) | Cel użytkownika „maksimum ficzerów": **font** (multi-size TTF, komponowane polskie glify, UTF-8/PL input), **światło** (tinted shadows, deszcz alpha-streaki + overcast + `/weather`), **worldgen overhaul** (świat 32×32, 4 nowe biomy + blending per-vertex, worm caves + ravines, miedź/redstone/lapis, studnia/ścieżki/stodoła/piramida), **mechaniki** (fall/lava/drowning), **audio** (18 syntezowanych dźwięków + wire), **quest log zawartość** (10 questów PL) | `feature/multiplayer`: rozległy diff — pełna lista w `.agents/studio/worldgen_report.md` | gotowe | Bramki: build OK, unit 210/210, auto_test 84/84, visual 7/7, shader 6/6, graphics 5/5, perf OK. NIE komitowane (czeka na decyzję użytkownika). Kolejka: husk/stray modele, wave-4 terrain |

| perf-engineer/engine-dev (sesje 2026-09-06/07) | Sesja 1: „ścina, mało płynna” + Sesja 2: „za słabo zoptymalizowana, dopracuj grafikę” — research Sodium/OptiFine, instrumentacja (FPS/jitter/CPU/GPU w get_state), shadow cull + incremental budgeted pass, greedy merge fix, debug context off, streaming budgets, **render scale** (/render_scale, OptiFine), **compact vertex 32→24 B** (Sodium), **cienie chmur**, F3 double-toggle fix + nowe metryki, graphics_test torch/leaf rigi deterministyczne | `renderer/renderer.*`, `renderer/chunk_mesh.*`, `renderer/mesh_builder.cpp`, `shaders/chunk.vert/frag`, `gameplay/game.*`, `core/settings.hpp`, `scripts/perf_opt15-17*.json` | gotowe | **GPU/klatkę ~70 → ~6-8 ms; steady 12→31-32 fps; jitter 90→34 ms** (DisplayLink = hard cap ~30 fps). Bramki: 210/210, 84/84, 7/7, 6/6, 5/5, perf OK. Dodatkowo: fix bujania pochodni (materiał 40 = statyczny cross; wiatr tylko na trawie/kwiatach). NIE komitowane |

## Kolejka (proponowane, nikomu nie przypisane)

| Zadanie | Zgłosił | Kto by się przydał |
|---|---|---|
| Sodium-style shared geometry buffer + glMultiDrawElementsIndirect (1 draw zamiast ~100 w shadow pass; AMD GL = wysoki koszt/draw) | perf-engineer | engine-dev |
| `player.bbmodel` + skin 64×64 (log: „no model for player") — §4.3 | studio-head | technical-artist |
| Sesja zdjęciowa mobów w grze (zombie/cow/golem) — ocena modeli po poprawkach światła | studio-head | technical-artist + qa-lead |
| Husk/stray: modele .geo.json + biome-aware spawny (desert/snowy) — pattern `make_golem_example.py` | studio-head | technical-artist |
| Wave-4 terrain: 3D density (overhangi), oceany, kompresja pasa beach (~25%) | studio-head | level-designer + engine-dev |
| Axes & shovels jako itemy (klasy narzędzi już liczone w mining.hpp, brak itemów/receptur) | studio-head | gameplay-dev |
| Zbroje: sloty inventory 36-39 + HUD gotowe, brak itemów zbroi | studio-head | gameplay-dev |

## Handoffy otwarte

Patrz tagi `[OPEN]` w `chat.md` — ta sekcja tylko sumuje (rola → rola, temat).

---
*Historia zakończonych prac: git log + archiwum na dole `chat.md`.*
