#!/usr/bin/env python3
"""Synthesise a seamless night-forest ambience for the camp.

    python tools/scripts/make_ambience.py

Generated rather than downloaded, for the reason `make_sky_hdr.py` and
`make_fire_sound.py` are: an asset in the repository needs a licence and a
provenance, and one this simple is cheaper to derive than to acquire. It is
also the only way to guarantee the loop is seamless, which a clip found
elsewhere almost never is.

**A night wood is four layers, and getting any of them wrong is audible.** A
wind bed in the canopy; the leaves it moves; a continuous high wash of distant
insects; and a few close crickets over the top, with the odd longer trill.

The first version had two of those and built its insects out of **sine waves**,
which is why it did not work. A sine at four kilohertz under a fast envelope is
a smoke alarm, not a cricket: a real chirp is a burst of *noise* with a sharp
resonance and a stridulation buzz inside it, and the two are not close enough
for any amount of envelope shaping to bridge. The high wash was missing
altogether, and it is the layer that makes a recording sound like somewhere
rather than like a wind machine with three insects in front of it.

**Seamlessness is the part worth being careful about.** A looping source
restarts at sample zero, so anything discontinuous there ticks once per loop --
and a tick every eight seconds is more noticeable than the crickets. Three
measures, and they are the same ones the fire loop uses:

  * Every continuous layer is built in the **frequency domain** from a whole
    number of cycles per loop, so it is periodic by construction rather than by
    fading.
  * Every insect repeats at a rate that **divides the loop exactly**, so its
    last chirp is followed by its first at the same spacing.
  * Anything that would run off the end **wraps** to the beginning instead of
    being cut, so its tail is genuinely the sound that precedes it.

The claim is measured at the end rather than asserted: the step across the loop
point has to be no larger than the steps the waveform takes inside itself.
"""

import argparse
import math
import pathlib
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]

RATE = 22050
SECONDS = 8.0            # long enough that the pattern is not countable
SAMPLES = int(RATE * SECONDS)

SEED = 0x6E69676874


def wind():
    """The canopy: a soft low bed, periodic by construction.

    Built as a spectrum and inverse-transformed, so every component completes a
    whole number of cycles in the loop and the join is exact. Steeply
    low-passed -- wind in trees has almost nothing above a few hundred hertz,
    and flat noise is rain.
    """
    rng = np.random.default_rng(SEED)
    bins = SAMPLES // 2 + 1
    freqs = np.arange(bins) * (RATE / SAMPLES)

    magnitude = 1.0 / np.maximum(freqs, 1.0) ** 1.3
    magnitude *= np.exp(-(freqs / 380.0) ** 2)
    magnitude[0] = 0.0

    phase = rng.uniform(0.0, 2.0 * math.pi, bins)
    bed = np.fft.irfft(magnitude * np.exp(1j * phase), SAMPLES)

    # A slow swell, one cycle per loop exactly, so the wind breathes rather
    # than sitting at one level -- and so the swell itself does not tick.
    t = np.arange(SAMPLES) / RATE
    return bed * (1.0 + 0.35 * np.sin(2.0 * math.pi * t / SECONDS))


def place(track, sound, start):
    """Add `sound` at sample `start`, wrapping round the end of the loop."""
    finish = start + len(sound)
    if finish <= SAMPLES:
        track[start:finish] += sound
    else:
        split = SAMPLES - start
        track[start:] += sound[:split]
        track[:finish - SAMPLES] += sound[split:]


def band(noise, centre, width):
    """Keep only the part of a noise burst near `centre` Hz.

    **This is the whole rework.** Everything that is supposed to sound like an
    insect here is band-limited noise rather than a tone, because that is what
    an insect is: a resonance an octave wide, not a frequency.
    """
    spectrum = np.fft.rfft(noise)
    freqs = np.fft.rfftfreq(len(noise), 1.0 / RATE)
    spectrum = spectrum * np.exp(-((freqs - centre) / width) ** 2)
    return np.fft.irfft(spectrum, len(noise))


def chirp(rng, centre, width, length, buzz):
    """One cricket chirp: a burst of band-limited noise, hard on and soft off.

    `buzz` is the stridulation rate -- the wing beat inside a single chirp, at
    a couple of hundred hertz. It is what stops the chirp being a *tick* and
    makes it a *creak*, and it is the second thing the sine version lacked.
    """
    count = max(int(RATE * length), 16)
    t = np.arange(count) / RATE

    sound = band(rng.uniform(-1.0, 1.0, count), centre, width)
    sound *= 0.55 + 0.45 * np.sin(2.0 * math.pi * buzz * t)

    # Fast attack, slower decay. The other way round is a bird.
    edge = max(int(count * 0.12), 1)
    envelope = np.ones(count)
    envelope[:edge] = np.linspace(0.0, 1.0, edge)
    envelope[edge:] = np.linspace(1.0, 0.0, count - edge) ** 1.5

    return sound * envelope


def crickets():
    """Three voices, each repeating at a rate that divides the loop exactly.

    Three, not five, and slower. The first version had five voices firing up to
    twenty times a loop and the result was a swarm. A night wood has a few
    close insects you could count and a wash of distant ones, and the wash is a
    different layer -- see `hiss`.

    Every period divides 176 400 samples exactly, so the last chirp of a loop
    is followed by the first at the spacing it would have had.
    """
    rng = np.random.default_rng(SEED ^ 0xC0FFEE)
    track = np.zeros(SAMPLES)

    # (centre Hz, band width, groups per loop, chirps per group, gap, level)
    voices = (
        (3900.0, 900.0, 6, 3, 0.085, 1.00),
        (4600.0, 700.0, 5, 2, 0.100, 0.60),
        (3300.0, 800.0, 4, 4, 0.075, 0.45),
    )

    for centre, width, per_loop, group, gap, level in voices:
        period = SAMPLES // per_loop
        offset = int(rng.integers(0, period))

        for index in range(per_loop):
            start = (offset + index * period) % SAMPLES
            for note in range(group):
                sound = chirp(rng, centre, width, 0.030, 210.0) * level
                place(track, sound, (start + int(note * gap * RATE)) % SAMPLES)

    return track


def hiss():
    """The wash of distant insects: a continuous narrow band up near 6 kHz.

    The layer that makes this sound like a night. It is not meant to be heard
    as anything -- take it out and the bed sounds empty; leave it in and nobody
    can say what it is.

    Built in the frequency domain like the wind, so it is periodic too, and
    swelled by two rates that both divide the loop.
    """
    rng = np.random.default_rng(SEED ^ 0x515515)
    bins = SAMPLES // 2 + 1
    freqs = np.arange(bins) * (RATE / SAMPLES)

    magnitude = np.exp(-((freqs - 5800.0) / 1600.0) ** 2)
    magnitude[0] = 0.0

    phase = rng.uniform(0.0, 2.0 * math.pi, bins)
    wash = np.fft.irfft(magnitude * np.exp(1j * phase), SAMPLES)

    t = np.arange(SAMPLES) / RATE
    return wash * (1.0 + 0.30 * np.sin(2.0 * math.pi * 2.0 * t / SECONDS)
                   + 0.20 * np.sin(2.0 * math.pi * 3.0 * t / SECONDS + 1.1))


def rustle():
    """The canopy actually moving: brief bursts of soft noise, four a loop.

    Wind that never varies is a fan. Four short rustles, wrapped, is what makes
    the bed sound like it is coming off leaves rather than out of a vent.
    """
    rng = np.random.default_rng(SEED ^ 0x1EAF)
    track = np.zeros(SAMPLES)

    for at in (0.07, 0.31, 0.55, 0.82):
        length = int(rng.uniform(0.7, 1.4) * RATE)
        noise = band(rng.uniform(-1.0, 1.0, length), 1500.0, 1400.0)
        envelope = np.sin(np.linspace(0.0, math.pi, length)) ** 1.6
        place(track, noise * envelope * rng.uniform(0.5, 1.0),
              int(at * SAMPLES))

    return track


def trills():
    """The larger insects: a long band-limited trill, twice a loop.

    Two, not ten. A trill is what makes the bed sound like a place rather than
    a texture, and it works because it is rare.
    """
    rng = np.random.default_rng(SEED ^ 0x7411)
    track = np.zeros(SAMPLES)

    for frequency, length, at, level in ((2300.0, 1.4, 0.19, 0.55),
                                         (2750.0, 0.9, 0.66, 0.35)):
        count = int(RATE * length)
        t = np.arange(count) / RATE

        tone = band(rng.uniform(-1.0, 1.0, count), frequency, 450.0)
        tone *= 0.45 + 0.55 * np.sin(2.0 * math.pi * 34.0 * t)
        tone *= np.sin(np.linspace(0.0, math.pi, count)) ** 1.2

        place(track, tone * level, int(at * SAMPLES))

    return track


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default="camp_night.wav")
    args = parser.parse_args()

    def unit(signal):
        return signal / (np.abs(signal).max() or 1.0)

    bed = unit(wind())
    leaves = unit(rustle())
    wash = unit(hiss())
    insects = unit(crickets())
    calls = unit(trills())

    # **The ratio is the sound.** More bed and it is a motorway; more crickets
    # and it is a pet shop. The wash sits under everything and is nearly the
    # loudest thing here -- and it is the one nobody will identify.
    mix = (0.44 * bed + 0.18 * leaves + 0.26 * wash
           + 0.16 * insects + 0.06 * calls)

    mix = unit(mix) * 0.7   # headroom: this plays under a fire that is also
                            # making noise

    pcm = np.clip(mix * 32767.0, -32768, 32767).astype(np.int16)

    out = ROOT / "SampleProject" / "assets" / "audio" / args.output
    out.parent.mkdir(parents=True, exist_ok=True)

    with wave.open(str(out), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(pcm.tobytes())

    handle_value = 0xCBF29CE484222325
    for byte in out.name.encode("utf-8"):
        handle_value ^= byte
        handle_value = (handle_value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF

    source = 0xCBF29CE484222325
    for byte in out.read_bytes():
        source ^= byte
        source = (source * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF

    out.with_name(out.name + ".meta").write_text(
        "Handle: {0}\nType: Audio\nSourceHash: {1}\n".format(handle_value, source),
        encoding="utf-8")

    # **The claim that the loop is seamless, measured rather than asserted.**
    # The step from the last sample back to the first has to be no larger than
    # the steps the waveform takes anywhere inside itself; if it is, that is a
    # click, and it will be audible once every eight seconds forever.
    join = abs(int(pcm[0]) - int(pcm[-1]))
    inner = int(np.abs(np.diff(pcm.astype(np.int32))).max())
    print("{0}: {1:g}s at {2} Hz, {3} bytes".format(
        out, SECONDS, RATE, out.stat().st_size))
    print("  loop join {0} against a largest inner step of {1}".format(join, inner))
    if join > inner:
        raise SystemExit("the loop point is a bigger step than anything inside "
                         "the loop, which is a click once per repeat")
    print("  handle {0}".format(handle_value))


if __name__ == "__main__":
    main()
