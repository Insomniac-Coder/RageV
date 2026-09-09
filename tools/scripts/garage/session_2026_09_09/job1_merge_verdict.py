# -*- coding: utf-8 -*-
"""Job 3, judged the way the project's rule says: by diff image.

The sea's frame averaging on the shared contract, against the sea's own, at the
camera the scene is composed for. Three pictures out:

  job1m_off.png    the shipped look
  job1m_on.png     the contract's
  job1m_diff.png   where they differ, amplified -- red where the contract is
                  brighter, blue where it is darker, so the *shape* of the
                  change is visible and not just its size.

A scalar cannot tell "less noise" from "the white lights smeared into the red";
that is what cost the day, and this is the instrument that would have caught it.
"""
import os, subprocess, sys
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
CAMS = {
    'headland': '500,89.47,-1100,0.01,-157.08,8.88',
    'pier': '70,4.5,705,0.01,-46.98,-2.86',
}


def shot(tag, cam, extra=()):
    out = os.path.join(SHOTS, tag + '.png')
    if os.path.exists(out):
        os.remove(out)
    subprocess.run([os.path.join(RT, 'RageVRuntime.exe'),
                    '--project=' + os.path.join(ROOT, 'SampleProject'),
                    '--scene=scenes/GoldenGateDemo.rage', '--rhi=vulkan',
                    '--render-defaults=off', '--vsync=off',
                    '--width=2560', '--height=1600',
                    '--screenshot=' + out, '--screenshot-frame=60',
                    '--screenshot-count=1', '--frame-time=0.0166',
                    '--camera=' + cam] + list(extra),
                   cwd=RT, capture_output=True, text=True, timeout=1800,
                   errors='replace')
    return out if os.path.exists(out) else None


def arr(p):
    return np.asarray(Image.open(p).convert('RGB'), dtype=float)


for name, cam in CAMS.items():
    off = shot('job1m_%s_off' % name, cam, ['--water-direct=off'])
    on = shot('job1m_%s_on' % name, cam, ['--water-direct=on'])
    if not (off and on):
        sys.exit('no frame for ' + name)
    a, b = arr(off), arr(on)
    lum = lambda x: x @ np.array([0.2126, 0.7152, 0.0722])
    delta = lum(b) - lum(a)
    # Amplified eight times and split by sign: brighter red, darker blue, so a
    # smear reads as a shape rather than as a magnitude.
    amp = np.clip(np.abs(delta) * 8.0, 0, 255)
    diff = np.zeros(a.shape, dtype=np.uint8)
    diff[..., 0] = np.where(delta > 0, amp, 0).astype(np.uint8)
    diff[..., 2] = np.where(delta < 0, amp, 0).astype(np.uint8)
    diff[..., 1] = (amp * 0.25).astype(np.uint8)
    Image.fromarray(diff).save(os.path.join(SHOTS, 'job1m_%s_diff.png' % name))

    d = np.abs(b - a).max(axis=2)
    changed = d > 0
    print('%-9s %6.2f%% of pixels, max %3.0f levels; the contract is brighter on '
          '%5.2f%% and darker on %5.2f%%'
          % (name, 100 * changed.mean(), d.max(),
             100 * (delta > 0.5).mean(), 100 * (delta < -0.5).mean()))
