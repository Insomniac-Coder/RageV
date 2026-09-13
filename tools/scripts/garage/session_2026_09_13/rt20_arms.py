# -*- coding: utf-8 -*-
"""RT-20: two ways to stop the resolve's surface test firing on the jitter, as staged arms.

The cause (rt20_edges.py): with the camera parked, a pixel on an edge has its
jittered sample on one side of the edge in some frames and the other side in
others. RT-6's test compares this frame's sample with last frame's, calls every
such flip a different surface, and replaces the pixel's own history -- the
coverage blend the anti-aliasing had built -- with a neighbour's, which is pure
object or pure background. The blend never forms and the edge flickers.

  A  **a still silhouette keeps its own history.** The centre refused, the
     pixel sits on a silhouette in this frame's identity lane, and nothing in
     its 3x3 moved: the flip can only be the jitter, so the centre is kept.
     The reflection accumulator's rule, gated on stillness.
  B  **last frame's surface is still here.** The centre refused, look for the
     surface it held last frame among this frame's eight neighbours. Found,
     the edge is the same edge the jitter crossed and the centre is kept;
     not found, RT-6's search runs as before.

Library: `import rt20_arms` registers the variants 'r20A' and 'r20B' in
stage_run.VARIANTS, and TAA_VARIANT_TEXT gives the staged text of each.
"""
import os, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stage_run  # noqa: E402

TAA = stage_run.TAA

# **The arms are substitutions on HEAD's resolve, whatever the source holds.**
# patch_rt20.py has since written the kept fix into the source, so every arm
# below starts by staging the committed file whole and substitutes into that.
HEAD_TAA = (TAA, None, subprocess.run(['git', 'show', 'HEAD:RageVEditor/assets/shaders/' + TAA],
                                      cwd=stage_run.ROOT, capture_output=True, check=True).stdout.decode('utf-8'))
stage_run.VARIANTS['head'] = [HEAD_TAA]

PROTOTYPE_AT = ('ivec2 MatchingTexel(vec2 uv, vec2 pastUv, out bool found, out bool bilinear)\n{\n')
LOOP_TAIL = ('\t\t\tbilinear = (k == 0);\n\t\t\treturn at;\n\t\t}\n\t}\n\tfound = false;\n')
DEFINE_AT = 'vec3 Expand(vec3 c)     { return c / max(1.0 - c.x, 1e-4); }\n'


def keep_centre_when(condition):
    return (TAA, LOOP_TAIL,
            '\t\t\tbilinear = (k == 0);\n\t\t\treturn at;\n\t\t}\n'
            '\t\t// RT-20: the centre refused -- unless the refusal is the jitter.\n'
            '\t\tif (k == 0 && ' + condition + ')\n'
            '\t\t{\n'
            '\t\t\tg_Refusal = kKept;\n'
            '\t\t\tfound = true;\n'
            '\t\t\tbilinear = true;\n'
            '\t\t\treturn at;\n'
            '\t\t}\n'
            '\t}\n\tfound = false;\n')


# --- A: a still silhouette -----------------------------------------------------
A_DEFINITION = '''
// RT-20 (A): whether this pixel sits on a silhouette that nothing near it is
// moving across. Silhouette: a neighbour in this frame's identity lane is a
// different surface. Still: every velocity in the 3x3 -- the sea's own where
// a wave was drawn, as main() reads it -- under kStillWithinTexels.
bool StillSilhouette(vec2 uv, vec4 now)
{
	const ivec2 guideSize = textureSize(u_GuideCurrent, 0);
	const ivec2 motionSize = textureSize(u_Velocity, 0);
	const ivec2 waterSize = textureSize(u_WaterMotion, 0);
	const ivec2 guideTexel = ivec2(uv * vec2(guideSize));
	const ivec2 motionTexel = ivec2(uv * vec2(motionSize));
	const ivec2 waterTexel = ivec2(uv * vec2(waterSize));
	bool silhouette = false;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const ivec2 o = ivec2(x, y);
			const vec4 water = texelFetch(u_WaterMotion, clamp(waterTexel + o, ivec2(0), waterSize - 1), 0);
			const vec2 moved = water.z > 0.5
				? water.xy
				: texelFetch(u_Velocity, clamp(motionTexel + o, ivec2(0), motionSize - 1), 0).xy;
			if (length(moved / u_Params.TexelSize) >= kStillWithinTexels)
				return false;
			if ((x != 0 || y != 0)
				&& !Matches(now, texelFetch(u_GuideCurrent, clamp(guideTexel + o, ivec2(0), guideSize - 1), 0), false))
				silhouette = true;
		}
	}
	return silhouette;
}
'''

VARIANT_A = [
    HEAD_TAA,
    (TAA, PROTOTYPE_AT, 'bool StillSilhouette(vec2 uv, vec4 now);\n\n' + PROTOTYPE_AT),
    keep_centre_when('StillSilhouette(uv, now)'),
    (TAA, DEFINE_AT, DEFINE_AT + A_DEFINITION),
]

# --- B: last frame's surface still here ----------------------------------------
B_DEFINITION = '''// RT-20 (B): whether the surface this pixel held last frame is one of the
// surfaces around it now -- the edge the jitter crossed, still here.
bool StillAround(vec2 uv, vec4 was)
{
	const ivec2 size = textureSize(u_GuideCurrent, 0);
	const ivec2 texel = ivec2(uv * vec2(size));
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			if (x == 0 && y == 0)
				continue;
			const ivec2 at = clamp(texel + ivec2(x, y), ivec2(0), size - 1);
			if (Matches(texelFetch(u_GuideCurrent, at, 0), was, false))
				return true;
		}
	}
	return false;
}

'''

VARIANT_B = [
    HEAD_TAA,
    (TAA, PROTOTYPE_AT, B_DEFINITION + PROTOTYPE_AT),
    keep_centre_when('StillAround(uv, texelFetch(u_GuidePrevious, at, 0))'),
]

stage_run.VARIANTS['r20A'] = VARIANT_A
stage_run.VARIANTS['r20B'] = VARIANT_B

# --- C: A, and what the pixel showed last frame was not moving -------------------
# A's measured leak: a fast object moves two pixels or more past a still edge in
# one frame, the edge's 3x3 is still and holds a silhouette, and A keeps the
# pixel's history -- which is still the object. What A cannot see is that the
# surface it is keeping was moving a frame ago. So every path of the resolve
# now writes whether this pixel moved, in the sign of the count it already
# stores (negative: moved; the count itself is read through abs()), and the
# rule reads that sign at the reprojected texel. Parked nothing moves and C is
# A; behind a moving object the flag is set and C is the resolve as shipped.
VELOCITY_LINES = ('\tconst vec4 water = texture(u_WaterMotion, uv);\n'
                  '\tvec2 velocity = water.z > 0.5 ? water.xy : texture(u_Velocity, uv).xy;\n'
                  '\tvelocity.y = flip ? -velocity.y : velocity.y;\n')
LUMA_AT = '\tconst float currentLuma = ToYCoCg(current.rgb).x;\n'
WAS_STILL = '''// RT-20 (C): whether the surface this texel held last frame stood still --
// the sign of the count the resolve wrote there, negative where it moved.
bool WasStill(vec2 pastUv)
{
	if (u_Params.HasMoments < 0.5)
		return false;
	const ivec2 size = textureSize(u_Moments, 0);
	return texelFetch(u_Moments, clamp(ivec2(pastUv * vec2(size)), ivec2(0), size - 1), 0).x > 0.0;
}

'''
VARIANT_C = VARIANT_A + [
    (TAA, PROTOTYPE_AT, WAS_STILL + PROTOTYPE_AT),
    (TAA, '\t\tif (k == 0 && StillSilhouette(uv, now))\n', '\t\tif (k == 0 && StillSilhouette(uv, now) && WasStill(pastUv))\n'),
    # the velocity, read before the first early out, since every path writes the flag
    (TAA, VELOCITY_LINES, ''),
    (TAA, LUMA_AT, LUMA_AT + '\n' + VELOCITY_LINES
     + '\tconst bool movedNow = length(velocity / u_Params.TexelSize) >= kStillWithinTexels;\n'),
    (TAA, '\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  float(kNoHistory));',
     '\t\to_Moments = vec4(movedNow ? -1.0 : 1.0, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  float(kNoHistory));'),
    (TAA, '\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  offScreen ? float(kOffScreen)',
     '\t\to_Moments = vec4(movedNow ? -1.0 : 1.0, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  offScreen ? float(kOffScreen)'),
    (TAA, '\tfloat frames = min(prevMoments.x + 1.0, kMaxFrames);', '\tfloat frames = min(abs(prevMoments.x) + 1.0, kMaxFrames);'),
    (TAA, '\to_Moments = vec4(frames,', '\to_Moments = vec4(movedNow ? -frames : frames,'),
]
stage_run.VARIANTS['r20C'] = VARIANT_C

# --- probes: how often each path fires, through the benchmark's refusal line ----
# The resolve counts a neighbour-served pixel as reused, so the counters cannot
# say how often the search substituted a history. These probes count it in the
# "sky" lane instead -- zero in the garage and on the bridge, both of which
# draw their sky as geometry (RT-6.7) -- and take the pixel out of "reused".
# Pictures from a probe are not the arm's: only the counters are read.
COUNT_AT = '\tCountTemporal(true, kKept, frames);\n'
PROBE_SERVED = (TAA, COUNT_AT, '\tCountTemporal(!neighbourServed, neighbourServed ? kSkyCrossing : kKept, frames);\n')
FLIP_FLAG = [
    (TAA, PROTOTYPE_AT, 'bool g_FlipKept = false;\n\n' + PROTOTYPE_AT),
    (TAA, '\t\t\tfound = true;\n\t\t\tbilinear = true;\n', '\t\t\tg_FlipKept = true;\n\t\t\tfound = true;\n\t\t\tbilinear = true;\n'),
    (TAA, COUNT_AT, '\tCountTemporal(!g_FlipKept, g_FlipKept ? kSkyCrossing : kKept, frames);\n'),
]
stage_run.VARIANTS['r20probe_served'] = [HEAD_TAA, PROBE_SERVED]
# The kept fix, as the source holds it after patch_rt20.py: its rule's keeps,
# and the search's substitutions that are left.
stage_run.VARIANTS['r20probe_F'] = FLIP_FLAG
stage_run.VARIANTS['r20probe_Fserved'] = [PROBE_SERVED]
stage_run.VARIANTS['r20probe_A'] = VARIANT_A + FLIP_FLAG
stage_run.VARIANTS['r20probe_Aserved'] = VARIANT_A + [PROBE_SERVED]
stage_run.VARIANTS['r20probe_B'] = VARIANT_B + FLIP_FLAG
stage_run.VARIANTS['r20probe_C'] = VARIANT_C + FLIP_FLAG


def staged_text(variant):
    return stage_run.build_variant(variant)[TAA]


if __name__ == '__main__':
    # Write the two staged texts beside the build for reading, no run.
    out = os.path.join(stage_run.OUT, 'rt20')
    os.makedirs(out, exist_ok=True)
    for v in ('r20A', 'r20B', 'r20C'):
        with open(os.path.join(out, 'taa_resolve_%s.rvshader' % v), 'w', encoding='utf-8', newline='') as f:
            f.write(staged_text(v))
        print('substitutions match once each:', v)
