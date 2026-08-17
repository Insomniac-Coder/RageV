# Assets

Everything in a project's `assets/` folder that the engine can load, how it
gets there, and what happens to it on the way to a shipped game.

## Handles, and the `.meta` beside each file

**Every asset has a UUID, and nothing refers to an asset by path.** A scene
stores handles; a material stores handles to its textures.

The UUID lives in a `.meta` sidecar next to the file — `brick.png` has
`brick.png.meta`. That is what makes renaming and moving an asset safe: the
handle travels with the `.meta`, and every reference keeps resolving.

> [!TRAP]
> **Keep the `.meta` with its file.** Delete one and the asset is minted a new
> UUID on next import, which means every reference to it is now dangling. If
> you move assets with a file manager, move both.

> [!TRAP]
> **Assets dropped into the build output lose their handles on a clean build.**
> The assets root is the folder beside the executable, which CMake copies from
> the source tree. Put new assets in the *source* tree.

## The asset types

| Type | Extension | Where it comes from |
|---|---|---|
| Mesh | `.gltf`, `.glb`, or a built-in primitive | glTF import |
| Texture | `.png`, `.jpg`, `.hdr` | Dropped in |
| Material | `.rvmaterial` | Created in the editor, or by a glTF import |
| Scene | `.rage` | Created in the editor |
| Prefab | `.rvprefab` | Created from an entity |
| Audio | `.wav`, `.ogg`, `.mp3` | Dropped in |
| Curve | `.rcurve` | Created in the editor |
| Font | `.rvfont` | Baked from a `.ttf` by `rvfont` |
| Post profile | `.rvpostprofile` | Created in the editor |
| LUT | `.cube` or `.rvlut` | Exported from a grading tool, or authored here |
| Terrain | `.rvterrain` | Written by `tools/scripts/make_terrain.py` from a 16-bit heightmap or from noise |

The built-in mesh primitives — `Cube`, `Sphere`, `Plane`, `Cylinder` and
`Quad` — are not files. They have reserved handles and are always available.

> [!NOTE]
> A `.ttf` is **not** an asset. Nothing at runtime can read one, and treating
> it as an asset would offer a handle that never resolves. The `.rvfont` baked
> from it is the asset.

## Importing

Drop a file into `assets/` and the editor imports it on the next scan. glTF is
the model format:

- **glTF covers Blender, Maya, Substance** and every online library, and it is
  metallic-roughness at both ends — so materials transfer without a second
  translation.
- **FBX and Collada are not supported.** That is a decision, not a gap: each is
  a second material translation with its own failure modes.
- Textures embedded in a `.glb` are extracted on import.

## Cooking, and the import cache

Source assets are not what the engine ultimately reads. A PNG has to be decoded
and block-compressed; a glTF mesh has to be turned into vertex buffers. That is
**cooking**, and it produces `.rvtex` and `.rvmesh`.

Cooking happens **on import, into `<project>/Cache`**, keyed on a hash of the
source file. The hash is in the filename, so a lookup is a stat rather than a
parse, and a stale entry is visibly stale.

Before this existed, the editor re-decoded every texture on every launch: 3.2
seconds of PNG decoding on the first *draw*, which looked like slow project
loading and was not. Warm launch went from 4.79 s to 2.31 s, and a first open
from 17.2 s to 4.9 s.

`--import-cache=off` disables it, which is only useful when you suspect the
cache itself.

> [!TRAP]
> **`Cache/` is git-ignored, and should stay that way.** The hazard is not its
> size — it is that a cooked file arriving over a checkout carries another
> machine's hash and claims to be current for a source it never saw.

The cache lives *outside* `assets/`, because the registry would otherwise index
it as content.

## Packaging

**File → Package** produces a folder that runs on a machine with no editor and
no source assets:

- the runtime executable,
- `content.pak` — every asset the project needs, cooked, in one file,
- the project's script assemblies.

The packager cooks as it goes, so what ships is the cooked form. At runtime a
**virtual file system** mounts the pak, and every content reader goes through
it — so the same code path loads from a pak in a shipped game and from loose
files in the editor.

> [!NOTE]
> A font atlas is deliberately **not** cooked. Block-compressing and
> mip-chaining a distance field are exactly the two things that make text soft,
> and the packager knows to skip them.

## Where things live

```
MyProject/
  MyProject.rvproject     the project file: start scene, render settings
  assets/                 everything the engine loads
    scenes/  models/  textures/  materials/  audio/  fonts/  post/  curves/
  Source/                 C++ game module
  Scripts/                C# scripts
  Cache/                  cooked assets, git-ignored
  bin/                    built game module and assemblies
```

## Where to go next

- [Materials](materials.md) — what an imported material carries
- [Getting started](getting-started.md) — creating a project and packaging one
- [Core concepts](concepts.md#asset) — handles and the registry
