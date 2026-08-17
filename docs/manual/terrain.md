# Terrain

A heightfield: a square grid of heights that the engine turns into ground.
One `.rvterrain` asset holds the heights and, once painted, which of up to
four materials each point of the ground is made of; one `TerrainComponent`
places it, sizes it, names the four materials and gives it a collider.
Everything else the engine already does -- shadows, ray tracing, culling,
picking, physics -- takes the terrain as the ordinary meshes it is built
from.

## Making one

A terrain starts as a heightmap image or as noise, through the script, and is
then sculpted and painted with the brush in the editor (below):

```bash
python tools/scripts/make_terrain.py --png heights.png assets/terrain/valley.rvterrain
```

The PNG must be square with 2^n + 1 pixels a side -- 33, 65, 129, 257, 513,
1025, 2049 or 4097 -- and 16-bit greyscale is what the format keeps; an 8-bit
image is accepted and scaled up, with the terracing 8 bits imply. Run the
script with no arguments to write the sample project's own terrains (hills
from noise, painted by slope and height; a ridge; a cliff; a flat painted
test card) and the scenes that stand on them.

Then, in the editor: add a **Terrain** component to an entity, pick the
asset, and set the size and height. The terrain is centred on the entity.
Give it a material as **Layer 0**, and up to three more layers where the
asset's paint says they go.

## The asset

`.rvterrain` is a small binary file: a header, then one unsigned 16-bit
sample per grid point, row-major. Sample 0 is the base of the terrain and
65535 is `Height` metres above it. It is not a texture on purpose -- a height
needs all sixteen bits, and the texture cooker would compress a `_height`
map to eight the way it should for a parallax map -- and it is its own file
because sculpting, when it arrives, writes heights back.

A painted terrain carries its **paint** in the same file: after the heights,
one RGBA byte per grid point, one channel per layer, on the same grid. The
header's layer count is 0 (heights only -- a terrain with one material) or 4
(the paint follows). The four channels need not sum to 255: the engine
normalises them, so a lightly painted map is not a dark one, and where every
channel is zero the ground is layer 0. An unpainted terrain and a stage-one
file look exactly the same for that reason. `make_terrain.py` writes paint
for the sample project's hills (from slope and height -- a mossy soil on the
flats, a sandy one in the low ground) and for the `layers` test card; the
brush that paints it by hand is the next stage.

## The component

| Field | Default | What it does |
|---|---|---|
| `Terrain` | invalid | The `.rvterrain`. Invalid draws nothing |
| `Size` | `256` | Metres a side, centred on the entity |
| `Height` | `40` | Metres the highest possible sample stands above the base |
| `Material` | invalid | **Layer 0**: the material everywhere nothing else is painted, and the whole terrain when there is no paint. Invalid is the renderer's default |
| `Layer1` | invalid | The material the paint's second channel blends in. Invalid means the layer is not there and its channel is ignored |
| `Layer2` | invalid | The third channel's, likewise |
| `Layer3` | invalid | The fourth channel's, likewise |
| `TextureScale` | `4` | Metres per repeat of the layers' textures; each layer's own tiling multiplies on top |
| `Collision` | `true` | A static height-field collider under the surface, exact to the triangle |

The entity's transform places, turns and scales it like any mesh. A
`RigidBody` on the same entity is ignored: the terrain is its own collider,
static by nature, and needs no `Collider` component.

The field is called `Material` in the scene file and "Layer 0" in the
inspector: it is the one material a terrain had before layers existed, and a
scene saved then reads and looks unchanged.

## Layers

Each layer is an ordinary `.rmat`. Its base colour, normal and roughness
maps are read, and its scalars -- colour, metallic, roughness, occlusion,
specular, emissive, tiling -- are honoured; its occlusion, metallic,
specular, emissive and height *maps* are not, on either backend, because the
four layers together would ask for more texture units than OpenGL gives a
fragment shader, and the two backends are kept to the same picture. Where a
layer's paint runs out its weight is zero and it costs nothing; where two
overlap the surface is a blend of both, normals included. Parallax is not
applied on layers, and a steep face stretches its texture as it would on any
mesh.

## Sculpting and painting

Select the terrain, and under its component's fields is **Brush**: four
modes, and the brush's size, strength and hardness.

| Mode | What a drag does | Shift |
|---|---|---|
| **Raise** | Lifts the ground under the brush. At strength 1 the centre climbs a quarter of `Height` per second | Lowers it |
| **Smooth** | Blends each sample toward the mean of its neighbours | -- |
| **Flatten** | Pulls the ground toward the height where the drag began | -- |
| **Paint** | Paints the chosen layer -- one of the four -- into the asset's weights | Erases it |

- **Size** is the radius in metres; `[` and `]` change it in the viewport.
- **Strength** is a rate: a full-strength raise climbs a quarter of the height
  each second, and smooth, flatten and paint close an eighth of their gap per
  sixtieth of a second, whatever the frame rate.
- **Hardness** is how much of the radius is at full weight before the fall-off:
  0 a soft cone, 1 a hard disc. A ring on the ground shows the rim and the core.

While a mode is chosen, a plain left drag on the terrain sculpts and a click
does not select; Alt+drag still orbits and the right button still flies.
Escape, or the lit mode button, puts the brush down. **One drag is one undo
step.** The tool is inert while the scene is playing.

Strokes edit the terrain asset, not the scene, and are **written when the
scene is saved** (Ctrl+S) -- the unsaved mark counts them. The height-field
collider is built from the data at the next Play, so what was sculpted is
what a body rests on.

`--brush=mode,x,z,radius,strength,seconds[,layer]` applies one stroke on the
`--select`ed terrain when the editor opens and saves it, which is how the
checks hold the brush.

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

- Four layers at most, each reading three maps (base colour, normal,
  roughness); the paint is at the heights' resolution.
- One brush shape -- a disc with a hardness. Brush textures, noise, erosion,
  ramp and clone tools are the next stage.
- A held smooth or flatten stops within four height units of its target
  (a fraction of a millimetre on a ten-metre terrain); a held paint reaches
  full and empty.
- A terrain seen in a ray-traced reflection shows layer 0 only.
- No holes: a heightfield cannot have caves or overhangs.
- Everything is built at load and stays resident. A 1025 terrain is a few
  tens of megabytes of geometry; 2049 is around 180 MB and the practical
  ceiling until terrains stream.
- Seen from *off* the terrain's edge, the skirts on interior seams show as
  short vertical legs below the rim. From on the ground, where a game camera
  stands, they are under the surface.

## Where to go next

- [Materials](materials.md) -- what a layer is
- [Physics](physics.md) -- what rests on it
- [Component reference](components.md#terraincomponent) -- the fields again
