#!/usr/bin/env python3
"""Synthesise a seamless looping fire, for the courtyard's brazier.

    python tools/scripts/make_fire_sound.py

Generated rather than downloaded, for the reason `make_sky.py` is: an asset
in the repository needs a licence and a provenance, and one this simple is
cheaper to derive than to acquire. It is also the only way to guarantee the
loop is seamless, which a clip found elsewhere almost never is.

**A fire is two sounds.** A broadband roar that never stops, and sharp
crackles that arrive at random. Synthesising only the first gives white noise,
which reads as rain; only the second gives popcorn. Both together, at roughly
the right ratio, is recognisable within half a second.

Seamlessness is the part worth being careful about. A looping source restarts
at sample zero, so anything discontinuous there ticks once per loop -- and a
tick every four seconds is more noticeable than the fire. Two measures:

  * The roar is built in the **frequency domain** from a whole number of
    cycles per loop, so it is periodic by construction rather than by fading.
  * A crackle that would run off the end **wraps** to the beginning instead of
    being cut, so its tail is genuinely the sound that precedes it.
"""

import math
import pathlib
import struct
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]

RATE = 22050          # a fire has nothing above 10 kHz worth the bytes
SECONDS = 4.0
SAMPLES = int(RATE * SECONDS)

# Reproducible: two runs produce the same bytes, so the file only changes when
# this script does and a diff means something.
SEED = 0x726167655601


def roar(rng):
    """The broadband bed, periodic by construction.

    Built as a spectrum and inverse-transformed, so every component completes
    a whole number of cycles in the loop and the join is exact. Shaped to fall
    off with frequency -- a fire is mostly low, and flat noise is rain.
    """
    bins = SAMPLES // 2 + 1
    freqs = np.fft.rfftfreq(SAMPLES, 1.0 / RATE)

    # Roughly 1/f above a low shelf. The shelf is what stops all the energy
    # collecting in the first few bins and turning the roar into a throb.
    shape = 1.0 / np.sqrt(1.0 + (freqs / 90.0) ** 2)
    shape *= 1.0 / (1.0 + (freqs / 2600.0) ** 2)
    shape[0] = 0.0

    phase = rng.uniform(0.0, 2.0 * math.pi, bins)
    spectrum = shape * np.exp(1j * phase)
    return np.fft.irfft(spectrum, SAMPLES)


def crackles(rng):
    """The transients, wrapped rather than clipped at the loop boundary."""
    out = np.zeros(SAMPLES)

    # About twelve a second. Enough to read as a fire rather than as a
    # campfire prop that pops occasionally.
    count = int(SECONDS * 12)

    for _ in range(count):
        start = rng.integers(0, SAMPLES)
        length = int(rng.uniform(0.004, 0.030) * RATE)

        # Sharp attack, exponential decay -- the envelope of something small
        # breaking.
        envelope = np.exp(-np.linspace(0.0, 7.0, length))
        noise = rng.uniform(-1.0, 1.0, length)

        # Brighter than the bed: a crackle is the high end of the sound.
        tone = noise * (0.5 + 0.5 * np.sin(np.linspace(0, rng.uniform(20, 90), length)))
        burst = envelope * tone * rng.uniform(0.25, 1.0)

        # Wrap. A crackle cut off at the end of the buffer is a click at the
        # start of every loop, which is the one artefact a listener localises
        # immediately.
        index = (np.arange(length) + start) % SAMPLES
        np.add.at(out, index, burst)

    return out


def main():
    rng = np.random.default_rng(SEED)

    bed = roar(rng)
    bed /= np.abs(bed).max() or 1.0

    pops = crackles(rng)
    pops /= np.abs(pops).max() or 1.0

    # The ratio that makes it a fire. More bed and it is wind; more crackle
    # and it is a bowl of cereal.
    mix = 0.72 * bed + 0.42 * pops

    # A slow swell, also periodic, so the fire breathes rather than sitting at
    # one level. One cycle per loop exactly, or the swell itself would tick.
    t = np.arange(SAMPLES) / RATE
    swell = 1.0 + 0.14 * np.sin(2.0 * math.pi * t / SECONDS)
    mix *= swell

    mix /= np.abs(mix).max() or 1.0
    mix *= 0.82   # headroom, so the spatial attenuation has somewhere to go

    pcm = np.clip(mix * 32767.0, -32768, 32767).astype(np.int16)

    out = ROOT / "SampleProject" / "assets" / "audio" / "fire.wav"
    out.parent.mkdir(parents=True, exist_ok=True)

    with wave.open(str(out), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(pcm.tobytes())

    # The sidecar, with a handle derived from the name. A handle is minted by
    # the registry on first scan, which is *after* the scene naming it has
    # been written -- so a generator cannot ask for one, and writes it instead.
    # Same reason and same arithmetic as postprofile.py's.
    handle = 0xCBF29CE484222325
    for byte in out.name.encode("utf-8"):
        handle ^= byte
        handle = (handle * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF

    out.with_name(out.name + ".meta").write_text(
        chr(10).join([f"Handle: {handle}", "Type: Audio", "SourceHash: 0"])
        + chr(10), encoding="utf-8")

    # The join, measured rather than asserted: the step from the last sample
    # back to the first has to be no larger than a step inside the buffer, or
    # the loop ticks.
    inner = float(np.abs(np.diff(pcm.astype(np.float64))).max())
    join = abs(float(pcm[0]) - float(pcm[-1]))

    print(f"{out.relative_to(ROOT)}: {SECONDS:g}s at {RATE} Hz, "
          f"{out.stat().st_size} bytes")
    print(f"  loop join {join:.0f} against a largest inner step of {inner:.0f}")
    print(f"  handle {handle}")

    if join > inner:
        raise SystemExit("the loop join is a bigger jump than anything inside "
                         "the buffer, so it will tick once per loop")


if __name__ == "__main__":
    main()
