# Particles

A **particle emitter** is a component. Add one to an entity, press Play, and it
throws quads.

Everything about an emitter is on that one component — there is no separate
particle asset, no editor window, no node graph. That is a deliberate ceiling:
an emitter describes one effect, and an effect made of several behaviours is
several emitters parented together.

## The shape of an emitter

| Field | What it does |
|---|---|
| `Emit`, `Rate` | Continuous emission, in particles per second. |
| `Burst` | Fired once and consumed. An explosion is `Emit` off and a burst. |
| `Lifetime`, `LifetimeJitter` | How long each lives, and how much that varies. |
| `Shape`, `Box size` | Where a particle is born: at the emitter, or anywhere inside a volume. See below. |
| `Motion` | Whether it then moves at all. |
| `Direction`, `Spread`, `Speed`, `SpeedJitter` | The cone they leave through. `Spread` is a half-angle in degrees; 180 is a sphere. |
| `Gravity`, `Drag` | The emitter's **own** gravity, not the physics world's. |
| `SizeStart`, `SizeEnd`, `ColorStart`, `ColorEnd` | Interpolated over each particle's life. |
| `Size curve`, `Colour gradient`, `Alpha curve` | Shapes for the same three ramps, when a straight line will not do. See below. |
| `Spin` | Maximum degrees per second, signed at random per particle. |
| `Texture` | Optional sprite. A plain quad without one. |
| `MaxParticles` | The pool. A full pool refuses to spawn rather than recycling. |

Nothing collides. A particle is appearance: it has no rigid body, nothing can
be hit by one, and no script is told about one.

> [!NOTE]
> **Particles advance per frame, not per simulation step.** This is the
> opposite call from scripts and physics, and it is deliberate — appearance
> should move as smoothly as the display can show it. A hitch is forgiven
> rather than paid back, so a two-second stall does not produce two seconds of
> particles in one frame.

## Spawning in a volume

`Shape: Point` puts every particle at the emitter's own position. That is right
for anything with a source — a chimney, a muzzle, a wound — and wrong for
anything spread over an area.

The failure is worth recognising, because it is not fixed by tuning. Fireflies
over a clearing, made from a point emitter with `Spread` at 180 and a lot of
speed jitter, read as a slow fountain: there is a hole where the emitter is,
because nothing has drifted away from it yet, and a shell where the oldest have
got to. **The tell is the empty space at the origin, not the speed**, so making
them slower makes it worse.

`Shape: Box` spawns each particle at a uniformly random point inside `Box size`,
so the volume is full from the first frame and stays full.

```
Shape        Box
Box size     15, 2.6, 11
Motion       Directional
Speed        0.13
```

`Box size` is the box's **full extents in metres**, in the emitter's own frame.
It is its own size rather than a multiple of the entity's scale — the rule
`ColliderComponent` follows, so an emitter parented to something scaled is not
scaled twice. It does turn with the emitter.

> [!NOTE]
> A **local-space** emitter is the one exception, and cannot be otherwise. Its
> particles are stored in the emitter's frame and the transform is applied when
> they are drawn, so its box rides the entity's scale along with everything else
> it emits.

### Motion: whether they go anywhere

`Motion: Directional` is the cone above, then `Gravity` and `Drag` — what every
emitter has always done, and still the default.

`Motion: Stationary` gives a particle no velocity and applies neither force. It
appears where it was born, lives out its lifetime and goes. With a box that is
a field of things blinking on and off in place, which is a different effect
from a slow drift and worth reaching for directly.

> [!WARNING]
> `Stationary` is **not** the same as `Speed` at zero. Gravity would still take
> it, and turning gravity off as well spells one intention across two fields
> that have to be read together — and that the next person to open the
> inspector has to reverse-engineer.

> [!TIP]
> **Window > Show Emitters** (F4) draws every box emitter's volume in the scene
> view, and nothing in the game view. It is off by default, like the collider
> overlay beside it. It is drawn from the same function the
> emitter spawns from, so it cannot show a box the particles do not use.

## Curves, when two endpoints are not enough

A start and an end can only say "from this, to that, evenly". Plenty of
effects need a shape: smoke that swells fast and then drifts, a spark that
flashes and decays over a long tail, a puff that fades in **and** out. That
last one is worth dwelling on — it needs three values, and a pair has two, so
no amount of tuning `ColorStart` and `ColorEnd` will ever produce it.

Curves are assets. Drop a `.rcurve` on **Size curve**, **Colour gradient** or
**Alpha curve** and it takes over that ramp; leave one empty and the pair
still decides it. They are independent, so authoring a size curve does not
oblige you to author a colour one, and every emitter made before curves
existed keeps working exactly as it did.

The editor draws the curve under its own field, because a ramp only means
anything beside the emitter it shapes:

- **Drag** a point to move it. Drag one past its neighbour and the curve
  re-sorts rather than turning inside out.
- **Double click** empty space to add a point, or a point to remove it.
- A **scalar curve** (size, alpha) is a graph, and its vertical range grows to
  fit — a size curve in world units is not confined to 0..1.
- A **gradient** is a stop strip. Stops move in time by dragging; hover one to
  get a colour picker for it.

`tools/scripts/make_curve_presets.py` writes four to start from — `swell`,
`fade_in_out`, `flash_decay` and `ember` — and a scene putting a curved
emitter beside an uncurved one.

> [!NOTE]
> **Alpha is its own curve, not the gradient's fourth channel.** Opacity and
> hue almost never want the same shape, and keeping them apart is what lets
> one gradient be shared between emitters that fade differently.

> [!NOTE]
> Curves work identically on the GPU. Both paths read the same 64-sample table
> — resolved once on the CPU, so the simulation never has to decide which of a
> curve and a pair wins — which is why switching `SimulateOnGpu` cannot change
> how an emitter looks.

> [!TRAP]
> A curve makes an emitter's particles differ more from each other in colour
> and opacity, and that is exactly the condition under which unsorted alpha
> goes wrong. On a **GPU** emitter using `Alpha`, expect a curved ramp to make
> the lack of sorting more visible than a plain one did. `Additive` and
> `WeightedBlended` are unaffected, because neither depends on order.

## Facing: 3D and 2D from one component

`Billboard` turns each quad to face the camera. That is smoke, fire, sparks —
anything in a 3D scene.

`Flat` lies in the XY plane instead. That is what a 2D game wants, and it is
the same emitter with the same fields; there is no separate 2D particle system
to learn.

## Space: who the particles belong to

`World` leaves a particle where it was born. Smoke keeps hanging where the
chimney was when the chimney moves.

`Local` carries particles with the emitter. A torch flame moves with the torch.

The transform is applied when a particle is *drawn*, not while it is
integrated — which is why a local emitter's particles ride it rather than being
dragged behind it.

## Blending: three answers to one hard problem

Transparency has an ordering problem. These are the three ways out, and the
choice matters more than any other field on the component.

**`Additive`** sums light. Fire, sparks, magic, muzzle flashes. Order cannot
affect a sum, so this is correct from every angle and is the cheapest of the
three. Reach for it first.

**`Alpha`** is ordinary transparency, drawn back to front. Smoke, dust, debris —
anything that reads as *matter* rather than light. Particles are sorted within
a CPU emitter, and emitters are sorted against each other.

**`WeightedBlended`** is alpha that needs no sort. Fragments accumulate into two
buffers — a sum and a product, both order-independent — and a second pass works
out the result. A thousand overlapping particles land in any order and look the
same.

Use it when alpha ordering is visibly wrong: two emitters that interpenetrate,
or a GPU emitter, which cannot sort itself at all.

> [!NOTE]
> Weighted blending costs two full-resolution buffers and a resolve pass, paid
> once per frame however many emitters use it — and **nothing at all** in a
> frame where none do. The engine allocates them only when a scene contains a
> weighted emitter.

It is an approximation, and worth knowing where it is loose. Each fragment gets
a weight that falls with distance, and only the *ratio* between overlapping
fragments reaches the picture. That ratio is a guess about how much a fragment
is hidden by whatever is in front of it — and no function of depth alone can
always be right, because what matters is how many layers are in front, not how
far away they are. In practice: dense plumes come out very close to sorted, and
a stack of equally transparent layers spread over a huge depth range comes out
with the near one too strong. `Alpha` is what exactness costs a sort.

> [!TRAP]
> If weighted blending ever looks flat — near particles failing to sit *in
> front* of far ones, the whole effect reading as an even wash — the depth
> weight has stopped discriminating rather than the blending being broken.
> `tools/scripts/check_oit.py` is the check for exactly that, and for the
> resolve landing the right way up.

## Simulating on the GPU

`SimulateOnGpu` moves an emitter's pool into a storage buffer and integrates it
with a compute shader. The CPU never touches a particle again.

The look is the same by construction: both paths write the same instance
layout and reach the same vertex shader. What changes is who pays. Measured at
roughly sixteen thousand particles across three emitters, vsync off:

| | CPU | GPU |
|---|---|---|
| Vulkan | 3.30 ms | 1.37 ms |
| OpenGL | 3.80 ms | 1.58 ms |

**Switching is free and can be done at any time**, including while playing. The
compute pipeline is built once, and an emitter's buffers are kept while the
emitter exists rather than while it is using them — so ticking the box is a
branch, not an allocation.

Switching *to* the GPU carries the particles already in the air across.
Switching back does not: reading the pool off the GPU would mean a stall, which
is the one thing the GPU path exists to avoid, so it drops what is in flight and
refills.

> [!TRAP]
> **A GPU emitter cannot sort its own particles**, because sorting them would
> mean reading them back. An `Alpha` GPU emitter blends in pool order, which is
> visibly wrong wherever its particles differ strongly in colour or opacity.
> Use `Additive` or `WeightedBlended` for anything simulating on the GPU.

## Firing a burst from a script

`Burst` is an order, not a rate: it is consumed by the next simulation step and
reset to zero. Writing it again fires again.

From C++, with the component in hand:

```cpp
if (HasComponent<ParticleEmitterComponent>())
    GetComponent<ParticleEmitterComponent>().Burst = 40;
```

From C#, through the component bridge:

```csharp
Entity.SetComponentField("ParticleEmitterComponent", "Burst", "40");
```

An emitter authored with `Emit` off and a non-zero `Burst` fires once when the
scene starts and never again — which is the whole of an explosion prefab, with
no script at all.

## Common mistakes

| Symptom | Cause |
|---|---|
| Particles stop appearing under load | The pool is full. A full pool refuses to spawn; raise `MaxParticles` or shorten `Lifetime`. |
| A moving emitter drags its particles | `Space` is `Local` where `World` was meant. |
| Smoke looks wrong from one side only | `Alpha` on a GPU emitter, which cannot sort. Use `WeightedBlended`. |
| An additive emitter never fades out | Additive ignores the alpha channel; fade `ColorEnd`'s **colour** toward black, not just its alpha. |
| Nothing draws at all | The emitter is on an entity with no transform, or `MaxParticles` is zero. |
