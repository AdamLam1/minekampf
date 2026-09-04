#!/usr/bin/env python3
"""Minekampf fully-automated in-game test runner.

Launches the game with an in-memory world + automation API, drives it through
a scripted scenario (teleports, time of day, block placement), captures
screenshots for AI vision review, checks state invariants, then KILLS the
game process. The user never has to touch the game window.

Usage:
  python auto_test.py [--keep] [--skip-build] [--exe PATH] [--out DIR]
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_DIR = os.path.dirname(HERE)
DEFAULT_EXE = os.path.join(GAME_DIR, "build", "release", "bin", "minekampf.exe")


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


class Api:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.sock.settimeout(15)
        self.welcome = self.sock.recv(4096).decode().strip()

    def cmd(self, payload):
        self.sock.sendall((json.dumps(payload) + "\n").encode())
        buf = b""
        while b"\n" not in buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            buf += chunk
        return json.loads(buf.decode().strip())

    def state(self):
        return self.cmd({"cmd": "get_state"})

    def exec(self, line):
        return self.cmd({"cmd": "exec", "command": line})

    def look(self, yaw=None, pitch=None):
        p = {"cmd": "look"}
        if yaw is not None:
            p["yaw"] = yaw
        if pitch is not None:
            p["pitch"] = pitch
        return self.cmd(p)

    def shot(self, filename):
        return self.cmd({"cmd": "screenshot", "filename": filename})


class Runner:
    def __init__(self, api, outdir):
        self.api = api
        self.outdir = outdir
        self.results = []
        self.shots = []

    def check(self, name, ok, detail=""):
        self.results.append((name, bool(ok), detail))
        print(f"  [{'PASS' if ok else 'FAIL'}] {name} {detail}")

    def shot(self, name):
        path = os.path.join(self.outdir, name + ".bmp")
        resp = self.api.shot(path)
        # The capture is deferred a few frames server-side — wait for the file.
        f = resp.get("file", "")
        ok = resp.get("status") == "ok"
        if ok:
            for _ in range(50):
                if os.path.exists(f):
                    break
                time.sleep(0.1)
            ok = os.path.exists(f)
        self.check(f"shot:{name}", ok, f)
        if ok:
            self.shots.append(f)
        return ok

    def exec_check(self, name, line, expect=None):
        resp = self.api.exec(line)
        ok = resp.get("status") == "ok"
        if ok and expect:
            ok = expect in resp.get("msg", "")
        self.check(f"exec:{name}", ok, resp.get("msg", ""))
        return ok

    def wait(self, pred, timeout=90, poll=1.0, desc="condition"):
        t0 = time.time()
        last = None
        while time.time() - t0 < timeout:
            try:
                last = self.api.state()
                if pred(last):
                    return last
            except (OSError, json.JSONDecodeError):
                pass
            time.sleep(poll)
        return last


def build_arena(r, cx=60, cy=90, cz=60):
    """Flat 7x7 stone platform high above the terrain: removes water,
    caves and tree canopies from the dynamic-fight phases. MUST stay inside
    the bounded world (+-127 blocks) — setblock outside is a silent no-op."""
    r.api.exec(f"/tp {cx + 0.5} {cy + 3} {cz + 0.5}")  # load chunks first
    time.sleep(0.8)
    for dx in range(-3, 4):
        for dz in range(-3, 4):
            r.api.exec(f"/setblock {cx + dx} {cy} {cz + dz} stone")
    r.api.exec(f"/tp {cx + 0.5} {cy + 1} {cz + 0.5}")
    time.sleep(0.6)


def to_png(path):
    try:
        from PIL import Image
        png = path[:-4] + ".png"
        Image.open(path).save(png)
        return png
    except Exception as e:
        print(f"  [warn] PNG conversion failed: {e}")
        return None


def run_scenario(r):
    st = r.wait(lambda s: s.get("state") == "playing" and s.get("chunks", 0) >= 100, timeout=120)
    r.check("entered_playing", st is not None and st.get("state") == "playing", str(st and st.get("chunks")))
    if not st:
        return
    r.check("world_loaded", st.get("chunks", 0) >= 100, f"chunks={st.get('chunks')} meshes={st.get('meshes')}")

    # Console commands
    r.exec_check("help", "/help", "Commands:")
    r.exec_check("give_sand", "/give sand 16", "Gave")
    r.exec_check("gamemode", "/gamemode creative", "Game mode")

    # Teleport accuracy
    r.exec_check("tp", "/tp 6 80 6", "Teleported")
    time.sleep(1.0)
    st = r.api.state()
    pos = st.get("pos", [0, 0, 0])
    r.check("tp_position", abs(pos[0] - 6) < 1.5 and abs(pos[2] - 6) < 1.5, str(pos))

    # Day overview at spawn
    r.exec_check("time_noon", "/time 0.5", "Time set")
    r.api.look(0, 10)
    time.sleep(0.5)
    r.shot("01_overview_noon")

    # Beach & water close-up — found programmatically (seed-independent)
    r.exec_check("goto_water", "/goto water", "Found")
    r.exec_check("fly_on", "/fly", "Flying enabled")
    time.sleep(2.5)  # let chunks stream in
    r.api.look(35, 18)
    time.sleep(0.5)
    r.shot("02_beach_water")

    # Water from above (flat-surface check)
    r.exec_check("goto_water2", "/goto water", "Found")
    time.sleep(2.0)
    r.api.look(0, 55)
    time.sleep(0.5)
    r.shot("03_water_above")
    r.exec_check("fly_off", "/fly", "Flying disabled")

    # Torch at night (dynamic light)
    r.exec_check("tp_home", "/tp 6 80 6", "Teleported")
    r.exec_check("time_night", "/time 0.0", "Time set")
    r.exec_check("place_torch", "/setblock 9 68 6 torch", "Set torch")
    time.sleep(1.5)
    r.api.look(270, 5)  # face east (+X), toward the torch
    time.sleep(0.5)
    r.shot("04_torch_night")

    # Cactus in desert (may be out of range on this seed; screenshot anyway)
    r.exec_check("time_noon2", "/time 0.45", "Time set")

    # Survival-phase sanity: hunger/Xp systems must not touch a creative bot.
    st = r.api.state()
    r.check("creative_food_full", st.get("food") == 20, f"food={st.get('food')}")

    # Night survival check: switch to survival, force-spawn the new mob
    # species deterministically (/spawnmob test hook), confirm the live
    # simulation ticks them (state invariant `mobs`).
    r.exec_check("gamemode_survival", "/gamemode survival", "Game mode")
    r.exec_check("time_midnight", "/time 0.0", "Time set")
    r.exec_check("spawn_mobs", "/spawnmob 3", "Spawned")
    st = r.wait(lambda s: s.get("mobs", 0) >= 1, timeout=10, desc="forced mobs")
    st = r.api.state()
    r.check("forced_mobs_alive", st.get("mobs", 0) >= 1, f"mobs={st.get('mobs')} mspt={st.get('mspt')}")
    r.api.look(45, 8)
    time.sleep(0.5)
    r.shot("05_night_survival_mobs")

    # Restore bot-safe state (creative, noon), then pose the new skeletal rig
    # in daylight for visual inspection (species colors, joint pivots).
    r.exec_check("back_creative", "/gamemode creative", "Game mode")
    r.exec_check("time_noon3", "/time 0.45", "Time set")
    r.exec_check("spawn_rig_mobs", "/spawnmob 4", "Spawned")
    time.sleep(1.5)
    r.api.look(120, 15)
    r.shot("06_rig_daytime")

    # Furnace end-to-end: place block, load coal+iron ore, wait for the live
    # tick system to smelt (state invariant `smelted` counts output items).
    r.exec_check("place_furnace", "/setblock 10 67 10 furnace", "Set furnace")
    time.sleep(0.5)
    r.exec_check("fill_furnace", "/fillfurnace", "Filled furnace")
    burning = r.wait(lambda s: s.get("burning", 0) >= 1, timeout=8, desc="furnace ignite")
    st = r.api.state()
    r.check("furnace_ignites", st.get("burning", 0) >= 1, str(st))
    r.shot("07_furnace_burning")
    smelted = r.wait(lambda s: s.get("smelted", 0) >= 1, timeout=20, desc="smelt done")
    st = r.api.state()
    r.check("furnace_smelts_iron", st.get("smelted", 0) >= 1,
            f"smelted={st.get('smelted')} burning={st.get('burning')}")

    # Skeleton archer: force-spawn skeletons nearby; they must loose arrows
    # within line of sight. `arrows_fired` is a lifetime counter so a fast
    # despawning projectile can never race the poll (flake-proof).
    # Open shoreline = guaranteed long sightlines (kills the LOS flake).
    r.exec_check("goto_open_water", "/goto water", "Found")
    time.sleep(2.0)
    r.exec_check("spawn_skeletons", "/spawnmob 4 skeleton", "Spawned")
    fired = r.wait(lambda s: s.get("arrows_fired", 0) >= 1, timeout=30, poll=0.4, desc="arrows")
    st = r.api.state()
    r.check("skeleton_shoots", st.get("arrows_fired", 0) >= 1,
            f"arrows_fired={st.get('arrows_fired')}")
    r.shot("08_skeleton_archer")

    # Player bow E2E: give bow, spawn one zombie, auto-aim, fire. The
    # `arrow_hits` lifetime counter proves the player arrow hit a mob.
    build_arena(r)
    r.exec_check("give_bow", "/give bow", "Gave")
    r.exec_check("select_bow", "/select bow", "Selected")
    # Nearly-stationary pig on the open shore (wanders ~1/120 ticks), aimed
    # by species filter so earlier-phase mobs cannot steal the aim.
    r.exec_check("spawn_target", "/spawnmob 1 pig 2", "Spawned")
    time.sleep(0.5)
    hits = 0
    for attempt in range(4):
        r.api.exec("/aimnearest pig")
        shot = r.api.exec("/shoot 1")
        time.sleep(1.2)
        st = r.api.state()
        hits = st.get("arrow_hits", 0)
        if hits >= 1:
            break
    r.check("player_bow_hits_mob", hits >= 1, f"arrow_hits={hits}")
    st = r.api.state()
    r.check("player_arrow_fired", st.get("arrows_fired", 0) >= 1,
            f"arrows_fired={st.get('arrows_fired')}")
    r.shot("09_bow_hit")

    # Dungeon E2E: teleport into the buried room and probe its anatomy —
    # diamond prize on the pedestal, cobble shell, dark interior.
    tp = r.api.exec("/tpdungeon")
    r.check("tpdungeon_ok", "Teleported to dungeon" in tp.get("msg", ""), tp.get("msg", ""))
    parts = tp.get("msg", "").split()
    dx, dy, dz = int(parts[-3]), int(parts[-2]), int(parts[-1])
    time.sleep(1.0)  # let chunks stream around the new position
    prize = r.api.exec(f"/probe {dx} {dy + 1} {dz}").get("msg", "")
    r.check("dungeon_prize", prize.endswith("diamond_ore"), prize)
    interior = r.api.exec(f"/probe {dx + 1} {dy} {dz}").get("msg", "")
    r.check("dungeon_interior_air", interior.endswith("air"), interior)
    wall = r.api.exec(f"/probe {dx + 5} {dy} {dz}").get("msg", "")
    r.check("dungeon_wall", wall.endswith("cobblestone") or wall.endswith("stone"), wall)
    pedestal = r.api.exec(f"/probe {dx} {dy} {dz}").get("msg", "")
    r.check("dungeon_pedestal", pedestal.endswith("cobblestone"), pedestal)
    st = r.api.state()
    r.check("dungeon_underground", st.get("pos", [99])[1] < 52, str(st.get("pos")))
    r.shot("10_dungeon")

    # ---- Quest NPC E2E ----
    # 1) Village + NPC block present.
    tpv = r.api.exec("/tpvillage")
    r.check("tpvillage_ok", "Teleported to village" in tpv.get("msg", ""), tpv.get("msg", ""))
    parts = tpv.get("msg", "").split()
    vx, vy, vz = int(parts[-3]), int(parts[-2]), int(parts[-1])
    time.sleep(1.0)
    npc = r.api.exec(f"/probe {vx} {vy + 1} {vz}").get("msg", "")
    r.check("quest_npc_present", npc.endswith("quest_npc"), npc)

    # 2) Collect quest: accept, verify state, turn in with materials.
    r.exec_check("quest_accept_collect", "/quest accept 0", "Przyjeto")
    st = r.api.state()
    r.check("quest_collect_active", st.get("quest_id") == 0 and st.get("quest_state") == 1,
            str({k: st.get(k) for k in ("quest_id", "quest_state", "quest_progress")}))
    r.exec_check("give_gold_ore", "/give gold_ore 3", "Gave")
    turnin = r.api.exec("/quest turnin")
    r.check("quest_collect_turnin", "Nagroda" in turnin.get("msg", ""), turnin.get("msg", ""))
    st = r.api.state()
    r.check("quest_collect_done", st.get("quest_id") == 0 and st.get("quest_state") == 3,
            str({k: st.get(k) for k in ("quest_id", "quest_state")}))

    # 3) Kill quest: accept, then really kill 2 zombies with the bow.
    r.exec_check("quest_accept_kill", "/quest accept 1", "Przyjeto")
    # Sky arena: no seabed drowning, no canopy pathing traps, and /killmobs
    # removes the accumulated herd so the aim cannot be stolen.
    build_arena(r, cx=100, cy=90, cz=100)
    r.exec_check("clear_arena", "/killmobs", "Despawned")
    r.exec_check("spawn_z1", "/spawnmob 1 zombie 1", "Spawned")
    r.exec_check("spawn_z2", "/spawnmob 1 zombie 1", "Spawned")
    # Zombies have 20 HP vs 9 damage per arrow -> 3 hits per kill, 6 total.
    # Point-blank spawns keep the target on the platform; respawning keeps
    # the pair alive when a knockbacked zombie walks off the edge.
    for attempt in range(20):
        st = r.api.state()
        if st.get("quest_state") == 2:  # ReadyToTurnIn
            break
        if st.get("mobs", 0) < 2:
            r.api.exec("/spawnmob 1 zombie 1")
            time.sleep(0.5)
        aim = r.api.exec("/aimnearest zombie").get("msg", "")
        r.api.exec("/shoot 1")
        time.sleep(1.0)
        d = r.api.state()
        print(f"    [dbg-qkill] a{attempt}: {aim} mobs={d.get('mobs')} "
              f"fired={d.get('arrows_fired')} hits={d.get('arrow_hits')} "
              f"q={d.get('quest_progress')} state={d.get('quest_state')}")
    st = r.api.state()
    r.check("quest_kill_progress", st.get("quest_state") == 2 and st.get("quest_progress") >= 2,
            str({k: st.get(k) for k in ("quest_state", "quest_progress")}))
    turnin2 = r.api.exec("/quest turnin")
    r.check("quest_kill_turnin", "Nagroda" in turnin2.get("msg", ""), turnin2.get("msg", ""))
    st = r.api.state()
    r.check("quest_kill_done", st.get("quest_state") == 3,
            str({k: st.get(k) for k in ("quest_id", "quest_state")}))
    r.shot("11_quest_village")

    # ---- Survival damage & death/respawn E2E ----
    build_arena(r, cx=80, cy=90, cz=80)
    r.exec_check("clear_arena2", "/killmobs", "Despawned")
    r.exec_check("gamemode_survival2", "/gamemode survival", "Game mode")
    r.exec_check("spawn_attacker", "/spawnmob 1 zombie 2", "Spawned")
    hurt = r.wait(lambda d: d.get("health", 20) < 20, timeout=30, poll=0.5, desc="zombie hits player")
    for _ in range(6):
        aim = r.api.exec("/aimnearest zombie").get("msg", "")
        st = r.api.state()
        print(f"    [dbg-zombie] {aim} | health={st.get('health')} mobs={st.get('mobs')} "
              f"pos={[round(v,1) for v in st.get('pos', [])]}")
        if st.get("health", 20) < 20:
            break
        time.sleep(2.5)
    st = r.api.state()
    r.check("survival_zombie_damages_player", st.get("health", 20) < 20,
            f"health={st.get('health')}")
    # Death and respawn via console
    r.exec_check("suicide", "/kill", "Player killed")
    time.sleep(2.5)  # let fall physics settle the player on the ground
    st = r.api.state()
    r.check("respawn_restores_health", st.get("health") == 20, f"health={st.get('health')}")

    # ---- Dimension portal E2E ----
    r.exec_check("clear_arena3", "/killmobs", "Despawned")
    ex = api_pos = r.api.state().get("pos", [0, 80, 0])
    px, py, pz = int(ex[0]), int(ex[1]), int(ex[2])
    r.exec_check("place_portal", f"/setblock {px} {py} {pz} nether_portal", "Set nether_portal")
    dims = r.wait(lambda d: d.get("dimension") == 1, timeout=15, poll=0.5, desc="nether switch")
    st = r.api.state()
    r.check("portal_to_nether", st.get("dimension") == 1,
            f"dim={st.get('dimension')} chunks={st.get('chunks')} pos={st.get('pos')}")
    r.shot("12_nether")
    # Return trip: drop a portal right under the bot, following it down —
    # in the Nether the bot keeps falling through caverns for a while.
    deadline = time.time() + 25  # cover the whole long Nether free-fall
    attempt = 0
    while time.time() < deadline:
        st = r.api.state()
        if st.get("dimension") == 0:
            break
        nx, ny, nz = [int(float(v)) for v in st.get("pos", [0, 90, 0])]
        r.api.exec(f"/setblock {nx} {ny} {nz} nether_portal")
        time.sleep(0.7)
        attempt += 1
    # Portal cooldown after the inbound trip is 5 s; give the loop room.
    r.wait(lambda d: d.get("dimension") == 0, timeout=20, poll=0.5, desc="portal cooldown")
    st = r.api.state()
    r.check("portal_back_overworld", st.get("dimension") == 0,
            f"dim={st.get('dimension')} pos={st.get('pos')}")

    # ---- Data-driven modding E2E ----
    # mods/ ships two JSON files next to the game sources:
    # - example_recipes.json: overrides sticks (4 -> 9) and adds shapeless
    #   sand+coal -> 2x glass. The registry the crafting UI reads must
    #   reflect the FILE, not the builtins.
    # - quests.json: adds three quests (ids 2..4) to the catalog.
    st = r.api.state()
    r.check("mod_files_loaded", st.get("mod_files") == 2 and st.get("mod_overridden") == 1
            and st.get("mod_added") == 1 and st.get("quests_added") == 3
            and st.get("quests_overridden") == 0,
            str({k: st.get(k) for k in ("mod_files", "mod_added", "mod_overridden",
                                        "quests_added", "quests_overridden")}))
    r.check("mod_quests_loaded", "Lowca szkieletow" in st.get("quest_titles", ""),
            str(st.get("quest_titles", "")))
    recipes = r.api.exec("/recipes")
    r.check("mod_stick_override_live", "stickx9" in recipes.get("msg", ""),
            recipes.get("msg", ""))
    r.check("mod_glass_recipe_live", "glassx2" in recipes.get("msg", ""),
            recipes.get("msg", ""))

    # Final state sanity (after Nether load settles back down)
    time.sleep(6.0)
    st = r.api.state()
    r.check("final_playing", st.get("state") == "playing")
    r.check("no_mspt_spike", st.get("mspt", 0) < 50.0, f"mspt={st.get('mspt')}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=DEFAULT_EXE)
    ap.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "/tmp"), "minekampf_auto_test"))
    ap.add_argument("--keep", action="store_true", help="leave the game running after tests")
    ap.add_argument("--skip-build", action="store_true")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    if not args.skip_build:
        print("[1/5] Building...")
        rc = subprocess.call(["cmd", "/c", os.path.join(HERE, "dev.bat"), "build"])
        if rc != 0:
            print("BUILD FAILED")
            return 1
    if not os.path.exists(args.exe):
        print(f"EXE not found: {args.exe}")
        return 1

    port = free_port()
    print(f"[2/5] Launching game on port {port}...")
    stderr_path = os.path.join(args.out, "game_stderr.log")
    stderr_fh = open(stderr_path, "wb")
    env = dict(os.environ, MINEKAMPF_AI_DIAG="1")
    proc = subprocess.Popen([args.exe, "--auto-play", "--automation-port", str(port)],
                            cwd=os.path.dirname(args.exe), env=env,
                            stdout=stderr_fh, stderr=stderr_fh)
    exit_code = 0
    try:
        api = None
        for _ in range(30):
            if proc.poll() is not None:
                break
            try:
                api = Api(port)
                break
            except OSError:
                time.sleep(1.0)
        if api is None:
            print("FAILED to connect to automation API")
            return 1
        print(f"[3/5] Connected: {api.welcome}")
        r = Runner(api, args.out)
        print("[4/5] Running scenario...")
        run_scenario(r)

        print("[5/5] Summary")
        failed = [x for x in r.results if not x[1]]
        for name, ok, detail in r.results:
            print(f"  {'PASS' if ok else 'FAIL'}  {name}  {detail}")
        print(f"\n{len(r.results) - len(failed)}/{len(r.results)} checks passed.")
        for f in r.shots:
            png = to_png(f)
            if png:
                print(f"  screenshot: {png}")
        exit_code = 1 if failed else 0
    finally:
        rc = proc.poll()
        if rc is not None and rc != 0:
            print(f"!!! GAME PROCESS DIED: exit code {rc:#010x} "
                  f"(0xC0000005 = access violation)")
            stderr_path = os.path.join(args.out, "game_stderr.log")
            if os.path.exists(stderr_path):
                tail = open(stderr_path, "rb").read()[-800:]
                if tail.strip():
                    print("!!! stderr tail:", tail.decode(errors="replace"))
        if args.keep:
            print("Keeping the game running (--keep).")
        else:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
            print("Game process terminated.")
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
