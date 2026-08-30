# mark85.fbx

The figure in `scenes/showroom-mark85.rage`. Downloaded from Sketchfab as
`iron-man-mark-85-rigged`.

## ⚠ The notice is missing, and it has to be filled in

**This model carries no author and no licence, anywhere.** Unlike the two
Sketchfab `.glb`s beside it, an FBX has no `asset.extras` block, and the fields
that could have held it — `Author`, `Comment`, `Keywords`, `Subject` — are all
present and all empty:

```bash
python -c "import re;d=open('mark85.fbx','rb').read(400000);print(re.sub(rb'[^\x20-\x7e]',b'.',d[d.find(b'Author'):d.find(b'Author')+120]).decode())"
```

So the credit bar in `showroom-mark85.rage` currently says the attribution is
pending and points at this file. **That is a placeholder, not an answer.** The
Sketchfab page the archive came from names the author and states the licence;
both need to be copied here and into `CREDIT` in
`tools/scripts/make_showroom_mark85_scene.py`, and the scene regenerated.

Until that happens, treat the model as the strictest thing it plausibly is —
**non-commercial, attribution required** — which is what the other two Iron Man
models on this repository are, and do not ship it in anything sold.

## What the model does and does not carry

**The geometry and the maps are excellent; the material definitions are not
there at all.** All six materials arrive as a flat Lambert colour with metallic
0 and roughness 0, which is what an FBX out of a Substance workflow contains:
the maps are baked beside it and assigned in the material editor, and none of
that reaches the file.

The maps *are* in the archive — eighteen PNGs at 4096 square, named after the
material they belong to — so `make_showroom_mark85_scene.py` reattaches them by
name. That is the scene making a claim about the model rather than the importer
guessing: an importer that went looking for unreferenced images in a folder and
bound them by hope would be wrong far more often than it was right.

| Material | Maps found beside it |
|---|---|
| `Silver Part` | base colour, metallic, roughness, normal |
| `Gold Part` | base colour, metallic, roughness, normal |
| `Red Part` | base colour, metallic, roughness, normal |
| `Arc Reactor` | base colour, metallic, roughness, normal |
| `Lights` | base colour only — it is an emitter, and the scene supplies the emission |
| `Glass` | none; the scene gives the visor a coated-glass surface |

Two numbers the scene argues with:

- **The metallic maps are 0.97** and the scene multiplies them by 0.55. A pure
  conductor has no diffuse term, so under a ceiling-sized source in a charcoal
  room the red and the gold collapse to a near-black mirror. The map still says
  *where* the metal is; the factor says how much of one it is.
- **Roughness 0 in the material** is the FBX default, not a value. The roughness
  maps run 0.13 to 0.35 — polished panels against worn edges — and are used
  whole.

## The rig, and why this file is in the repository at all

One armature, **52 bones**, and the mesh cut into six material parts with a
**skin deformer on each**. That shape is what `FbxImporter` used to get wrong:
it took `skin_deformers[0]` and warned about the rest, on the theory that a
second skin means a second character, so five of the six parts imported static.
It is also the file that exposed `AssetManager::InstantiateModel` flattening
every imported hierarchy — this rig's bones were out by up to 3.086 radians.

Both are fixed, and this is the model they are fixed against. A reproduction
that lives in somebody's Downloads folder is a reproduction nobody can re-run.

## What is downloaded and what is generated

| File | Where it came from |
|---|---|
| `mark85.fbx` | the archive's `source/Og.fbx`, unchanged but renamed |
| `For_Substance_Low_Polly_*.png` | the archive's `textures/`, unchanged |
| `mark85_*.rmat` | written by `rvimport` from the FBX's six materials |
| `showroom_mark85_*.rmat` (in `materials/`) | written by the scene generator |
