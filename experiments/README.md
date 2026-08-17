# experiments

Code kept for reference that is **not built** and not part of the engine.

Nothing in here is on the include path or in any CMake target. To revive
something, move it back into a target's source tree — the engine globs
`RageV/src/**`, so that is all it takes.

## terrain/

An attempt at Minecraft-style world generation: `Perlin.h` for the noise and
`Chunk.h`/`Chunk.cpp` for the chunk data.

It worked, but it generated one **entity per visible cube face**, which is
thousands of entities for a single chunk — every one carrying a transform, a
tag, a relationship and a colour component, and every one drawn as its own
quad. A real implementation needs a greedy mesher producing one mesh per
chunk, which is a different piece of code rather than an improvement to this
one.

Terrain was built for real as roadmap item 8.4 (ENGINE-NOTES 7ap): a
heightfield asset (`.rvterrain`), a `TerrainComponent`, and chunk meshes with
levels of detail that every existing system -- culling, shadows, ray tracing,
picking, Jolt's height-field collider -- takes as ordinary meshes. This
experiment is kept, at the owner's direction, as the shape to avoid: one
entity per face. It was moved out of `RageV/src/RageV/Renderer/` because it
is not a renderer concern and it was compiling into the engine library --
including its warnings -- and the header-only `PerlinNoise` it used is no
longer linked into the engine either.
