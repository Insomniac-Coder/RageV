#!/usr/bin/env python3
"""Break a claim on purpose, so a green check has been seen to go red (CHK.2).

**A threshold nobody has watched fail is a threshold nobody has read.** Every
check in this directory was falsified when it was written; ENGINE-NOTES 7ba
calls a claim that has never been on the wrong side of itself shape 3, and
CHK.2 put all twenty scripts through this. The table below is what it used.

The lever is cheap on purpose. The runtime loads `assets/shaders/*.rvshader`
**as source, from beside the exe**, and compiles them at launch through a
content-hashed cache -- so editing the deployed copy is the same defect a
source edit plus a rebuild produces, without the rebuild. `restore` copies the
editor's shader tree back over the runtime's, which is exactly what the build
step does, so no break can survive a run.

Claims no shader can reach (an explicit `false` rendering what silence
renders; a frame reproducing; the whole of the temporal jitter) need an engine
edit and a build; 7ba records which, and why the break is what it is.

    python tools/scripts/falsify.py list
    python tools/scripts/falsify.py <break>       # restore, then apply it
    python tools/scripts/falsify.py restore

Then run the check the break is aimed at, and read the FAIL line.
"""

import io
import pathlib
import shutil
import subprocess
import sys

import rvcheck

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'RageVEditor' / 'assets' / 'shaders'


def deployed(config='Release'):
    """The shaders the runtime actually loads, beside its exe."""
    return ROOT / 'build' / 'bin' / config / 'RageVRuntime' / 'assets' / 'shaders'


BREAKS = {
    # --- check_ssao -----------------------------------------------------------
    'ssao-zero': [('ssao_compute.rvshader',
                   'occlusion += inFront * inRange;',
                   'occlusion += 0.0 * inFront * inRange;')],
    'ssao-triple': [('ssao_compute.rvshader',
                     'occlusion += inFront * inRange;',
                     'occlusion += 3.0 * inFront * inRange;')],
    'ssao-nobias': [('ssao_compute.rvshader',
                     'const float kBias = 0.02;\nconst float kBiasPerMetre = 0.012;',
                     'const float kBias = 0.0;\nconst float kBiasPerMetre = 0.0;')],
    'ssao-dipped-kernel': [('ssao_compute.rvshader',
                            'const vec3 direction = normalize(vec3(cos(theta) * r, sin(theta) * r,\n\t\t\t\t\t\t\t\t\t\t\t  0.4 + 0.6 * (1.0 - r)));',
                            'const vec3 direction = normalize(vec3(cos(theta) * r, sin(theta) * r,\n\t\t\t\t\t\t\t\t\t\t\t  -0.4 + 0.6 * (1.0 - r)));')],
    'ssao-flat-intensity': [('ssao_apply.rvshader',
                             'pow(clamp(ao, 0.0, 1.0), u_Params.Intensity)',
                             'pow(clamp(ao, 0.0, 1.0), 1.0)')],
    'ssao-written-normal': [('ssao_compute.rvshader',
                             'if (dot(written, normal) > kAgreement)',
                             'if (dot(written, normal) > -2.0)')],
    'ssao-normal-z-sign': [('ssao_compute.rvshader',
                            'return dot(normal, centre) > 0.0 ? -normal : normal;',
                            'return normal.z < 0.0 ? -normal : normal;')],

    'rtao-no-hits': [('rtao_compute.rvshader',
                      'occlusion += 1.0;',
                      'occlusion += 0.0;')],
    'rtao-triple': [('rtao_compute.rvshader',
                     'occlusion += 1.0;',
                     'occlusion += 3.0;')],
    'rtao-no-guard': [('rtao_compute.rvshader',
                       'if (dot(direction, geometric) < 0.0)\n\t\t\tcontinue;',
                       'if (false)\n\t\t\tcontinue;')],

    # --- check_ssr ------------------------------------------------------------
    'ssr-nohit': [('ssr_trace.rvshader', '\tif (!hit)\n', '\tif (true)\n')],
    'ssr-double': [('ssr_resolve.rvshader',
                    'o_Color = vec4(radiance / weight, weight);',
                    'o_Color = vec4(2.0 * radiance / weight, weight);')],
    'ssr-ignore-roughness': [
        ('ssr_resolve.rvshader',
         'const float radius = roughness * roughness * kMaxBlurPixels;',
         'const float radius = 0.0;'),
        ('ssr_trace.rvshader',
         'const float roughFade = 1.0 - smoothstep(0.55, 0.9, roughness);',
         'const float roughFade = 1.0;')],
    'ssr-roughness-lost': [('include/pbr_fragment.glsl',
                            'float roughness = clamp(surface.Roughness, 0.045, 1.0);',
                            'float roughness = 0.045;')],
    'ssr-miss-glow': [('ssr_resolve.rvshader',
                       'if (weight <= 0.0)\n\t{\n\t\to_Color = vec4(0.0);',
                       'if (weight <= 0.0)\n\t{\n\t\to_Color = vec4(1.0);')],
    'ssr-noflip': [('ssr_trace.rvshader',
                    'const bool flip = u_Params.FlipY > 0.5;',
                    'const bool flip = false;')],
    'ssr-rt-reversed': [('include/pbr_fragment.glsl',
                         'vec3 traced = TraceReflection(v_WorldPos, normalize(v_Normal), reflect(-V, N));',
                         'vec3 traced = TraceReflection(v_WorldPos, normalize(v_Normal), -reflect(-V, N));')],

    # --- check_motion_blur ----------------------------------------------------
    'mb-no-velocity': [('motionblur_pack.rvshader',
                        'vec2 velocity = texture(u_Velocity, uv).xy;',
                        'vec2 velocity = vec2(0.0);')],
    'mb-triple-velocity': [('motionblur_pack.rvshader',
                            'vec2 velocity = texture(u_Velocity, uv).xy;',
                            'vec2 velocity = 3.0 * texture(u_Velocity, uv).xy;')],
    'mb-fixed-shutter': [
        ('motionblur_gather.rvshader',
         'texture(u_NeighborMax, uv).xy * u_Params.Shutter;',
         'texture(u_NeighborMax, uv).xy * 0.5;'),
        ('motionblur_gather.rvshader',
         'const float halfC = min(length(packedC.xy / texel) * u_Params.Shutter * 0.5,',
         'const float halfC = min(length(packedC.xy / texel) * 0.5 * 0.5,'),
        ('motionblur_gather.rvshader',
         'const float halfT = min(length(packedT.xy / texel) * u_Params.Shutter * 0.5,',
         'const float halfT = min(length(packedT.xy / texel) * 0.5 * 0.5,')],
    'mb-swap-axes': [('motionblur_pack.rvshader',
                      'o_Color = vec4(velocity, depth, 0.0);',
                      'o_Color = vec4(velocity.yx, depth, 0.0);')],
    'mb-velocity-floor': [('motionblur_pack.rvshader',
                           'o_Color = vec4(velocity, depth, 0.0);',
                           'o_Color = vec4(velocity + vec2(0.0, 0.004), depth, 0.0);')],

    # --- check_oit ------------------------------------------------------------
    'oit-noflip': [('oit_resolve.rvshader',
                    'vec2 uv = vec2(v_UV.x, u_Params.FlipY > 0.5 ? 1.0 - v_UV.y : v_UV.y);',
                    'vec2 uv = v_UV;')],
    # A control, not a claim: does an edit to this shader reach the picture at
    # all? Flattening the weight changed nothing, which is either a check that
    # cannot fail or a lever that does not pull.
    'oit-weighted-red': [('particle_weighted.rvshader',
                          'o_Accumulate = vec4(color.rgb * color.a, color.a) * weight;',
                          'o_Accumulate = vec4(1.0, 0.0, 0.0, color.a) * weight;')],
    'oit-flat-weight': [('particle_weighted.rvshader',
                         'float weight = color.a * clamp(kUnitWeightDistance / max(d, 1e-3), 1e-2, 3e3);',
                         'float weight = color.a * 1.0;')],

    # --- check_color_grading --------------------------------------------------
    'grade-half-texel': [('tonemap.rvshader',
                          'vec3 coord = (clamp(color, 0.0, 1.0) * (n - 1.0) + 0.5) / n;',
                          'vec3 coord = clamp(color, 0.0, 1.0);')],
    'grade-lut-ignored': [('tonemap.rvshader',
                           'color = mix(color, graded, clamp(u_Params.LutStrength, 0.0, 1.0));',
                           'color = mix(color, graded, 0.0);')],
    'grade-noflip': [('tonemap.rvshader',
                      'vec2 uv = vec2(v_UV.x, u_Params.FlipY > 0.5 ? 1.0 - v_UV.y : v_UV.y);',
                      'vec2 uv = v_UV;')],

    # --- check_depth_of_field -------------------------------------------------
    'dof-zero-coc': [('dof_prepass.rvshader',
                      'return clamp(pixels, -u_Params.MaxRadius, u_Params.MaxRadius);',
                      'return 0.0;')],
    'dof-triple-coc': [('dof_prepass.rvshader',
                        'const float pixels = coc / kSensorHeight * u_Params.FrameHeight;',
                        'const float pixels = 3.0 * coc / kSensorHeight * u_Params.FrameHeight;')],
    'dof-tenfold-coc': [('dof_prepass.rvshader',
                         'const float pixels = coc / kSensorHeight * u_Params.FrameHeight;',
                         'const float pixels = 10.0 * coc / kSensorHeight * u_Params.FrameHeight;')],
    'dof-no-aperture': [('dof_prepass.rvshader',
                         '* (f * f) / max(u_Params.FNumber * (d - f), 1.0e-6);',
                         '* (f * f) / max(1.4 * (d - f), 1.0e-6);')],
    'dof-bleed': [('dof_gather.rvshader',
                   'const float capped = tap.a < centreCoC\n\t\t\t\t\t\t   ? tapRadius\n\t\t\t\t\t\t   : min(tapRadius, centreRadius);',
                   'const float capped = tapRadius;')],

    # --- check_auto_exposure --------------------------------------------------
    'ae-metering-ignored': [('tonemap.rvshader',
                             'color *= u_Params.Exposure * u_Exposure.Exposure;',
                             'color *= u_Params.Exposure;')],
    'ae-no-compensation': [('tonemap.rvshader',
                            'color *= u_Params.Exposure * u_Exposure.Exposure;',
                            'color *= u_Exposure.Exposure;')],
    'ae-overcorrect': [('tonemap.rvshader',
                        'color *= u_Params.Exposure * u_Exposure.Exposure;',
                        'color *= u_Params.Exposure * u_Exposure.Exposure * u_Exposure.Exposure;')],

    # --- check_lens_effects ---------------------------------------------------
    'lens-vignette-inverted': [('tonemap.rvshader',
                                'color *= mix(1.0, 1.0 - u_Params.Vignette, falloff);',
                                'color *= mix(1.0 - u_Params.Vignette, 1.0, falloff);')],
    'lens-aberration-dead': [('tonemap.rvshader',
                              'if (u_Params.Aberration > 0.0)',
                              'if (false)')],
    # **Two overdrives, added by the CHK.3 audit.** Every lens break above
    # switches an effect *off* or inverts it, and claim 2 is `changed == 0` --
    # so nothing in this file has ever asked how *much* the vignette and the
    # aberration do. These two are the shape-1 question for them.
    'lens-vignette-everywhere': [
        ('tonemap.rvshader',
         'float falloff = smoothstep(1.0 - u_Params.VignetteSmoothness, 1.0, d);',
         'float falloff = smoothstep(0.0, 1.0, d);')],
    'lens-aberration-wide': [
        ('tonemap.rvshader',
         'vec2 offset = toCentre * u_Params.Aberration;',
         'vec2 offset = toCentre * u_Params.Aberration * 12.0;')],
    # And the corner band's ceiling: the vignette multiplied in twice, which
    # is what a second call site or a doubled pass would do.
    'lens-vignette-twice': [
        ('tonemap.rvshader',
         'color *= mix(1.0, 1.0 - u_Params.Vignette, falloff);',
         'color *= mix(1.0, 1.0 - u_Params.Vignette, falloff);'
         '\n\t\tcolor *= mix(1.0, 1.0 - u_Params.Vignette, falloff);')],
    'lens-grain-white-noise': [('tonemap.rvshader',
                                'vec3 grain = GrainField(gl_FragCoord.xy / max(u_Params.GrainSize, 1.0),',
                                'vec3 grain = GrainField(gl_FragCoord.xy * 64.0 / max(u_Params.GrainSize, 1.0),')],
    'lens-grain-blocks': [('tonemap.rvshader',
                           'vec3 grain = GrainField(gl_FragCoord.xy / max(u_Params.GrainSize, 1.0),',
                           'vec3 grain = GrainField(floor(gl_FragCoord.xy / max(u_Params.GrainSize, 1.0)),')],
    'lens-grain-blurred': [('tonemap.rvshader',
                            'vec3 grain = GrainField(gl_FragCoord.xy / max(u_Params.GrainSize, 1.0),',
                            'vec3 grain = GrainField(0.12 * gl_FragCoord.xy / max(u_Params.GrainSize, 1.0),')],
    'lens-grain-static': [('tonemap.rvshader',
                           'uint(u_Params.Frame));',
                           'uint(0));')],
    # --- check_tangent_frame --------------------------------------------------
    # The parity fixture's whole point: a frame rotated 180 degrees about the
    # geometric normal, which is what a flipped dFdy produces. Rendered by
    # hand into a PNG and handed to the check.
    'tangent-flipped': [('include/pbr_fragment.glsl',
                         'return normalize(TBN * tangentNormal);',
                         'return normalize(TBN * vec3(-tangentNormal.xy, tangentNormal.z));')],

    # --- check_ray_shadows ----------------------------------------------------
    'rs-reversed-ray': [('include/pbr_fragment.glsl',
                         '0xFFu, worldPos + Ng * offset, 0.0, L, tMax);',
                         '0xFFu, worldPos + Ng * offset, 0.0, -L, tMax);')],
    'rs-endless-ray': [('include/pbr_fragment.glsl',
                        '0xFFu, worldPos + Ng * offset, 0.0, L, tMax);',
                        '0xFFu, worldPos + Ng * offset, 0.0, L, 1.0e4);')],
    # Two functions compute the same offset (the shadow ray's, and the
    # reflection ray's shadow toward the sun); this is the shadow ray's,
    # anchored on the line before it, which names L.
    'rs-no-offset': [('include/pbr_fragment.glsl',
                      'float slope = sqrt(1.0 - NgdotL * NgdotL) / max(NgdotL, 0.15);\n\tfloat offset = 0.002 * (1.0 + min(slope, 4.0));',
                      'float slope = sqrt(1.0 - NgdotL * NgdotL) / max(NgdotL, 0.15);\n\tfloat offset = 0.0;')],

    # --- check_terrain (the layered surface) ----------------------------------
    'terrain-no-normalise': [('include/pbr_fragment.glsl',
                              'weight = sum > 1e-4 ? weight / sum : vec4(1.0, 0.0, 0.0, 0.0);',
                              'weight = sum > 1e-4 ? weight : vec4(1.0, 0.0, 0.0, 0.0);')],
    'terrain-layer-swap': [('include/pbr_fragment.glsl',
                            'vec4 weight = texture(u_Weights, wuv);',
                            'vec4 weight = texture(u_Weights, wuv).yxzw;')],

    # --- check_bindless -------------------------------------------------------
    'bindless-normal-slot': [('include/pbr_fragment.glsl',
                              '#define u_NormalMap    u_Textures[nonuniformEXT(g_Material.Maps0.y)]',
                              '#define u_NormalMap    u_Textures[nonuniformEXT(g_Material.Maps0.x)]')],
    'bindless-bad-index': [('include/pbr_fragment.glsl',
                            '#define u_BaseColorMap u_Textures[nonuniformEXT(g_Material.Maps0.x)]',
                            '#define u_BaseColorMap u_Textures[nonuniformEXT(g_Material.Maps0.x + 90000u)]')],

    # --- check_depth_sort -----------------------------------------------------
    # Early-z is what the speed-up claim measures; a shader that writes depth
    # turns it off without changing a pixel, which is the failure the check's
    # own message names.
    'sort-writes-depth': [('include/pbr_fragment.glsl',
                           'o_Color = vec4(color, baseColor.a);',
                           'o_Color = vec4(color, baseColor.a);\n\tgl_FragDepth = gl_FragCoord.z;')],

    # --- check_gi -------------------------------------------------------------
    'gi-ss-zero': [('ssgi_compute.rvshader',
                    'const vec3 irradiance = weight > 1.0e-4 ? gathered / weight : vec3(0.0);',
                    'const vec3 irradiance = vec3(0.0);')],
    'gi-ss-quadruple': [('ssgi_compute.rvshader',
                         'const vec3 irradiance = weight > 1.0e-4 ? gathered / weight : vec3(0.0);',
                         'const vec3 irradiance = weight > 1.0e-4 ? 4.0 * gathered / weight : vec3(0.0);')],
    'gi-ss-off-screen-taps': [('ssgi_compute.rvshader',
                               'if (any(lessThan(sampleUv, vec2(0.0))) || any(greaterThan(sampleUv, vec2(1.0))))\n\t\t\tcontinue;',
                               'if (false)\n\t\t\tcontinue;')],
    'gi-ss-fixed-taps': [('ssgi_compute.rvshader',
                          'int TapCount() { return int(u_Params.A + 0.5); }',
                          'int TapCount() { return 24; }')],
    'gi-traced-zero': [('include/pbr_fragment.glsl',
                        'bounced += ShadeTraced(first, arriving);',
                        'bounced += vec3(0.0);')],
    'gi-traced-triple': [('include/pbr_fragment.glsl',
                          'bounced += ShadeTraced(first, arriving);',
                          'bounced += 3.0 * ShadeTraced(first, arriving);')],
    'gi-sky-on-miss': [('include/pbr_fragment.glsl',
                        'if (first.Missed)\n\t\t\t\tcontinue;',
                        'if (first.Missed)\n\t\t\t{\n\t\t\t\tbounced += first.Sky;\n\t\t\t\tcontinue;\n\t\t\t}')],
    'gi-one-bounce': [('include/pbr_fragment.glsl',
                       'if (giBounces >= 2)',
                       'if (false)')],
    'gi-flat-second-bounce': [('include/pbr_fragment.glsl',
                               'arriving = second.Missed\n\t\t\t\t\t\t ? second.Sky\n\t\t\t\t\t\t : ShadeTraced(second, ProbeIrradiance(second.Normal));',
                               'arriving = arriving * 1.08;')],
    'gi-no-denoise-history': [('gi_denoise.rvshader',
                               'vec3 result = mix(current.rgb, history.rgb, u_Params.Feedback);',
                               'vec3 result = current.rgb;')],
    'gi-intensity-ignored': [('include/pbr_fragment.glsl',
                              'indirectTerm = max(bounced.rgb, vec3(0.0)) * bounced.a * u_Scene.Indirect.x;',
                              'indirectTerm = max(bounced.rgb, vec3(0.0)) * bounced.a;')],

    # --- check_gi, the voxel form (8.1, ENGINE-NOTES 7bc) --------------------
    # Each is one of the three findings that moved the numbers, put back.
    # The cones lifted by the finest voxel alone, whatever cascade the point is
    # in: every wall on a coarser cascade's edge reads itself through the first
    # step; off screen +0.36 -> **+0.21**, so claim 13 fails its floor of 0.3.
    # (Measured by running it; 7bc first recorded +0.08 from a build partway
    # through the work, and every reading in this block is now the shipped
    # one.) It also takes the reproducibility floor to 2-3 levels.
    'voxel-no-lift': [('include/voxel_cone.glsl',
                       'const float voxel = VoxelSize(VoxelCascadeAt(surface));',
                       'const float voxel = VoxelSize(0);')],
    # Thirty-degree cones: the footprint spans the wall and the floor beside
    # it, and off screen the bounce falls to **+0.11** against the floor of 0.3
    # (claim 13; recorded as +0.04 before it was run).
    'voxel-wide-cones': [('include/voxel_cone.glsl',
                          'const float kRatio = 0.577;',
                          'const float kRatio = 1.1547;')],
    # The directional chain sampled as if isotropic: every face reads as the
    # +X one, so a wall is as thin as it is whichever way the cone looks and
    # the leak returns. It fails **claim 14**, at 2.04 of the traced form's red
    # against a ceiling of 1.5 -- not claim 13, which this entry used to say:
    # the leak brightens the near corner more than it dims the off-screen
    # bounce, and off screen still clears 0.3.
    'voxel-iso-faces': [('include/voxel_cone.glsl',
                         'const int face = axis * 2 + (direction[axis] > 0.0 ? 1 : 0);',
                         'const int face = 1;')],
    # The injection without the sun's shadow: shadowed voxels light up.
    # **This one is not caught, and the fixture is why.** Run through
    # check_gi it moves the near brightness from +23.81 to +23.93 and the red
    # ratio not at all -- because on `gi_corner` the sun lights essentially
    # everything that contributes a bounce, so there is no shadowed surface for
    # the break to wrongly light. Claim 14's ceiling is not too wide; the scene
    # cannot see the defect. Catching it needs a fixture with a caster between
    # the sun and the bouncing wall, and until there is one the injection's
    # shadow term is unguarded -- stated here rather than implied by a break
    # that only ever trips the reproducibility band by perturbing the grid.
    # Not a claim's break but a *guard's*: rvcheck.require_drawn refuses a
    # frame nothing was drawn into. This compiles cleanly and renders black,
    # which is precisely the state 27 check_gi frames were in while every
    # claim measured +0.00 off them and reported it (ENGINE-NOTES 7be).
    'lit-black': [('include/pbr_fragment.glsl',
                   'o_Color = vec4(color, baseColor.a);',
                   'o_Color = vec4(0.0, 0.0, 0.0, baseColor.a);')],

    # check_graph claim 1 (8.10): the fixture graph's exec link is cut, so
    # On Create never reaches Set Field. **The graph still generates** -- an
    # unreached statement is a warning, not an error -- and the C# still
    # compiles, and the script still attaches, and it does nothing. That is
    # the exact shape of failure the whole check exists for, and no amount of
    # reading the generated text would catch it.
    #
    # The break is in the *fixture writer* rather than the .rvgraph, because
    # check_graph regenerates the fixture on every run and would overwrite an
    # edit to the asset before reading it.
    'graph-no-exec': [('check:make_graph_scene.py',
                       '''  - Id: 1
    From: [1, 0]
    To: [2, 0]
''', '')],

    # check_graph's content claim (CHK.3). The regression it is drawn from
    # really happened: a rename left two fixtures naming a node type that no
    # longer existed. The loader **drops** an unknown node and generates
    # anyway, so this does not stop `Roster.g.cs` being written -- it makes it
    # be written empty, which is why the claim had to become "what is
    # committed" rather than "a file exists".
    'graph-stale-node': [('asset:SampleProject/assets/graphs/Roster.rvgraph',
                          '    Type: NumbersAdd',
                          '    Type: NumbersAppend')],

    'voxel-no-shadow': [('voxel_inject.rvshader',
                         'const float shadow = light.Params.w > 0.5 ? CascadeShadow(world, N, L) : 1.0;',
                         'const float shadow = 1.0;')],
    # **Claim 15 has no break of its own here, and both attempts at one are
    # worth recording rather than a third guess.** Its floor half asks that the
    # voxel path stay as reproducible as 7bc measured; its radius half asks
    # that moving `GiRadius` do no more than relaunching does.
    #
    # The radius half cannot be broken with this lever, and that is the claim
    # rather than a gap: `GiRadius` reaches the voxel gather through no path at
    # all -- nowhere in `VoxelGI.cpp`, nowhere in `voxelgi_gather`, and the
    # graph hands it only to `PostProcess::SsgiCompute`. Patching a deployed
    # shader cannot make an absent plumbing route exist.
    #
    # The floor half had two breaks written for it and both survived, which is
    # how 7bc's diagnosis came to be wrong twice: every fragment writing in all
    # three axis passes (more writers, but carrying the same value, so which
    # one wins does not matter), and then every fragment writing a *different*
    # albedo keyed off its screen position (writers that disagree violently --
    # floor unmoved at 1 level over 0.7%, picture unmoved at +1.77 against
    # +1.78). Both are deleted rather than kept, by the rule CHK.2 set: a break
    # that does not break is not evidence. What the floor does have is
    # `voxel-no-lift` below, under which it reads 2 to 3 levels and fails -- so
    # it moves when the grid's sampling moves, even though nothing yet written
    # here moves it on purpose.

    # --- check_theme_contrast -------------------------------------------------
    # The palette *is* the thing under test, so the break is a colour: the
    # secondary grey that reads on charcoal, put on white unchanged -- the
    # exact mistake the file's own docstring names.
    'theme-light-secondary': [('check:check_theme_contrast.py',
                               '"TextSecondary": "#55555F",',
                               '"TextSecondary": "#A2A2B0",')],

    # --- check_smaa / check_taa_motion / check_taa_jitter ---------------------
    'smaa-passthrough': [('smaa_blend.rvshader',
                          'o_Color = vec4(first + second - centre, 1.0);',
                          'o_Color = vec4(centre, 1.0);')],
    'smaa-blur-flat': [('smaa_blend.rvshader',
                        'vec3 first  = texture(u_Source, uv + towardsA * weightA).rgb;\n\tvec3 second = texture(u_Source, uv + towardsB * weightB).rgb;',
                        'vec3 first  = texture(u_Source, uv + towardsA * 0.5).rgb;\n\tvec3 second = texture(u_Source, uv + towardsB * 0.5).rgb;')],
    # Blurring every pixel does *not* move a flat region: the two taps and the
    # centre are the same colour there, and the model's weights sum to one. A
    # kernel whose weights do not is what actually moves it.
    'smaa-unnormalised': [('smaa_blend.rvshader',
                           'o_Color = vec4(first + second - centre, 1.0);',
                           'o_Color = vec4(first + second - 0.9 * centre, 1.0);')],
    # The flat-region claim is held by the blend pass's *explicit* passthrough
    # -- its comment names the claim -- not by the kernel's normalisation.
    # Remove it alone, and then together with the unnormalised kernel.
    'smaa-no-passthrough': [('smaa_blend.rvshader',
                             'if (top + bottom + left + right < 1e-5)\n\t{\n\t\to_Color = vec4(centre, 1.0);\n\t\treturn;\n\t}',
                             'if (false)\n\t{\n\t\to_Color = vec4(centre, 1.0);\n\t\treturn;\n\t}')],
    'smaa-no-passthrough-unnormalised': [
        ('smaa_blend.rvshader',
         'if (top + bottom + left + right < 1e-5)\n\t{\n\t\to_Color = vec4(centre, 1.0);\n\t\treturn;\n\t}',
         'if (false)\n\t{\n\t\to_Color = vec4(centre, 1.0);\n\t\treturn;\n\t}'),
        ('smaa_blend.rvshader',
         'o_Color = vec4(first + second - centre, 1.0);',
         'o_Color = vec4(first + second - 0.9 * centre, 1.0);')],
    'smaa-noflip': [('smaa_edges.rvshader',
                     'vec2 up    = vec2(0.0, texel.y * (flip ? -1.0 : 1.0));',
                     'vec2 up    = vec2(0.0, texel.y);'),
                    ('smaa_blend.rvshader',
                     'vec2 up    = vec2(0.0, texel.y * (flip ? -1.0 : 1.0));',
                     'vec2 up    = vec2(0.0, texel.y);')],
    'ssaa-point-resolve': [('ssaa_resolve.rvshader',
                            'o_Color = total / float(factor * factor);',
                            'o_Color = texture(u_Source, origin);')],
    'taa-no-history': [('taa_resolve.rvshader',
                        'if (u_Params.HasHistory < 0.5)',
                        'if (true)')],
    'taa-reversed-velocity': [('taa_resolve.rvshader',
                               'vec2 historyUV = uv - velocity;',
                               'vec2 historyUV = uv + velocity;')],
    'taa-noflip-velocity': [('taa_resolve.rvshader',
                             'velocity.y = flip ? -velocity.y : velocity.y;',
                             'velocity.y = velocity.y;')],

    'lens-midtone-curve': [('tonemap.rvshader',
                            'float response = sqrt(max(4.0 * luma * (1.0 - luma), 0.0));',
                            'float response = 1.0 - luma * 0.5;')],
}

def restore(config='Release'):
    """The editor's shader tree over the runtime's -- what the build does."""
    live = deployed(config)
    for path in SOURCE.rglob('*'):
        if path.is_file():
            target = live / path.relative_to(SOURCE)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    # A break that lives in a check script comes back from git -- **that file
    # and only that file.** Reverting the whole directory took an uncommitted
    # threshold edit and this tool's own in-progress edits with it.
    scripts = sorted({path
                      for edits in BREAKS.values()
                      for filename, _, _ in edits
                      for path in [from_git(filename)]
                      if path})

    # A `.rvgraph` break leaves its generated C# holding what the broken graph
    # produced -- the generator overwrites that file, and `git checkout` of the
    # asset alone does not reach it. It is the same trap as the untracked-file
    # one below, one file along.
    scripts += [f'SampleProject/Scripts/Generated/{path.rsplit("/", 1)[-1][:-len(".rvgraph")]}.g.cs'
                for path in list(scripts) if path.endswith('.rvgraph')]
    scripts = sorted(set(scripts))
    #
    # **Checked, not fired and forgotten.** `git checkout` on a file git does
    # not track succeeds at doing nothing, and this used to ignore that: the
    # break stayed applied, the marker below was removed anyway, and the next
    # check measured a deliberately broken fixture with nothing to say it was
    # one. A new fixture is untracked exactly when it is newest, which is when
    # somebody is most likely to be falsifying it.
    for relative in scripts:
        tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', relative],
                                 cwd=str(ROOT), capture_output=True)
        if tracked.returncode != 0:
            print(f'FAIL: {relative} is not tracked by git, so this cannot put it '
                  f'back. The break is still applied. Commit the file, or undo the '
                  f'edit by hand.')
            sys.exit(1)
        if subprocess.run(['git', 'checkout', '--', relative],
                          cwd=str(ROOT)).returncode != 0:
            print(f'FAIL: could not restore {relative}; the break is still applied.')
            sys.exit(1)

    # Last, and only once every edit really is back: a marker removed while a
    # break survives is worse than no marker at all, because the next check
    # then reports a broken renderer as a clean one.
    marker = live / rvcheck.FALSIFY_MARKER
    if marker.is_file():
        marker.unlink()


def target(filename, config='Release'):
    """A deployed shader by default; `check:<name>` for a script under tools;
    `asset:<repo-relative path>` for anything the project ships."""
    if filename.startswith('check:'):
        return ROOT / 'tools' / 'scripts' / filename[len('check:'):]
    if filename.startswith('asset:'):
        return ROOT / filename[len('asset:'):]
    return deployed(config) / filename


def from_git(filename):
    """The repo-relative path of a break that git has to put back, or None."""
    if filename.startswith('check:'):
        return 'tools/scripts/' + filename[len('check:'):]
    if filename.startswith('asset:'):
        return filename[len('asset:'):]
    return None


def apply(name, config='Release'):
    restore(config)
    for filename, old, new in BREAKS[name]:
        path = target(filename, config)
        raw = io.open(path, encoding='utf-8', newline='').read()
        crlf = '\r\n' in raw
        s = raw.replace('\r\n', '\n')
        if s.count(old) != 1:
            print(f"FAIL: {filename} has {s.count(old)} of that text, not one -- "
                  f"the break has drifted from the shader")
            sys.exit(1)
        s = s.replace(old, new, 1)
        io.open(path, 'w', encoding='utf-8', newline='').write(
            s.replace('\n', '\r\n') if crlf else s)
        print(f'  {filename}: {old[:60]!r}')

    # Name the break where a check will find it. `rvcheck.require_current_shaders`
    # reads this and prints a banner instead of refusing -- a deliberate break
    # is not staleness -- so a forgotten `restore` can no longer be mistaken
    # for a clean measurement.
    live = deployed(config)
    live.mkdir(parents=True, exist_ok=True)
    io.open(live / rvcheck.FALSIFY_MARKER, 'w', encoding='utf-8').write(name)


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ('-h', '--help'):
        print(__doc__)
        return
    argument = sys.argv[1]
    config = sys.argv[2] if len(sys.argv) > 2 else 'Release'
    if argument == 'restore':
        restore(config)
        print('the shipped shaders are back')
    elif argument == 'list':
        for name in BREAKS:
            print(name)
    elif argument not in BREAKS:
        print(f"FAIL: no break called {argument!r}; `list` prints them all")
        sys.exit(1)
    else:
        print(f'break: {argument}')
        apply(argument, config)


if __name__ == '__main__':
    main()
