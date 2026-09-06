# Protokół współpracy studia Minekampf

Ten dokument definiuje, jak role-studenci (skille w `.agents/skills/`) pracują
razem — także wtedy, gdy działają w różnych sesjach agenta, w różnym czasie.
Każda sesja w tym repo może wcielić się w dowolną rolę; tożsamość roli wynika
z aktualnego zadania, nie z sesji.

## 0. Zasada nadrzędna

**Nikt nie pracuje w izolacji.** Zanim zaczniesz, sprawdź co robią inni.
Gdy czegoś ci brakuje, poproś. Gdy skończysz, zostaw ślad. Wszystko przez
dwa pliki w tym katalogu i narzędzia ZCode — nigdy przez „zakładam, że
ktoś się tym zajmie".

## 1. Kanały komunikacji — kiedy który

| Sytuacja | Kanał | Dlaczego |
|---|---|---|
| Drobna wiedza/umiejętność innej roli (np. odpowiedź „jak działa X") | **Wczytaj SKILL.md tej roli i zrób sam** — potem krótka wiadomość `[FYI]` do `chat.md` | Najtańsze; nie blokujesz się na odpowiedź |
| Realna praca dla innej roli (np. graphic designer potrzebuje nowego bloku w kodzie) | **`[OPEN]` wiadomość w `chat.md` + wiersz w kolejce `BOARD.md`** | Odbiorca znajdzie to w kolejnej sesji; praca jest śledzona |
| Zajmujesz większy kawałek kodu (pliki, gałąź) | **Wiersz „Aktywne prace" w `BOARD.md`** | Zapobiega konfliktom równoległych sesji (zwłaszcza `feature/multiplayer`) |
| Podzadanie równoległe w własnej sesji (szukanie po repo, wyizolowana praca) | **Agent tool ZCode**: subagent `general-purpose` (pełne narzędzia) lub `Explore` (tylko czytanie/szukanie) | Szybkość; prompt subagenta musi być samowystarczalny (bez kontekstu rozmowy) |
| Kompilacja wiedzy z wielu plików w jedno wnioski | Subagent `Explore` z „medium"/„very thorough" | Zwraca wnioski, nie surowe pliki |
| Decyzja zmieniająca zakres/architekturę lub blokująca >1 rolę | **Eskalacja do użytkownika** (przez producer) | Użytkownik decyduje o kierunku, nie role |

## 2. Czat między rolami — `chat.md`

Plik append-only: **nowe wiadomości na dole**. Odpowiedź wklejaj pod
wiadomością jako cytat (`>`). Zmiana statusu = edycja tagu w nagłówku
(`[OPEN]` → `[DONE]`), nie kasowanie treści.

Format nagłówka (jedna linia, potem treść):

```
## [RRRR-MM-DD GG:MM] <z-roli> → <do-roli> | [OPEN|ANSWERED|DONE|FYI] | <temat>
<konkretne pytanie/zlecenie: co, dlaczego, gdzie, do kiedy, pliki>
> [RRRR-MM-DD GG:MM] <do-roli>: <odpowiedź/ustalenie>
```

- `[OPEN]` — czeka na odbiorcę; `[ANSWERED]` — odbiorca odpisał, sprawa
  trwa; `[DONE]` — ustalone i wykonane; `[FYI]` — tylko informacja (bez akcji).
- Zlecenie pisz **wykonalne bez dopytywania**: co dokładnie, w jakich plikach,
  kryterium ukończenia. Przykład: nie „potrzebuję tekstury", tylko
  „tekstura `AmberOre.png` 64×64 w stylu `Stone.png` (noise seed 7), drop
  do craftingu — realizuje game-designer recepturę w `mods/example_recipes.json`".
- Adresatami mogą być też role jako grupa (`→ @wszyscy`) — np. ogłoszenie
  zmiany, która dotyka wielu obszarów.
- Przed dopisaniem nowej wiadomości sprawdź, czy temat już nie istnieje —
  dopisuj odpowiedź do istniejącego wątku zamiast tworzyć duplikat.

## 3. Tablica zadań — `BOARD.md`

Sekcja „Aktywne prace" = umowa nie-kolidowania. Zasady:

1. **Przed startem** większej pracy (nowy system, dotknięcie `game.cpp`,
   plików `network/`, shaderów): sprawdź tabelę. Koliduje z czyjąś pracą?
   Najpierw wiadomość w `chat.md`, nie pobiegnij obok.
2. **Zajmij wiersz**: kto (rola + „sesja <data>"), co, gałąź/pliki, status
   (`w-toku` / `w-recenzji` / `gotowe`). Status aktualizuj na bieżąco.
3. **Po zakończeniu**: status `gotowe` + wynik bramek jakości w kolumnie
   notatki. Wiersze `gotowe` starsze niż 7 dni kasuj (historia jest w git
   i `chat.md`).
4. Producer ma prawo czyścić i porządkować tablicę; reszta ról tylko
   edytuje swoje wiersze i dodaje do kolejki.

## 4. Wspólne budowanie narzędzi testowych

Narzędzia testowe są własnością wspólną (katalog `game/scripts/`). Gdy
jakakolwiek rola stwierdzi, że brakuje narzędzia (np. graphic designer chce
automatyczny porównywarkę palet, game designer — symulator balansu dropów):

1. **Propozycja**: `[OPEN]` w `chat.md` do `qa-lead` (co ma robić, jaki
   problem rozwiązuje, jak mierzymy sukces).
2. **Specyfikacja**: qa-lead potwierdza kształt i odpowiedzialności
   (kto pisze, kto weryfikuje — zwykle: autor = zgłaszająca rola lub
   gameplay-dev, akceptacja = qa-lead).
3. **Implementacja** zgodna z konwencjami repo (patrz niżej).
4. **Rejestracja**: wiersz w tabeli inwentarza testów w skillu
   `minekampf-qa-lead` + wzmianka w README, jeśli narzędzie jest dla graczy.

Konwencje narzędzi (jak istniejące skrypty): Python 3 + Pillow/stdlib,
argparse, **samodzielnie uruchamiają i zabijają instancję gry** (wzorzec
`auto_test.py`/`ai_shot.py`; po testach `scripts\dev.bat kill`), asercje na
danych z automation API (`get_state`), nie na sleep/timingach; deterministyczne
(seed), docstring z usage w nagłówku. Testy jednostkowe → `game/tests/` +
rejestracja w `game/tests/CMakeLists.txt`.

## 5. Subagenci (Agent tool) — wzorce

- **`Explore`** do rozpoznania („znajdź wszystkie miejsca, gdzie X"): 1
  wywołanie zamiast 20 grepow; zwróć wnioski do własnego kontekstu.
- **`general-purpose`** do zamkniętych prac równoległych: „zbuduj i uruchom
  test X, zwróć wynik PASS/FAIL + log". Prompt = pełne zadanie (ścieżki,
  komendy, kryterium), bo subagent nie widzi Twojej rozmowy.
- Odpalaj niezależne subagenty **równolegle** (w jednym bloku narzędzi).
- Klamra jakości obowiązuje też subagentów — prompt zawsze zawiera wymagane
  komendy testowe.

## 6. Eskalacja i konflikty

- **Konflikt plików/gałęzi**: wykryty = zatrzymaj się, zostaw `[OPEN]` do
  roli-posiadacza wiersza na tablicy, zaproponuj podział; sam nie rebase'uj
  cudzej pracy. Przed startem zawsze `git fetch` + `git status`.
- **Niejasny priorytet / koszt > korzyść / zmiana zakresu**: producer zbiera
  i eskaluje do użytkownika — role nie przepisują zaakceptowanych decyzji.
- **Blokada >1 sesji roboczych na tym samym problemie**: eskaluj od razu,
  nie kręć kołem.
- Komit/push tylko na wyraźną prośbę użytkownika (albo gdy trwający plan
  jawnie tego wymaga). Destructive akcje (kasowanie plików poza swoim
  zadaniem, reset gita) — zawsze poza protokołem, do użytkownika.

## 7. Higiena

- `chat.md`: tematy `[DONE]` starsze niż 14 dni przenoś na koniec do sekcji
  „Archiwum" (na dole pliku), żeby aktywna strefa była czytelna.
- `BOARD.md` i `chat.md` to pliki robocze — komituj je razem z pracą, której
  dotyczą, żeby inne sesje (i inne maszyny) widziały stan.
- Koniec sesji = obowiązkowe: zamknij/oddaj wiersze na tablicy, dokończ
  wątki (`[ANSWERED]`/`[DONE]`), zostaw `[FYI]` z tym, co odkryłeś, a co
  przyda się innym.
