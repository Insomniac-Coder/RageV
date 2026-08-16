# Animation

Skeletal animation, skinned on the GPU, with cross-fading between clips driven
by the component rather than by code.

Add an **Animator** to an entity whose mesh has a skeleton — which means a
glTF model that was rigged. The built-in primitives have no skeleton and
nothing to animate.

## The component

| Field | Default | What it does |
|---|---|---|
| Clip | `0` | Which clip to play, picked **by name** from a searchable dropdown |
| Playing | `true` | Advancing, or held on the current pose |
| Loop | `true` | Restart at the end, or hold the last frame |
| Speed | `1.0` | Playback multiplier. Negative plays backwards |
| Run in editor | `false` | Animate without entering Play |
| Blend time | `0.15` | Seconds to cross-fade when the clip changes. 0 cuts |

## Cross-fading

**Changing `Clip` starts a cross-fade, not a cut.** The outgoing clip keeps
advancing while the incoming one blends in over `BlendTime`, so a character
going from walk to run does not snap.

That is why the fade is a property of the component rather than something a
script drives: set the clip and the blend happens. A script that wants a cut
sets `BlendTime` to 0.

The state behind it — which clip is fading out, how far in the fade is — is
runtime state, listed in the
[component reference](components.md#animatorcomponent). It is not saved, and
setting it directly is not how the fade is meant to be driven.

## Run in editor

**Off by default, deliberately.** With it off, the editor shows the saved
pose — what you see is what is in the file. With it on, the animation plays in
the viewport without entering Play mode, which is what you want while tuning a
clip and not what you want while placing objects.

> [!NOTE]
> It is per animator rather than a global editor toggle, so one character can
> be previewing while the rest of the scene holds still.

## Bounds

A skinned mesh's bounding volume is computed to cover **the whole animation**,
not just the bind pose. A character that reaches out during a clip does not get
culled when the reach takes them outside their resting bounds.

This matters more than it sounds: bounds from the bind pose alone make
characters vanish at the screen edge exactly when they are most visible, and
the bug looks like a culling bug rather than an animation one.

## Skinning

Bone matrices are computed on the CPU and uploaded per frame; the vertex shader
does the skinning. The matrices are runtime state on the component
(`Skinning`), readable but not meant to be written.

## What is not here

Animation is deliberately a small feature at this stage:

- **No blend trees or state machines.** One clip plays, another can cross-fade
  in. Anything more structured is script logic choosing clips.
- **No root motion.** A clip animates the skeleton; moving the entity is the
  transform's job.
- **No IK.**
- **No animation events.** A script polling the animator's time is the current
  answer.

## Where to go next

- [Component reference](components.md#animatorcomponent) — every field
- [Assets](assets.md) — importing a rigged glTF
- [Scripting](scripting/index.md) — choosing clips from code
