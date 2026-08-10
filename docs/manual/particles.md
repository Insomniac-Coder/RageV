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
| `Direction`, `Spread`, `Speed`, `SpeedJitter` | The cone they leave through. `Spread` is a half-angle in degrees; 180 is a sphere. |
| `Gravity`, `Drag` | The emitter's **own** gravity, not the physics world's. |
| `SizeStart`, `SizeEnd`, `ColorStart`, `ColorEnd` | Interpolated over each particle's life. |
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
