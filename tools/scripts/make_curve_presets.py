#!/usr/bin/env python3
"""Write a few stock .rcurve assets, and a scene that shows them working.

Same convention as make_sky_hdr.py and make_game_audio.py: the asset is
generated rather than committed as an opaque blob, so the shape it has is
readable as the code that made it.

These are the ramps a start/end pair cannot express, which is the whole
reason curves exist -- something that swells fast and then holds, something
that fades in *and* out, a colour that passes through a hue on its way rather
than crossfading straight to it.

    python tools/scripts/make_curve_presets.py
    build/bin/Debug/RageVRuntime/RageVRuntime.exe --project=SampleProject \
        --scene=scenes/particles_curves.rage --rhi=vulkan --validation=on \
        --screenshot=curves.png
"""

import argparse
import os

# name -> (channels, [(time, [values...]), ...])
CURVES = {
    # Size: a puff that grows quickly and then drifts at roughly its final
    # size, instead of the straight line a pair gives.
    "swell": (1, [(0.0, [0.05]), (0.25, [0.55]), (1.0, [0.8])]),
    # Alpha: invisible at both ends. The shape a start/end pair simply cannot
    # make, because it has only two values and this needs three.
    "fade_in_out": (1, [(0.0, [0.0]), (0.15, [0.75]), (0.6, [0.6]), (1.0, [0.0])]),
    # Alpha: a spark -- bright immediately, then a long decay.
    "flash_decay": (1, [(0.0, [1.0]), (0.1, [0.9]), (1.0, [0.0])]),
    # Colour: through orange on the way from yellow to a dark red, which a
    # straight crossfade would cut the corner of.
    "ember": (3, [(0.0, [1.0, 0.95, 0.55]),
                  (0.3, [1.0, 0.55, 0.12]),
                  (0.7, [0.65, 0.14, 0.04]),
                  (1.0, [0.18, 0.03, 0.02])]),
}

SCENE = """Scene: ParticlesCurves
Version: 5
Environment:
  AmbientColor: [0.42, 0.47, 0.58]
  AmbientIntensity: 0.12
  Sky: 1
  SkyHorizon: [0.2, 0.22, 0.3]
  SkyZenith: [0.05, 0.07, 0.14]
  SkyGround: [0.1, 0.1, 0.11]
  SkyIntensity: 1
  SkyRotation: 0
  SkyTexture: 0
Entities:
  - EntityID: 7777000000000000001
    TagComponent:
      Tag: Camera
    TransformComponent:
      Position: [0, 1.6, 6]
      Rotation: [-0.08, 0, 0]
      Scale: [1, 1, 1]
    CameraComponent:
      ViewRank: 0
      FixedAspectRatio: false
      ProjectionType: Perspective
      PerspectiveFOV: 60
      PerspectiveNearClip: 0.01
      PerspectiveFarClip: 1000
      OrthographicScale: 10
      OrthographicNearClip: -1
      OrthographicFarClip: 1
"""


def emitter(index, name, x, curves):
    """One plume. Identical settings either side; only the curves differ."""
    lines = [
        f"  - EntityID: 777700000000000001{index}",
        "    TagComponent:",
        f"      Tag: {name}",
        "    TransformComponent:",
        f"      Position: [{x:g}, 0, 0]",
        "      Rotation: [0, 0, 0]",
        "      Scale: [1, 1, 1]",
        "    ParticleEmitterComponent:",
        "      Emit: true",
        "      Rate: 45",
        "      Burst: 0",
        "      Lifetime: 2.4",
        "      LifetimeJitter: 0.15",
        "      Direction: [0, 1, 0]",
        "      Spread: 16",
        "      Speed: 1.1",
        "      SpeedJitter: 0.25",
        "      Gravity: [0, 0.35, 0]",
        "      Drag: 0.5",
        # The pair the curves replace. Left alone on the reference emitter, and
        # overridden one channel at a time on the other -- so the difference in
        # the picture is the curves and nothing else.
        "      SizeStart: 0.05",
        "      SizeEnd: 0.8",
        "      ColorStart: [1, 0.95, 0.55, 1]",
        "      ColorEnd: [0.18, 0.03, 0.02, 0]",
        "      Spin: 20",
        "      Facing: Billboard",
        "      Blend: Alpha",
        "      Space: World",
        "      Texture: 0",
        "      MaxParticles: 512",
        "      SimulateOnGpu: false",
    ]
    for field, value in curves.items():
        lines.append(f"      {field}: {value}")
    return "\n".join(lines) + "\n"


def write_curve(path, channels, keys):
    lines = [f"Curve: {channels}", "Keys:"]
    for time, values in keys:
        packed = ", ".join(f"{v:g}" for v in values)
        lines.append(f"  - T: {time:g}")
        lines.append(f"    V: [{packed}]")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")


def read_handle(meta_path):
    """A .meta the engine already minted tells us the handle to reference."""
    if not os.path.exists(meta_path):
        return None
    with open(meta_path, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("Handle:"):
                return line.split(":", 1)[1].strip()
    return None


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", default=os.path.join(root, "SampleProject"))
    args = parser.parse_args()

    curve_dir = os.path.join(args.project, "assets", "curves")
    os.makedirs(curve_dir, exist_ok=True)

    handles = {}
    for name, (channels, keys) in CURVES.items():
        path = os.path.join(curve_dir, name + ".rcurve")
        write_curve(path, channels, keys)
        handles[name] = read_handle(path + ".meta")
        print(f"wrote {path}")

    # The scene can only name curves the registry has already seen. On a first
    # run there are no .meta files yet, so the emitters are written without
    # them and a second run fills them in -- stated rather than silently
    # producing a scene whose curve fields are all zero.
    missing = [name for name, handle in handles.items() if not handle]
    if missing:
        print("\nNo .meta yet for: " + ", ".join(sorted(missing)))
        print("Open the project once (or run the runtime) so the registry mints them,")
        print("then run this again to write the scene that references them.")

    scene = SCENE
    scene += emitter(0, "Pairs", -1.6, {})
    scene += emitter(1, "Curves", 1.6, {
        key: handles[name]
        for key, name in (("SizeCurve", "swell"),
                          ("ColorGradient", "ember"),
                          ("AlphaCurve", "fade_in_out"))
        if handles.get(name)
    })

    scene_path = os.path.join(args.project, "assets", "scenes", "particles_curves.rage")
    with open(scene_path, "w", encoding="utf-8") as handle:
        handle.write(scene)
    print(f"wrote {scene_path}")

    # The same scene simulating on the GPU. Both emitters read the ramp the
    # CPU resolved, so the two files should be indistinguishable apart from
    # the RNG -- which is the check that the compute path samples the same
    # table rather than its own idea of one.
    gpu_path = os.path.join(args.project, "assets", "scenes", "particles_curves_gpu.rage")
    with open(gpu_path, "w", encoding="utf-8") as handle:
        handle.write(scene.replace("SimulateOnGpu: false", "SimulateOnGpu: true"))
    print(f"wrote {gpu_path}")


if __name__ == "__main__":
    main()
