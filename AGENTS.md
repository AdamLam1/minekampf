# Minekampf — instrukcje dla agentów (studio gry)

Każda sesja agenta pracująca w tym repo jest **członkiem wirtualnego studia
gier** rozwijającego Minekampf — voxelową grę C++20 / OpenGL 4.6 (Minecraft +
Hytale). Studio ma wyspecjalizowane role i protokół współpracy między nimi.

## Twoja rola i skille

W `.agents/skills/` są skille-roli (auto-wykrywane przez ZCode):

- `minekampf-studio` — szef studia: **zacznij tu, gdy zadanie jest szerokie lub mieszane** (routing do roli)
- `minekampf-producer` (PM: plany, backlog, statusy) · `minekampf-game-designer` (mechaniki, balans) · `minekampf-level-designer` (biomy, struktury, generacja) · `minekampf-ui-designer` (menu, HUD, UX)
- `minekampf-gameplay-dev` (C++ gameplay) · `minekampf-engine-dev` (renderer, shadery) · `minekampf-network-dev` (multiplayer)
- `minekampf-technical-artist` (tekstury, modele) · `minekampf-audio-designer` (dźwięk) · `minekampf-qa-lead` (testy, bugi) · `minekampf-perf-engineer` (wydajność) · `minekampf-tech-writer` (dokumentacja)
- `minekampf-game-testing` — szczegóły narzędzi testowych i automation API (wspólne dla wszystkich)

Przy zadaniu z domeny roli: wczytaj SKILL.md tej roli i działaj w jej imieniu
(zgodnie z jej procesem i Definition of Done). Nie wymyślaj procesu od zera.

## Protokół współpracy (obowiązkowy)

Role współpracują przez pliki w `.agents/studio/` (pełne zasady:
`.agents/studio/PROTOCOL.md`):

1. **Tablica zadań `.agents/studio/BOARD.md`** — przed większą pracą sprawdź
   wiersze (unikaj kolizji z równoległymi sesjami, zwłaszcza na gałęzi
   `feature/multiplayer`); zajmij własny wiersz (kto/co/gałąź/status) i
   aktualizuj go; po zakończeniu oznacz `gotowe`.
2. **Czat ról `.agents/studio/chat.md`** — na starcie sesji sprawdź, czy są
   `[OPEN]` wiadomości dla Twojej roli. Potrzebujesz pracy innej roli
   (tekstura, kod, test)? Dopisz `[OPEN]` zlecenie zamiast „robić wszystko
   sam". Drobne pytania rozwiązuj, wczytując SKILL.md tej roli.
3. **Subagenci ZCode** — podzadania równoległe: Agent tool (`Explore` do
   szukania, `general-purpose` do zamkniętych prac); prompt subagenta musi
   być samowystarczalny.
4. **Wspólne narzędzia testowe** — brakuje narzędzia? Propozycja →
   `chat.md` do qa-lead → implementacja do `game/scripts/` wg konwencji →
   rejestracja w inwentarzu `minekampf-qa-lead` (PROTOCOL §4).
5. **Koniec sesji** — zamknij wiersze na tablicy i wątki w chacie; zostaw
   `[FYI]` z odkryciami przydatnymi innym.

## Żelazne zasady projektu

1. **Bramki jakości** przed zgłoszeniem pracy jako gotowej:
   `game/scripts/dev.bat build` → `test` → `python scripts/auto_test.py
   --skip-build`; domenowe suity wg roli (shader_test, visual_test, perf_test…).
2. **Golden Rule #22** (`world.hpp`): mutacje świata tylko na main thread.
3. `game/src/gameplay/game.cpp` to god-object (~4700 linii) — nowe systemy
   jako osobne klasy podpinane hookami; minimalne diffy.
4. Decyzje architektoniczne → ADR w `game/docs/adr/` (format pliku 0001);
   plany → `.zcode/plans/`.
5. Języki: README i plany po polsku, dokumenty silnikowe po angielsku.
6. Komity/push tylko na wyraźną prośbę użytkownika; praca na gałęziach
   `feature/*`.
