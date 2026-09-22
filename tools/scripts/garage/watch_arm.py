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
sys.path.insert(0, os.path.join(HERE, 'session_2026_09_15'))
import stage_run as sr          # noqa: E402
import burst                    # noqa: E402
import watch_mover as wm        # noqa: E402
import emitter_mover as em      # noqa: E402

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
    # **The stray map (2026-09-22).** Every stray ray -- one that came back far
    # brighter than the 5x5 around it -- painted by what made it bright: red the
    # lamps' glint on the struck surface, green the struck thing's own glow, blue
    # anything else; yellow where the resolve's cap would have scaled it. The rest
    # of the floor is the reflection at quarter brightness. Run it as
    #   watch_arm.py car 20 --stage=strays --reflection-history=off --debug-view=reflection-picture
    # The trace writes the glint's luminance into the travel lane's fourth channel,
    # which nothing reads (a literal zero since the pass was built).
    'strays': [
        ('reflection_trace.rvshader',
         '	float emissiveSum = hit.Missed ? 0.0 : dot(hit.Emissive, vec3(0.2126, 0.7152, 0.0722));',
         '	float emissiveSum = hit.Missed ? 0.0 : dot(hit.Emissive, vec3(0.2126, 0.7152, 0.0722));\n'
         '	float specularSum = hit.Missed ? 0.0 : dot(hit.Specular, vec3(0.2126, 0.7152, 0.0722));'),
        ('reflection_trace.rvshader',
         '		emissiveSum += glow;',
         '		emissiveSum += glow;\n'
         '		specularSum += hitMore.Missed ? 0.0 : dot(hitMore.Specular, vec3(0.2126, 0.7152, 0.0722));'),
        ('reflection_trace.rvshader',
         '	emissiveSum /= float(rays);',
         '	emissiveSum /= float(rays);\n	specularSum /= float(rays);'),
        ('reflection_trace.rvshader',
         'clamp(travelSum / moversStruck, vec3(-1.0e4), vec3(1.0e4)) : vec3(0.0), float(rays))',
         'clamp(travelSum / moversStruck, vec3(-1.0e4), vec3(1.0e4)) : vec3(0.0), min(specularSum, 6.0e4))'),
        ('reflection_trace.rvshader',
         'clamp(hit.Travel, vec3(-1.0e4), vec3(1.0e4)), float(rays));',
         'clamp(hit.Travel, vec3(-1.0e4), vec3(1.0e4)), min(specularSum, 6.0e4));'),
        ('reflection_resolve.rvshader',
         '	o_ResolvedMoving = fresh.a >= 0.0 && StruckMover(texel) ? vec4(fresh.rgb, 1.0) : vec4(0.0);',
         '	o_ResolvedMoving = fresh.a >= 0.0 && StruckMover(texel) ? vec4(fresh.rgb, 1.0) : vec4(0.0);\n'
         '	// STRAY MAP (staged arm): paint each stray ray by what made it bright.\n'
         '	const float lumaHere = dot(fresh.rgb, vec3(0.2126, 0.7152, 0.0722));\n'
         '	float strayMean = 0.0, strayCount = 0.0;\n'
         '	for (int y = -2; y <= 2; ++y)\n'
         '		for (int x = -2; x <= 2; ++x)\n'
         '		{\n'
         '			if (x == 0 && y == 0) continue;\n'
         '			const vec4 n = texelFetch(u_Fresh, clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0);\n'
         '			if (n.a < 0.0) continue;\n'
         '			strayMean += dot(n.rgb, vec3(0.2126, 0.7152, 0.0722));\n'
         '			strayCount += 1.0;\n'
         '		}\n'
         '	strayMean = strayCount > 0.0 ? strayMean / strayCount : 0.0;\n'
         '	const bool isStray = fresh.a >= 0.0 && lumaHere > max(8.0 * strayMean, 2.0);\n'
         '	{\n'
         '		const float glow = texelFetch(u_Hit, texel, 0).w;\n'
         '		const float glint = texelFetch(u_Travel, texel, 0).w;\n'
         '		vec3 paint = vec3(lumaHere);\n'
         '		if (isStray)\n'
         '			paint = (glint >= glow && glint > 0.5 * lumaHere) ? vec3(4.0, 0.0, 0.0)\n'
         '				  : (glow > 0.5 * lumaHere) ? vec3(0.0, 4.0, 0.0) : vec3(0.0, 0.0, 4.0);\n'
         '		o_Resolved = vec4(paint, fresh.a);\n'
         '	}'),
        ('reflection_resolve.rvshader',
         '	vec3 sum = vec3(0.0);\n	float distanceSum = 0.0;\n	float weightSum = 0.0;',
         '	if (isStray && fireflySigmas > 0.0)\n'
         '	{\n'
         '		const vec2 lanesHere = texelFetch(u_Hit, texel, 0).zw;\n'
         '		float capHere = tapCap;\n'
         '		if (lanesHere.y / max(lumaHere, 1.0e-6) > kEmitterShare)\n'
         '			capHere = 1.0e30;\n'
         '		else if (meanPdf > 0.0)\n'
         '			capHere *= clamp(lanesHere.x / meanPdf, 1.0, kCredibleDensity);\n'
         '		if (lumaHere > capHere)\n'
         '			o_Resolved = vec4(4.0, 4.0, 0.0, fresh.a);\n'
         '	}\n'
         '	return;\n'
         '	vec3 sum = vec3(0.0);\n	float distanceSum = 0.0;\n	float weightSum = 0.0;'),
    ],
    # The balance heuristic back, for the aimed ray and a struck lamp's glow. The
    # power heuristic replaced it on 2026-09-23 (RT-22): the grain on the car
    # window's reflection of the moving glowing cube, owner-judged gone. Inert on
    # the parked floor's 16-ray score (2026-09-22). This arm shows what it was.
    'balance': [
        ('reflection_trace.rvshader',
         '	const float share = pLobe * pLight / max(pLobe * pLobe + pLight * pLight, 1.0e-18);',
         '	const float share = pLobe / max(pLobe + pLight, 1.0e-9);'),
        ('reflection_trace.rvshader',
         '	return pLobe * pLobe / max(pLobe * pLobe + pLight * pLight, 1.0e-18);',
         '	return pLobe / max(pLobe + pLight, 1.0e-9);'),
    ],
    # RT-22 (2026-09-22): a clearly measured change restarts the average outright
    # instead of blending by its share. Priced on the drive it halves the floor's
    # trail -- and REJECTED by eye: a moving reflection changes every frame, so it
    # restarted every frame and the cube's image on the floor went raw and shaky.
    # Kept as the switch that shows it.
    'restart': ('reflection_accumulate.rvshader',
                'blendFrames = max(min(blendFrames, 1.0 / measured.x), 1.0);',
                'blendFrames = measured.x > 0.1 ? 1.0 : max(min(blendFrames, 1.0 / measured.x), 1.0);'),
    # RT-22 (2026-09-23): the frame filter's temporal floor off -- the box no longer
    # widens by the pixel's own recent swing, which a light sweeping past a still
    # pixel inflates. Thin geometry flickers again under the jitter with it off.
    'noflickerfloor': ('taa_resolve.rvshader',
                       'const float kTemporalSigma = 2.0;',
                       'const float kTemporalSigma = 0.0;'),
    # The candidate fix: the floor kept only where the 3x3 spans another surface --
    # an outline, where jitter coverage flicker happens -- and strict mid-surface.
    'edgefloor': ('taa_resolve.rvshader',
                  'const float temporalSigma = sqrt(max(momMeanSq - momMean * momMean, 0.0)) * stillness;',
                  'const float temporalSigma = sqrt(max(momMeanSq - momMean * momMean, 0.0)) * stillness\n'
                  '\t\t\t\t\t\t\t  * ((!boxGeometry || sameSurface < 8) ? 1.0 : 0.0);'),
    # RT-22's self-check: red wherever the resolve reads the pixel as covered by a
    # see-through surface (the transparent pass's revealage, binding 13).
    'coveredmap': ('taa_resolve.rvshader',
                   'const bool outline = !boxGeometry || sameSurface < 8;',
                   'const bool outline = !boxGeometry || sameSurface < 8;\n'
                   '\tif (covered) { o_Color = vec4(4.0, 0.0, 0.0, 1.0); o_Moments = vec4(1.0); return; }'),
    # RT-22: the pane's settled reflection (RT-13 stage 3) not taken, so the nearest
    # pane casts its own rays as before stage 3; its settled lamp light stays.
    'glassownrays': ('include/pbr_fragment.glsl',
                     'const bool glassReflected = directSignal && textureSize(u_GlassReflection, 0).x > 1;',
                     'const bool glassReflected = false && directSignal && textureSize(u_GlassReflection, 0).x > 1;'),
    # RT-22: red wherever the resolve still widens by the pixel's own swing (an
    # outline, not under glass) -- where a stopped mover's reflection can still hang.
    'relaxmap': ('taa_resolve.rvshader',
                 'const bool outline = !boxGeometry || sameSurface < 8;',
                 'const bool outline = !boxGeometry || sameSurface < 8;\n'
                 '\tif (outline && !covered) { o_Color = vec4(4.0, 0.0, 0.0, 1.0); o_Moments = vec4(1.0); return; }'),
    # RT-22: four rays a texel where no allocation ran -- which is the glass layer's
    # trace alone (the opaque trace's tile allocation overwrites it).
    'glassrays4': ('reflection_trace.rvshader',
                   '\tint rays = 1;',
                   '\tint rays = 4;'),
    # RT-22: the pane's lamp light back on the loop (the pane walks and traces its
    # own lamps), its settled reflection kept -- which of the two carries the grain.
    'glassownlamps': [
        ('include/pbr_fragment.glsl',
         '\tconst bool directSignal = glassDepth > 0.0',
         '\tconst bool directSignal = false;\n\tconst bool glassSettled = glassDepth > 0.0'),
        ('include/pbr_fragment.glsl',
         'const bool glassReflected = directSignal && textureSize(u_GlassReflection, 0).x > 1;',
         'const bool glassReflected = glassSettled && textureSize(u_GlassReflection, 0).x > 1;'),
    ],
    'noexempt': ('reflection_resolve.rvshader',
                 '			if (emissiveShare > kEmitterShare)',
                 '			if (false)'),
}


def scene_for(case, seconds):
    # The scene as it is on disk, not the committed one: the tubes' length and
    # anything else the owner has set and not yet committed must reach the arm.
    head = io.open(os.path.join(sr.SCENES, 'showroom.rage'), 'rb').read()
    if case == 'cube':
        return sr.moving_cube_scene(head, -9.0, SPEED, seconds)
    if case == 'light':
        # RT-22's drive: a glowing cube with a live point light under it, 1.5 m/s
        # across the close shot (session_2026_09_15/emitter_mover.py).
        return em.scene(seconds)
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
    cam = '-4.3,1.0,-2,3.2,25,8' if case == 'light' else DEFAULT_CAM
    stage = None
    no_jitter = False
    rest = []
    for f in flags:
        if f == '--no-jitter':
            # The temporal jitter's scale is a project setting with no flag; the
            # project is edited for the run and restored byte for byte after it.
            no_jitter = True
        elif f.startswith('--cam='):
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
    # The engine finds a bake by the scene file's name and this is a renamed copy,
    # so without this every arm played under live bounce light (burst.sync_bake).
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        if no_jitter:
            text = sr.PROJECT_BYTES.decode('utf-8')
            was, now = sr.NO_JITTER
            eol = '\r\n' if '\r\n' in text else '\n'
            was, now = was.replace('\n', eol), now.replace('\n', eol)
            if text.count(was) != 1:
                sys.exit('TemporalJitterScale matched %d times' % text.count(was))
            io.open(sr.PROJECT, 'wb').write(text.replace(was, now).encode('utf-8'))
            print('project: TemporalJitterScale 0 for this run')
        if stage:
            spec = VARIANTS[stage]
            triples = spec if isinstance(spec, list) else [spec]
            texts = {}
            for name, was, now in triples:
                if name not in texts:
                    src = os.path.join(sr.ROOT, 'RageVEditor', 'assets', 'shaders', name)
                    texts[name] = io.open(src, encoding='utf-8', newline='').read()
                text = texts[name]
                crlf, lf = chr(13) + chr(10), chr(10)
                eol = crlf if text.count(crlf) > text.count(lf) // 2 else lf
                a, b = was.replace(lf, eol), now.replace(lf, eol)
                if text.count(a) != 1:
                    sys.exit('%s: the anchor matched %d times: %s' % (stage, text.count(a), was[:60]))
                texts[name] = text.replace(a, b)
            for name, text in texts.items():
                io.open(os.path.join(sr.STAGED_DIR, name), 'w', encoding='utf-8',
                        newline='').write(text)
            print('staged arm: %s (%s)' % (stage, ', '.join(texts)))
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
