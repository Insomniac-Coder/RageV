# Terrain

A heightfield: a square grid of heights that the engine turns into ground.
One `.rvterrain` asset holds the heights; one `TerrainComponent` places it,
sizes it, gives it a material and a collider. Everything else the engine
already does -- shadows, ray tracing, culling, picking, physics -- takes the
terrain as the ordinary meshes it is built from.

## Making one

There is no sculpting tool yet. A terrain starts as a heightmap image or as
noise, through the script:

```bash
python tools/scripts/make_terrain.py --png heights.png assets/terrain/valley.rvterrain
```

The PNG must be square with 2^n + 1 pixels a side -- 33, 65, 129, 257, 513,
1025, 2049 or 4097 -- and 16-bit greyscale is what the format keeps; an 8-bit
image is accepted and scaled up, with the terracing 8 bits imply. Run the
script with no arguments to write the sample project's own terrains (hills
from noise, a ridge, a cliff) and the scenes that stand on them.

Then, in the editor: add a **Terrain** component to an entity, pick the
asset, and set the size and height. The terrain is centred on the entity.

## The asset

`.rvterrain` is a small binary file: a header, then one unsigned 16-bit
sample per grid point, row-major. Sample 0 is the base of the terrain and
65535 is `Height` metres above it. It is not a texture on purpose -- a height
needs all sixteen bits, and the texture cooker would compress a `_height`
map to eight the way it should for a parallax map -- and it is its own file
because sculpting, when it arrives, writes heights back.

## The component

| Field | Default | What it does |
|---|---|---|
| `Terrain` | invalid | The `.rvterrain`. Invalid draws nothing |
| `Size` | `256` | Metres a side, centred on the entity |
| `Height` | `40` | Metres the highest possible sample stands above the base |
| `Material` | invalid | One `.rmat`, tiled over the terrain. Invalid is the renderer's default |
| `TextureScale` | `4` | Metres per repeat of the material's textures; the material's own tiling multiplies on top |
| `Collision` | `true` | A static height-field collider under the surface, exact to the triangle |

The entity's transform places, turns and scales it like any mesh. A
`RigidBody` on the same entity is ignored: the terrain is its own collider,
static by nature, and needs no `Collider` component.

## What happens underneath

The grid is cut into **chunks of 64 quads**, each built at four levels of
detail (every 1st, 2nd, 4th and 8th sample) as an ordinary mesh. Each frame
every chunk picks a level from the camera's distance -- full detail within
four chunk widths, one level coarser per doubling after that -- and that
level is what is drawn, what casts shadows, what a ray hits and what a click
lands on. Where two neighbouring chunks meet at different levels a **skirt**
-- the chunk's edge dropped a little way down -- hides the sliver of
background that would otherwise show. A chunk pops when its level changes;
temporal anti-aliasing softens it.

Collision is Jolt's height-field shape over the same samples, split into
triangles along the same diagonal as the meshes, so a body rests exactly on
what is drawn. Ray casts hit it and report the terrain's entity.

## Limits

- One material for the whole terrain. Layers painted by a weight map are the
  next stage.
- No sculpting or painting in the editor. The next stage after that.
- No holes: a heightfield cannot have caves or overhangs.
- Everything is built at load and stays resident. A 1025 terrain is a few
  tens of megabytes of geometry; 2049 is around 180 MB and the practical
  ceiling until terrains stream.
- Seen from *off* the terrain's edge, the skirts on interior seams show as
  short vertical legs below the rim. From on the ground, where a game camera
  stands, they are under the surface.

## Where to go next

- [Materials](materials.md) -- the one material a terrain wears
- [Physics](physics.md) -- what rests on it
- [Component reference](components.md#terraincomponent) -- the fields again
