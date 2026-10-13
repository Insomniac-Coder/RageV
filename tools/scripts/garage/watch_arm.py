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
sys.path.insert(0, os.path.join(HERE, '..'))
import vulkan_messages as vm    # noqa: E402
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
    # RT-24: the turn rule (RT-6.3's DirectionConfidence) scaled by the lobe's width,
    # alpha = roughness^2, instead of by roughness: linear in roughness it let a
    # chrome pole (0.055) swing ~5-7 degrees a frame and keep its whole memory.
    'tightturn': ('reflection_accumulate.rvshader',
                  '\tconst float lowest = mix(kMirrorSensitivity, kRoughSensitivity,\n'
                  '\t\t\t\t\t\t\t clamp(roughness, 0.0, 1.0));',
                  '\tconst float lowest = mix(kMirrorSensitivity, kRoughSensitivity,\n'
                  '\t\t\t\t\t\t\t clamp(roughness * roughness, 0.0, 1.0));'),
    # RT-24: the reflection memory kept, but the frame filter handed the surface's
    # own motion on every pixel instead of the reflection's image motion (RT-6.1's
    # lane) -- the second thing `--reflection-history=off` changes.
    # RT2-0 arm A1: the frame filter lines up every pixel by its surface's own motion,
    # never the reflected image's (RT-6.1's lane off for TAA only; the accumulator is untouched).
    'taasurfacemotion': ('reflection_composite.rvshader',
                         'o_Motion = reflected > rest ? imageMotion : surfaceMotion;',
                         'o_Motion = surfaceMotion;'),
    # RT2-0 arm A2: no extra box room for rough dielectrics (RT-6.2's material widening off).
    'nomatwiden': ('taa_resolve.rvshader',
                   'const float kStableWiden = 8.0;',
                   'const float kStableWiden = 1.0;'),
    # RT2-0 arm J: while a pixel moves, the frame filter keeps no more frames on it than
    # its reflection has. The composite hands the filter, in the alpha nothing after it
    # reads, the least share of this frame the pixel needs: its reflection's share of the
    # brightness over the reflection's own frame count. Parked, nothing changes.
    'taacap': [
        ('reflection_composite.rvshader',
         '	o_Color = vec4(scene.rgb + weight * added, 0.0);',
         '	// arm J: written below, once the reflection\'s share of the pixel is known.'),
        ('reflection_composite.rvshader',
         '	o_Motion = reflected > rest ? imageMotion : surfaceMotion;',
         '	o_Motion = reflected > rest ? imageMotion : surfaceMotion;\n'
         '	o_Color = vec4(scene.rgb + weight * added,\n'
         '				   reflection.a > 0.0 ? (reflected / max(reflected + rest, 1.0e-6)) / max(reflection.a, 1.0) : 0.0);'),
        ('taa_resolve.rvshader',
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);',
         '	if (stillness < 0.5 && current.a > 0.0)\n'
         '	{\n'
         '		frames = max(min(frames, 1.0 / current.a), 1.0);\n'
         '		alpha = max(alpha, 1.0 / frames);\n'
         '	}\n'
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);'),
    ],
    # RT2-0 arm J, scaled by speed (2026-09-24): the cap fades in between 4 and 24 texels of
    # motion a frame, so a slight move keeps the frame filter's full memory (and its smoothed
    # edges) and a fast swing gets the whole cap. The owner saw edges jitter on the slightest
    # move with the unscaled arm, which capped any motion at all.
    'taacap2': [
        ('reflection_composite.rvshader',
         '	o_Color = vec4(scene.rgb + weight * added, 0.0);',
         '	// arm J: written below, once the reflection\'s share of the pixel is known.'),
        ('reflection_composite.rvshader',
         '	o_Motion = reflected > rest ? imageMotion : surfaceMotion;',
         '	o_Motion = reflected > rest ? imageMotion : surfaceMotion;\n'
         '	o_Color = vec4(scene.rgb + weight * added,\n'
         '				   reflection.a > 0.0 ? (reflected / max(reflected + rest, 1.0e-6)) / max(reflection.a, 1.0) : 0.0);'),
        ('taa_resolve.rvshader',
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);',
         '	const float capWeight = smoothstep(4.0, 24.0, motionTexels);\n'
         '	if (capWeight > 0.0 && current.a > 0.0)\n'
         '	{\n'
         '		frames = mix(frames, max(min(frames, 1.0 / current.a), 1.0), capWeight);\n'
         '		alpha = max(alpha, 1.0 / frames);\n'
         '	}\n'
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);'),
    ],
    # RT-24: arm J with the frame filter following the surface -- the two rules edit one line.
    'taacap2surf': [
        ('reflection_composite.rvshader',
         '	o_Color = vec4(scene.rgb + weight * added, 0.0);',
         '	// arm J: written below, once the reflection\'s share of the pixel is known.'),
        ('reflection_composite.rvshader',
         '	o_Motion = reflected > rest ? imageMotion : surfaceMotion;',
         '	o_Motion = surfaceMotion;\n'
         '	o_Color = vec4(scene.rgb + weight * added,\n'
         '				   reflection.a > 0.0 ? (reflected / max(reflected + rest, 1.0e-6)) / max(reflection.a, 1.0) : 0.0);'),
        ('taa_resolve.rvshader',
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);',
         '	const float capWeight = smoothstep(4.0, 24.0, motionTexels);\n'
         '	if (capWeight > 0.0 && current.a > 0.0)\n'
         '	{\n'
         '		frames = mix(frames, max(min(frames, 1.0 / current.a), 1.0), capWeight);\n'
         '		alpha = max(alpha, 1.0 / frames);\n'
         '	}\n'
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);'),
    ],
    # RT-24: arm J in its general form (owner rule 2026-09-24: nothing that depends on speed).
    # No motion gate: the frame filter never keeps more frames on a pixel than that pixel's
    # reflection has, over the reflection's share of it -- whatever the camera does. Parked, a
    # reflection's history is full and the cap sits above anything the filter keeps.
    'taacapall': [
        ('reflection_composite.rvshader',
         '	o_Color = vec4(scene.rgb + weight * added, 0.0);',
         '	// arm J: written below, once the reflection\'s share of the pixel is known.'),
        ('reflection_composite.rvshader',
         '	o_Motion = reflected > rest ? imageMotion : surfaceMotion;',
         '	o_Motion = surfaceMotion;\n'
         '	o_Color = vec4(scene.rgb + weight * added,\n'
         '				   reflection.a > 0.0 ? (reflected / max(reflected + rest, 1.0e-6)) / max(reflection.a, 1.0) : 0.0);'),
        ('taa_resolve.rvshader',
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);',
         '	if (current.a > 0.0)\n'
         '	{\n'
         '		frames = max(min(frames, 1.0 / current.a), 1.0);\n'
         '		alpha = max(alpha, 1.0 / frames);\n'
         '	}\n'
         '	const float momentAlpha = max(alpha, 1.0 / kMomentFrames);'),
    ],
    # **MEASURED AND DROPPED (2026-09-24):** pole ghost 6-9% lower while moving, but the parked
    # flicker in the pole's reflection doubled (2.42 against 1.21 at the parked floor), because the
    # four-frame average is grainy and the long history clamped to it lets the grain through.
    # RT2-0 arm H (2026-09-24): a fast history. A second, short running average (four frames)
    # is kept in the moving-layer lane, which the floor leaves unused while the moving layer is
    # off (its default); the long history is then held within two of the fresh spreads, scaled
    # to a four-frame average, of the short one -- so an old picture cannot outlive the scene
    # by more than the short memory, at any speed. The glass pane keeps its moving layer and is
    # untouched.
    'fasthist': [
        ('reflection_accumulate.rvshader',
         'bool g_Kept = false;',
         'bool g_Kept = false;\n'
         '// arm H: this frame\'s fast history and its count, written into the moving-layer lane.\n'
         'vec3 g_FastKept = vec3(0.0);\n'
         'float g_FastFrames = 0.0;\n'
         'const float kFastFrames = 4.0;\n'
         'const float kFastSpread = 2.0;'),
        ('reflection_accumulate.rvshader',
         '	vec3 kept = freshStill;\n	float frames = 1.0;',
         '	vec3 kept = freshStill;\n	float frames = 1.0;\n'
         '#if !defined(RV_SIGNAL_PAIR) && !defined(RV_SIGNAL_DIFFUSE)\n'
         '	g_FastKept = freshStill;\n'
         '	g_FastFrames = 1.0;\n'
         '#endif'),
        ('reflection_accumulate.rvshader',
         '			const vec3 held = ClipToBound(pastStill, mean, halfWidth);',
         '			vec3 held = ClipToBound(pastStill, mean, halfWidth);\n'
         '#if !defined(RV_SIGNAL_PAIR) && !defined(RV_SIGNAL_DIFFUSE)\n'
         '			if (!MovingLayerLive())\n'
         '			{\n'
         '				const vec4 pastFast = PastMovingAt(c);\n'
         '				if (pastFast.a > 0.5)\n'
         '				{\n'
         '					g_FastFrames = min(pastFast.a + 1.0, kFastFrames);\n'
         '					g_FastKept = mix(ClipToBound(pastFast.rgb, mean, halfWidth), freshStill, 1.0 / g_FastFrames);\n'
         '					held = ClipToBound(held, g_FastKept,\n'
         '									   max(sd * (kFastSpread / sqrt(g_FastFrames)), vec3(1.0e-4)));\n'
         '				}\n'
         '			}\n'
         '#endif'),
        ('reflection_accumulate.rvshader',
         '	o_MovingLayer = vec4(StoreAsHalf(keptMoving, HalfRoundingDraw(roundingPixel, roundingFrame, 5u)),\n'
         '						 framesMoving);',
         '	o_MovingLayer = MovingLayerLive()\n'
         '				  ? vec4(StoreAsHalf(keptMoving, HalfRoundingDraw(roundingPixel, roundingFrame, 5u)), framesMoving)\n'
         '				  : vec4(StoreAsHalf(g_FastKept, HalfRoundingDraw(roundingPixel, roundingFrame, 5u)), g_FastFrames);'),
    ],
    # RT2-0 (2026-09-24): the history read with a Catmull-Rom kernel, the temporal resolve's own
    # (RT-6.11), instead of bilinear -- a picture resampled bilinearly every frame it moves
    # blurs by a little each time, and a slow camera move keeps seven or more of those frames.
    # Alpha (the frame count) stays the bilinear read's.
    'cubichist': [
        ('reflection_accumulate.rvshader',
         'vec4 OwnHistoryPicture(vec2 pastUv, ivec2 pastTexel, ivec2 size)',
         'vec4 HistoryCatmullRom(vec2 uv, ivec2 size)\n'
         '{\n'
         '	const vec2 texelSize = 1.0 / vec2(size);\n'
         '	const vec2 samplePos = uv * vec2(size);\n'
         '	const vec2 centre = floor(samplePos - 0.5) + 0.5;\n'
         '	const vec2 f = samplePos - centre;\n'
         '	const vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));\n'
         '	const vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);\n'
         '	const vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));\n'
         '	const vec2 w3 = f * f * (-0.5 + 0.5 * f);\n'
         '	const vec2 w12 = w1 + w2;\n'
         '	const vec2 offset12 = w2 / max(w12, vec2(1.0e-5));\n'
         '	const vec2 p0 = (centre - 1.0) * texelSize;\n'
         '	const vec2 p3 = (centre + 2.0) * texelSize;\n'
         '	const vec2 p12 = (centre + offset12) * texelSize;\n'
         '	vec3 sum = vec3(0.0);\n'
         '	sum += texture(u_History, vec2(p0.x,  p0.y)).rgb  * (w0.x  * w0.y);\n'
         '	sum += texture(u_History, vec2(p12.x, p0.y)).rgb  * (w12.x * w0.y);\n'
         '	sum += texture(u_History, vec2(p3.x,  p0.y)).rgb  * (w3.x  * w0.y);\n'
         '	sum += texture(u_History, vec2(p0.x,  p12.y)).rgb * (w0.x  * w12.y);\n'
         '	sum += texture(u_History, vec2(p12.x, p12.y)).rgb * (w12.x * w12.y);\n'
         '	sum += texture(u_History, vec2(p3.x,  p12.y)).rgb * (w3.x  * w12.y);\n'
         '	sum += texture(u_History, vec2(p0.x,  p3.y)).rgb  * (w0.x  * w3.y);\n'
         '	sum += texture(u_History, vec2(p12.x, p3.y)).rgb  * (w12.x * w3.y);\n'
         '	sum += texture(u_History, vec2(p3.x,  p3.y)).rgb  * (w3.x  * w3.y);\n'
         '	return vec4(max(sum, vec3(0.0)), texture(u_History, uv).a);\n'
         '}\n\n'
         'vec4 OwnHistoryPicture(vec2 pastUv, ivec2 pastTexel, ivec2 size)'),
        ('reflection_accumulate.rvshader',
         '	return texture(u_History, pastUv);',
         '	return HistoryCatmullRom(pastUv, size);'),
    ],
    # RT-24: the same Catmull-Rom, held to the range of the four texels the bilinear read
    # would have mixed. Unheld, its overshoot at a tube's reflection (values to 64) was never
    # trimmed -- the accumulator's bound is twelve spreads wide on shiny floors -- and grew
    # frame on frame ('cubichist' measured sharpness 0.23). Held, it cannot ring, and the
    # picture no longer softens a little at every frame it is carried.
    'cubicclamp': [
        ('reflection_accumulate.rvshader',
         'vec4 OwnHistoryPicture(vec2 pastUv, ivec2 pastTexel, ivec2 size)',
         'vec4 HistoryCatmullRom(vec2 uv, ivec2 size)\n'
         '{\n'
         '	const vec2 texelSize = 1.0 / vec2(size);\n'
         '	const vec2 samplePos = uv * vec2(size);\n'
         '	const vec2 centre = floor(samplePos - 0.5) + 0.5;\n'
         '	const vec2 f = samplePos - centre;\n'
         '	const vec2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));\n'
         '	const vec2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);\n'
         '	const vec2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));\n'
         '	const vec2 w3 = f * f * (-0.5 + 0.5 * f);\n'
         '	const vec2 w12 = w1 + w2;\n'
         '	const vec2 offset12 = w2 / max(w12, vec2(1.0e-5));\n'
         '	const vec2 p0 = (centre - 1.0) * texelSize;\n'
         '	const vec2 p3 = (centre + 2.0) * texelSize;\n'
         '	const vec2 p12 = (centre + offset12) * texelSize;\n'
         '	vec3 sum = vec3(0.0);\n'
         '	sum += texture(u_History, vec2(p0.x,  p0.y)).rgb  * (w0.x  * w0.y);\n'
         '	sum += texture(u_History, vec2(p12.x, p0.y)).rgb  * (w12.x * w0.y);\n'
         '	sum += texture(u_History, vec2(p3.x,  p0.y)).rgb  * (w3.x  * w0.y);\n'
         '	sum += texture(u_History, vec2(p0.x,  p12.y)).rgb * (w0.x  * w12.y);\n'
         '	sum += texture(u_History, vec2(p12.x, p12.y)).rgb * (w12.x * w12.y);\n'
         '	sum += texture(u_History, vec2(p3.x,  p12.y)).rgb * (w3.x  * w12.y);\n'
         '	sum += texture(u_History, vec2(p0.x,  p3.y)).rgb  * (w0.x  * w3.y);\n'
         '	sum += texture(u_History, vec2(p12.x, p3.y)).rgb  * (w12.x * w3.y);\n'
         '	sum += texture(u_History, vec2(p3.x,  p3.y)).rgb  * (w3.x  * w3.y);\n'
         '	const ivec2 base = ivec2(floor(samplePos - 0.5));\n'
         '	vec3 lo = vec3(1.0e30), hi = vec3(-1.0e30);\n'
         '	for (int i = 0; i < 4; ++i)\n'
         '	{\n'
         '		const vec3 t = texelFetch(u_History, clamp(base + ivec2(i & 1, i >> 1), ivec2(0), size - 1), 0).rgb;\n'
         '		lo = min(lo, t);\n'
         '		hi = max(hi, t);\n'
         '	}\n'
         '	return vec4(clamp(sum, lo, hi), texture(u_History, uv).a);\n'
         '}\n\n'
         'vec4 OwnHistoryPicture(vec2 pastUv, ivec2 pastTexel, ivec2 size)'),
        ('reflection_accumulate.rvshader',
         '	return texture(u_History, pastUv);',
         '	return HistoryCatmullRom(pastUv, size);'),
    ],
    # RT-24: the old picture looked for where this TEXEL's centre was, not where this
    # frame's jittered sample was. The lookup took the jittered sample's point back to last
    # frame and read the picture there -- half a texel or so off the grid every frame, parked
    # included -- so every frame resampled the whole history at a new sub-texel offset: a
    # blur that builds over the memory with a soft read, a shimmer with a sharp one. Shifted
    # by this frame's jitter, a parked texel reads itself exactly and a moving one resamples
    # once per frame of real motion; the fresh sample is the jittered one, as a temporal
    # resolve's is.
    'gridlookup': [
        ('reflection_accumulate.rvshader',
         '	c.thenNdc = clipThen.xy / clipThen.w - u_Scene.Jitter.zw + objectShift;',
         '	c.thenNdc = clipThen.xy / clipThen.w - u_Scene.Jitter.zw + objectShift + u_Scene.Jitter.xy;'),
        ('reflection_accumulate.rvshader',
         '			const vec2 shift = c.thenNdc - (nowNdc - u_Scene.Jitter.xy);',
         '			const vec2 shift = c.thenNdc - nowNdc;'),
        ('reflection_accumulate.rvshader',
         '				const vec2 movingShift = cm.thenNdc - (nowNdc - u_Scene.Jitter.xy);',
         '				const vec2 movingShift = cm.thenNdc - nowNdc;'),
    ],
    'surfacemotion': ('reflection_accumulate.rvshader',
                      'o_Motion = vec4(g_HaveMotion ? g_ImageMotion : texelFetch(u_Velocity, texel, 0).xy,',
                      'o_Motion = vec4(texelFetch(u_Velocity, texel, 0).xy,'),
    # RT-24: where each reflection texel's memory came from this frame, painted over
    # the reflection itself -- red the surface's own old place (the fallback), green
    # where the reflected image was, black nothing kept (fresh rays only).
    'sourcemap': ('reflection_accumulate.rvshader',
                  '\to_MovingLayer = vec4(StoreAsHalf(keptMoving, HalfRoundingDraw(roundingPixel, roundingFrame, 5u)),\n'
                  '\t\t\t\t\t\t framesMoving);\n',
                  '\to_MovingLayer = vec4(StoreAsHalf(keptMoving, HalfRoundingDraw(roundingPixel, roundingFrame, 5u)),\n'
                  '\t\t\t\t\t\t framesMoving);\n'
                  '\tif (u_Reflection.Probe.z < 0.5)\n'
                  '\t\to_Accumulated = vec4(choice > 0.75 ? vec3(0.0, 3.0, 0.0)\n'
                  '\t\t\t\t\t\t\t : choice > 0.25 ? vec3(3.0, 0.0, 0.0) : vec3(0.0), frames);\n'),
    # RT-24: the surface's old spot (the fallback when the reflection's own spot is not
    # found) refused where the view has turned past what the surface's lobe tolerates --
    # there it holds the old camera's reflection, the smear -- with the turn rule scaled
    # by the lobe (alpha) as 'tightturn' has it.
    'nofallback': [
        ('reflection_accumulate.rvshader',
         '\tconst float lowest = mix(kMirrorSensitivity, kRoughSensitivity,\n'
         '\t\t\t\t\t\t\t clamp(roughness, 0.0, 1.0));',
         '\tconst float lowest = mix(kMirrorSensitivity, kRoughSensitivity,\n'
         '\t\t\t\t\t\t\t clamp(roughness * roughness, 0.0, 1.0));'),
        ('reflection_accumulate.rvshader',
         '\t\tif (!have && haveSurface && !flatMover)\n',
         '\t\tconst bool turnedPast = u_Reflection.PreviousEye.w > 0.5 && haveSurface\n'
         '\t\t\t&& DirectionConfidence(reflect(sight, N),\n'
         '\t\t\t\t\t\t\t\t   reflect(normalize(P - u_Reflection.PreviousEye.xyz),\n'
         '\t\t\t\t\t\t\t\t\t\t   DecodeSurfaceNormal(atSurface.reflector.rg)),\n'
         '\t\t\t\t\t\t\t\t   roughness) < 0.5;\n'
         '\t\tif (!have && haveSurface && !flatMover && !turnedPast)\n'),
    ],
    # RT-24: the bound's widening with the count (2026-09-21) off -- a young texel's
    # bound is tight and eats a bright reflection, which then returns over 32 frames.
    'nosettled': ('reflection_accumulate.rvshader',
                  'const float kSettledBound = 2.0;',
                  'const float kSettledBound = 1.0;'),
    # RT-24: no floor under the moving memory (kMovingMemory, 8 frames) -- while
    # anything moves a texel kept at least eight frames, so at a stop the picture is
    # still partly the last eight poses.
    'nomovingfloor': ('reflection_accumulate.rvshader',
                      '#define kMovingMemory (u_Reflection.Tuning.y)',
                      '#define kMovingMemory 1.0'),
    # RT-24: the old trust fade back, for comparison -- the traced picture trusted a quarter a
    # frame of its age, the baked probe the rest (removed 2026-09-23: under a turning camera
    # every shiny pixel is young, and the floor flipped to the probe -- the spin smear).
    'fade': ('include/pbr_fragment.glsl',
             'float share = (traced.a > 0.0 ? 1.0 : 0.0) * reflectionWindow;',
             'float share = clamp(traced.a * u_Scene.ScreenReflections.x * mix(4.0, 1.0, smoothstep(0.0, 0.3, roughness)), 0.0, 1.0) * reflectionWindow;'),
    # RT-24: the direct light (the pair instance) keeps its memory under camera motion -- a
    # still surface reprojects exactly, and shortened, its few-ray soft shadows showed as a
    # fan of hard lines at the pole bases while the camera swung.
    'directkeep': ('reflection_accumulate.rvshader',
                   '\tfloat memory = base / (1.0 + moved / slack);\n',
                   '#ifdef RV_SIGNAL_PAIR\n\treturn max(base, fewest);\n#endif\n'
                   '\tfloat memory = base / (1.0 + moved / slack);\n'),
    # RT-24: the image distance the reflection's history is looked up by, this frame's
    # own -- not blended with the history's over up to eight frames. A reflected pole's
    # history fetched by a distance that is part wall drew its old image slanted.
    'freshimage': ('reflection_accumulate.rvshader',
                   '\t\t\timage = mix(atSurface.reflector.a, image, 1.0 / min(atSurface.past.a + 1.0, 8.0));',
                   '\t\t\t// (arm) this frame\'s image distance only'),
    # RT-24: the reflection memory's own flicker floor off -- the bound on an old picture is
    # never narrower than the texel's recent swing, and a reflection sweeping across the
    # floor IS a swing: the same rule that let the frame filter keep the window streak.
    'accfloor': ('reflection_accumulate.rvshader',
                 'const float kTemporalSigma = 2.0;',
                 'const float kTemporalSigma = 0.0;'),
    # RT-24: sixteen reflection rays a texel, moving and settled alike -- the grain all but
    # gone, so what still differs between a moving frame and a settled one is the ghost.
    # truth_test.py's MANY, as an arm that combines with others (`--stage=many16+NAME`).
    'many16': ('reflection_trace.rvshader',
               '\tvec3 radianceSum = radiance;',
               # Not in the re-light, whose count is a constant it replays from the record --
               # which then holds the sixteen the trace cast.
               '#ifndef RV_REFLECTION_RELIGHT\n\trays = 16;\n#endif\n\tvec3 radianceSum = radiance;'),
    # RT-24: the bound at the rough surface's width on shiny ones too, instead of twelve
    # spreads -- how far an old picture may sit from this frame's neighbourhood.
    'tightbound': ('reflection_accumulate.rvshader',
                   '							  : mix(12.0, max(u_Reflection.Probe.x, 0.0),',
                   '							  : mix(max(u_Reflection.Probe.x, 0.0), max(u_Reflection.Probe.x, 0.0),'),
    # RT-24: no minimum memory -- a rough-ish surface kept at least History.z (four) frames
    # however far its picture moved, so under a turning camera every frame was three
    # quarters an old, misplaced picture.
    'nofewest': ('reflection_accumulate.rvshader',
                 '\t\t\tconst float fewest = mix(1.0, max(u_Reflection.History.z, 1.0), gloss);',
                 '\t\t\tconst float fewest = 1.0;'),
    'noexempt': ('reflection_resolve.rvshader',
                 '			if (emissiveShare > kEmitterShare)',
                 '			if (false)'),
    # RT-24 diagnostic: every surface flat to its mesh -- no normal maps anywhere. Splits
    # "the reflection slides across the floor's bumps under an orbit" from "the depth the
    # old picture is found by is wrong": a flat floor keeps the second and loses the first.
    'flatnormals': ('include/pbr_fragment.glsl',
                    '	if (HasMap(MAP_NORMAL))\n		s.N = PerturbNormal(TBN, uv);',
                    '	if (false)\n		s.N = PerturbNormal(TBN, uv);'),
    # RT-24 diagnostic: and one roughness per material -- no roughness maps anywhere, so the
    # wet floor's puddles are gone. Splits "the reflection slides across the puddle pattern"
    # from everything geometric.
    'flatrough': ('include/pbr_fragment.glsl',
                  '	if (HasMap(MAP_ROUGHNESS))\n		s.Roughness *= texture(u_RoughnessMap, uv).r;',
                  '	if (false)\n		s.Roughness *= texture(u_RoughnessMap, uv).r;'),
    # RT-24: the old picture found by the NEAREST thing the rays struck, not the average of
    # everything they struck (misses as 10 km included). A small near reflection among far
    # ones -- the object by the car, a pole -- is otherwise looked for at a depth between the
    # two and trails copies of itself under an orbit. The trace keeps its rays' nearest hit;
    # the resolve hands on the 3x3 nearest it already sizes its disc by (ReBLUR's tracking).
    'nearest': [
        ('reflection_trace.rvshader',
         '	float travelledSum = travelled;\n',
         '	float travelledSum = travelled;\n	float nearestHit = travelled;\n'),
        ('reflection_trace.rvshader',
         '		travelledSum += hitMore.Missed ? 1.0e4 : length(hitMore.Position - P);\n',
         '		travelledSum += hitMore.Missed ? 1.0e4 : length(hitMore.Position - P);\n'
         '		nearestHit = min(nearestHit, hitMore.Missed ? 1.0e4 : length(hitMore.Position - P));\n'),
        ('reflection_trace.rvshader',
         '	o_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelledSum / float(rays));',
         '	o_Reflection = vec4(min(radiance, vec3(64.0)) * tint, nearestHit);'),
        ('reflection_resolve.rvshader',
         '		o_Resolved = vec4(sum / weightSum, distanceSum / weightSum);',
         '		o_Resolved = vec4(sum / weightSum, hitDistance);'),
    ],
    # RT-24 instrument: while the camera moves, the reflection keeps ONLY its old picture --
    # no new rays, no bound -- carried frame to frame by the lookup alone. Parked it
    # accumulates as it ships, so the swing starts from the settled picture; at a centre
    # crossing, the difference from the settled frame is the lookup's own drift, integrated
    # over the swing. Run with --no-frame-memory so the frame filter does not add its own.
    'driftonly': ('reflection_accumulate.rvshader',
                  '			kept = mix(held, freshStill, 1.0 / blendFrames);',
                  '			kept = (distance(u_Scene.CameraPosition.xyz, u_Reflection.PreviousEye.xyz) > 1.0e-5 || moved > 1.0)\n'
                  '				 ? pastStill : mix(held, freshStill, 1.0 / blendFrames);'),
    # RT-24: a history is this texel's only if it was gathered with the same lobe. The
    # traced picture is the room seen through THIS surface's roughness -- sharp in a puddle,
    # soft on the dry floor -- so it carries the floor's roughness pattern, and the image
    # reprojection carries that pattern along with the reflection under an orbit. The old
    # test let roughness differ by 0.5. Now the lobe widths (roughness squared) must agree:
    # refused past half apart, trusted less from 15% (floor 0.01, a near mirror).
    'roughmatch': [
        ('reflection_accumulate.rvshader',
         '			if (abs(UnpackRoughness(c.extra.r) - roughness) > 0.5)\n',
         '			const float lobeThen = UnpackRoughness(c.extra.r) * UnpackRoughness(c.extra.r);\n'
         '			const float lobeGap = abs(lobeThen - roughness * roughness)\n'
         '								/ max(max(lobeThen, roughness * roughness), 0.01);\n'
         '			if (abs(UnpackRoughness(c.extra.r) - roughness) > 0.5 || lobeGap > 0.5)\n'),
        ('reflection_accumulate.rvshader',
         '			c.matchConfidence = smoothstep(0.8, 0.94, facing)\n',
         '			c.matchConfidence = smoothstep(0.8, 0.94, facing) * (1.0 - smoothstep(0.15, 0.5, lobeGap))\n'),
    ],
    # RT-24: and the filtered read of the old picture kept to texels of the same lobe --
    # the bilinear otherwise mixes a puddle's sharp picture with the dry floor's soft one at
    # every puddle edge (RT-15c's rule for objects, applied to the lobe).
    'roughbilinear': [
        ('reflection_accumulate.rvshader',
         'float g_Metallic = 0.0;\n',
         'float g_Metallic = 0.0;\nfloat g_Roughness = 0.0;\n'),
        ('reflection_accumulate.rvshader',
         '	g_Metallic = clamp(surface.a, 0.0, 1.0);\n',
         '	g_Metallic = clamp(surface.a, 0.0, 1.0);\n	g_Roughness = roughness;\n'),
        ('reflection_accumulate.rvshader',
         '	return texture(u_History, pastUv);\n',
         '	{\n'
         '		const ivec2 base = ivec2(floor(pastUv * vec2(size) - 0.5));\n'
         '		const float lobeNow = g_Roughness * g_Roughness;\n'
         '		for (int i = 0; i < 4; ++i)\n'
         '		{\n'
         '			const ivec2 at = clamp(base + ivec2(i & 1, i >> 1), ivec2(0), size - 1);\n'
         '			const float was = UnpackRoughness(texelFetch(u_HistoryExtra, at, 0).r);\n'
         '			if (abs(was * was - lobeNow) / max(max(was * was, lobeNow), 0.01) > 0.5)\n'
         '				return texelFetch(u_History, pastTexel, 0);\n'
         '		}\n'
         '	}\n'
         '	return texture(u_History, pastUv);\n'),
    ],
    # RT-24: the two accumulator rules scoped as they would ship -- the reflection kind only
    # (glass included), never the direct light, the occlusion or the bounce, whose histories
    # sit on the surface and do not slide across it.
    'nomovingfloorrefl': ('reflection_accumulate.rvshader',
                          '#define kMovingMemory (u_Reflection.Tuning.y)',
                          '#if !defined(RV_SIGNAL_DIFFUSE)\n#define kMovingMemory 1.0\n#else\n'
                          '#define kMovingMemory (u_Reflection.Tuning.y)\n#endif'),
    'roughmatchrefl': [
        ('reflection_accumulate.rvshader',
         '			if (abs(UnpackRoughness(c.extra.r) - roughness) > 0.5)\n',
         '#if !defined(RV_SIGNAL_DIFFUSE)\n'
         '			const float lobeThen = UnpackRoughness(c.extra.r) * UnpackRoughness(c.extra.r);\n'
         '			const float lobeGap = abs(lobeThen - roughness * roughness)\n'
         '								/ max(max(lobeThen, roughness * roughness), 0.01);\n'
         '#else\n'
         '			const float lobeGap = 0.0;\n'
         '#endif\n'
         '			if (abs(UnpackRoughness(c.extra.r) - roughness) > 0.5 || lobeGap > 0.5)\n'),
        ('reflection_accumulate.rvshader',
         '			c.matchConfidence = smoothstep(0.8, 0.94, facing)\n',
         '			c.matchConfidence = smoothstep(0.8, 0.94, facing) * (1.0 - smoothstep(0.15, 0.5, lobeGap))\n'),
    ],
    # RT-24: the lobe check, only where the reflection slid across the surface. Parked, the
    # jitter moves a puddle-edge texel's sample from wet to dry frame to frame, so a check on
    # every history refused those edges and raised the parked flicker (pole 1.28 -> 1.37). The
    # slide is how far the image's history sits from the surface's own -- zero parked and
    # under a pan, where the history is this very point's -- and the check fades in from
    # half a texel to one and a half. Reflection kind only (this block is its alone).
    'roughslide': ('reflection_accumulate.rvshader',
                   '		const bool flatMover = dot(objectShift, objectShift) > 0.0 && curvature <= kCurvedMover;\n',
                   '		if (have && haveSurface)\n'
                   '		{\n'
                   '			const float slide = length((c.thenNdc - atSurface.thenNdc) * 0.5 * vec2(size));\n'
                   '			const float lobeThen = UnpackRoughness(c.extra.r) * UnpackRoughness(c.extra.r);\n'
                   '			const float lobeGap = abs(lobeThen - roughness * roughness)\n'
                   '								/ max(max(lobeThen, roughness * roughness), 0.01);\n'
                   '			c.matchConfidence *= mix(1.0, 1.0 - smoothstep(0.15, 0.5, lobeGap), smoothstep(0.5, 1.5, slide));\n'
                   '		}\n'
                   '		const bool flatMover = dot(objectShift, objectShift) > 0.0 && curvature <= kCurvedMover;\n'),
    # RT-24 change 2: the lobe check as the overlap of the two lobes' pictures, no tuned edges.
    # Each lobe blurs the reflected picture by 2*alpha radians (a reflection doubles angles)
    # over the image distance, seen from the eye; in texels, with the pixel itself added in
    # quadrature (one texel). The overlap of two Gaussian blurs of widths s1, s2 is
    # 2*s1*s2/(s1^2+s2^2) (their Bhattacharyya coefficient): one for equal lobes, near one for
    # two lobes that both blur less than a pixel. Weighted by min(slide, 1) -- whether the
    # image's history came from another pixel than the surface's own -- so parked and under a
    # pan (slide 0) nothing changes.
    'roughoverlap': ('reflection_accumulate.rvshader',
                     '		const bool flatMover = dot(objectShift, objectShift) > 0.0 && curvature <= kCurvedMover;\n',
                     '		if (have && haveSurface)\n'
                     '		{\n'
                     '			const float slide = length((c.thenNdc - atSurface.thenNdc) * 0.5 * vec2(size));\n'
                     '			const vec4 alongHere = u_Reflection.InverseViewProjection * vec4(nowNdc, 0.5, 1.0);\n'
                     '			const vec4 alongNext = u_Reflection.InverseViewProjection\n'
                     '								 * vec4(nowNdc + vec2(2.0 / float(size.x), 0.0), 0.5, 1.0);\n'
                     '			const float pixelAngle = max(length(normalize(alongNext.xyz / alongNext.w - eye)\n'
                     '											   - normalize(alongHere.xyz / alongHere.w - eye)), 1.0e-7);\n'
                     '			const float perAlpha = 2.0 * image / ((eyeDistance + image) * pixelAngle);\n'
                     '			const float rThen = UnpackRoughness(c.extra.r);\n'
                     '			const float sThen = sqrt(rThen * rThen * rThen * rThen * perAlpha * perAlpha + 1.0);\n'
                     '			const float sNow = sqrt(roughness * roughness * roughness * roughness * perAlpha * perAlpha + 1.0);\n'
                     '			const float overlap = 2.0 * sThen * sNow / (sThen * sThen + sNow * sNow);\n'
                     '			c.matchConfidence *= mix(1.0, overlap, min(slide, 1.0));\n'
                     '		}\n'
                     '		const bool flatMover = dot(objectShift, objectShift) > 0.0 && curvature <= kCurvedMover;\n'),
    # RT-24 sharpness study (diagnostic): the lamps' own specular highlight off, leaving each
    # tube's reflection to the traced image of its glowing mesh alone (RT2-11's double count).
    'nodirectspec': ('include/pbr_fragment.glsl',
                     '			+ texelFetch(u_DirectSpecular, directTexel, 0).rgb;',
                     '			+ vec3(0.0);'),
    # RT-24 sharpness study (diagnostic): the specular anti-aliasing widening off -- the lamps'
    # own highlight drawn with the surface's real roughness instead of the widened one.
    'nospecaa': ('include/pbr_fragment.glsl',
                 '		const float kernel = min(2.0 * variance, kKappa);',
                 '		const float kernel = 0.0;'),
    # RT-24 sharpness bisect (diagnostic): RT-21's GGX divisor floor back at 1e-4 (before 50c678f).
    'ggxfloorold': ('include/pbr_fragment.glsl',
                    '	return a2 / max(PI * denom * denom, 1e-9);',
                    '	return a2 / max(PI * denom * denom, 0.0001);'),
    # RT-24 sharpness bisect (diagnostic): the old GGX floor in ONE path at a time. The old floor
    # in the denominator is the same as capping D at roughness^4 * 1e4.
    'ggxold_direct': ('direct_trace.rvshader',
                      '	const float NDF = DistributionGGX(N, H, specRoughness);',
                      '	const float NDF = min(DistributionGGX(N, H, specRoughness),\n'
                      '						  specRoughness * specRoughness * specRoughness * specRoughness * 1.0e4);'),
    'ggxold_refl': [
        ('reflection_trace.rvshader',
         '	const float D = DistributionGGX(N, H, roughness);',
         '	const float D = min(DistributionGGX(N, H, roughness), roughness * roughness * roughness * roughness * 1.0e4);'),
        ('reflection_trace.rvshader',
         '	const float pdf = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)',
         '	const float pdf = min(DistributionGGX(N, H, roughness), roughness * roughness * roughness * roughness * 1.0e4)\n'
         '					* max(dot(N, H), 1.0e-4)'),
        ('reflection_resolve.rvshader',
         '		const float pdfHere = DistributionGGX(N, H, roughness) * max(dot(N, H), 1.0e-4)',
         '		const float pdfHere = min(DistributionGGX(N, H, roughness), roughness * roughness * roughness * roughness * 1.0e4)\n'
         '							* max(dot(N, H), 1.0e-4)'),
        ('reflection_resolve.rvshader',
         '		const float pdfTheirs = DistributionGGX(Nn, Hn, rn) * max(dot(Nn, Hn), 1.0e-4)',
         '		const float pdfTheirs = min(DistributionGGX(Nn, Hn, rn), rn * rn * rn * rn * 1.0e4)\n'
         '							  * max(dot(Nn, Hn), 1.0e-4)'),
    ],
    'ggxold_lit': ('include/pbr_fragment.glsl',
                   '			NDF = DistributionGGX(N, H, specRoughness);',
                   '			NDF = min(DistributionGGX(N, H, specRoughness),\n'
                   '					  specRoughness * specRoughness * specRoughness * specRoughness * 1.0e4);'),
    'nobakedhl_direct': ('direct_trace.rvshader',
                         '						bakedHighlight = (Db * Gb * Fb / (4.0 * p.NdotV * NdotLb + 0.0001))',
                         '						bakedHighlight = 0.0 * (Db * Gb * Fb / (4.0 * p.NdotV * NdotLb + 0.0001))'),
    'ggxold_bakeddirect': ('direct_trace.rvshader',
                           '						const float Db = DistributionGGX(p.N, Hb, p.Roughness);',
                           '						const float Db = min(DistributionGGX(p.N, Hb, p.Roughness),\n'
                           '											 p.Roughness * p.Roughness * p.Roughness * p.Roughness * 1.0e4);'),
    # RT-24 diagnostic view: every surface painted by the roughness it is shaded with, in bands,
    # and no reflection composited over it (alpha 0). Blue < 0.05, cyan 0.05-0.1, green 0.1-0.2,
    # yellow 0.2-0.3, red 0.3-0.5, grey rougher.
    'roughmap': ('include/pbr_fragment.glsl',
                 '	o_Color = vec4(color, reflectionWeight);',
                 '	{\n'
                 '		const vec3 band = roughness < 0.05 ? vec3(0.0, 0.0, 1.0)\n'
                 '						: roughness < 0.10 ? vec3(0.0, 1.0, 1.0)\n'
                 '						: roughness < 0.20 ? vec3(0.0, 1.0, 0.0)\n'
                 '						: roughness < 0.30 ? vec3(1.0, 1.0, 0.0)\n'
                 '						: roughness < 0.50 ? vec3(1.0, 0.0, 0.0) : vec3(0.5);\n'
                 '		o_Color = vec4(band * 1.2, 0.0);\n'
                 '	}'),
    # RT-24 look preview for the owner (not an engine change): every upward-facing surface --
    # the floor, and the tops of a few things -- shaded at half its material's roughness.
    'floorhalf': ('include/pbr_fragment.glsl',
                  '	float roughness = clamp(surface.Roughness, 0.045, 1.0);   // fully smooth aliases badly',
                  '	float roughness = clamp(surface.Roughness * (Ngeo.y > 0.9 ? 0.5 : 1.0), 0.045, 1.0);'),
    'nobakedhl': ('include/pbr_fragment.glsl',
                  '			Lo += bakedHighlight;',
                  '			// (diagnostic: the baked highlight left out)'),
    'ggxold_hit': [
        ('include/pbr_fragment.glsl',
         '			const vec3 H = normalize(hitV + L);\n			const float D = DistributionGGX(hitNormal, H, roughness);',
         '			const vec3 H = normalize(hitV + L);\n			const float D = min(DistributionGGX(hitNormal, H, roughness), roughness * roughness * roughness * roughness * 1.0e4);'),
        ('include/pbr_fragment.glsl',
         '			const vec3 H = normalize(hitV + hitKeptL);\n			const float D = DistributionGGX(hitNormal, H, roughness);',
         '			const vec3 H = normalize(hitV + hitKeptL);\n			const float D = min(DistributionGGX(hitNormal, H, roughness), roughness * roughness * roughness * roughness * 1.0e4);'),
    ],
    # RT-24 sharpness study (diagnostic): no fixed brightness ceiling on a texel's reflection.
    'nocap64': ('reflection_trace.rvshader',
                '	o_Reflection = vec4(min(radiance, vec3(64.0)) * tint, travelledSum / float(rays));',
                '	o_Reflection = vec4(radiance * tint, travelledSum / float(rays));'),
    # RT-24 change 2, revised: agreement as the ratio of the two blurred pictures' peaks. A 2D
    # blur's peak falls with the square of its width, so a point-like feature (a tube's core)
    # seen through widths s1 < s2 differs by 1 - (s1/s2)^2 -- the worst case over pictures.
    'roughratio': ('reflection_accumulate.rvshader',
                   '		const bool flatMover = dot(objectShift, objectShift) > 0.0 && curvature <= kCurvedMover;\n',
                   '		if (have && haveSurface)\n'
                   '		{\n'
                   '			const float slide = length((c.thenNdc - atSurface.thenNdc) * 0.5 * vec2(size));\n'
                   '			const vec4 alongHere = u_Reflection.InverseViewProjection * vec4(nowNdc, 0.5, 1.0);\n'
                   '			const vec4 alongNext = u_Reflection.InverseViewProjection\n'
                   '								 * vec4(nowNdc + vec2(2.0 / float(size.x), 0.0), 0.5, 1.0);\n'
                   '			const float pixelAngle = max(length(normalize(alongNext.xyz / alongNext.w - eye)\n'
                   '											   - normalize(alongHere.xyz / alongHere.w - eye)), 1.0e-7);\n'
                   '			const float perAlpha = 2.0 * image / ((eyeDistance + image) * pixelAngle);\n'
                   '			const float rThen = UnpackRoughness(c.extra.r);\n'
                   '			const float sThen = sqrt(rThen * rThen * rThen * rThen * perAlpha * perAlpha + 1.0);\n'
                   '			const float sNow = sqrt(roughness * roughness * roughness * roughness * perAlpha * perAlpha + 1.0);\n'
                   '			const float narrowOverWide = min(sThen, sNow) / max(sThen, sNow);\n'
                   '			c.matchConfidence *= mix(1.0, narrowOverWide * narrowOverWide, min(slide, 1.0));\n'
                   '		}\n'
                   '		const bool flatMover = dot(objectShift, objectShift) > 0.0 && curvature <= kCurvedMover;\n'),
    # RT-24 A/B: the TAA's no-history exits back to the raw jittered sample (before 2026-09-24).
    'taarawexit': [
        ('taa_resolve.rvshader',
         '		o_Color = vec4(CurrentAtCentre(uv, current.rgb), current.a);\n		// One frame seen',
         '		o_Color = current;\n		// One frame seen'),
        ('taa_resolve.rvshader',
         '		o_Color = vec4(CurrentAtCentre(uv, current.rgb), current.a);\n		// Disoccluded',
         '		o_Color = current;\n		// Disoccluded'),
    ],
    # RT-24 diagnostic: no fall back to the surface's own old place when the image's is refused.
    'nofallback': ('reflection_accumulate.rvshader',
                   '		if (!have && haveSurface && !flatMover)',
                   '		if (false)'),
}

# RT-24 A/B: change 3 undone (the reflection memory back to its travel rules), generated from
# the edit that made it, so the 'before' of a before/after is today's source minus change 3.
VARIANTS['prechange3'] = [
    ('reflection_accumulate.rvshader',
     "\tif (silhouette)\n\t\tmemory = min(memory, max(base / (1.0 + neighbourMotion / slack), kSilhouetteMemory));\n\treturn max(memory, fewest);\n}\n\n#if !defined(RV_SIGNAL_DIFFUSE)\n// **RT-24 (2026-09-24): how wide, in texels, a lobe of this roughness blurs the reflected\n// picture.** The reflected ray spreads 2*alpha radians (a reflection doubles the angle); over the\n// image distance that is a spot seen from the eye across (eyeDistance + image), measured against\n// one texel's angle at this pixel, with the pixel itself added in quadrature -- so a near mirror\n// blurs by one texel, never by less. Maths and the pixel, no tuned number.\nfloat LobeBlurTexels(float r, float image, float eyeDistance, vec2 nowNdc, ivec2 size, vec3 eye)\n{\n\tconst vec4 alongHere = u_Reflection.InverseViewProjection * vec4(nowNdc, 0.5, 1.0);\n\tconst vec4 alongNext = u_Reflection.InverseViewProjection\n\t\t\t\t\t\t * vec4(nowNdc + vec2(2.0 / float(size.x), 0.0), 0.5, 1.0);\n\tconst float pixelAngle = max(length(normalize(alongNext.xyz / alongNext.w - eye)\n\t\t\t\t\t\t\t\t\t   - normalize(alongHere.xyz / alongHere.w - eye)), 1.0e-7);\n\tconst float perAlpha = 2.0 * image / ((eyeDistance + image) * pixelAngle);\n\treturn sqrt(r * r * r * r * perAlpha * perAlpha + 1.0);\n}\n\n// **RT-24 (2026-09-24): how uncertain the image distance is here** -- the spread of the depths\n// this frame's rays found across the 3x3, in the image's own units. Wide where the reflection\n// changes depth (a pole's reflection over the wall behind it), nothing over one reflected surface.\nfloat ImageSpread(ivec2 texel, ivec2 size, float image, float travelled)\n{\n\tfloat sum = 0.0, sumSq = 0.0, count = 0.0;\n\tfor (int y = -1; y <= 1; ++y)\n\t{\n\t\tfor (int x = -1; x <= 1; ++x)\n\t\t{\n\t\t\tconst float d = texelFetch(u_Fresh, clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0).a;\n\t\t\tif (d < 0.0)\n\t\t\t\tcontinue;\n\t\t\tconst float t = min(d, 1.0e3);\n\t\t\tsum += t;\n\t\t\tsumSq += t * t;\n\t\t\tcount += 1.0;\n\t\t}\n\t}\n\tconst float mean = sum / max(count, 1.0);\n\tconst float spread = count > 1.0 ? sqrt(max(sumSq / count - mean * mean, 0.0)) : 0.0;\n\treturn spread * image / max(travelled, 1.0e-4);\n}\n\n// **RT-24 (2026-09-24): the memory a reflection may keep, from how far its old picture can be\n// misplaced -- never from how fast the camera moves.** The history is found through the image\n// point, which is only as exact as the image distance. Projected through last frame's camera at\n// the two ends of that distance's uncertainty, the two points land this many texels apart: how\n// far the old picture can sit from where it belongs, per frame. An average of n frames can then\n// be off by n times that, and it may be off by no more than the picture's own blur there\n// (LobeBlurTexels). Parked, and under a pan, both ends project to one point -- the lookup is\n// exact -- and the whole memory stands; over one flat reflected surface it barely shortens at\n// any speed; across a pole's reflection it falls to one frame. This replaces the travel-based\n// rules the reflection kind used (the hyperbola, the smear cap, the slack and the shrinking\n// floor of 2026-09-23), each of which shortened a picture's memory for moving at all.\nfloat LookupMemory(vec3 P, vec3 sight, float image, float imageSpread, float lobeBlur,\n\t\t\t\t   ivec2 size, float base)\n{\n\tconst vec4 nearThen = u_Reflection.PreviousViewProjection\n\t\t\t\t\t\t* vec4(P + sight * max(image - imageSpread, 0.0), 1.0);\n\tconst vec4 farThen = u_Reflection.PreviousViewProjection\n\t\t\t\t\t   * vec4(P + sight * (image + imageSpread), 1.0);\n\tif (nearThen.w <= 0.0 || farThen.w <= 0.0)\n\t\treturn 1.0;\n\tconst float misplaced = length((nearThen.xy / nearThen.w - farThen.xy / farThen.w)\n\t\t\t\t\t\t\t\t   * 0.5 * vec2(size));\n\treturn clamp(lobeBlur / max(misplaced, 1.0e-6), 1.0, base);\n}\n#endif\n",
     '\tif (silhouette)\n\t\tmemory = min(memory, max(base / (1.0 + neighbourMotion / slack), kSilhouetteMemory));\n\treturn max(memory, fewest);\n}\n'),
    ('reflection_accumulate.rvshader',
     '\t\t\tconst float sThen = LobeBlurTexels(UnpackRoughness(c.extra.r), image, eyeDistance, nowNdc, size, eye);\n\t\t\tconst float sNow = LobeBlurTexels(roughness, image, eyeDistance, nowNdc, size, eye);\n',
     '\t\t\tconst vec4 alongHere = u_Reflection.InverseViewProjection * vec4(nowNdc, 0.5, 1.0);\n\t\t\tconst vec4 alongNext = u_Reflection.InverseViewProjection\n\t\t\t\t\t\t\t\t * vec4(nowNdc + vec2(2.0 / float(size.x), 0.0), 0.5, 1.0);\n\t\t\tconst float pixelAngle = max(length(normalize(alongNext.xyz / alongNext.w - eye)\n\t\t\t\t\t\t\t\t\t\t\t   - normalize(alongHere.xyz / alongHere.w - eye)), 1.0e-7);\n\t\t\tconst float perAlpha = 2.0 * image / ((eyeDistance + image) * pixelAngle);\n\t\t\tconst float rThen = UnpackRoughness(c.extra.r);\n\t\t\tconst float sThen = sqrt(rThen * rThen * rThen * rThen * perAlpha * perAlpha + 1.0);\n\t\t\tconst float sNow = sqrt(roughness * roughness * roughness * roughness * perAlpha * perAlpha + 1.0);\n'),
    ('reflection_accumulate.rvshader',
     "#else\n\t\t\t// **RT-24 (2026-09-24): the memory from the lookup's own uncertainty\n\t\t\t// (LookupMemory), and the floor never above it.** The floor is what a rough\n\t\t\t// surface keeps against its grain; it may not keep more frames than its old\n\t\t\t// picture can be trusted for. (It used to shrink with the picture's travel\n\t\t\t// instead -- a speed rule.)\n\t\t\tconst float lookupMemory = LookupMemory(P, sight, image, ImageSpread(texel, size, image, travelled),\n\t\t\t\t\t\t\t\t\t\t\t\t\tLobeBlurTexels(roughness, image, eyeDistance, nowNdc, size, eye),\n\t\t\t\t\t\t\t\t\t\t\t\t\tsize, max(u_Reflection.History.y, 1.0));\n\t\t\tconst float fewest = min(mix(1.0, max(u_Reflection.History.z, 1.0), gloss), lookupMemory);\n#endif\n",
     "#else\n\t\t\t// **RT-24 (2026-09-23): and the floor shrinks as the picture moves, as the\n\t\t\t// memory above it does.** A reflection's old picture is found by where its\n\t\t\t// image was, and under a turning camera that is only approximately where it\n\t\t\t// is: a floor that kept four frames however far its picture had travelled\n\t\t\t// blended three quarters of a misplaced picture into every frame -- the haze\n\t\t\t// and the ghosts behind every reflected pole and car a fast swing left on the\n\t\t\t// wet floor. Measured with sixteen rays a texel so grain could not hide it:\n\t\t\t// the floor mid-swing came out as close to its settled picture as with the\n\t\t\t// reflection memory off. Still (the picture within a slack of where it was),\n\t\t\t// the floor is exactly what it was, and a diffuse signal's history is the\n\t\t\t// surface's own and keeps it.\n\t\t\tconst float fewest = max(mix(1.0, max(u_Reflection.History.z, 1.0), gloss)\n\t\t\t\t\t\t\t\t\t / (1.0 + max(moved - slack, 0.0) / slack), 1.0);\n#endif\n"),
    ('reflection_accumulate.rvshader',
     "#ifdef RV_SIGNAL_DIFFUSE\n\t\t\tfloat memory = ShortenedMemory(max(u_Reflection.History.y, 1.0),\n\t\t\t\t\t\t\t\t\t\t   moved, slack, fewest, silhouette,\n\t\t\t\t\t\t\t\t\t\t   neighbourMotion);\n#else\n\t\t\t// The reflection kind: the lookup's own memory, and RT-18's rule at a silhouette\n\t\t\t// whose other side moves on its own.\n\t\t\tfloat memory = lookupMemory;\n\t\t\tif (silhouette)\n\t\t\t\tmemory = min(memory, max(max(u_Reflection.History.y, 1.0) / (1.0 + neighbourMotion / slack),\n\t\t\t\t\t\t\t\t\t\t kSilhouetteMemory));\n\t\t\tmemory = max(memory, fewest);\n#endif\n",
     '\t\t\tfloat memory = ShortenedMemory(max(u_Reflection.History.y, 1.0),\n\t\t\t\t\t\t\t\t\t\t   moved, slack, fewest, silhouette,\n\t\t\t\t\t\t\t\t\t\t   neighbourMotion);\n'),
]

# RT-24 speckle diagnostic (2026-09-25): the reflection's fewest frames kept at its still value under
# any motion -- RT-24's fix 2 undone -- to measure how much of the fast-swing speckle is the memory
# falling to one frame. (Fix 2 was for the ghosting; this is a measurement, not a candidate.)
VARIANTS['nofix2'] = ('reflection_accumulate.rvshader',
    '\t\t\tconst float fewest = max(mix(1.0, max(u_Reflection.History.z, 1.0), gloss)\n\t\t\t\t\t\t\t\t\t / (1.0 + max(moved - slack, 0.0) / slack), 1.0);',
    '\t\t\tconst float fewest = mix(1.0, max(u_Reflection.History.z, 1.0), gloss);')

# RT-24 A/B (2026-09-25): fix 1 undone -- the aimed samples' light back inside the shared taps (the
# halo) and in the lane the resolve's clamp exemption reads. Run with --reflection-lamp-pool=0, so
# nothing is added back after the sharing: the "before fix 1" of the three-way comparison.
VARIANTS['prefix1'] = [
    ('reflection_trace.rvshader',
     '\t\temissiveSum = dot(kept, vec3(0.2126, 0.7152, 0.0722));',
     '\t\temissiveSum = dot(kept + aimed, vec3(0.2126, 0.7152, 0.0722));'),
    ('reflection_trace.rvshader',
     '\t\t\tglow = dot(keptMore, vec3(0.2126, 0.7152, 0.0722));',
     '\t\t\tglow = dot(keptMore + aimedMore, vec3(0.2126, 0.7152, 0.0722));'),
    ('reflection_trace.rvshader',
     '\to_Reflection = vec4(min(max(radiance - aimedMean, vec3(0.0)), vec3(64.0)) * tint,',
     '\to_Reflection = vec4(min(radiance, vec3(64.0)) * tint,'),
    ('reflection_trace.rvshader',
     '\to_Aimed = vec4(min(aimedMean, vec3(64.0)) * tint, lampDrawn.w);',
     '\to_Aimed = vec4(0.0, 0.0, 0.0, lampDrawn.w);'),
]

# RT2-0 attribution arms (2026-09-25), measurements only. (c) DirectTrace's specular left out of the
# lit pass: which copy of a lamp's shine carries the fast-swing noise. (e) TAA's same-surface depth
# test off, and at 1% instead of 2%: whether that test's width moves the edge jitter at all.
VARIANTS['nodirectspec'] = ('include/pbr_fragment.glsl',
    '\t\t\t+ texelFetch(u_DirectSpecular, directTexel, 0).rgb;',
    '\t\t\t+ vec3(0.0);')
VARIANTS['taadepthoff'] = ('taa_resolve.rvshader',
    'const float kDepthTolerance = 0.02;',
    'const float kDepthTolerance = 1.0e6;')
VARIANTS['taadepth1'] = ('taa_resolve.rvshader',
    'const float kDepthTolerance = 0.02;',
    'const float kDepthTolerance = 0.01;')



def scene_for(case, seconds):
    # The scene as it is on disk, not the committed one: the tubes' length and
    # anything else the owner has set and not yet committed must reach the arm.
    head = io.open(os.path.join(sr.SCENES, 'showroom.rage'), 'rb').read()
    if case == 'cube':
        return sr.moving_cube_scene(head, -9.0, SPEED, seconds)
    if case == 'spin':
        # RT-24: the garage as it is, its own mouse camera kept, nothing driven --
        # the owner spins round the car by hand (drag), which is how the smear shows.
        return head
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
    no_frame_memory = False
    rest = []
    for f in flags:
        if f == '--no-frame-memory':
            # The frame filter keeps nothing (both feedbacks 0): the same project edit,
            # restored byte for byte after the run.
            no_frame_memory = True
        elif f == '--no-jitter':
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
    scene = scene_for(case, seconds)
    io.open(path, 'wb').write(scene if case == 'spin' else wm.still_camera(scene))
    # The engine finds a bake by the scene file's name and this is a renamed copy,
    # so without this every arm played under live bounce light (burst.sync_bake).
    burst.sync_bake('showroom_head')
    try:
        if not sr.restore():
            sys.exit('the staged shader copies did not restore')
        edits = ([sr.NO_JITTER] if no_jitter else []) + (
            [('    TemporalFeedback: 0.9\n', '    TemporalFeedback: 0\n'),
             ('    TemporalStillFeedback: 0.98\n', '    TemporalStillFeedback: 0\n')]
            if no_frame_memory else [])
        if edits:
            text = sr.PROJECT_BYTES.decode('utf-8')
            eol = '\r\n' if '\r\n' in text else '\n'
            for was, now in edits:
                was, now = was.replace('\n', eol), now.replace('\n', eol)
                if text.count(was) != 1:
                    sys.exit('%r matched %d times' % (was.strip(), text.count(was)))
                text = text.replace(was, now)
                print('project: %s for this run' % now.strip())
            io.open(sr.PROJECT, 'wb').write(text.encode('utf-8'))
        if stage:
            # Several arms at once, joined by '+', as spin_measure.py stages them.
            triples = []
            for part in stage.split('+'):
                spec = VARIANTS[part]
                triples += spec if isinstance(spec, list) else [spec]
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
        exe_dir = os.path.join(sr.ROOT, 'build', 'bin', 'Release', 'RageVRuntime')
        common = [os.path.join(exe_dir, 'RageVRuntime.exe'),
                  '--project=' + sr.PROJECT, '--scene=scenes/' + sr.HEAD_SCENE,
                  '--rhi=vulkan', '--render-defaults=off', '--import-cache=off']
        # **Refuse to show a broken arm** (RT2-0). A variant that fails to compile drops
        # its pass or lets another variant run, and the owner would judge a picture that
        # is not the arm at all. A few frames in benchmark mode first -- which exits 3 on
        # a failed shader -- and the window opens only if they came through clean.
        # RT2-1: with the validation layers on (sync validation included), and a
        # layer message refuses the arm like a failed shader does: the owner would
        # be judging undefined behaviour. The window itself runs without them.
        pre = subprocess.run(common + ['--vsync=off', '--width=640', '--height=360', '--benchmark=3',
                                       '--validation=on'] + list(flags), cwd=exe_dir, capture_output=True,
                             text=True, errors='replace', timeout=900)
        bad = [l for l in (pre.stdout + pre.stderr).splitlines()
               if re.search(r'Shader (compilation|linking) failed|did not compile|no compute pipeline', l)]
        bad += vm.vulkan_messages(pre.stdout + pre.stderr)
        if pre.returncode != 0 or bad:
            print('NOT SHOWN: the arm did not build cleanly (exit %d)' % pre.returncode)
            print('\n'.join(bad[:12]) or (pre.stdout + pre.stderr)[-2000:])
            return
        subprocess.run(common + ['--vsync=on', '--width=1600', '--height=900']
                       # Zero seconds: no limit, the window stays until it is closed.
                       + (['--benchmark=%d' % int(seconds * 60)] if seconds > 0 else [])
                       + ([] if case == 'spin' else ['--camera=' + cam]) + list(flags),
                       cwd=exe_dir,
                       # And no timeout either when there is no limit (the owner's rule).
                       timeout=None if seconds <= 0 else seconds + 600)
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
