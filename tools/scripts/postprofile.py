#!/usr/bin/env python3
"""Write a `.rvpostprofile` and its sidecar, for the scenes the checks generate.

Post settings left the scene file in version 6: exposure and bloom belong to a
profile asset that a camera points at, and a `.rage` that still writes
`BloomEnabled: false` is a `.rage` writing a key nothing reads.
ENGINE-NOTES 7s.

**That matters here more than anywhere.** Two generated scenes turn bloom off
on purpose -- it spreads a bright edge across exactly the pixels the
anti-aliasing and temporal measurements are looking at -- and after the move
that line became inert. The check would still run, still print numbers, and
the numbers would be measuring a different picture.

So a generator that needs an effect off writes a real profile beside the
scene and points the camera at it: the same path a person uses, exercised by
the test suite on every run rather than by a person once.

**The sidecar is written too, and that is the load-bearing part.** A handle is
minted by the registry on first scan, which is *after* the scene naming it has
been written -- so a generator cannot ask for one. Writing the `.meta` fixes
the handle in advance; the registry reads it rather than minting, exactly as
it does for a `.meta` in version control.
"""

import pathlib


def profile_handle(name):
    """A stable 64-bit handle for a profile with this name.

    Derived from the name rather than drawn at random, for two reasons: two
    runs of the generator produce byte-identical files, which is what lets a
    diff mean something; and regenerating a scene keeps the handle the copy
    already on disk refers to.

    The name, not the absolute path -- a path would differ between machines
    and make the generated files unshareable.

    FNV-1a, because it is four lines and this needs to be reproducible from
    any language that might later write one of these, not fast.
    """
    h = 0xCBF29CE484222325
    for byte in name.encode("utf-8"):
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF

    # Zero is the invalid handle. The odds are astronomical rather than nil,
    # so it is excluded rather than assumed away.
    return h or 0x7261676556504F53


def source_hash(path):
    """The registry's SourceHash for a file: FNV-1a over its bytes on disk.

    Written into the `.meta` so a generated fixture is *already* what the
    registry would rewrite it to on its next scan. Before this the generators
    wrote 0 and the first run of the engine rewrote every generated `.meta`,
    which left them modified after every check -- the churn the handoff kept
    telling people to `git checkout` before committing.
    """
    h = 0xCBF29CE484222325
    for byte in pathlib.Path(path).read_bytes():
        h ^= byte
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def write_named(path, settings):
    """Write a profile at an exact path, and its `.meta`. Returns the handle.

    For a check that needs several profiles for one scene -- grading wants an
    ungraded one, an identity one and a swapped one, all against the same
    picture, which is the only way the comparison means anything.
    """
    path = pathlib.Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)

    handle = profile_handle(path.name)

    lines = ["PostProfile: 1"]
    for key, value in settings.items():
        if isinstance(value, bool):
            lines.append(f"{key}: {'true' if value else 'false'}")
        elif isinstance(value, int) and not isinstance(value, bool):
            lines.append(f"{key}: {value}")
        else:
            lines.append(f"{key}: {value:g}")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    meta = path.with_name(path.name + ".meta")
    meta.write_text(
        f"Handle: {handle}\nType: PostProfile\nSourceHash: {source_hash(path)}", encoding="utf-8")

    return handle


def write_beside(scene_path, settings):
    """Write `<scene>.rvpostprofile` and its `.meta`. Returns the handle.

    Beside the scene and sharing its stem, so the pair is obviously one thing:
    deleting a generated scene and forgetting its profile leaves an orphan
    that is at least named after what it belonged to.

    `settings` is a dict of registry key to value, and only the keys given are
    written -- a key a profile omits reads back as the struct's default, which
    is exactly what "leave that one alone" should mean.
    """
    path = pathlib.Path(scene_path).with_suffix(".rvpostprofile")
    path.parent.mkdir(parents=True, exist_ok=True)

    handle = profile_handle(path.name)

    lines = ["PostProfile: 1"]
    for key, value in settings.items():
        if isinstance(value, bool):
            lines.append(f"{key}: {'true' if value else 'false'}")
        elif isinstance(value, str):
            # An enum field is written by name, the way the registry reads it.
            lines.append(f"{key}: {value}")
        else:
            lines.append(f"{key}: {value:g}")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    # The registry's own hash of the bytes just written, so the sidecar is
    # already what a scan would make it and a check run leaves nothing dirty.
    meta = path.with_name(path.name + ".meta")
    meta.write_text(
        f"Handle: {handle}\nType: PostProfile\nSourceHash: {source_hash(path)}", encoding="utf-8")

    return handle
