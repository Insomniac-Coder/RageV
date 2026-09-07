"""v10: the accumulator's history lookup searches the 3x3 around the
reprojected texel for a matching surface when the centre's fails (SVGF).
Under jitter an edge texel's surface flips sides every frame; refusing the
history there left every silhouette one frame old and shaking."""
p = 'C:/Users/ism19/Code/RageV/RageVEditor/assets/shaders/reflection_accumulate.rvshader'
s = open(p, 'rb').read().decode('utf-8'); nl = '\r\n' if '\r\n' in s else '\n'
a = s.index('\tconst ivec2 pastTexel = clamp(ivec2(c.pastUv * vec2(size)), ivec2(0), size - 1);')
b = s.index('\treturn c.past.a > 0.0;' + nl + '}', a) + len('\treturn c.past.a > 0.0;' + nl)
print('--- replaced span (code lines only) ---'); print('\n'.join(l for l in s[a:b].replace(nl, '\n').split('\n') if l.strip() and not l.strip().startswith('//')))
new = """	// The texel under the reprojection first, then its eight neighbours
	// (SVGF's search). Under jitter an edge texel's surface flips sides
	// every frame, and refusing the history there left every silhouette one
	// frame old and shaking: parked, bright edges moved 8.1 levels a frame
	// against 3.3 with the reflections off (2026-09-06). The neighbour that
	// kept this surface's picture holds the same picture, a texel over.
	const ivec2 centre = clamp(ivec2(c.pastUv * vec2(size)), ivec2(0), size - 1);
	const ivec2 offsets[9] = ivec2[9](ivec2(0, 0), ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1),
									  ivec2(1, 1), ivec2(-1, -1), ivec2(1, -1), ivec2(-1, 1));
	for (int k = 0; k < 9; ++k)
	{
		const ivec2 pastTexel = clamp(centre + offsets[k], ivec2(0), size - 1);
		c.reflector = texelFetch(u_HistorySurface, pastTexel, 0);
		if (c.reflector.a < 0.0)
			continue;
		const vec3 wasN = OctDecode(c.reflector.rg);
		if (dot(wasN, N) < 0.8)
			continue;
		if (abs(dot(wasN, P) - c.reflector.b) > 0.05 + 0.01 * eyeDistance)
			continue;
		c.extra = texelFetch(u_HistoryExtra, pastTexel, 0);
		if (abs(c.extra.r - roughness) > 0.5)
			continue;
		// Bilinear where the texel itself matched; the neighbour's own value
		// where it did not, since the texels between belong to the other side.
		c.past = k == 0 ? texture(u_History, c.pastUv) : texelFetch(u_History, pastTexel, 0);
		if (c.past.a > 0.0)
			return true;
	}
	return false;
""".replace('\n', nl)
open(p, 'wb').write((s[:a] + new + s[b:]).encode('utf-8')); print('v10 patched')
