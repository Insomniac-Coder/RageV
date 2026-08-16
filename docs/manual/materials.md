# Materials

How a surface responds to light. RageV shades with metallic-roughness PBR, the
same model glTF uses — so a material exported from Blender, Substance or any
glTF-aware tool means here what it meant there.

A material is an **asset**, shared by every mesh that names it. Create one from
the Content browser, or let a glTF import make them for you.

## The scalar parameters

Every material has these, whether or not it has any textures.

| Parameter | Default | Range | What it does |
|---|---|---|---|
| Base colour | white | RGBA | The albedo. For metals this *is* the reflection tint |
| Emissive colour | black | RGBA | Light the surface gives off. Not affected by any light, and what bloom picks up |
| Metallic | `0.0` | 0 to 1 | 0 is a dielectric (plastic, wood, stone), 1 is bare metal. **Values between are almost always wrong** except where a metal is partly covered |
| Roughness | `0.5` | 0 to 1 | 0 is a mirror, 1 is fully diffuse. The single most expressive control here |
| Occlusion | `1.0` | 0 to 1 | Baked ambient occlusion. 1 is unoccluded |
| Normal scale | `1.0` | ≥ 0 | How strongly the normal map perturbs the surface. 0 flattens it |
| Specular | `0.5` | 0 to 1 | Reflectance when **not** metal, as `F0 = 0.08 × Specular`. 0.5 is the 4% almost every dielectric has |
| Height scale | `0.05` | UV units | How deep the height map displaces. 0 turns parallax off even with a map bound |
| UV transform | `1, 1, 0, 0` | — | `xy` scales the texture coordinate, `zw` offsets it |

### Specular, and when to touch it

Leave it at 0.5 unless you know why. 4% reflectance is right for almost every
non-metal. The exceptions are real but few: **water is nearer 2%**, and
gemstones and some plastics are considerably higher.

### The UV transform, and why it exists

The built-in primitives give every face UVs spanning 0 to 1, and **scaling an
entity does not touch its UVs**. So a ground plane scaled to twelve metres
stretches one copy of its texture across the whole thing.

The UV transform is where a surface's texel density is stated. Set `xy` to
`12, 12` and the tile repeats twelve times.

> [!NOTE]
> Tiling is on the **material**, not the entity — it is a property of the
> texture set. Colour varies per object constantly, which is why the colour
> overrides are per entity and this is not.

## Texture maps

| Map | Replaces | Notes |
|---|---|---|
| Base colour | the base colour parameter | Multiplied by it, so the parameter tints the map |
| Normal | — | Tangent space. Strength is `NormalScale` |
| Metallic | the metallic parameter | |
| Roughness | the roughness parameter | |
| Occlusion | the occlusion parameter | |
| Emissive | the emissive parameter | |
| Specular | the specular parameter | |
| Height | — | Drives parallax, scaled by `HeightScale` |

**A map that is not bound falls back to its scalar.** The shader knows which
maps are present from a bit field, so there is no cost to leaving one out and
no need for a white placeholder texture.

## Per-entity overrides

A `MeshComponent` can override the base colour, emissive, metallic, roughness,
occlusion and normal scale **without needing its own material asset**.

These are **free**. Those values already travel in the renderer's per-instance
stream — deliberately, so a thousand cubes differing only in colour stay one
draw call. Overriding them **cannot split a batch**.

Each has its own checkbox, so overriding roughness alone leaves everything else
coming from the material.

> [!NOTE]
> Texture maps and the UV transform are **not** overridable per entity. Those
> would split the batch, which is exactly what the split between material and
> override exists to avoid.

## What a glTF import produces

Importing a `.gltf` or `.glb` creates a material asset per material in the
file, with its maps extracted and its scalars carried across. Textures embedded
in a `.glb` are extracted too.

Because the model is metallic-roughness at both ends, the values transfer
directly — there is no second translation with its own failure modes, which is
the reason FBX is not supported.

## The default material

A mesh with no material uses the renderer's shared default: white, dielectric,
roughness 0.5. It is not an asset and cannot be edited — assign a material to
change anything.

## Where to go next

- [Component reference](components.md#meshcomponent) — the override fields
- [Lighting](lighting.md) — what the surface is responding to
- [Assets](assets.md) — importing, and where materials live on disk
