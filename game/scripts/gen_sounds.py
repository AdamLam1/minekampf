#!/usr/bin/env python3
"""Generate the procedural sound set for Minekampf (assets/sounds/*.wav).

The game ships without any audio assets; this script synthesizes the whole
starter set with the Python standard library only (no numpy): filtered noise
bursts for digging/steps, tonal knocks for wood/placing, a hurt "oof", UI
clicks, a loopable rain bed and a simple ambient music pad.

Deterministic (fixed seed) so rebuilds produce identical files.

Usage:
    python scripts/gen_sounds.py [--out DIR]
"""

import argparse
import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050


def write_wav(path: Path, samples: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = b"".join(
        struct.pack("<h", max(-32767, min(32767, int(s * 32767)))) for s in samples
    )
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)


def silence(dur: float) -> list[float]:
    return [0.0] * int(RATE * dur)


def env(i: int, n: int, attack: float = 0.005, release: float = 0.5) -> float:
    """Attack/release envelope: fast in, exponential out."""
    t = i / RATE
    a = min(1.0, t / attack) if attack > 0 else 1.0
    prog = i / max(1, n - 1)
    r = (1.0 - prog) ** (1.0 / max(0.001, release)) if release > 0 else 1.0
    return a * r


def lowpass(samples: list[float], alpha: float) -> list[float]:
    out, y = [], 0.0
    for s in samples:
        y += alpha * (s - y)
        out.append(y)
    return out


def noise_burst(dur: float, amp: float, lp: float, punch: float = 0.5,
                rng: random.Random | None = None) -> list[float]:
    rng = rng or random.Random(1)
    n = int(RATE * dur)
    raw = [rng.uniform(-1, 1) for _ in range(n)]
    body = lowpass(raw, lp)
    return [body[i] * amp * env(i, n, 0.002, punch) for i in range(n)]


def tone(freq: float, dur: float, amp: float, decay: float = 0.35,
         kind: str = "sine") -> list[float]:
    n = int(RATE * dur)
    out = []
    for i in range(n):
        t = i / RATE
        if kind == "sine":
            v = math.sin(2 * math.pi * freq * t)
        else:  # soft square
            v = math.tanh(2.5 * math.sin(2 * math.pi * freq * t))
        out.append(v * amp * env(i, n, 0.004, decay))
    return out


def mix(*tracks: list[float]) -> list[float]:
    return add(*tracks)


def add(*tracks: list[float]) -> list[float]:
    n = max(len(t) for t in tracks)
    out = [0.0] * n
    for t in tracks:
        for i, s in enumerate(t):
            out[i] += s
    return out


def concat(*parts: list[float]) -> list[float]:
    out: list[float] = []
    for p in parts:
        out.extend(p)
    return out


def normalize(samples: list[float], peak: float = 0.82) -> list[float]:
    m = max(1e-6, max(abs(s) for s in samples))
    k = peak / m
    return [s * k for s in samples]


def gen_all(out: Path) -> None:
    rng = random.Random(20260906)

    def write(name: str, samples: list[float]) -> None:
        write_wav(out / name, normalize(samples))
        print(f"  {name:20s} {len(samples) / RATE:5.2f}s")

    # --- digging / placing -------------------------------------------------
    write("dig_stone.wav", noise_burst(0.14, 0.9, 0.35, 0.4, rng))
    write("dig_dirt.wav", noise_burst(0.13, 0.8, 0.12, 0.5, rng))
    write("dig_grass.wav", add(noise_burst(0.10, 0.6, 0.22, 0.6, rng),
                               noise_burst(0.05, 0.35, 0.6, 0.8, rng)))
    write("dig_wood.wav", add(tone(170, 0.11, 0.5, 0.25),
                              noise_burst(0.05, 0.5, 0.5, 0.5, rng)))
    write("dig_sand.wav", noise_burst(0.12, 0.65, 0.08, 0.55, rng))
    write("place.wav", add(tone(220, 0.09, 0.45, 0.3),
                           noise_burst(0.04, 0.4, 0.55, 0.6, rng)))

    # --- steps --------------------------------------------------------------
    write("step_grass.wav", noise_burst(0.07, 0.5, 0.18, 0.7, rng))
    write("step_stone.wav", noise_burst(0.06, 0.55, 0.4, 0.8, rng))
    write("step_wood.wav", add(tone(150, 0.06, 0.3, 0.4),
                               noise_burst(0.04, 0.3, 0.4, 0.8, rng)))

    # --- player / UI ----------------------------------------------------------
    hurt = add(tone(310, 0.10, 0.55, 0.3), tone(220, 0.14, 0.5, 0.35),
               noise_burst(0.06, 0.3, 0.3, 0.6, rng))
    write("hurt.wav", hurt)
    write("click.wav", tone(660, 0.035, 0.4, 0.5))
    munch = concat(noise_burst(0.07, 0.6, 0.25, 0.7, rng),
                   silence(0.05),
                   noise_burst(0.07, 0.55, 0.22, 0.7, rng),
                   silence(0.05),
                   noise_burst(0.08, 0.5, 0.2, 0.7, rng))
    write("eat.wav", munch)
    write("level_up.wav", add(tone(523, 0.14, 0.4, 0.3),
                              concat(silence(0.09), tone(784, 0.2, 0.4, 0.35))))
    write("splash.wav", lowpass(noise_burst(0.35, 0.9, 0.18, 1.2, rng), 0.25))
    write("zombie_growl.wav", lowpass(
        [math.sin(2 * math.pi * (95 + 25 * math.sin(2 * math.pi * 6 * i / RATE)) * i / RATE)
         * env(i, int(RATE * 0.5), 0.05, 0.8) for i in range(int(RATE * 0.5))], 0.3))
    write("skeleton_rattle.wav", concat(
        *[noise_burst(0.035, 0.6, 0.7, 0.9, rng) + [0.0] * 0 for _ in range(4)]))

    # --- rain bed (2.8 s seamless-ish loop) ----------------------------------
    n = int(RATE * 2.8)
    raw = [rng.uniform(-1, 1) for _ in range(n)]
    body = lowpass(lowpass(raw, 0.4), 0.55)
    # crossfade the tail into the head so the loop point is inaudible
    fade = int(RATE * 0.25)
    for i in range(fade):
        k = i / fade
        body[i] = body[i] * k + body[n - fade + i] * (1 - k)
    loop = body[: n - fade]
    write("rain_loop.wav", [s * 0.5 for s in loop])

    # --- ambient music pad (16 s, 4-chord loop, soft sines) ------------------
    chords = [(220.0, 261.63, 329.63),   # A minor
              (174.61, 220.0, 261.63),   # F major
              (196.0, 246.94, 293.66),   # G major
              (130.81, 164.81, 196.0)]   # C major (low)
    seg = int(RATE * 4)
    pad: list[float] = []
    for chord in chords:
        segn = seg
        base = [0.0] * segn
        for j, f in enumerate(chord):
            fd = f * (1.0 + (j - 1) * 0.0012)
            amp = 0.15 / (j + 1)
            for i in range(segn):
                t = i / RATE
                swell = 0.45 + 0.55 * math.sin(math.pi * (i / segn))
                base[i] += math.sin(2 * math.pi * fd * t) * amp * swell
        pad.extend(base)
    write("music_ambient.wav", lowpass(pad, 0.5))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path(__file__).resolve().parent.parent / "assets" / "sounds"))
    args = ap.parse_args()
    out = Path(args.out)
    print(f"Generating sounds into {out}")
    gen_all(out)
    print("done")


if __name__ == "__main__":
    main()
