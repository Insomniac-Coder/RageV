# -*- coding: utf-8 -*-
"""RT-20: the resolve keeps its own history where a refusal is only the jitter.

Writes the measured fix (rt20_arms.py's C, with the surfaces' own motion in
place of the composite's lane) into RageVEditor/assets/shaders/taa_resolve.rvshader.
Every substitution must match exactly once; the file keeps its CRLF.
"""
import io, os, sys

ROOT = r'C:\Users\ism19\Code\RageV'
P = os.path.join(ROOT, 'RageVEditor', 'assets', 'shaders', 'taa_resolve.rvshader')
raw = io.open(P, encoding='utf-8', newline='').read()
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')
if 'JitterCrossing' in s:
    sys.exit('already patched')

SUBS = []

# --- the count's sign, documented where the attachment is ------------------------
SUBS.append((
    '//   .x  frames accumulated here, capped\n',
    '//   .x  frames accumulated here, capped -- **and negative where this pixel\'s\n'
    '//       surface moved this frame** (RT-20: JitterCrossing reads the sign a\n'
    '//       frame later), so the count itself is only ever read through abs()\n'))

# --- binding 10 -------------------------------------------------------------------
SUBS.append((
    'layout(set = 0, binding = 9) uniform sampler2D u_WaterMotion;\n',
    'layout(set = 0, binding = 9) uniform sampler2D u_WaterMotion;\n'
    '// **RT-20: the surfaces\' own motion** -- the scene\'s velocity lane. Where the\n'
    '// reflection composite ran, `u_Velocity` above is *its* lane, in which a pixel\n'
    '// made mostly of a reflection moves by its virtual image (RT-6.1): the right\n'
    '// motion to fetch a history with, and the wrong one to ask whether a surface\n'
    '// moved. A flat mirror sliding along itself has an image that stands still,\n'
    '// and a chrome cube crossing the garage read as standing still over most of\n'
    '// its face in that lane (2026-09-13). Read only by SurfaceMotionTexels.\n'
    '// Point sampled, like the other.\n'
    'layout(set = 0, binding = 10) uniform sampler2D u_SurfaceVelocity;\n'))

# --- MatchingTexel: the exception, before the search --------------------------------
SUBS.append((
    '// **The reprojected texel, or the nearest neighbour that is this surface,\n'
    '// or nothing.** Returns the texel to read the history from and whether one\n'
    '// was found; `bilinear` says whether the centre itself matched, because\n'
    '// only then are the texels between it and its neighbours this surface too.\n',
    '// **The reprojected texel, or the nearest neighbour that is this surface,\n'
    '// or nothing.** Returns the texel to read the history from and whether one\n'
    '// was found; `bilinear` says whether the centre itself matched, because\n'
    '// only then are the texels between it and its neighbours this surface too.\n'
    '// **RT-20:** and the reprojected texel whatever the test said, where the\n'
    '// only thing that changed is which side of an edge the jitter sampled.\n'))
SUBS.append((
    'ivec2 MatchingTexel(vec2 uv, vec2 pastUv, out bool found, out bool bilinear)\n{\n',
    '// RT-20: defined further down, beside the stillness threshold it needs.\n'
    'bool JitterCrossing(vec2 uv, vec2 pastUv, vec4 now);\n\n'
    'ivec2 MatchingTexel(vec2 uv, vec2 pastUv, out bool found, out bool bilinear)\n{\n'))
SUBS.append((
    '\t\t\tbilinear = (k == 0);\n\t\t\treturn at;\n\t\t}\n\t}\n\tfound = false;\n',
    '\t\t\tbilinear = (k == 0);\n\t\t\treturn at;\n\t\t}\n'
    '\t\t// **RT-20: unless the refusal is only the jitter\'s.** Asked before the\n'
    '\t\t// search, because the search is what replaced this pixel\'s blend with a\n'
    '\t\t// neighbour\'s -- see JitterCrossing.\n'
    '\t\tif (k == 0 && JitterCrossing(uv, pastUv, now))\n'
    '\t\t{\n'
    '\t\t\tg_Refusal = kKept;\n'
    '\t\t\tfound = true;\n'
    '\t\t\tbilinear = true;\n'
    '\t\t\treturn at;\n'
    '\t\t}\n'
    '\t}\n\tfound = false;\n'))

# --- the rule, beside kStillWithinTexels ----------------------------------------------
RULE = r'''
// RT-20: how far the surface `offset` texels from `uv` moved since last frame,
// in texels of this target -- the sea's own motion where a wave was drawn
// (RT-8), the scene's otherwise, and never the reflection composite's.
float SurfaceMotionTexels(vec2 uv, ivec2 offset)
{
	const ivec2 waterSize = textureSize(u_WaterMotion, 0);
	const vec4 water = texelFetch(u_WaterMotion,
								  clamp(ivec2(uv * vec2(waterSize)) + offset, ivec2(0), waterSize - 1), 0);
	const ivec2 size = textureSize(u_SurfaceVelocity, 0);
	const vec2 motion = water.z > 0.5
					  ? water.xy
					  : texelFetch(u_SurfaceVelocity,
								   clamp(ivec2(uv * vec2(size)) + offset, ivec2(0), size - 1), 0).xy;
	return length(motion / u_Params.TexelSize);
}

// **RT-20: whether a refused centre is only the jitter crossing an edge.**
//
// With the camera parked, a pixel on an edge has its jittered sample on one
// side of the edge in some frames and on the other side in the rest. The
// surface test compares this frame's sample with last frame's, so it read
// every such flip as a different surface -- and the neighbour search then
// replaced the pixel's own history, which is the coverage blend the jitter
// exists to build, with a neighbour's, which is pure object or pure
// background. The blend never formed and every edge flickered with the jitter
// from RT-6 on: parked, the garage's car edges changed 7.97 levels a frame and
// the tubes' 11.86, against 1.04 and 0.99 with the test off and 0.97 and 0.82
// with no jitter at all. RT-6 was judged on a single frame, where an edge that
// is never blended reads as a sharp one.
//
// The centre is kept, whatever the test said, when three things hold:
//
//   1. **It is an edge.** A neighbour in this frame's identity lane is another
//      surface; a flip cannot happen anywhere else.
//   2. **Nothing in its 3x3 moved**, by the surfaces' own motion, under the
//      same threshold as the still feedback above.
//   3. **What this texel showed last frame was not moving either** -- the
//      sign of the count this pass wrote there. Without it an object that
//      clears an edge by two pixels in one frame leaves the edge's 3x3 still,
//      and the history kept there is the object's: behind a chrome cube
//      crossing at four pixels a frame, 3-4 levels further from the truth than
//      the resolve without the rule, fading over forty frames.
//
// Anything that moved takes RT-6's search exactly as before, which is what
// stops a moving object trailing. Of two ways measured this is the one that
// holds everywhere: the other -- keep the centre wherever last frame's surface
// is still among this frame's eight neighbours -- missed the car's small parts
// parked and trailed behind moving objects.
//
// What it does not cover: an object that starts moving two pixels a frame from
// a standstill was not moving last frame, so the edges it uncovers on its first
// frame keep its colour (RT-18's coverage mask is the structural answer); and a
// history kept here is still held to the box, which is loose on rough surfaces.
bool JitterCrossing(vec2 uv, vec2 pastUv, vec4 now)
{
	if (u_Params.HasMoments < 0.5)
		return false;
	const ivec2 momentsSize = textureSize(u_Moments, 0);
	const float count = texelFetch(u_Moments, clamp(ivec2(pastUv * vec2(momentsSize)),
													ivec2(0), momentsSize - 1), 0).x;
	if (!(count > 0.0))
		return false;

	const ivec2 guideSize = textureSize(u_GuideCurrent, 0);
	const ivec2 guideTexel = ivec2(uv * vec2(guideSize));
	bool edge = false;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const ivec2 offset = ivec2(x, y);
			if (SurfaceMotionTexels(uv, offset) >= kStillWithinTexels)
				return false;
			if (offset != ivec2(0)
				&& !Matches(now, texelFetch(u_GuideCurrent,
											clamp(guideTexel + offset, ivec2(0), guideSize - 1), 0), false))
				edge = true;
		}
	}
	return edge;
}
'''
SUBS.append(('const float kStillWithinTexels = 0.003;\n', 'const float kStillWithinTexels = 0.003;\n' + RULE))

# --- main: the flag on every path, the count through abs() ----------------------------
SUBS.append((
    '\tconst float currentLuma = ToYCoCg(current.rgb).x;\n',
    '\tconst float currentLuma = ToYCoCg(current.rgb).x;\n'
    '\n'
    '\t// **RT-20: whether this pixel\'s surface moved**, carried to the next frame\n'
    '\t// in the sign of the count on every path below. JitterCrossing asks it of\n'
    '\t// last frame.\n'
    '\tconst float movedSign = SurfaceMotionTexels(uv, ivec2(0)) >= kStillWithinTexels ? -1.0 : 1.0;\n'))
SUBS.append((
    '\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  float(kNoHistory));',
    '\t\to_Moments = vec4(movedSign, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  float(kNoHistory));'))
SUBS.append((
    '\t\to_Moments = vec4(1.0, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  offScreen ? float(kOffScreen)',
    '\t\to_Moments = vec4(movedSign, currentLuma, currentLuma * currentLuma,\n\t\t\t\t\t\t  offScreen ? float(kOffScreen)'))
SUBS.append((
    '\tfloat frames = min(prevMoments.x + 1.0, kMaxFrames);\n',
    '\t// RT-20: through abs(), because the sign is the moved flag, not the count.\n'
    '\tfloat frames = min(abs(prevMoments.x) + 1.0, kMaxFrames);\n'))
SUBS.append(('\to_Moments = vec4(frames,\n', '\to_Moments = vec4(movedSign * frames,\n'))

for old, new in SUBS:
    n = s.count(old)
    if n != 1:
        sys.exit('matched %d times: %r' % (n, old[:80]))
    s = s.replace(old, new)

io.open(P, 'w', encoding='utf-8', newline='').write(s.replace('\n', '\r\n') if crlf else s)
print('patched %s (%d substitutions)' % (P, len(SUBS)))
