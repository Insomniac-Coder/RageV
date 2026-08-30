# iron_man.glb

The figure in `scenes/showroom2.rage`. Downloaded from Sketchfab.

Attribution is a condition of the licence, which is why this sits beside the
model rather than in a changelog somebody would have to go looking for. It is
also the reason this file is not named `.meta` — that extension belongs to the
asset registry, which rewrites what it owns.

## The notice shown in the scene

The showroom2 scene prints this along the bottom of the frame, as supplied by
the project's owner:

> "Iron Man" (https://skfb.ly/6TKyK) by Grant Riley is licensed under Creative
> Commons Attribution-NonCommercial
> (http://creativecommons.org/licenses/by-nc/4.0/).

## ⚠ Non-commercial, and this one is not in dispute

The `.glb`'s own `asset.extras` block agrees with the notice above, which is
more than can be said for the car beside it:

| | |
|---|---|
| Title | `Iron Man` |
| Author | Grant Riley — <https://sketchfab.com/grandriley> |
| Licence | **CC BY-NC 4.0** — <http://creativecommons.org/licenses/by-nc/4.0/> |
| Source | <https://sketchfab.com/3d-models/iron-man-69dde1ad49e94852984e3d83928efd65> |

**NC means what it says.** This scene is a demonstration of the engine and that
is a non-commercial use; a build of it sold, or shipped as part of anything
sold, is not. Read it out of the file rather than trusting this document:

```bash
python -c "import json,struct,pathlib;d=pathlib.Path('iron_man.glb').read_bytes();o=12;n=struct.unpack_from('<II',d,o);print(json.loads(d[o+8:o+8+n[0]])['asset'])"
```

## What the model already gets right

Unusually, almost everything. The car in the scene next door needed forty-five
of its materials invented for it, because the upload carried no maps at all.
This one arrives with a base colour, a metallic-roughness pair, a normal map
and an emissive map, all 4096 square and all authored:

- **The metallic channel is 255 over 99.8% of the surface.** The armour *is*
  metal and the file says so. The scene argues that number down to 0.55 anyway
  — see `tools/scripts/make_showroom2_scene.py` — because a pure conductor has
  no diffuse term, and in a room lit from directly overhead that left the red
  and the gold as a near-black mirror.
- **The emissive map is keyed exactly where it needs to be.** The base colour
  `[188, 188, 188]` appears on 28,012 texels and on no others, and the emissive
  map lights precisely those: the eye slits, the palms, the chest reactor, the
  boot jets and two helmet temples. Nothing here had to go looking for them.

## ⚠ Two numbers the exporter wrote wrongly

**`normalTexture.scale` is 0.32152478940888196 and
`KHR_materials_emissive_strength` is 3.2152478940888196.** One of those is a
real authored value and the other is it divided by ten, written into a field
that has nothing to do with it. At 0.32 the shader flattens every panel line,
rivet and vent to a third of its depth, so the scene puts the normal scale back
to 1.

The emissive strength is not read by this engine's importer at all, so the
scene sets the emissive level itself.

## What is downloaded and what is generated

| File | Where it came from |
|---|---|
| `iron_man.glb` | the Sketchfab download, unchanged |
| `iron_man_*.png` | extracted from the `.glb` by `rvimport` |
| `iron_man_1_metallic.png`, `iron_man_1_roughness.png` | the packed map, split by the importer |
| `iron_man_0_default.rmat` | written by `rvimport` from the glTF's one material |
| `showroom2_suit.rmat` (in `materials/`) | written by the scene generator |
