# -*- coding: utf-8 -*-
"""What a check has to know before it believes a number.

Three verification holes were found the hard way building 8.13 (ENGINE-NOTES
7be), and every one of them let a completely broken renderer report success.
Two of the three are closed from here, because they are properties of *the
measurement*, not of the engine:

  * **The runtime does not load the shaders in the source tree.** It loads
    `assets/` beside its own exe, which the build copies there. Editing
    `RageVEditor/assets/shaders/` and re-running measures the *last build's*
    shader. Three timing variants were measured that way and all three
    silently read the baseline back.
  * **A uniformly black frame is not a measurement.** `check_gi` read +0.00
    off 27 blank renders, reported them as results, and printed OK for a
    feature doing nothing on a renderer drawing nothing.

The third -- a shader that fails to compile at runtime while `cmake --build`
stays green -- is closed in the engine, because only the engine knows: the
compiler counts its failures and a run that was asked for a screenshot or a
benchmark exits non-zero rather than reporting whatever the frame contains.

Import this from a check script:

    import rvcheck
    rvcheck.require_current_shaders(root, config)      # once, before measuring
    image = rvcheck.require_drawn(image, "vulkan gi_corner")
"""

import pathlib
import sys

# The marker `falsify.py` leaves in the deployed tree while a break is applied.
# Without it, a check run after a forgotten `restore` measures a deliberately
# broken shader and says nothing -- which is the same hole one level along.
FALSIFY_MARKER = '.falsify'


def deployed_shaders(root, config='Release'):
    """The shaders the runtime actually opens, beside its exe."""
    return (pathlib.Path(root) / 'build' / 'bin' / config
            / 'RageVRuntime' / 'assets' / 'shaders')


def source_shaders(root):
    return pathlib.Path(root) / 'RageVEditor' / 'assets' / 'shaders'


def require_current_shaders(root, config='Release', quiet=False):
    """Refuse to measure a runtime whose shaders are not the ones in git.

    A deliberate break is not staleness, so an active `falsify` marker is
    allowed through -- but announced, loudly, because a run that measures a
    broken shader must never be mistaken for a clean one.
    """
    live = deployed_shaders(root, config)
    src = source_shaders(root)

    if not live.is_dir():
        print('FAIL: no deployed shaders at {0} -- build {1} first'.format(
            live, config))
        sys.exit(1)

    marker = live / FALSIFY_MARKER
    if marker.is_file():
        name = marker.read_text(encoding='utf-8').strip() or 'unknown'
        print('=' * 72)
        print('  A FALSIFY BREAK IS ACTIVE: {0}'.format(name))
        print('  These shaders are deliberately wrong. Any number below is a')
        print('  broken-renderer number. `falsify.py restore` puts them back.')
        print('=' * 72)
        return

    stale, missing = [], []
    for path in src.rglob('*'):
        if not path.is_file():
            continue
        relative = path.relative_to(src)
        other = live / relative
        if not other.is_file():
            missing.append(relative)
        elif other.read_bytes() != path.read_bytes():
            stale.append(relative)

    if not stale and not missing:
        if not quiet:
            print('shaders: deployed {0} tree matches the source tree'.format(config))
        return

    print('FAIL: the runtime would load shaders that are not the ones in the '
          'source tree.')
    print('      It loads assets/ beside its exe, which the build copies '
          'there, so an')
    print('      edit to RageVEditor/assets/shaders/ is invisible until you '
          'rebuild.')
    for relative in sorted(stale)[:12]:
        print('        differs: {0}'.format(relative))
    for relative in sorted(missing)[:12]:
        print('        missing: {0}'.format(relative))
    if len(stale) + len(missing) > 24:
        print('        ... and {0} more'.format(len(stale) + len(missing) - 24))
    print('      Rebuild {0}, or run `python tools/scripts/falsify.py restore`.'
          .format(config))
    sys.exit(1)


# A frame with nothing in it. Every real check_gi frame measured at the time
# this was written had a mean of 113 or more and better than half its pixels
# lit; the black ones had a mean of exactly 0 and not one lit pixel. The bar
# below sits two orders of magnitude under the darkest real frame on purpose:
# it asks "did the renderer draw anything at all", not "is this bright enough",
# so a legitimately dim fixture is never refused by it.
MIN_LIT_LEVEL = 8
MIN_LIT_FRACTION = 0.01


def require_drawn(image, label):
    """Return `image`, or exit if nothing was drawn into it.

    A check that cannot tell a black frame from a measured zero is not a
    check: it reads +0.00, calls it a result, and a claim floored at the null
    result then prints OK. The usual cause is a shader that did not compile,
    which the build will have reported as success.
    """
    import numpy as np

    array = np.asarray(image)
    lit = float((array.max(axis=2) > MIN_LIT_LEVEL).mean()) if array.ndim == 3 \
        else float((array > MIN_LIT_LEVEL).mean())
    if lit >= MIN_LIT_FRACTION:
        return image

    print('FAIL: {0} rendered an empty frame -- {1:.2f}% of pixels above level '
          '{2}, and a check'.format(label, lit * 100.0, MIN_LIT_LEVEL))
    print('      that measures one is measuring nothing. The usual cause is a '
          'shader that')
    print('      did not compile: shaders compile at runtime, so the build '
          'stayed green.')
    print('      Look at the image, and read the runtime log for '
          '"Shader ... failed".')
    sys.exit(1)
