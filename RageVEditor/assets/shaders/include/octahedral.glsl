// **The octahedral mapping, in one place.**
//
// Fold the sphere onto an octahedron, unfold that to a square: a direction
// becomes two numbers in zero to one, and back. Area distortion is mild and
// there are no poles, which is what a cube map spends six faces avoiding.
//
// Three things in this engine use it and they must agree exactly, because two
// of them write what the third reads: the surface attachment stores a world
// normal this way (`pbr.rvshader` writes, the screen-space passes read), and
// the irradiance field bins its stored distances by direction with it. A second
// copy of either half is how those stop agreeing -- they would agree until
// somebody improved one of them.
//
// Guarded, because a shader may reach this through more than one include:
// `rtgi_trace` takes both `view_reconstruction.glsl` and `pbr_fragment.glsl`,
// and a second body of the same function is a compile error rather than a
// silent choice.

#ifndef RV_OCTAHEDRAL_GLSL
#define RV_OCTAHEDRAL_GLSL

// Unit vector to [0,1]^2. Two 8-bit channels give about a degree of direction,
// which a reflection ray does not notice.
vec2 OctEncode(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
															 n.y >= 0.0 ? 1.0 : -1.0);
	return e * 0.5 + 0.5;
}

// And back: [0,1]^2 to a unit vector. The exact inverse of the fold above.
vec3 OctDecode(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
		n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return normalize(n);
}

#endif
