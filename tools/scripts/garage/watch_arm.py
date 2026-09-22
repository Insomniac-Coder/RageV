# -*- coding: utf-8 -*-
"""Play one arm live, in a window, with nothing captured.

Bisecting a moving artefact by still frames does not work -- a speckle is a
thing that changes between frames, and a GIF of it costs a judgement call per
round trip. This opens the scene, drives the mover slowly for the whole watch,
and lets whoever is looking say what they see.

    watch_arm.py car  10 --reflection-history=off
    watch_arm.py cube 10 --hit-light-sampling=off
    watch_arm.py car  10                      (as it ships)

Every argument after the seconds goes straight to the runtime, so any engine
switch can be an arm. The mover's speed is deliberately low (0.35 m/s): at the
1.0 the older harnesses use it crosses the frame and stops before the eye has
settled on it.
"""
import io
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'session_2026_09_13'))
sys.path.insert(0, os.path.join(HERE, 'session_2026_09_21'))
import stage_run as sr          # noqa: E402
import watch_mover as wm        # noqa: E402

SPEED = 0.35
# The garage's default pose -- the one the owner judges from. The close 'car'
# camera the older harnesses use crops out the floor either side, which is
# where this artefact lives. Override with --cam=x,y,z,dist,yaw,pitch.
DEFAULT_CAM = '-2.3,0.72,-2,11,0,4'

# Staged shader arms, for the parts of the chain that have no engine switch.
# `--stage=NAME` picks one; the staged copies are restored afterwards either way.
VARIANTS = {
    # The resolve with its neighbour gather off, so a texel keeps only its own
    # ray. The gather re-weights a neighbour's hit by this lobe's density over
    # the density that neighbour was drawn at, and a ratio like that is a
    # standard way to make speckle out of perfectly good samples.
    'centre': ('reflection_resolve.rvshader',
               '	const int taps = min(clamp(int(u_Scene.Indirect.w + 0.5), 1, 8) * 8, kMaxTaps);',
               '	const int taps = 0;'),
    # The clamp's emitter exemption removed, so a tap that found a lamp is
    # capped like any other. Added in the RT-11 session to stop the clamp
    # eating a lamp's reflection; it lifts the cap *entirely*, which leaves a
    # tube tap free to be as bright as it likes and the gather to spread it.
    # The ratio weight removed: every admitted tap counts the same, scaled only
    # by how far it sits from the centre. If the speckle goes here it is the
    # lobe-over-pdf estimator, not the content being borrowed.
    'flatweight': ('reflection_resolve.rvshader',
                   'const float w = min(pdfHere / max(pdfTheirs, 1.0e-4), kMaxWeight) * exp(-2.0 * r * r);',
                   'const float w = exp(-2.0 * r * r);'),
    # The cap without its second-brightest floor. That floor exists so a real
    # bright thing seen by two rays is not clamped -- but the gather's discs
    # overlap, so one bright ray reaches two taps and lifts the cap itself.
    'nosecond': ('reflection_resolve.rvshader',
                 'tapCap = max(mean + fireflySigmas * sd, secondHighest);',
                 'tapCap = mean + fireflySigmas * sd;'),
    # The gather's disc rotation frozen. It is advanced by the frame counter, so
    # every frame a texel samples a different ring of neighbours -- parked, the
    # accumulator averages that into extra coverage; under motion the history is
    # too short to average it and each frame answers differently.
    'stillrot': ('reflection_resolve.rvshader',
                 'const vec2 f = vec2(texel) + 5.588238 * float(int(u_Scene.GlobalIllumination.y) & 63);',
                 'const vec2 f = vec2(texel);'),
    # RT-17's identity test neutralised: what the ray struck no longer scales the
    # history's confidence. It is what cut the ghosting from 3.37% to 2.30%, and
    # the suspicion is that it is also what shortens the floor's history wherever
    # the car's reflection sweeps -- exposing the resolve's per-frame noise.
    'noidentity': ('reflection_accumulate.rvshader',
                   'const float kHitIdentityAgree = 0.2;',
                   'const float kHitIdentityAgree = 1.0;'),
    'noexempt': ('reflection_resolve.rvshader',
                 '			if (emissiveShare > kEmitterShare)',
                 '			if (false)'),
}


def scene_for(case, seconds):
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=sr.ROOT, capture_output=True, check=True).stdout
    if case == 'cube':
        return sr.moving_cube_scene(head, -9.0, SPEED, seconds)
    text = head.decode('utf-8').replace('\r\n', '\n')
    out, count = [], 0
    for part in re.split(r'(?=\n  - EntityID: )', text):
        tag = re.search(r'\n      Tag: (.*)', part)
        if tag and tag.group(1).strip() == 'porsche_992_gt3_r':
            body = part.rstrip('\n')
            part = body + '\n' + (wm.SLIDER % (SPEED, seconds)).rstrip('\n') + part[len(body):]
            count += 1
        out.append(part)
    if count != 1:
        sys.exit('the car matched %d entities' % count)
    return ''.join(out).encode('utf-8')


def play(case, seconds, flags):
    cam = DEFAULT_CAM
    stage = None
    rest = []
    for f in flags:
        if f.startswith('--cam='):
            cam = f.split('=', 1)[1]
        elif f.startswith('--stage='):
            stage = f.split('=', 1)[1]
        else:
            rest.append(f)
    flags = rest
    path = os.path.join(sr.SCENES, sr.HEAD_SCENE)
    # still_camera, or --camera is ignored and the run comes out at the orbit
    # script's pose instead of the one asked for.
    io.open(path, 'wb').write(wm.still_camera(scene_for(case, seconds)))
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        if stage:
            name, was, now = VARIANTS[stage]
            src = os.path.join(sr.ROOT, 'RageVEditor', 'assets', 'shaders', name)
            text = io.open(src, encoding='utf-8', newline='').read()
            crlf, lf = chr(13) + chr(10), chr(10)
            eol = crlf if text.count(crlf) > text.count(lf) // 2 else lf
            a, b = was.replace(lf, eol), now.replace(lf, eol)
            if text.count(a) != 1:
                sys.exit('%s: the anchor matched %d times' % (stage, text.count(a)))
            io.open(os.path.join(sr.STAGED_DIR, name), 'w', encoding='utf-8',
                    newline='').write(text.replace(a, b))
            print('staged arm: %s' % stage)
        print('%s, %g m/s for %gs -- %s' % (case, SPEED, seconds,
                                            ' '.join(flags) if flags else 'as it ships'))
        subprocess.run([os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime',
                                     'RageVRuntime.exe'),
                        '--project=' + sr.PROJECT, '--scene=scenes/' + sr.HEAD_SCENE,
                        '--rhi=vulkan', '--render-defaults=off', '--vsync=on',
                        '--width=1600', '--height=900', '--import-cache=off',
                        '--camera=' + cam,
                        '--benchmark=%d' % int(seconds * 60)] + list(flags),
                       cwd=os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime'),
                       timeout=3600)
    finally:
        sr.restore()
        for name in (sr.HEAD_SCENE, sr.HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(sr.SCENES, name))
            except OSError:
                pass


if __name__ == '__main__':
    case = sys.argv[1] if len(sys.argv) > 1 else 'car'
    secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    play(case, secs, sys.argv[3:])
