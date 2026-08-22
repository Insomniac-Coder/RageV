# porsche_992_gt3_r.glb

The car in `scenes/showroom.rage`. Downloaded from Sketchfab as
`2024-porsche-992-gt3-r.zip`.

Attribution is a condition of the licence, which is why this sits beside the
model rather than in a changelog somebody would have to go looking for. It is
also the reason this file is not named `.meta` — that extension belongs to the
asset registry, which rewrites what it owns.

## The notice shown in the scene

The showroom scene prints this along the bottom of the frame, as supplied by
the project's owner:

> "2024 Porsche 992 GT3 R" (https://skfb.ly/pMLAu) by Dave Love SketchFab is
> licensed under Creative Commons Attribution
> (http://creativecommons.org/licenses/by/4.0/).

## ⚠ The file's own metadata says something different

**This needs resolving before the project is distributed.** The `.glb`'s
`asset.extras` block, written by the Sketchfab exporter, records:

| | |
|---|---|
| Title | `porsche_992_gt3_r` |
| Author | MattDoesBlender — <https://sketchfab.com/MattDoesBlender> |
| Licence | **CC BY-NC-SA 4.0** — <http://creativecommons.org/licenses/by-nc-sa/4.0/> |
| Source | <https://sketchfab.com/3d-models/porsche-992-gt3-r-03ea07f7972648aa9350853b2a1a942a> |

That is a different author and a **materially different licence** from the
notice above: `BY-NC-SA` forbids commercial use and requires derivatives to
carry the same terms, where plain `BY` does neither. Either the archive is a
re-upload of somebody else's model, or the notice was taken from a different
Sketchfab page.

Read it out of the file rather than trusting this document:

```bash
python -c "import json,struct,pathlib;d=pathlib.Path('porsche_992_gt3_r.glb').read_bytes();o=12;n=struct.unpack_from('<II',d,o);print(json.loads(d[o+8:o+8+n[0]])['asset'])"
```

Until it is settled, treat the model as **non-commercial, share-alike** — the
stricter of the two — and do not ship it in anything sold.

## The livery is in the archive and not in the model

The Sketchfab page shows this car in a yellow E-TECK #91 Manthey wrap. **The
`.glb` does not contain that assignment.** Its `EXT_Carpaint_Inst` material —
the entire body — carries `baseColorFactor: [0.8, 0.8, 0.8]` and no texture at
all, so the body imports as flat grey and renders as a white silhouette. Forty
of the model's other eighty-nine materials are the same.

The wrap is in the archive beside the model, as a 4096-square `decals.png` that
nothing in the glTF references — the viewer on the website applies it through
Sketchfab's own material editor, and that assignment is what the glTF export
dropped. It is kept here as `porsche_992_gt3_r_livery.png` and reattached by
`tools/scripts/make_showroom_scene.py`, which also mirrors its V axis: the
image was never referenced by the glTF, so nothing ever made it agree with
glTF's top-left origin.

The same script gives the other untextured materials physical values — paint,
alloy, anodised caliper, carbon, alcantara. **The model supplies the geometry
and the livery; the showroom supplies the paint.**

## What is generated and what is downloaded

| File | Where it came from |
|---|---|
| `porsche_992_gt3_r.glb` | the archive, unchanged |
| `porsche_992_gt3_r_livery.png` | the archive's `textures/decals.png`, unchanged |
| `porsche_992_gt3_r_*.png` | extracted from the `.glb` by `rvimport` |
| `porsche_992_gt3_r_*.rmat` | written by `rvimport` from the glTF's materials |
| `showroom_car_*.rmat` (in `materials/`) | written by the scene generator |
