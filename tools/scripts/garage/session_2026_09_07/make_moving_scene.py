# -*- coding: utf-8 -*-
"""Author `showroom_moving.rage`: the garage with one object crossing it.

**Why this scene has to exist.** Every ghosting claim in this repository has
been measured in a scene where **only the camera moves**. That is the wrong
experiment: the failure a temporal filter is judged on is a smear trailing a
*moving object*, and neither the garage nor the bridge has one. RT-6.2's clamp
width, RT-6.10's whole subject and RT-5's anti-lag are all blocked on it.

**Why a purpose-built cube rather than the car.** `burst.py`'s `BURST_SLIDE`
can drive the car, and the first attempt did -- but the model is 49 entities and
a tag prefix matches the root *and* its parts, so each part takes its own Slider
**and** its parent's translation, and the body separates from the rest at double
speed. The `=tag` form exists for that, and two entities here share the exact tag
`porsche_992_gt3_r`, so even that is ambiguous. A single flat-hierarchy object
has none of these problems and is a better instrument besides: one rectangle,
unambiguous edges, easy to mask.

**What it is: a near-mirror chrome cube** (metallic 1, roughness 0.12), above
the car's roofline and in front of the graffiti wall -- matte, rough, non-metal, which is exactly the surface
RT-6.2's widening applies to -- and over the wet floor, which reflects it, which
is RT-6.10's case.

**Metal on purpose** (owner, 2026-09-07: *"metallic objects tend to create a lot
of ghosting, like the car and the chrome poles"*). An emissive white cube was
tried first and is the *easy* case: its colour does not change with the view, so
a stale history is nearly right and the filter is barely tested. A mirror's
colour is entirely a function of view direction, so every frame's history is
genuinely a different answer -- which is why smearing is worst on shiny surfaces
and why the moving object here has to be one. It carries the Slider itself, so the scene is self-contained:
capture with the camera at `--speed=0` and the object is the only thing moving.

**`Static: false` matters.** A static flag would put it on the baked/static side
of the split and it would not move at all.
"""
import io
import os

SRC = 'SampleProject/assets/scenes/showroom.rage'
DST = 'SampleProject/assets/scenes/showroom_moving.rage'

CUBE = 8241982477996916736      # BuiltinAssets::kPrimitiveBase + PrimitiveType::Cube

ENTITY = """  - EntityID: 7311000000000000101
    TagComponent:
      Tag: MovingPanel
    TransformComponent:
      Position: [-9, 1.6, -6]
      Rotation: [0, 0, 0]
      Scale: [1.2, 1.2, 1.2]
    MeshComponent:
      Static: false
      Mesh: %d
      Material:
        BaseColor: [0.95, 0.95, 1, 1]
        Emissive: [0, 0, 0, 1]
        Metallic: 1
        Roughness: 0.12
        Occlusion: 1
    NativeScriptComponent:
      Script: Slider
      Fields:
        Speed: 3
        StopAfter: 3
""" % CUBE

s = io.open(SRC, encoding='utf-8', newline='').read()
if 'MovingPanel' in s:
    raise SystemExit('the source scene already has one')

# Append to the entity list. The file ends inside the last entity's last
# component, so the new entity goes on the end with the list's own indent.
if not s.endswith('\n'):
    s += '\n'
s += ENTITY.replace('\n', '\r\n') if '\r\n' in s else ENTITY

io.open(DST, 'w', encoding='utf-8', newline='').write(s)
print('wrote %s (+1 entity: MovingPanel, a cube on a Slider at 3 m/s for 3 s)' % DST)
print('capture with: BURST_SCENE=showroom_moving.rage python tools/scripts/garage/burst.py <tag> --speed=0 ...')
