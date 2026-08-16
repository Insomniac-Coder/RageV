# Audio

Positional sound on **miniaudio**, with four mixing buses, streaming for long
clips, and one-shots you can fire from a script without an entity.

Two components: an `AudioSourceComponent` makes sound, an
`AudioListenerComponent` hears it. A scene with sources and no listener is
silent.

## The source

| Field | Default | What it does |
|---|---|---|
| Clip | none | Which sound |
| Bus | `SFX` | `Master`, `Music`, `SFX` or `UI` |
| Volume | `1.0` | Linear gain |
| Pitch | `1.0` | Playback rate — also shifts the pitch, like a tape |
| Loop | `false` | Restart at the end |
| Play on awake | `true` | Start when the scene starts playing |
| Spatial | `true` | Positioned in 3D |
| Min distance | `1.0` | Within this, full volume |
| Max distance | `50.0` | Beyond this, silent |
| Stream | `false` | Decode as it plays rather than loading whole |

### Spatial, and when to turn it off

**On**, the sound comes from the entity's position: it pans as the listener
turns, and it attenuates between `MinDistance` and `MaxDistance`.

**Off**, it plays flat at full volume regardless of where anything is. That is
what music and most UI sounds want — a menu click that gets quieter when you
turn your head is a bug.

### Streaming

`Stream` decodes the file as it plays instead of loading it into memory first.

- **On** for music and long ambience: a three-minute track is tens of megabytes
  decoded, and you do not want that resident or the hitch of decoding it.
- **Off** for everything short. A footstep that has to hit disk on the first
  play is a footstep that arrives late.

## The listener

`AudioListenerComponent` marks what hears the scene — usually the camera, or
the player's head.

`ListenerRank` picks between several the same way `ViewRank` picks a camera:
**lowest wins**, ties broken by entity id so the answer is the same every run.

## Buses

Four, and every source names one:

| Bus | For |
|---|---|
| `Master` | Everything — the others feed into it |
| `Music` | The soundtrack |
| `SFX` | The world |
| `UI` | Menus and feedback |

Bus volumes are settable from a script, which is what an options menu's three
sliders are wired to.

## One-shots from a script

For a sound with no entity of its own — an impact, a pickup, a click — both
languages have:

- **`PlayOneShot(clip, volume, pitch)`** — flat, no position
- **`PlayOneShotAt(clip, position, volume, pitch)`** — positioned in 3D

A one-shot needs no `AudioSourceComponent` and cleans itself up when it
finishes. Randomising the pitch slightly per call is the cheapest way to stop
repetition from sounding mechanical:

```cpp
PlayOneShotAt(m_ImpactClip, collision.Point, 1.0f,
              0.9f + Math::Fract(Time::Now() * 7919.0f) * 0.2f);
```

## No audio device

**The engine runs perfectly with no sound card, no driver and no speakers.** A
null backend takes over, every call succeeds, and nothing is played.

That is the design rather than a fallback: it means a build machine, a
container or a headless check runs the same code path as a player's machine,
and "it crashed on the server" is not a category of audio bug here.

## Where to go next

- [Component reference](components.md#audio) — every field on both components
- [Scripting](scripting/index.md) — one-shots, bus volumes and playback control
