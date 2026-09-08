# -*- coding: utf-8 -*-
"""RT-8 job 3: the twin gets a memory instead of a ceiling.

Measured on the sea: the contract keeps 99.7% of water pixels and holds them
**four frames**, where the sea's own pass held its glint sixteen -- and four is
the scattered half's number. The twin was not short because anything decided it
should be; it was short because it could not be long.

`frames2 = min(frames, PairMemory)` can only ever *shorten* the twin, so a pair
whose second half wants a longer memory than its first cannot say so. That was
never noticed because the one pair in the engine -- the direct light -- wants
its twin shorter. The sea is the case that runs the other way: what enters the
water is broad and forgets in four, what glints off it wants sixteen.

So the shortening chain becomes a function of a base memory, and each half is
put through it with its own. Every reduction is shared, which is the part that
matters: the twin still loses its memory to motion, to a smear, to a silhouette
and to a marginal match at exactly the rate the first half does.
"""
import io, sys

CRLF, LF = chr(13) + chr(10), chr(10)
P = r'RageVEditor/assets/shaders/reflection_accumulate.rvshader'
src = io.open(P, encoding='utf-8', newline='').read()
crlf = CRLF in src
s = src.replace(CRLF, LF)
if 'ShortenedMemory' in s:
    sys.exit('already patched')


def once(old, new, what):
    global s
    if s.count(old) != 1:
        sys.exit('%s matched %d' % (what, s.count(old)))
    s = s.replace(old, new, 1)


# --- the chain, lifted into a function of the base -----------------------
once('''			float memory = max(u_Reflection.History.y, 1.0) / (1.0 + moved / slack);
			memory = min(memory, max(kSmearTexels / max(moved, 1.0e-3), kMovingMemory));
			if (silhouette)
				memory = min(memory, max(max(u_Reflection.History.y, 1.0) / (1.0 + neighbourMotion / slack),
										 kSilhouetteMemory));
			memory = max(memory, fewest);''',
'''			//
			// **Written against a base rather than against History.y** (RT-8):
			// a pair's two halves may want different memories, and every
			// shortening below applies to both alike. See ShortenedMemory.
			float memory = ShortenedMemory(max(u_Reflection.History.y, 1.0),
										   moved, slack, fewest, silhouette,
										   neighbourMotion);''',
     'memory chain')

once('''			memory = max(mix(fewest, memory, c.matchConfidence), fewest);
			frames = min(c.past.a + 1.0, memory);''',
'''			memory = max(mix(fewest, memory, c.matchConfidence), fewest);
			// **RT-8: how much of all that the twin keeps.** Every reduction
			// above is a property of the pixel, not of the payload -- how far
			// the picture moved, whether it sits on a silhouette, how well the
			// surface matched -- so the twin is put through the same ones from
			// its own base. `confidence` and `matchConfidence` are folded in as
			// a single ratio for the same reason: they are already multiplied
			// into `memory`, and re-deriving them would be a second copy to
			// keep in step.
			const float reduced = memory / max(ShortenedMemory(max(u_Reflection.History.y, 1.0),
															   moved, slack, fewest, silhouette,
															   neighbourMotion), 1.0e-4);
			frames = min(c.past.a + 1.0, memory);''',
     'twin allowance')

once('''				const vec3 held2 = clamp(past2.rgb, mean2 - halfWidth2, mean2 + halfWidth2);
				const float frames2 = u_Reflection.Tuning.w > 0.0 ? min(frames, u_Reflection.Tuning.w) : frames;
				kept2 = mix(held2, fresh2.rgb, 1.0 / frames2);''',
'''				const vec3 held2 = clamp(past2.rgb, mean2 - halfWidth2, mean2 + halfWidth2);
				// **RT-8: its own memory, and its own count of the frames
				// behind it.** This was `min(frames, Tuning.w)`, which could
				// only ever make the twin shorter than the first half -- right
				// for the direct light, whose highlight forgets faster than its
				// diffuse, and impossible for the sea, whose glint wants
				// sixteen frames where the light entering the water wants four.
				// The sea was measured holding four (2026-09-08) and reading
				// noisier than the private pass it replaced; this is why.
				//
				// The twin's w now carries the twin's own count. Nothing else
				// reads it -- the blur copies it through untouched -- so the
				// change is confined to the two lines that write and read it.
				float frames2 = frames;
				if (u_Reflection.Tuning.w > 0.0)
				{
					const float memory2 =
						max(ShortenedMemory(max(u_Reflection.Tuning.w, 1.0),
											moved, slack, fewest, silhouette,
											neighbourMotion) * reduced, fewest);
					frames2 = min(past2.a + 1.0, memory2);
				}
				kept2 = mix(held2, fresh2.rgb, 1.0 / frames2);
				twinFrames = frames2;''',
     'twin memory')

once('''#ifdef RV_SIGNAL_PAIR
	const vec4 fresh2 = texelFetch(u_Fresh2, texel, 0);
	vec3 kept2 = fresh2.rgb;
#endif''',
'''#ifdef RV_SIGNAL_PAIR
	const vec4 fresh2 = texelFetch(u_Fresh2, texel, 0);
	vec3 kept2 = fresh2.rgb;
	// RT-8: the twin counts its own frames now; one until a history is kept.
	float twinFrames = 1.0;
#endif''',
     'twin frames declaration')

once('''#ifdef RV_SIGNAL_PAIR
	o_Accumulated2 = vec4(kept2, frames);''',
'''#ifdef RV_SIGNAL_PAIR
	// RT-8: the twin's own frame count, not the first half's -- see the
	// memory block above for why the two must be able to differ.
	o_Accumulated2 = vec4(kept2, twinFrames);''',
     'twin output')

# --- the function itself -------------------------------------------------
once('''bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,''',
'''// **RT-8: the memory a pixel is allowed, given a base and how it moved.**
//
// Lifted out of main so a pair's two halves can each be put through it with
// their own base. Every step is a property of the pixel rather than of the
// payload: how far the picture travelled since last frame, whether the average
// would span more than the smear cap allows, and whether the texel sits on a
// silhouette whose other side moves. The floor is the caller's `fewest`.
//
// Bit-identical to the chain it replaces when called with History.y, which is
// what the first half calls it with.
float ShortenedMemory(float base, float moved, float slack, float fewest,
					  bool silhouette, float neighbourMotion)
{
	float memory = base / (1.0 + moved / slack);
	memory = min(memory, max(kSmearTexels / max(moved, 1.0e-3), kMovingMemory));
	if (silhouette)
		memory = min(memory, max(base / (1.0 + neighbourMotion / slack), kSilhouetteMemory));
	return max(memory, fewest);
}

bool HistoryAt(vec3 world, ivec2 size, vec3 P, vec3 N, float roughness,''',
     'ShortenedMemory')

io.open(P, 'w', encoding='utf-8', newline=CRLF if crlf else LF).write(s)
print('the twin has a memory of its own')
