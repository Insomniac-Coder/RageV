import os as _os
_RAGEV = _os.path.abspath(_os.path.join(_os.path.dirname(__file__), '..', '..', '..', '..')).replace('\\', '/')  # the repo root, wherever it lives
import io


def edit(p, a, b):
    t = io.open(p, encoding='utf-8', newline='').read()
    eol = '\r\n' if t.count('\r\n') > t.count('\n') // 2 else '\n'
    a, b = a.replace('\n', eol), b.replace('\n', eol)
    assert t.count(a) == 1, (p, t.count(a), a[:70])
    io.open(p, 'w', encoding='utf-8', newline='').write(t.replace(a, b))


A = (_RAGEV + '/RageVEditor/assets/shaders/reflection_accumulate.rvshader')

# 1. Helpers, after ShortenedMemory.
edit(A, """	if (silhouette)
		memory = min(memory, max(base / (1.0 + neighbourMotion / slack), kSilhouetteMemory));
	return max(memory, fewest);
}
""", """	if (silhouette)
		memory = min(memory, max(base / (1.0 + neighbourMotion / slack), kSilhouetteMemory));
	return max(memory, fewest);
}

#if !defined(RV_SIGNAL_DIFFUSE)
// **RT-24 (2026-09-24): how wide, in texels, a lobe of this roughness blurs the reflected
// picture.** The reflected ray spreads 2*alpha radians (a reflection doubles the angle); over the
// image distance that is a spot seen from the eye across (eyeDistance + image), measured against
// one texel's angle at this pixel, with the pixel itself added in quadrature -- so a near mirror
// blurs by one texel, never by less. Maths and the pixel, no tuned number.
float LobeBlurTexels(float r, float image, float eyeDistance, vec2 nowNdc, ivec2 size, vec3 eye)
{
	const vec4 alongHere = u_Reflection.InverseViewProjection * vec4(nowNdc, 0.5, 1.0);
	const vec4 alongNext = u_Reflection.InverseViewProjection
						 * vec4(nowNdc + vec2(2.0 / float(size.x), 0.0), 0.5, 1.0);
	const float pixelAngle = max(length(normalize(alongNext.xyz / alongNext.w - eye)
									   - normalize(alongHere.xyz / alongHere.w - eye)), 1.0e-7);
	const float perAlpha = 2.0 * image / ((eyeDistance + image) * pixelAngle);
	return sqrt(r * r * r * r * perAlpha * perAlpha + 1.0);
}

// **RT-24 (2026-09-24): how uncertain the image distance is here** -- the spread of the depths
// this frame's rays found across the 3x3, in the image's own units. Wide where the reflection
// changes depth (a pole's reflection over the wall behind it), nothing over one reflected surface.
float ImageSpread(ivec2 texel, ivec2 size, float image, float travelled)
{
	float sum = 0.0, sumSq = 0.0, count = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			const float d = texelFetch(u_Fresh, clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0).a;
			if (d < 0.0)
				continue;
			const float t = min(d, 1.0e3);
			sum += t;
			sumSq += t * t;
			count += 1.0;
		}
	}
	const float mean = sum / max(count, 1.0);
	const float spread = count > 1.0 ? sqrt(max(sumSq / count - mean * mean, 0.0)) : 0.0;
	return spread * image / max(travelled, 1.0e-4);
}

// **RT-24 (2026-09-24): the memory a reflection may keep, from how far its old picture can be
// misplaced -- never from how fast the camera moves.** The history is found through the image
// point, which is only as exact as the image distance. Projected through last frame's camera at
// the two ends of that distance's uncertainty, the two points land this many texels apart: how
// far the old picture can sit from where it belongs, per frame. An average of n frames can then
// be off by n times that, and it may be off by no more than the picture's own blur there
// (LobeBlurTexels). Parked, and under a pan, both ends project to one point -- the lookup is
// exact -- and the whole memory stands; over one flat reflected surface it barely shortens at
// any speed; across a pole's reflection it falls to one frame. This replaces the travel-based
// rules the reflection kind used (the hyperbola, the smear cap, the slack and the shrinking
// floor of 2026-09-23), each of which shortened a picture's memory for moving at all.
float LookupMemory(vec3 P, vec3 sight, float image, float imageSpread, float lobeBlur,
				   ivec2 size, float base)
{
	const vec4 nearThen = u_Reflection.PreviousViewProjection
						* vec4(P + sight * max(image - imageSpread, 0.0), 1.0);
	const vec4 farThen = u_Reflection.PreviousViewProjection
					   * vec4(P + sight * (image + imageSpread), 1.0);
	if (nearThen.w <= 0.0 || farThen.w <= 0.0)
		return 1.0;
	const float misplaced = length((nearThen.xy / nearThen.w - farThen.xy / farThen.w)
								   * 0.5 * vec2(size));
	return clamp(lobeBlur / max(misplaced, 1.0e-6), 1.0, base);
}
#endif
""")

# 2. Change 2's block uses the shared blur helper.
edit(A, """			const vec4 alongHere = u_Reflection.InverseViewProjection * vec4(nowNdc, 0.5, 1.0);
			const vec4 alongNext = u_Reflection.InverseViewProjection
								 * vec4(nowNdc + vec2(2.0 / float(size.x), 0.0), 0.5, 1.0);
			const float pixelAngle = max(length(normalize(alongNext.xyz / alongNext.w - eye)
											   - normalize(alongHere.xyz / alongHere.w - eye)), 1.0e-7);
			const float perAlpha = 2.0 * image / ((eyeDistance + image) * pixelAngle);
			const float rThen = UnpackRoughness(c.extra.r);
			const float sThen = sqrt(rThen * rThen * rThen * rThen * perAlpha * perAlpha + 1.0);
			const float sNow = sqrt(roughness * roughness * roughness * roughness * perAlpha * perAlpha + 1.0);
""", """			const float sThen = LobeBlurTexels(UnpackRoughness(c.extra.r), image, eyeDistance, nowNdc, size, eye);
			const float sNow = LobeBlurTexels(roughness, image, eyeDistance, nowNdc, size, eye);
""")

# 3. The floor and the memory, for the reflection kind.
edit(A, """#else
			// **RT-24 (2026-09-23): and the floor shrinks as the picture moves, as the
			// memory above it does.** A reflection's old picture is found by where its
			// image was, and under a turning camera that is only approximately where it
			// is: a floor that kept four frames however far its picture had travelled
			// blended three quarters of a misplaced picture into every frame -- the haze
			// and the ghosts behind every reflected pole and car a fast swing left on the
			// wet floor. Measured with sixteen rays a texel so grain could not hide it:
			// the floor mid-swing came out as close to its settled picture as with the
			// reflection memory off. Still (the picture within a slack of where it was),
			// the floor is exactly what it was, and a diffuse signal's history is the
			// surface's own and keeps it.
			const float fewest = max(mix(1.0, max(u_Reflection.History.z, 1.0), gloss)
									 / (1.0 + max(moved - slack, 0.0) / slack), 1.0);
#endif
""", """#else
			// **RT-24 (2026-09-24): the memory from the lookup's own uncertainty
			// (LookupMemory), and the floor never above it.** The floor is what a rough
			// surface keeps against its grain; it may not keep more frames than its old
			// picture can be trusted for. (It used to shrink with the picture's travel
			// instead -- a speed rule.)
			const float lookupMemory = LookupMemory(P, sight, image, ImageSpread(texel, size, image, travelled),
													LobeBlurTexels(roughness, image, eyeDistance, nowNdc, size, eye),
													size, max(u_Reflection.History.y, 1.0));
			const float fewest = min(mix(1.0, max(u_Reflection.History.z, 1.0), gloss), lookupMemory);
#endif
""")
edit(A, """			float memory = ShortenedMemory(max(u_Reflection.History.y, 1.0),
										   moved, slack, fewest, silhouette,
										   neighbourMotion);
""", """#ifdef RV_SIGNAL_DIFFUSE
			float memory = ShortenedMemory(max(u_Reflection.History.y, 1.0),
										   moved, slack, fewest, silhouette,
										   neighbourMotion);
#else
			// The reflection kind: the lookup's own memory, and RT-18's rule at a silhouette
			// whose other side moves on its own.
			float memory = lookupMemory;
			if (silhouette)
				memory = min(memory, max(max(u_Reflection.History.y, 1.0) / (1.0 + neighbourMotion / slack),
										 kSilhouetteMemory));
			memory = max(memory, fewest);
#endif
""")
print('ok')
