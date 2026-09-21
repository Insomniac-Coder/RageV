# -*- coding: utf-8 -*-
"""Staged-shader arms for RT-5, one harness instead of one script per question.

A variant is a list of (shader, old, new) substitutions made to the runtime's
staged copies; an arm is a variant, the flags it runs with, and the burst it
takes. Every substitution must match exactly once or nothing runs, the staged
copies are restored from source after every run whatever happens, and the
restore is compared byte for byte -- the three things that went wrong on
2026-09-09 (a patch that did not apply, a measurement of an unchanged build,
a staged copy left behind).

Library use:
    from stage_run import run_arms, VARIANTS
    run_arms([('tag', 'variant', ['--flag'], dict(frames=100, first=150))])

Each arm writes build/garage_burst/rt5b_<tag>_<n>.png and, when `mean_from` is
given, build/rt5/<tag>.npy -- the float mean of the frames from there on.

The scene is HEAD's showroom written beside the working copy, because the
working copy was re-saved by the editor on 2026-09-11 and nothing measured here
should depend on an uncommitted file.
"""
import io, os, re, shutil, subprocess, sys, time
import numpy as np
from PIL import Image

ROOT = r'C:\Users\ism19\Code\RageV'
RT = os.path.join(ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
SRC_DIR = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders')
STAGED_DIR = os.path.join(RT, 'assets', 'shaders')
SCENES = os.path.join(ROOT, 'SampleProject', 'assets', 'scenes')
HEAD_SCENE = 'showroom_head.rage'
SHOTS = os.path.join(ROOT, 'build', 'garage_burst')
OUT = os.path.join(ROOT, 'build', 'rt5')
BURST = os.path.join(ROOT, 'tools', 'scripts', 'garage', 'burst.py')
CRLF, LF = chr(13) + chr(10), chr(10)

ACC = 'reflection_accumulate.rvshader'
TAA = 'taa_resolve.rvshader'
DIRECT = 'direct_trace.rvshader'
PROJECT = os.path.join(ROOT, 'SampleProject', 'SampleProject.rvproject')
# The one project edit an arm may ask for: the temporal jitter's scale, which
# is a render setting with no flag. Restored byte for byte after every arm.
NO_JITTER = ('    TemporalJitterScale: 1\n', '    TemporalJitterScale: 0\n')

ACC_CLAMP1 = ('const vec3 held = clamp(c.past.rgb, mean - halfWidth, mean + halfWidth);',
              'const vec3 held = c.past.rgb;')
ACC_CLAMP2 = ('const vec3 held2 = clamp(past2.rgb, mean2 - halfWidth2, mean2 + halfWidth2);',
              'const vec3 held2 = past2.rgb;')
TAA_CLIP = ('\thistory = ClipToBox(history, boxCentre, boxExtent);', '')
TAA_LINEAR = ('\tvec3 blended = Expand(mix(Compress(centreYCoCg), Compress(history),\n'
              '\t\t\t\t\t\t\t  1.0 - alpha));',
              '\tvec3 blended = mix(centreYCoCg, history, 1.0 - alpha);')

ROUNDING_GLSL = '''// A uniform draw in [0, 1) per texel, per frame and per stream.
float RoundingDraw(uvec2 at, uint stream)
{
	uint x = (at.x * 1973u) ^ (at.y * 9277u) ^ (uint(u_Scene.GlobalIllumination.y) * 26699u)
		   ^ (stream * 0x9E3779B9u);
	x = x * 747796405u + 2891336453u;
	x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	x = (x >> 22u) ^ x;
	return float(x >> 8u) / 16777216.0;
}

// Rounded onto the half-float grid here, up with the chance of how far past
// the step below the value sits, so the stored value is exactly representable
// and the mean of many frames is the mean of the values.
vec3 StoreAsHalf(vec3 v, float u)
{
	vec3 result;
	for (int i = 0; i < 3; ++i)
	{
		const float a = abs(v[i]);
		int e;
		frexp(a, e);
		const float spacing = ldexp(1.0, max(e - 11, -24));
		const float below = floor(a / spacing) * spacing;
		result[i] = sign(v[i]) * (below + ((a - below) / spacing > u ? spacing : 0.0));
	}
	return result;
}
'''

TAA_ROUNDING_GLSL = ROUNDING_GLSL.replace(
    'float RoundingDraw(uvec2 at, uint stream)\n{\n'
    '\tuint x = (at.x * 1973u) ^ (at.y * 9277u) ^ (uint(u_Scene.GlobalIllumination.y) * 26699u)\n'
    '\t\t   ^ (stream * 0x9E3779B9u);',
    'float TaaDraw(uint stream)\n{\n'
    '\tconst uvec2 at = uvec2(gl_FragCoord.xy);\n'
    '\tuint x = (at.x * 1973u) ^ (at.y * 9277u) ^ uint(fract(u_Params.Jitter.x * 97.0 + u_Params.Jitter.y * 13.0) * 16777216.0)\n'
    '\t\t   ^ (stream * 0x9E3779B9u);')
assert 'TaaDraw' in TAA_ROUNDING_GLSL, 'the resolve draw did not substitute'

VARIANTS = {
    'ship': [],
    'accfree': [(ACC,) + ACC_CLAMP1, (ACC,) + ACC_CLAMP2],
    'taafree': [(TAA,) + TAA_CLIP, (TAA,) + TAA_LINEAR],
    'allfree': [(ACC,) + ACC_CLAMP1, (ACC,) + ACC_CLAMP2, (TAA,) + TAA_CLIP, (TAA,) + TAA_LINEAR],
    # The reservoirs' draws walk with the frame whatever the jitter says.
    # **RT-5 part 3 as the row describes it**, staged for the owner to look at:
    # a history that has settled over 8..32 frames is held to a box four times
    # as wide, both payloads, keyed on the pixel's own history length (the
    # highlight half's memory never reaches 32, the pixel's does).
    'part3': [(ACC, 'const vec3 halfWidth = max(sd * width, vec3(kTemporalSigma * sigma));',
               'const float settled = smoothstep(8.0, 32.0, c.past.a);\n'
               '\t\t\tconst vec3 halfWidth = max(sd * width, vec3(kTemporalSigma * sigma)) * (1.0 + 3.0 * settled);'),
              (ACC, 'const vec3 halfWidth2 = max(sd2 * width, vec3(1.0e-4));',
               'const vec3 halfWidth2 = max(sd2 * width, vec3(1.0e-4)) * (1.0 + 3.0 * settled);')],
    # **The accumulator's writes rounded to the half-float grid by the shader,
    # stochastically**, so the hardware conversion -- which on this GPU rounds
    # toward zero, 99.9% of 1.3 M texel-frames measured -- has nothing left to
    # round. The picture, its twin and the two moments; nothing else changes.
    'roundfix': [
        (ACC, '\nvoid main()\n{\n\tconst ivec2 texel = ivec2(gl_FragCoord.xy);\n\tconst ivec2 size = textureSize(u_Fresh, 0);',
         '\n' + ROUNDING_GLSL + '\nvoid main()\n{\n\tconst ivec2 texel = ivec2(gl_FragCoord.xy);\n\tconst ivec2 size = textureSize(u_Fresh, 0);'),
        (ACC, '\to_Accumulated = vec4(kept, frames);',
         '\to_Accumulated = vec4(StoreAsHalf(kept, RoundingDraw(uvec2(texel), 1u)), frames);'),
        (ACC, '\to_Accumulated2 = vec4(kept2, twinFrames);',
         '\to_Accumulated2 = vec4(StoreAsHalf(kept2, RoundingDraw(uvec2(texel), 2u)), twinFrames);'),
        (ACC, '\to_Extra = vec4(PackMaterial(roughness, g_Metallic), momMean, momMeanSq,',
         '\to_Extra = vec4(PackMaterial(roughness, g_Metallic),\n'
         '\t\t\t\t   StoreAsHalf(vec3(momMean, momMeanSq, 0.0), RoundingDraw(uvec2(texel), 3u)).xy,'),
    ],
    # And the temporal resolve's history, the same way. It has no frame counter
    # of its own; the jitter it is handed changes every frame, which is enough
    # to walk the draw.
    'taaround': [
        (TAA, 'vec3 Expand(vec3 c)     { return c / max(1.0 - c.x, 1e-4); }\n',
         'vec3 Expand(vec3 c)     { return c / max(1.0 - c.x, 1e-4); }\n\n' + TAA_ROUNDING_GLSL),
        (TAA, '\to_Color = vec4(ToRGB(blended), mix(current.a, historyAlpha, 1.0 - alpha));',
         '\to_Color = vec4(StoreAsHalf(ToRGB(blended), TaaDraw(0u)), mix(current.a, historyAlpha, 1.0 - alpha));'),
        (TAA, '\to_Moments = vec4(frames, momMean, momMeanSq, float(g_Refusal));',
         '\to_Moments = vec4(frames, StoreAsHalf(vec3(momMean, momMeanSq, 0.0), TaaDraw(1u)).xy, float(g_Refusal));'),
    ],
    'roundall': None,   # filled below: roundfix + taaround
    # The same land reservoir with its draws held still: every frame the same
    # picks per pixel, whatever the jitter does -- the half of "jitter off"
    # that is about the sampler, without touching the project.
    'unsalted': [(DIRECT, '\t\tconst uint salt = u_Direct.Animated > 0.5\n\t\t\t\t\t\t?',
                  '\t\tconst uint salt = false\n\t\t\t\t\t\t?')],
    # The land reservoir's site only (two tabs); the sea's choose pass has its
    # own at three and is not in the garage.
    'salted': [(DIRECT, '\t\tconst uint salt = u_Direct.Animated > 0.5\n\t\t\t\t\t\t?',
                '\t\tconst uint salt = true\n\t\t\t\t\t\t?')],
}

VARIANTS['roundall'] = VARIANTS['roundfix'] + VARIANTS['taaround']


def _read(name):
    raw = io.open(os.path.join(SRC_DIR, name), encoding='utf-8', newline='').read()
    return raw, CRLF in raw


def build_variant(name):
    """The staged text of every shader the variant touches, or an exit."""
    texts = {}
    for shader, old, new in VARIANTS[name]:
        if shader not in texts:
            raw, crlf = _read(shader)
            texts[shader] = [raw.replace(CRLF, LF), crlf]
        # `old` None: the whole file is `new` -- a committed copy staged as it
        # was (RT-20 stages HEAD's resolve beside the patched source).
        if old is None:
            texts[shader][0] = new.replace(CRLF, LF)
            continue
        n = texts[shader][0].count(old)
        if n != 1:
            sys.exit('%s: a substitution in %s matched %d times: %r' % (name, shader, n, old[:60]))
        texts[shader][0] = texts[shader][0].replace(old, new)
    return {s: (t.replace(LF, CRLF) if crlf else t) for s, (t, crlf) in texts.items()}


PROJECT_BYTES = io.open(PROJECT, 'rb').read()


def restore():
    ok = True
    if io.open(PROJECT, 'rb').read() != PROJECT_BYTES:
        io.open(PROJECT, 'wb').write(PROJECT_BYTES)
    ok = io.open(PROJECT, 'rb').read() == PROJECT_BYTES
    # **Every shader any variant can stage, read from VARIANTS as it stands now** --
    # a script may add variants for shaders this file never names. The fixed list
    # this used to be (the accumulator, the resolve, the direct pass) left a staged
    # `reflection_resolve.rvshader` in place after RT-4's R11 arms (2026-09-14), and
    # every run after the first one that staged it rendered with the modified
    # resolve: two "ship" runs a sequence apart differed by up to 233 levels, which
    # read as run-to-run nondeterminism in the engine.
    # And any other staged file that differs from its source, whichever process
    # staged it: a variant registered only in another script's run is not in this
    # process's VARIANTS.
    names = {ACC, TAA, DIRECT}
    for subs in VARIANTS.values():
        for shader, _, _ in subs or []:
            names.add(shader)
    for folder, _, files in os.walk(STAGED_DIR):
        for f in files:
            rel = os.path.relpath(os.path.join(folder, f), STAGED_DIR)
            src = os.path.join(SRC_DIR, rel)
            if os.path.exists(src) and io.open(src, 'rb').read() != io.open(os.path.join(folder, f), 'rb').read():
                names.add(rel)
    for name in sorted(names):
        src, dst = os.path.join(SRC_DIR, name), os.path.join(STAGED_DIR, name)
        shutil.copyfile(src, dst)
        ok = ok and io.open(src, 'rb').read() == io.open(dst, 'rb').read()
    return ok


def frames_of(tag):
    fs = [f for f in os.listdir(SHOTS) if re.match(re.escape('rt5b_' + tag) + r'_\d+\.png$', f)]
    return sorted(fs, key=lambda f: int(re.search(r'_(\d+)\.png$', f).group(1)))


# **The owner's lights button, pressed off at a time.** ShowroomLights starts
# with the car's lamps off; this copy starts them on, and puts the engine's
# Switcher on exactly what the script's Toggle() would switch -- the four spot
# lamps (to intensity 0) and every car part whose tag contains one of its
# lens names (to emissive 0, which is what Apply() writes). All four lamps are
# Realtime, so nothing baked is involved: what fades is the filters' memory.
LAMPS = ('Headlamp Beam Left', 'Headlamp Beam Right', 'Tail Glow Left', 'Tail Glow Right')
LENSES = ('EXT_Emissive_Light_Front', 'ST_FRONT_', 'EXT_Emissive_Light_Rear', 'EXT_Glass_Emissive_Rear')


def lamps_off_scene(head, seconds):
    text = head.decode('utf-8').replace(CRLF, LF)
    if text.count('        StartOn: false\n') != 1:
        sys.exit('ShowroomLights StartOn matched %d' % text.count('        StartOn: false\n'))
    text = text.replace('        StartOn: false\n', '        StartOn: true\n')
    block = ('    NativeScriptComponent:\n      Script: Switcher\n      Fields:\n'
             '        AtSeconds: %g\n        Intensity: 0\n        Emissive: 0\n' % seconds)
    out, lamps, lenses = [], 0, 0
    for part in re.split(r'(?=\n  - EntityID: )', text):
        m = re.search(r'\n      Tag: (.*)', part)
        tag = m.group(1).strip() if m else ''
        is_lamp = tag in LAMPS and 'LightComponent' in part
        is_lens = any(n in tag for n in LENSES) and 'MeshComponent' in part
        if is_lamp or is_lens:
            if 'NativeScriptComponent' in part:
                sys.exit('%s already has a native script' % tag)
            body = part.rstrip('\n')
            part = body + '\n' + block.rstrip('\n') + part[len(body):]
            lamps += is_lamp
            lenses += is_lens
        out.append(part)
    if lamps != len(LAMPS) or lenses == 0:
        sys.exit('switch attached to %d lamps and %d lens parts' % (lamps, lenses))
    print('lamps on at start; %d lamps and %d lens parts switch off at %g s' % (lamps, lenses, seconds))
    return ''.join(out).encode('utf-8')


# **RT-20: one object crossing the parked garage** -- make_moving_scene.py's
# near-mirror chrome cube, added to HEAD's scene rather than taken from the
# committed showroom_moving.rage, which was authored from the 09-07 showroom.
# `start` is where it begins along world X; it drives +X at `speed` m/s for
# `stop` seconds of scene time, then holds still.
# **2026-09-21: this block used to be written in a schema the loader does not
# have.** `Material:` was a nested map of base colour, metallic and roughness --
# and MeshComponent's Material field is an *asset handle*, with the per-mesh
# values being overrides each gated on its own Override flag. So none of it was
# read: every moving-cube run this project has taken rendered the default matte
# material, and the "near-mirror chrome cube" was a grey box. Written the way
# showroom.rage writes it now.
#
# **And the material is a real asset now**, assets/materials/chrome_test.rmat:
# metallic 1, roughness 0.12, no maps. It has to be an asset, because `Material: 0`
# resolves to none and a MeshComponent with no material ignores its overrides too
# -- verified by overriding the base colour to pure red and getting the same grey
# box back. The Override flags below are kept anyway so the values are visible
# here and agree with the asset.
CUBE_ENTITY = '''  - EntityID: 7311000000000000101
    TagComponent:
      Tag: MovingPanel
    TransformComponent:
      Position: [%g, 1.6, -6]
      Rotation: [0, 0, 0]
      Scale: [1.2, 1.2, 1.2]
    MeshComponent:
      Static: false
      Mesh: 8241982477996916736
      Material: 7311000000000000202
      OverrideBaseColor: true
      BaseColor: [0.95, 0.95, 1, 1]
      OverrideEmissive: false
      EmissiveColor: [0, 0, 0, 1]
      OverrideMetallic: true
      Metallic: 1
      OverrideRoughness: true
      Roughness: 0.12
      OverrideOcclusion: false
      Occlusion: 1
      OverrideNormalScale: false
      NormalScale: 1
    NativeScriptComponent:
      Script: Slider
      Fields:
        Speed: %g
        StopAfter: %g
'''


def moving_cube_scene(scene, start, speed, stop):
    text = scene.decode('utf-8').replace(CRLF, LF)
    if 'MovingPanel' in text:
        sys.exit('the scene already has a MovingPanel')
    if not text.endswith(LF):
        text += LF
    return (text + CUBE_ENTITY % (start, speed, stop)).encode('utf-8')


# **RT-7: the ceiling tubes as the line lights they are.** HEAD's twenty Tube
# lights carry no size, so they shade as points; this copy gives each `length`
# metres along its local X (world X on these fixtures, the way the bars run) and
# `radius` metres of thickness.


def tube_length_scene(scene, length, radius=0.0):
    text = scene.decode('utf-8').replace(CRLF, LF)
    out, count = [], 0
    for part in re.split(r'(?=\n  - EntityID: )', text):
        tag = re.search(r'\n      Tag: (.*)', part)
        if tag and tag.group(1).strip().startswith('Tube ') and 'LightComponent' in part:
            if 'SourceLength' in part:
                sys.exit('%s already has a size' % tag.group(1))
            fields = '      SourceLength: %g\n      SourceRadius: %g\n' % (length, radius)
            part, n = re.subn(r'(\n    LightComponent:\n)', lambda m: m.group(1) + fields, part, count=1)
            count += n
        out.append(part)
    if count != 20:
        sys.exit('tube size set on %d lights, not 20' % count)
    return ''.join(out).encode('utf-8')


def run_arms(arms):
    os.makedirs(OUT, exist_ok=True)
    staged = {a[1]: build_variant(a[1]) for a in arms}   # every substitution checked before any run
    head = subprocess.run(['git', 'show', 'HEAD:SampleProject/assets/scenes/showroom.rage'],
                          cwd=ROOT, capture_output=True, check=True).stdout
    io.open(os.path.join(SCENES, HEAD_SCENE), 'wb').write(head)
    try:
        for tag, variant, flags, opts in arms:
            if not restore():
                sys.exit('staged copies did not restore before %s' % tag)
            scene = lamps_off_scene(head, opts['lamps_off_at']) if opts.get('lamps_off_at') else head
            if opts.get('cube'):
                scene = moving_cube_scene(scene, *opts['cube'])
            if opts.get('tube_length'):
                scene = tube_length_scene(scene, opts['tube_length'], opts.get('tube_radius', 0.0))
            io.open(os.path.join(SCENES, HEAD_SCENE), 'wb').write(scene)
            for shader, text in staged[variant].items():
                io.open(os.path.join(STAGED_DIR, shader), 'w', encoding='utf-8', newline='').write(text)
            if opts.get('no_jitter'):
                raw = PROJECT_BYTES.decode('utf-8')
                eol = CRLF if CRLF in raw else LF
                old, new = (x.replace(LF, eol) for x in NO_JITTER)
                if raw.count(old) != 1:
                    sys.exit('the jitter scale line matched %d' % raw.count(old))
                io.open(PROJECT, 'wb').write(raw.replace(old, new).encode('utf-8'))
            first, count = opts.get('first', 150), opts.get('frames', 100)
            env = dict(os.environ, BURST_SCENE=HEAD_SCENE)
            for key in ('BURST_SWITCH', 'BURST_SLIDE'):
                env.pop(key, None)
                if opts.get(key):
                    env[key] = opts[key]
            cmd = [sys.executable, BURST, 'rt5b_' + tag, '--speed=%g' % opts.get('speed', 0.0),
                   '--stop=%g' % opts.get('stop', 0.1), '--frames=%d' % count, '--from=%d' % first]
            cmd += ['--extra=' + f for f in flags]
            t0 = time.time()
            p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=3600,
                               errors='replace', env=env)
            got = frames_of(tag)
            nums = [int(re.search(r'_(\d+)\.png$', f).group(1)) for f in got]
            if nums != list(range(first, first + count)):
                print(p.stdout[-3000:], p.stderr[-3000:])
                sys.exit('%s: frames %s..%s, %d of %d' % (tag, nums[:1], nums[-1:], len(got), count))
            if opts.get('mean_from') is not None:
                acc, n = None, 0
                for f, k in zip(got, nums):
                    if k < opts['mean_from']:
                        continue
                    a = np.asarray(Image.open(os.path.join(SHOTS, f)).convert('RGB'), dtype=np.float64)
                    acc = a if acc is None else acc + a
                    n += 1
                np.save(os.path.join(OUT, tag + '.npy'), (acc / n).astype(np.float32))
            print('%-18s %-8s %d frames, %.0f s  %s' % (tag, variant, len(got), time.time() - t0, ' '.join(flags)))
            sys.stdout.flush()
    finally:
        print('staged copies restored and identical to source:', restore())
        for f in (HEAD_SCENE, HEAD_SCENE + '.meta'):
            try:
                os.remove(os.path.join(SCENES, f))
            except OSError:
                pass


if __name__ == '__main__':
    # tag=variant[,--flag...]  -- parked, frames 150..249, mean from 186.
    arms = []
    for spec in sys.argv[1:]:
        tag, rest = spec.split('=', 1)
        parts = rest.split(',')
        if parts[0] not in VARIANTS:
            sys.exit('no variant %s' % parts[0])
        opts = dict(frames=100, first=150, mean_from=186)
        flags = [f for f in parts[1:] if f != 'nojitter']
        opts['no_jitter'] = 'nojitter' in parts[1:]
        arms.append((tag, parts[0], flags, opts))
    run_arms(arms)
