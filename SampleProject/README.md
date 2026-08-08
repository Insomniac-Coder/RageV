# Sample project

The project the editor and the tests open by default.

A **project is a folder** with a `.rvproject` file in it. That file says where
the assets are, which scene to start on, and how fast the simulation runs.
Everything under `assets/` is addressed by handle, and the `.meta` sidecar
beside each file **is** that handle.

## Why the assets live here and not next to the editor

They used to live in `RageVEditor/assets/`, which CMake copies beside the
executable on every build. A `.meta` minted in that copy is destroyed by the
next compile, so an asset added there got a fresh handle each time and every
scene referring to it broke — silently, because a handle that resolves to
nothing looks the same as an asset that was never assigned.

Assets in a project folder are not a build artifact, so their handles persist.
That is the whole point of the project concept, and it is why roadmap 4.1 comes
before packaging rather than after.

`RageVEditor/assets/` still exists and still holds `shaders/` and `Fonts/`.
Those are **engine** assets: the renderer needs them to start at all, they are
not addressed by handle, and no game would edit them.

## The `.meta` files belong in version control

They are the identity. Deleting one and letting it regenerate gives the asset a
new handle, and every scene that referred to the old one now refers to nothing.
