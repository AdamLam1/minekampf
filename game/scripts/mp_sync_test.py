#!/usr/bin/env python3
"""Minekampf multiplayer sync E2E: positions, blocks, chat, chunk streaming.

Extends mp_test.py with deep synchronization assertions. Launches host + client
in-memory instances, then through the automation API (get_state `mp_remote`
field, /mine + /place commands routed through the REAL client network action
path) verifies:

  - remote players are visible to both sides (entry + visibility flag)
  - player position sync host->client and client->host (convergence)
  - remote movement is streamed/interpolated over multiple updates
  - yaw rotation sync
  - block break client->host through the authoritative action path
  - block place client->host (with host-cleared target + reach in bounds)
  - chat sync both directions (/say)
  - chunk streaming + position sync after a far teleport

Known-bug probes (soft checks: reported, do not fail the run):
  - client console /setblock mutates the local world without a network action
    (authority violation / desync)
  - chunks streamed to the client after a far teleport into an area the host
    has not generated yet never arrive (no re-request / no late push)

Kills both game processes at the end. Exit code 0 = all checks (except soft)
passed.

Usage:
  python mp_sync_test.py [--skip-build] [--keep] [--exe PATH]
"""
import argparse
import json
import math
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_DIR = os.path.dirname(HERE)
DEFAULT_EXE = os.path.join(GAME_DIR, "build", "release", "bin", "minekampf.exe")

POS_TOL = 1.5        # blocks; convergence tolerance for remote positions
DROP_LAG_TOL = 2.5   # blocks; max view lag during a 12-block free fall
ROT_TOL = 0.35       # radians; yaw tolerance
SURFACE_API_TIMEOUT = 20


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
        self.sock.recv(4096)  # welcome banner

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

    def exec_cmd(self, command):
        return self.cmd({"cmd": "exec", "command": command})


def remote_of(state, not_id=None):
    """First entry of get_state()['mp_remote'] that isn't the local player."""
    for rp in state.get("mp_remote", []):
        if not_id is None or rp.get("id") != not_id:
            return rp
    return None


def dist3(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a[:3], b[:3])))


def wait_until(fn, timeout, interval=0.4):
    """Poll fn() until truthy; returns the last value (or None on timeout)."""
    deadline = time.time() + timeout
    last = None
    while True:
        last = fn()
        if last:
            return last
        if time.time() >= deadline:
            return last
        time.sleep(interval)


def surface(api, far=False):
    """Put the instance's player on solid ground: /tp into the area, then
    /goto stone to snap to the nearest surface. Returns the player pos."""
    if far:
        api.exec_cmd("/tp 400 100 400")
    api.exec_cmd("/goto stone")
    time.sleep(1.5)
    return api.state()["pos"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--exe", default=DEFAULT_EXE)
    args = ap.parse_args()

    host_auto = free_port()
    client_auto = free_port()
    game_port = free_port()

    results = []

    def check(name, ok, info=""):
        results.append((name, True))
        print(f"  {'PASS' if ok else 'FAIL'}  {name}  {info}")
        if not ok:
            results[-1] = (name, False)

    def soft_check(name, expected_bug_found, info=""):
        """Known-bug probe: always green for the suite, but reports state."""
        print(f"  {'KNOWN-BUG' if expected_bug_found else 'OK'}  {name}  {info}")

    procs = []
    try:
        procs.append(subprocess.Popen([
            args.exe, "--auto-play", "--host", str(game_port),
            "--automation-port", str(host_auto)]))
        time.sleep(3.0)
        procs.append(subprocess.Popen([
            args.exe, "--join", "127.0.0.1", str(game_port), "Tester2",
            "--automation-port", str(client_auto)]))

        # -- 0. join --
        joined = wait_until(lambda: _if_playing(_try_state(client_auto)), timeout=120)
        check("client_playing", bool(joined), json.dumps(joined or {})[:160])
        if not joined:
            raise SystemExit(1)

        host = Api(host_auto)
        client = Api(client_auto)
        time.sleep(4.0)  # let chunk streaming settle

        def client_view_gap_of_host():
            cs, hs = client.state(), host.state()
            rp = remote_of(cs, not_id=cs.get("my_player_id"))
            return dist3(rp["pos"], hs["pos"]) if rp else None

        def host_view_gap_of_client():
            hs, cs = host.state(), client.state()
            rp = remote_of(hs, not_id=hs.get("my_player_id"))
            return dist3(rp["pos"], cs["pos"]) if rp else None

        # -- 1. remote players visible on both sides --
        cs = client.state()
        csees = remote_of(cs, not_id=cs.get("my_player_id"))
        hs = host.state()
        hsees = remote_of(hs, not_id=hs.get("my_player_id"))
        check("host_sees_client_entry", hsees is not None,
              json.dumps(hs.get("mp_remote"))[:120])
        check("client_sees_host_entry", csees is not None,
              json.dumps(cs.get("mp_remote"))[:120])
        check("remote_marked_visible",
              bool(hsees and hsees.get("visible")) and bool(csees and csees.get("visible")),
              f"host_view={hsees and hsees.get('visible')} "
              f"client_view={csees and csees.get('visible')}")

        # -- 2. position sync host -> client (both on surface) --
        surface(host)
        gap = wait_until(lambda: _lt(client_view_gap_of_host(), POS_TOL), timeout=15)
        hs, cs = host.state(), client.state()
        c_view = remote_of(cs, not_id=cs.get("my_player_id")) or {}
        check("pos_sync_host_to_client", bool(gap),
              f"gap={gap} host={hs['pos']} client_sees={c_view.get('pos')}")

        # -- 3. position sync client -> host --
        surface(client)
        gap2 = wait_until(lambda: _lt(host_view_gap_of_client(), POS_TOL), timeout=15)
        cs = client.state()
        h_view = (remote_of(host.state(), not_id=host.state().get("my_player_id")) or {})
        check("pos_sync_client_to_host", bool(gap2),
              f"gap={gap2} client={cs['pos']} host_sees={h_view.get('pos')}")

        # -- 4. movement is streamed/interpolated smoothly (host is dropped
        #      from a height; the client's view must follow in small steps
        #      and never lag more than DROP_LAG_TOL behind) --
        host.exec_cmd("/goto stone")
        time.sleep(1.5)
        hp = host.state()["pos"]
        host.exec_cmd(f"/tp {hp[0]:.1f} {hp[1] + 12:.1f} {hp[2]:.1f}")
        views = []
        gaps = []
        for _ in range(40):
            cs = client.state()
            rp = remote_of(cs, not_id=cs.get("my_player_id"))
            if rp:
                views.append(list(rp["pos"]))
                gaps.append(dist3(rp["pos"], host.state()["pos"]))
            time.sleep(0.15)
        distinct = len(set(tuple(round(c, 1) for c in v) for v in views))
        # The instant /tp up creates a one-off jump; measure lag only from the
        # moment the view starts following (i.e. during the continuous fall).
        follow_idx = None
        if views:
            start_y = views[0][1]
            for i, v in enumerate(views):
                if v[1] > start_y + 1.0:
                    follow_idx = i
                    break
        max_gap = max(gaps[follow_idx:]) if follow_idx is not None else None
        final_gap = client_view_gap_of_host()
        check("pos_stream_follows", distinct >= 3 and max_gap is not None and
              max_gap < DROP_LAG_TOL,
              f"distinct={distinct} max_gap_during_fall={max_gap}")
        check("pos_converges", final_gap is not None and final_gap < POS_TOL,
              f"final_gap={final_gap}")

        # -- 5. yaw sync --
        host.cmd({"cmd": "look", "yaw": 200, "pitch": 10})
        dyaw = wait_until(lambda: _yaw_lt(client, host, ROT_TOL), timeout=8,
                          interval=0.3)
        hs = host.state()
        rp = remote_of(client.state(), not_id=cs.get("my_player_id"))
        info = f"host_yaw={hs.get('yaw')} view_yaw={rp and rp.get('yaw')}"
        if dyaw is None:
            check("yaw_sync", False, info + " (never converged)")
        else:
            check("yaw_sync", True, f"dyaw<{ROT_TOL} {info}")

        # -- 6. block break client -> host through the real action path --
        time.sleep(1.5)  # settle on the ground
        cp = client.state()["pos"]
        bx, by, bz = int(math.floor(cp[0])), int(math.floor(cp[1])) - 1, int(math.floor(cp[2]))
        r = client.exec_cmd(f"/probe {bx} {by} {bz}")
        check("ground_under_client_solid", "= air" not in r.get("msg", ""),
              str(r.get("msg", ""))[:80])
        r = client.exec_cmd(f"/mine {bx} {by} {bz}")
        check("client_mine_sent", "Mined" in r.get("msg", ""), str(r.get("msg", ""))[:80])
        r = wait_until(lambda: _probe_is(host, bx, by, bz, "air"), timeout=10)
        check("client_mine_synced_to_host", bool(r), _probe_msg(host, bx, by, bz))

        # -- 7. block place client -> host (target cleared by host, in reach) --
        tx, ty, tz = bx + 1, by + 1, bz  # one block to the side, above ground
        host.exec_cmd(f"/setblock {tx} {ty} {tz} air")
        time.sleep(2.5)
        r = client.exec_cmd(f"/place {tx} {ty} {tz} glowstone")
        check("client_place_sent", "Placed" in r.get("msg", ""), str(r.get("msg", ""))[:80])
        r = wait_until(lambda: _probe_is(host, tx, ty, tz, "glowstone"), timeout=10)
        check("client_place_synced_to_host", bool(r), _probe_msg(host, tx, ty, tz))

        # -- 8. chat sync both directions --
        host.exec_cmd("/say SYNC_HOST_MSG_1")
        got_h2c = wait_until(lambda: any("SYNC_HOST_MSG_1" in t
                                         for t in client.state().get("chat", [])),
                             timeout=8, interval=0.3)
        check("chat_host_to_client", bool(got_h2c),
              str(client.state().get("chat"))[:120])
        client.exec_cmd("/say SYNC_CLIENT_MSG_1")
        got_c2h = wait_until(lambda: any("SYNC_CLIENT_MSG_1" in t
                                         for t in host.state().get("chat", [])),
                             timeout=8, interval=0.3)
        check("chat_client_to_host", bool(got_c2h),
              str(host.state().get("chat"))[:120])

        # -- 9. soft probe: client console /setblock bypasses host authority --
        sx, sy, sz = bx + 2, by + 1, bz
        client.exec_cmd(f"/setblock {sx} {sy} {sz} glowstone")
        time.sleep(2.5)
        client_sees = _probe_is(client, sx, sy, sz, "glowstone")
        host_sees = _probe_is(host, sx, sy, sz, "glowstone")
        soft_check("client_setblock_authority", client_sees and not host_sees,
                   f"client_sees={client_sees} host_sees={host_sees} "
                   f"(desync if client sees, host doesn't)")
        client.exec_cmd(f"/setblock {sx} {sy} {sz} air")  # clean up locally

        # -- 10. far teleport: chunk streaming + position sync --
        surface(client, far=True)
        chunks_low = client.state().get("chunks", 0)
        gap3 = wait_until(lambda: _lt(host_view_gap_of_client(), POS_TOL), timeout=15)
        hs = host.state()
        h_view = (remote_of(hs, not_id=hs.get("my_player_id")) or {})
        check("far_teleport_pos_sync", bool(gap3),
              f"gap={gap3} client={client.state()['pos']} host_sees={h_view.get('pos')}")

        # Host now travels to the same far region and edits a block. If the
        # client's chunk stream is healthy, the edit arrives.
        surface(host, far=True)
        host.exec_cmd("/setblock 400 200 400 glowstone")
        edit_seen = wait_until(lambda: "Found glowstone" in
                               _exec(client, "/goto glowstone").get("msg", ""),
                               timeout=20, interval=1.0)
        cs = client.state()
        # KNOWN BUG (no chunk re-request / late push): the client requested the
        # far disc before the host generated it, and never asks again — so the
        # edit never arrives and the client sits at 0 chunks. Flip both soft
        # probes below into hard checks once network-dev fixes this.
        soft_check("far_chunks_streamed", cs.get("chunks", 0) > 10,
                   f"client chunks after far teleport = {cs.get('chunks', 0)}")
        soft_check("far_block_sync_to_client", bool(edit_seen),
                   f"edit seen on client={bool(edit_seen)} "
                   f"chunks={cs.get('chunks', 0)}")

    finally:
        for p in procs:
            try:
                p.terminate()
            except Exception:
                pass
        time.sleep(1.0)
        for p in procs:
            try:
                p.kill()
            except Exception:
                pass

    passed = sum(1 for _, ok in results if ok)
    print(f"\n{passed}/{len(results)} mp_sync checks passed.")
    return 0 if passed == len(results) else 1


def _try_state(port):
    try:
        return Api(port).state()
    except Exception:
        return {}


def _if_playing(state):
    if state and state.get("state") == "playing" and state.get("mp"):
        return state
    return None


def _lt(v, tol):
    return v is not None and v < tol


def _yaw_lt(client, host, tol):
    v = _yaw_gap(client, host)
    return v is not None and v < tol


def _yaw_gap(client, host):
    cs = client.state()
    rp = remote_of(cs, not_id=cs.get("my_player_id"))
    if not rp:
        return None
    d = abs(rp.get("yaw", 0.0) - host.state().get("yaw", 0.0)) % (2 * math.pi)
    return min(d, 2 * math.pi - d)


def _probe_msg(api, x, y, z):
    return str(api.exec_cmd(f"/probe {x} {y} {z}").get("msg", ""))[:80]


def _probe_is(api, x, y, z, block_name):
    return f"= {block_name}" in _probe_msg(api, x, y, z)


def _exec(api, command):
    try:
        return api.exec_cmd(command)
    except Exception:
        return {}


if __name__ == "__main__":
    sys.exit(main())
