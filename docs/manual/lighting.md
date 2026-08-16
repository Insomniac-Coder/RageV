# Lighting

Three light types, image-based lighting from the sky, reflection probes for
what is nearby, and cascaded shadows. All of it is forward-rendered and
clustered, which is why there is **no cap on how many lights a scene may
have**.

## The LightComponent

Add **Light** to an entity. Its transform is the light: position for point and
spot, orientation for directional and spot.

| Field | Default | What it does |
|---|---|---|
| Type | `Directional` | `Directional`, `Point` or `Spot` |
| Colour | white | The light's colour |
| Intensity | `1.0` | Brightness multiplier |
| Range | `10.0` | Metres. Point and spot only — beyond it the light contributes nothing |
| Inner cone | `20.0` | Degrees. Spot only: the fully-lit cone |
| Outer cone | `30.0` | Degrees. Spot only: where it has fallen to nothing. The gap between the two is the soft edge |
| Cast shadows | `true` | Off makes the light much cheaper and lets it shine through walls |

### The three types

**Directional** has a direction and no position — the sun. Its `Range` is
ignored and it lights everything in the scene. It is the light that casts the
cascaded shadows described below.

**Point** radiates from a position in all directions. `Range` bounds it, and
that bound is what lets the clustering skip it for most of the screen.

**Spot** is a point light with a cone. `InnerCone` is fully lit, `OuterCone` is
the edge, and between them the falloff is smooth. Setting them equal gives a
hard-edged circle.

> [!NOTE]
> **Range is not a soft suggestion — it is where the light stops.** The
> clustering uses it to decide which lights touch which part of the screen, so
> a range far larger than the light's visible reach costs performance for
> nothing, and one too small clips the falloff visibly.

## Why there is no light cap

Lights are written into a storage buffer and the view frustum is divided into a
grid of clusters, each holding the indices of the lights that touch it. A pixel
looks up its own cluster and shades against those lights alone.

The practical consequence: **a hundred small lights in different rooms cost
about what a hundred small lights should**, because no pixel ever considers
more than the handful that reach it. What is expensive is a hundred lights that
all overlap the same pixels.

The `--benchmark` report prints the busiest cluster's light count, which is the
number that actually matters.

## Ambient light and the sky

Scene-wide lighting lives in **Render Settings → Environment**, because it
describes the *place* rather than the cost or the look.

| Setting | What it does |
|---|---|
| Ambient colour | A flat term added everywhere, for when there is no sky |
| Ambient intensity | How much of it |
| Sky | A procedural gradient or an HDR cube map |
| Sky horizon / zenith / ground | The three colours of the procedural gradient |
| Sky intensity | Brightness of the sky as a light source |
| Sky rotation | Turns the sky, which turns where its light comes from |

**The sky is a light, not a backdrop.** With a sky set, the engine builds
image-based lighting from it: an irradiance cube for diffuse, a prefiltered
roughness chain for specular, and a BRDF lookup table. A metal object with no
lights at all still looks like metal, because the sky is lighting it.

> [!TRAP]
> Changing the sky re-runs that convolution. It is not free, and it is why the
> sky colour pickers feel heavier than the ambient ones.

## Reflection probes

The sky lights everything, but it does not know your room is red. A
**ReflectionProbeComponent** captures the actual surroundings from one point
into a cube map, and nearby surfaces reflect that instead.

The scene picks **one probe per render**: the nearest whose `Influence` sphere
contains the camera, falling back to the sky when none does.

The fields are in the [component reference](components.md#reflectionprobecomponent);
the two that decide the cost are:

- **`Update`** — `Baked` captures once and costs nothing afterwards. `Realtime`
  re-captures forever.
- **`Rate`** — realtime only, and **this is where the cost actually is**. The
  demo's probe at 15 Hz is indistinguishable from per-frame and costs a quarter
  as much.

> [!NOTE]
> A reflection is seen through a rough or curved surface, so it can lag the
> scene by a few frames without anyone noticing. That is the whole argument for
> the rate dial, and it holds for everything except a true mirror.

## Shadows

The directional light casts **cascaded shadow maps**: the view is split into
several depth ranges, each with its own map, so texels stay small near the
camera without needing one enormous map.

Spot and point lights cast their own — a single map for a spot, a cube for a
point.

The settings are on the project, in [Rendering](rendering.md#shadows). The two
that matter most:

- **`ShadowResolution`** — the single biggest lever on both quality and cost.
  Four 2048 maps is 64 MB of depth.
- **`ShadowDistance`** — how far shadows are drawn at all, and **not** the
  camera's far plane. Past it the texels are so large the shadow is worse than
  none.

### Acne and peter-panning

The two shadow artefacts, and they pull against each other:

- **Acne** is a surface shadowing itself in stripes, because the shadow map's
  resolution cannot resolve its own slope.
- **Peter-panning** is a shadow detaching from its caster, so an object appears
  to float.

`ShadowNormalOffset` pushes the sample along the surface normal. Raising it
removes acne and starts causing peter-panning. **There is no value that has
neither** — which is why the shadow pass also renders back faces, so the depth
it compares against is the far side of the object rather than the near one.

## Where to go next

- [Component reference](components.md) — every field on the light and probe
- [Rendering](rendering.md) — the shadow settings and the frame's pass order
- [Materials](materials.md) — how a surface responds to all of this
