#!/usr/bin/env python3
"""Generate the scenes that validate the weighted-blended depth weight.

Weighted-blended transparency lives or dies on its depth weight, and the
failure mode is a picture that looks plausible.  The only way to know is to
compare it against a render that is definitionally correct, so these scenes
are built so that such a render exists.

Two modes, both placing three emitters at 2, 20 and 200 units, dead centre,
overlapping on screen, with every spatial quantity scaled by distance so all
three subtend exactly the same angle.  Anything that differs between them in
the final image is the depth weight and nothing else.

  layers  One particle each, motionless, no fade, alpha 0.5, red/green/blue
          near to far.  Every emitter shares the component's RNG seed and
          makes the same draws from it, so all three quads take the *same*
          random rotation and land perfectly concentric.  That makes the
          right answer arithmetic rather than opinion: compositing 0.5 red
          over 0.5 green over 0.5 blue gives 4:2:1, a red-dominant pixel.
          An unweighted average of the same three gives grey.

  smoke   The same experiment with real plumes, for the eyes -- the layers
          mode proves the ratio, this one shows what it looks like.

Render each blend mode and compare against `alpha`, which is the reference:
a CPU emitter sorts its own particles, and three emitters at distinct depths
sort correctly against each other, so the alpha render is ground truth here.

    python tools/scripts/make_depth_scene.py
    build/bin/Debug/RageVRuntime/RageVRuntime.exe --project=SampleProject \
        --scene=scenes/particles_depth_alpha.rage --rhi=vulkan \
        --validation=on --screenshot=alpha.png
"""

import argparse
import os

# Distance, and the scale everything spatial is multiplied by.  Near is 1, so
# the near emitter is authored at its natural size and the others grow to
# match its screen footprint.
LAYERS = [
    ("Near", 2.0, (1.0, 0.15, 0.15)),
    ("Mid", 20.0, (0.15, 1.0, 0.15)),
    ("Far", 200.0, (0.15, 0.15, 1.0)),
]

BLENDS = {"alpha": "Alpha", "additive": "Additive", "weighted": "WeightedBlended"}

HEADER = """Scene: ParticlesDepth
Version: 5
Environment:
  AmbientColor: [0, 0, 0]
  AmbientIntensity: 0
  Sky: 0
  SkyHorizon: [0, 0, 0]
  SkyZenith: [0, 0, 0]
  SkyGround: [0, 0, 0]
  SkyIntensity: 0
  SkyRotation: 0
  SkyTexture: 0
Entities:
  - EntityID: 7776000000000000001
    TagComponent:
      Tag: Camera
    TransformComponent:
      Position: [0, 0, 0]
      Rotation: [0, 0, 0]
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


def vec(values):
    return "[" + ", ".join(f"{v:g}" for v in values) + "]"


def emitter(index, name, distance, color, blend, mode):
    """One emitter, authored so its screen footprint is independent of distance."""
    scale = distance / LAYERS[0][1]
    rgb = list(color)

    if mode == "layers":
        # Motionless, ageless, opacity flat over life: the only variable left
        # in the image is which fragment the weight favours.
        fields = {
            "Emit": "false",
            "Rate": "0",
            "Burst": "1",
            "Lifetime": "100000",
            "LifetimeJitter": "0",
            "Direction": vec((0, 1, 0)),
            "Spread": "0",
            "Speed": "0",
            "SpeedJitter": "0",
            "Gravity": vec((0, 0, 0)),
            "Drag": "0",
            "SizeStart": f"{1.0 * scale:g}",
            "SizeEnd": f"{1.0 * scale:g}",
            "ColorStart": vec(rgb + [0.5]),
            "ColorEnd": vec(rgb + [0.5]),
            "Spin": "0",
            "MaxParticles": "8",
        }
    else:
        # A plume, with every length scaled so the three are the same picture
        # at three distances.  Rate and lifetime are not lengths and do not
        # scale, which is what keeps the particle *count* equal too.
        fields = {
            "Emit": "true",
            "Rate": "60",
            "Burst": "0",
            "Lifetime": "2.2",
            "LifetimeJitter": "0",
            "Direction": vec((0, 1, 0)),
            "Spread": "20",
            "Speed": f"{0.5 * scale:g}",
            "SpeedJitter": "0.3",
            "Gravity": vec((0, 0.25 * scale, 0)),
            "Drag": "0.5",
            "SizeStart": f"{0.35 * scale:g}",
            "SizeEnd": f"{0.9 * scale:g}",
            "ColorStart": vec(rgb + [0.5]),
            "ColorEnd": vec(rgb + [0.0]),
            "Spin": "25",
            "MaxParticles": "512",
        }

    # Centred on the view axis at its own distance, and below it in the smoke
    # case so the plume rises through the middle rather than out of frame.
    y = 0.0 if mode == "layers" else -0.6 * scale
    lines = [
        f"  - EntityID: 777600000000000001{index}",
        "    TagComponent:",
        f"      Tag: {name}",
        "    TransformComponent:",
        f"      Position: {vec((0, y, -distance))}",
        "      Rotation: [0, 0, 0]",
        "      Scale: [1, 1, 1]",
        "    ParticleEmitterComponent:",
    ]
    for key, value in fields.items():
        lines.append(f"      {key}: {value}")
    lines += [
        "      Facing: Billboard",
        f"      Blend: {blend}",
        "      Space: World",
        "      Texture: 0",
        "      SimulateOnGpu: false",
    ]
    return "\n".join(lines) + "\n"


def build(blend_key, mode):
    text = HEADER
    for index, (name, distance, color) in enumerate(LAYERS):
        text += emitter(index, name, distance, color, BLENDS[blend_key], mode)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--mode", choices=("layers", "smoke"), default="layers")
    parser.add_argument("--blend", choices=sorted(BLENDS) + ["all"], default="all")
    parser.add_argument("--out-dir", default=os.path.join("SampleProject", "assets", "scenes"))
    args = parser.parse_args()

    keys = sorted(BLENDS) if args.blend == "all" else [args.blend]
    suffix = "" if args.mode == "layers" else "_smoke"

    for key in keys:
        path = os.path.join(args.out_dir, f"particles_depth{suffix}_{key}.rage")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(build(key, args.mode))
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
