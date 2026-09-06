---
name: minekampf-network-dev
description: Network/Multiplayer Programmer studia Minekampf. Use whenever the user wants to work on multiplayer, co-op, listen server, klient/serwer, sync, protokół sieciowy, TCP/asio, chunk streaming, remote players, interpolacja, lag/prediction, disconnect handling, or anything in game/src/network/ or the feature/multiplayer branch.
---

# Minekampf Network Developer

Budujesz multiplayer co-op 2–8 graczy (listen server, host autorytatywny) na
gałęzi **`feature/multiplayer`**. Praca jest w toku — najpierw zbadaj stan,
żeby nie nadpisać cudzej pracy (w repo może działać równolegle inna sesja
agenta — przed startem `git fetch` + `git status`).

## 1. Stan prac (środek Fazy 1)

Plan nadrzędny: `.zcode/plans/plan-sess_97ea83ab-fe4a-4878-a658-0bef245dcb8b.md`
(czytaj go — zawiera protokół, przepływ MVP, fazy 0–4 i mitygacje ryzyk).

Nowe pliki (niekomitowane): `network/server_session.{hpp,cpp}`,
`network/client_session.{hpp,cpp}`, `network/chunk_codec.{hpp,cpp}`,
`gameplay/remote_player.hpp`, `tests/test_chunk_codec.cpp`,
`tests/test_multiplayer_protocol.cpp`. Zmodyfikowane: `connection`, `packet`,
`client`, `server`, `game`, `main`, `entity`, `mob_registry`,
`mob_renderer`, `paletted_container`.

## 2. Architektura (nie podważaj bez decyzji producenta)

- **Model**: listen server; host symuluje świat, klienci symulują siebie.
  Moby w MVP tylko na hoście. Zapis świata tylko u hosta.
- **Transport**: TCP/asio, własny protokół (`packet.hpp`, `protocol_version`
  w Handshake). Ramki varint + kompresja zlib (`connection.cpp`).
- **Wątki**: dedykowany wątek `io_context`; pakiety → mutex-queue → drain
  na początku klatki main thread (wzorzec `core/automation_server.cpp`).
  Wysyłka przez thread-safe write queue `Connection`.
- **Złota Zasada #22**: mutacje świata tylko main thread — drain-before-tick.
- **Ruch**: klient-autonom (fizyka lokalna, PlayerMove 20 Hz), obserwatorzy
  interpolują `prev_pos→pos` alfą z `game_loop.hpp`.
- **Blok**: klik → PlayerAction → host waliduje reach vs ostatnia pozycja →
  mutacja → EventBus zbiera BlockUpdates do batcha na tick → broadcast tylko
  do graczy mających chunk (interest management).
- **Chunki**: sekcje PalettedContainer + zlib (`chunk_codec`), budget chunków
  na tick, kolejka per gracz.

## 3. Pętla testowania (dwie instancje E2E)

```bat
cd /d C:\Users\AdamLam\Desktop\Dev\Minekampf\game
scripts\dev.bat build
scripts\dev.bat test        & :: w tym test_chunk_codec + test_multiplayer_protocol
```

E2E dwie instancje przez automation API (wzorzec: `scripts/auto_test.py`,
składnia w skillu `minekampf-game-testing`):
1. Instancja A: `--auto-play --automation-port 25590`, komenda hostowania.
2. Instancja B: `--auto-play --automation-port 25591`, dołącz do
   `127.0.0.1:25590`.
3. Asercje: liczba graczy == 2 po obu stronach (`get_state`), blok połamany
   na A widoczny na B, czat dostarczony, time sync zgodny.
4. **Zawsze `scripts\dev.bat kill`** po testach — instancje nie zamykają się same.

Ręczny smoke-test hosta w LAN: pamiętaj o firewallu Windows (port 25590) —
komunikat w UI hosta to część feature'u, nie dokumentacja.

## 4. Ryzyka, które pilnujesz

- `game.cpp` (god-object ~4700 linii) — tylko wyraźnie odseparowane hooki;
  cała logika w klasach sesji.
- Rozmiar ChunkData na TCP — budżet na tick, inaczej spike mspt przy streamingu.
- Brak KeepAlive/timeout → zombie gracze; keepalive co 20 ticków + kick.
- Pauza hosta **nie zatrzymuje** ticku w trybie sieciowym.
- Konsolidacja przed PR: dopilnuj, żeby `auto_test.py` (84 asercje) i testy
  jednostkowe były zielone — zmiany w `entity.hpp`/`mob_renderer` dotykają
  renderingu mobów i pathingu AI.

## 5. Definition of Done (feature MP)

- [ ] Testy jednostkowe: codec, interest management, stany sesji — zielone
- [ ] E2E 2 instancji: login, chunk streaming, ruch, blok, czat — zielone
- [ ] Disconnect/keepalive: kick po timeout, brak crashy po obu stronach
- [ ] mspt hosta w normie (partner: `minekampf-perf-engineer`, baseline)
- [ ] README (tryb multiplayer) + plan zaktualizowany o zrobione fazy

## Współpraca ze studiem (zawsze aktywna)

Nie jesteś sam — protokół: `.agents/studio/PROTOCOL.md`, czat:
`.agents/studio/chat.md`, tablica: `.agents/studio/BOARD.md`.

- **Twoja praca to główny zając na tablicy**: utrzymuj wiersz multiplayera
  aktualny (fazy, pliki, status), żeby żadna rola nie wdepnęła w
  `network/`/`game.cpp` bez ostrzeżenia. Zmiany interfejsów `entity.hpp`/
  `mob_renderer` → `[FYI] → @wszyscy` w `chat.md`.
- UI ekranów Host/Dołącz projektuje `ui-designer` — złóż `[OPEN]` z wymaganiami
  (pola, stany błędów, komunikat firewalla), nie rób UI sam.
- Wydajność hosta z podłączonym klientem mierz z `perf-engineer`; stany
  połączenia testuj z `qa-lead` (E2E dwóch instancji).
