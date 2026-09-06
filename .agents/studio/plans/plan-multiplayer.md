# Multiplayer dla Minekampf — plan wdrożenia (listen server, co-op 2–8 graczy, TCP)

## Wynik analizy — co już masz (dużo!)

- **Pętla dual-loop 20 Hz** (`game/src/core/game_loop.hpp`): stały tick serwerowy + render z interpolacją (alpha). To dokładnie architektura Minecrafta — serwer logiki już w zasadzie istnieje w `Game::tick()` (game.cpp:1284).
- **Niedopięty moduł sieciowy** `game/src/network/` (asio TCP przez FetchContent, zlib): `Connection` (ramki varint + kompresja), `Client`, `Server` (broadcast), `PacketBuffer`, `packet.hpp` (Handshake/Login/KeepAlive/PlayerPosition/BlockUpdate/ChunkData), fabryka pakietów per stan. Kompilowany do biblioteki silnika, z testami `tests/test_network*.cpp`, ale **nie używany przez grę** (zero include'ów poza src/network).
- **Wzorzec wątku sieciowego już wypracowany**: `AutomationServer` (core/automation_server.hpp) — wątek + mutex-queue, drain na main thread co klatkę.
- **EventBus używany produkcyjnie**: `BlockBreakEvent`/`BlockPlaceEvent` publikowane w `block_interaction.cpp` (czysty lejek mutacji bloków), `PlayerMoveEvent` w game.cpp:1440. Idealne punkty zaczepienia broadcastu.
- **MobSpawner już multi-player-ready**: `try_spawn(..., const std::vector<Vec3>& player_positions)` — cap mobów per gracz liczony z listy pozycji.
- **Zapis**: NBT + regiony, `LevelStorage::save_chunk(const Chunk&, dim)` — baza do serializacji chunków na sieć. Autosave co 6000 ticków.
- **Świat**: chunki 16×256×16 z `PalettedContainer` (sekcje), zasada „main thread owns all world mutations" (Golden Rule #22 w world.hpp).
- **Czat już istnieje** (chat_log_, execute_command), są też stany menu i HUD.
- **Dług techniczny**: `game.cpp` to god-object (3962 linii); AI mobów celuje w `const Vec3* target_player_pos` (jedyny gracz).

## Decyzje architektoniczne

1. **Model: listen server, autorytatywny host.** Host symuluje świat (jak dziś), klienci symulują tylko siebie. Zero wymagań determinizmu/lockstepu — moby i RNG zostają po stronie hosta. Zapis świata tylko u hosta (istniejący LevelStorage).
2. **Transport: TCP/asio, własny protokół.** Przepisujemy `packet.hpp` na własne `enum class PacketId` z `protocol_version` w handshake (rezygnujemy z udawania ID Minecrafta 0x21/0x25 — to atrapa). Dla 2–8 graczy i 20 Hz TCP w LAN w zupełności wystarczy; UDP to ewentualna przyszłość.
3. **Wątki: jak AutomationServer.** Ded ​​ykowany wątek `io_context` (Server/Client); pakiety przychodzące → mutex-queue → drain na początku klatki main thread → wszystkie mutacje świata dalej single-threaded (szanujemy Golden Rule #22). Wysyłka przez thread-safe write queue `Connection` (już jest).
4. **Ruch**: klient autonom (fizyka lokalna, wysyłka pos/rot 20 Hz, interpolacja `prev_pos→pos` z alpha po stronie obserwatorów — mechanizm już istnieje dla gracza). Zdalni gracze = kinematyczne encje, bez kolizji z lokalnym graczem na kliencie.
5. **MVP inventory lokalne** per gracz (drugi etap: dropy/pickupy). Hostuje się świat istniejący lub nowy — pełny flow z menu.

## Protokół (nowy `src/network/packets.hpp`)

- **Common**: Handshake(protocol_version, nazwa świata/port), Disconnect(reason), KeepAlive(id), ChatMessage(text).
- **C→S**: LoginStart(username) → ClientReady(center chunk) → PlayerMove(pos, yaw, pitch, on_ground, sneaking, sprinting) [20 Hz] → PlayerAction(type: BREAK/PLACE/ATTACK_MOB/INTERACT, block/entity, held item) → ChatMessage → RequestChunks(center, radius).
- **S→C**: LoginAccepted(player_id, seed, gamemode, time_of_day, spawn) → ChunkData(cx, cz, sekcje PalettedContainer, zlib) → ChunkAck/UnloadChunks → BlockUpdates(batch: pos+block_id) [na tick] → SpawnPlayer/DespawnPlayer(id, name, pos) → PlayerState(batch: id, pos, yaw, pitch, flags, swing) [20 Hz] → TimeSync(time_of_day) → ChatBroadcast(from, text) → PlayerList(add/remove, name, ping).

## Nowe pliki (praca na gałęzi `feature/multiplayer`)

- `src/network/packets.hpp` — przepisany protokół + rejestry serializacji.
- `src/network/server_session.{hpp,cpp}` — rejestr graczy (connection↔player_id, ostatnia pozycja, acknięte chunki, view radius), interest management (bloki/chunki tylko do graczy z chunkiem), broadcast po ticku, walidacje (reach, kick).
- `src/network/client_session.{hpp,cpp}` — maszyna stanów login→play, bufor interpolacji, kolejki do świata.
- `src/gameplay/remote_player.hpp` — encja zdalnego gracza (pos/prev_pos, yaw/pitch, flagi, nick, health) renderowana istniejącym **proceduralnym humanoid rig** (jak moby; zombie_arms=false) + billboard z nickiem. Blockbench player model/skiny — później.
- Pliki dotykane minimalnie: `game.cpp` (hooki drain/broadcast, stany `MultiplayerHost/Join`, ~200 linii), `block_interaction.cpp` (broadcast przez EventBus subskrypcji serwera), `game.hpp` (wskaźniki na sesje), `main.cpp` (flagi `--host`, `--join host:port`, `--name`).

## Przepływ MVP

- **Host**: menu „Multiplayer → Hostuj" (wybór świata istniejący) → `start_game()` + `ServerSession::start(port=25590)`. Tick: drain pakietów → symulacja jak dziś → broadcast (BlockUpdates batch, PlayerState, TimeSync) → keepalive co 20 ticków. Pauza hosta NIE zatrzymuje ticku gry (menu pauzy bez zatrzymania symulacji przy grze sieciowej).
- **Klient**: „Multiplayer → Dołącz" (IP:port, nick) → handshake/login → `LoginAccepted` → budowa świata z `ChunkData` (seed nie jest używany do regen po stronie klienta w MVP — chunki dostaje; worldgen pozostaje hostowy) → `Playing`. Wysyła `PlayerMove` co tick, `PlayerAction` na kliknięcie. Progresywne łamanie: predykcja lokalna + korekta z `BlockUpdates`.
- **Blok (łamanie/stawianie)**: klik klienta → `PlayerAction` → host waliduje dystans vs ostatnia pozycja gracza → `break_block/place_block` → EventBus publikuje → subskrypcja serwera zbiera zmiany do batcha na tick → broadcast do graczy mających chunk. Host stosuje swoje akcje bezpośrednio (loopback bez sieci).
- **Moby w MVP**: symulowane tylko na hoście; u klientów niewidoczne (Faza 2 doda snapshoty). Atak moba przez klienta → ignorowany w MVP (gracz ucieka 😉).

## Fazy

- **Faza 0 — fundament (1 PR)**: gałąź, nowy packets.hpp + adaptery starych testów frame'ów, szkielety Server/ClientSession, `remote_player.hpp`, seam `nearest_player_pos(Vec3)` w game.cpp dla AI (na razie zwraca hosta), brak zmian gameplay.
- **Faza 1 — MVP co-op grywalny**: menu Host/Dołącz, login flow, chunk streaming (serializator sekcji PalettedContainer + zlib, ~10–30 KB/chunk), ruch zdalnych graczy z interpolacją, render humanoid + nick, broadcast bloków, czat, time sync, disconnect/keepalive. **Testy**: jednostkowe (chunk codec, interest mgmt, session states — rozszerzyć 2 istniejące pliki test_network) + **E2E dwie instancje**: `minekampf.exe --auto-play --automation-port X` i nowe komendy `automation_exec`: `mp_host <port>`, `mp_join <ip:port> <name>`, asercje `mp_players_count==2`, `block_at` po obu stronach → rozszerzenie `scripts/auto_test.py` (mechanizm TCP automation już istnieje — duży atut).
- **Faza 2 — żywy świat**: moby host-autorytatywne: SpawnMob/DespawnMob + MobSnapshot (10 Hz, tylko moby w zasięgu view któregokolwiek gracza), interpolacja; `PlayerAction ATTACK_MOB`; pociski host-side; sync zdrowia graczy; pogoda w TimeSync.
- **Faza 3 — spójność i UX**: lista graczy (Tab), join/leave w chacie, respawn śmierć zdalnych, dźwięki kroków/uderzeń zdalnych, walidacje (speed/reach → kick), limit graczy (8), obsługa zgubienia połączenia (timeout → usunięcie gracza), autosave hosta.
- **Faza 4 — opcje późniejsze**: Nether/sync wymiarów, dedykowany headless `--server` (wymaga wydzielenia symulacji z Game), UDP LAN discovery, adaptive compression, hasło/whitelist.

## Ryzyka → mitygacje

- **God-object game.cpp** → sesje jako osobne klasy; Game dostaje tylko hooki; minimalny dotyk.
- **Golden Rule #22 (mutacje tylko main thread)** → drain kolejki na początku klatki; wysyłka asynchroniczna.
- **Konflikt z agentem sess_4f487d… działającym w tym repo** → cała praca na gałęzi `feature/multiplayer`, przed startem `git fetch/status`, po merge rebase; zmiany w game.cpp ograniczone do wyraźnie odseparowanych hooków.
- **Firewall Windows przy hostowaniu** → komunikat w UI hosta (port 25590), timeout przy braku połączenia.
- **Rozmiar chunków na TCP** → sekcje PalettedContainer + zlib; wysyłka max N chunków/tick (budget), kolejka per gracz.
- **Pauza/host wyłącza grę** → kick wszystkich z powodem; autosave przed zamknięciem.

## Kolejność realizacji (start od Fazy 0)

1. Gałąź + Faza 0 (fundament protokołu i szwy) → testy zielone.
2. Faza 1 w 3 krokach: (a) host-side session + broadcast bloków/czasu, (b) klient-side join + chunk streaming, (c) UI menu + render graczy + czat → E2E 2 instancje na zielono.
3. Demo MVP: dwa okna gry na jednym PC (host + join 127.0.0.1:25590).