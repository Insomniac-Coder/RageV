#!/usr/bin/env python3
"""Generates the Knockdown project's sounds.

Synthesised rather than recorded so the repository carries no audio of
unknown provenance and the assets can be remade from one command:

    python tools/scripts/make_game_audio.py [output-dir]

The default output is Knockdown/assets/audio. Everything is deterministic
(fixed seed), so running it twice produces byte-identical files and the
asset registry's source hashes stay put.
"""

import math
import os
import random
import struct
import sys
import wave

RATE = 44100


def write_wav(path, samples, peak):
    """16-bit mono PCM, normalised so the loudest sample sits at `peak`."""
    loudest = max(1e-9, max(abs(s) for s in samples))
    scale = peak / loudest
    data = b"".join(
        struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767))
        for s in samples
    )
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(data)
    print(f"  {os.path.basename(path)}  {len(samples) / RATE:.2f}s")


def seconds(duration):
    return range(int(RATE * duration))


def shot():
    """The launcher's thump: a fast downward pitch sweep with a noise edge."""
    rng = random.Random(1)
    out = []
    phase = 0.0
    for i in seconds(0.30):
        t = i / RATE
        pitch = 55.0 + 105.0 * math.exp(-t * 18.0)
        phase += 2.0 * math.pi * pitch / RATE
        body = math.sin(phase) * math.exp(-t * 11.0)
        edge = rng.uniform(-1, 1) * math.exp(-t * 160.0) * 0.5
        out.append(body + edge)
    return out


def impact():
    """A crate being hit: two damped wooden modes and a click."""
    rng = random.Random(2)
    out = []
    for i in seconds(0.18):
        t = i / RATE
        knock = (math.sin(2 * math.pi * 190 * t) * 0.7 +
                 math.sin(2 * math.pi * 317 * t) * 0.4) * math.exp(-t * 26.0)
        click = rng.uniform(-1, 1) * math.exp(-t * 300.0) * 0.4
        out.append(knock + click)
    return out


def plink():
    """One crate counted out: a small bright ping."""
    out = []
    for i in seconds(0.35):
        t = i / RATE
        attack = min(1.0, t / 0.004)
        tone = (math.sin(2 * math.pi * 1180 * t) +
                math.sin(2 * math.pi * 1770 * t) * 0.35)
        out.append(tone * attack * math.exp(-t * 12.0))
    return out


def win():
    """The last crate: a rising arpeggio that rings out."""
    notes = [440.0, 554.37, 659.25, 880.0]          # A major, up
    length = 1.2
    out = [0.0] * int(RATE * length)
    for n, freq in enumerate(notes):
        start = int(RATE * 0.16 * n)
        for i in seconds(length - 0.16 * n):
            t = i / RATE
            attack = min(1.0, t / 0.01)
            tone = (math.sin(2 * math.pi * freq * t) +
                    math.sin(4 * math.pi * freq * t) * 0.25)
            out[start + i] += tone * attack * math.exp(-t * 3.5) * 0.5
    return out


def wind():
    """Quiet ambience, loopable: low-passed noise, crossfaded end-to-start."""
    rng = random.Random(3)
    total = int(RATE * 3.5)
    raw = []
    level = 0.0
    for i in range(total):
        # One-pole lowpass keeps only the rumble of the noise.
        level += (rng.uniform(-1, 1) - level) * 0.03
        swell = 1.0 + 0.35 * math.sin(2 * math.pi * i / (RATE * 1.75))
        raw.append(level * swell)

    # The last half second fades into the first, and the overlap is cut, so
    # the loop point is inaudible.
    fade = int(RATE * 0.5)
    body = raw[: total - fade]
    for i in range(fade):
        blend = i / fade
        body[i] = body[i] * blend + raw[total - fade + i] * (1.0 - blend)
    return body


def main():
    default = os.path.join(os.path.dirname(__file__), "..", "..",
                           "Knockdown", "assets", "audio")
    directory = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else default)
    os.makedirs(directory, exist_ok=True)
    print(f"Writing into {directory}")

    write_wav(os.path.join(directory, "shot.wav"), shot(), 0.85)
    write_wav(os.path.join(directory, "impact.wav"), impact(), 0.8)
    write_wav(os.path.join(directory, "plink.wav"), plink(), 0.7)
    write_wav(os.path.join(directory, "win.wav"), win(), 0.8)
    write_wav(os.path.join(directory, "wind.wav"), wind(), 0.3)


if __name__ == "__main__":
    main()
