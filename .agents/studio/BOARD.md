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

## Kolejka (proponowane, nikomu nie przypisane)

| Zadanie | Zgłosił | Kto by się przydał |
|---|---|---|
| Tinted shadows a la Hytale (fiolet w cieniach) — research §4.1 | studio-head | engine-dev |
| Deszcz: wydłużone krople + alpha blend zamiast czarnych kwadracików — §4.2 | studio-head | engine-dev |
| `player.bbmodel` + skin 64×64 (log: „no model for player") — §4.3 | studio-head | technical-artist |
| Sesja zdjęciowa mobów w grze (zombie/cow/golem) — ocena modeli po poprawkach światła | studio-head | technical-artist + qa-lead |

## Handoffy otwarte

Patrz tagi `[OPEN]` w `chat.md` — ta sekcja tylko sumuje (rola → rola, temat).

---
*Historia zakończonych prac: git log + archiwum na dole `chat.md`.*
