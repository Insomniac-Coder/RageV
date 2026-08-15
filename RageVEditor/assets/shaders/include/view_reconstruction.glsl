// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
//
// The view-space reconstruction every depth-reading post pass shares. SSAO
// orients its hemisphere in it and SSR marches its ray through it, and the
// two passes agreeing about it is not optional: the day one of them differs
// is the day a normal that is right for occlusion is wrong for reflection.
// So it is stated once, here, and both include it. ENGINE-NOTES 7ae.
//
// **The reconstruction frame is not the camera's view space.** It is what
// falls out of turning a sampling-space uv and a depth back into a point:
//
//   x -- right, as the camera's is;
//   y -- along *increasing texel rows*, because the uv it is built from is
//        the one the pass samples the depth buffer with. Rows run up the
//        picture on the backend whose row 0 is the bottom (OpenGL) and down
//        it on the one whose row 0 is the top (Vulkan) -- the same fact
//        every post pass's FlipY exists for;
//   z -- the *linear depth*, positive in front of the camera. View space
//        looks down -z; this frame looks down +z.
//
// Positions and directions computed inside the frame are consistent with
// each other by construction: ViewToUv is the exact inverse of ViewPosition,
// so a march step lands where the depth it is compared with was read from.
// The one thing that arrives from *outside* the frame is a normal the scene
// pass wrote in world space, and it has to be brought in through the same
// map -- which is what ReconstructionNormal is, and why nothing else in
// this file has an opinion about world space.
//
// Nothing here declares a uniform or a sampler. Every pass carries its own
// push-constant block with its own tail, and GLSL allows one such block per
// stage, so the shared code takes what it needs as arguments and the pass
// keeps a two-line wrapper that reads its own depth texture.

// The camera's near/far planes turn the buffer's 0..1 back into metres.
float LinearDepth(float depth, float nearClip, float farClip)
{
	return (nearClip * farClip) / max(farClip - depth * (farClip - nearClip), 1.0e-6);
}

// A sampling-space uv and its linear depth to a point in the reconstruction
// frame. `invProjection` is the inverse of the projection's [0][0] and
// [1][1] -- what turns an NDC coordinate back into a view-space one at unit
// depth. Perspective is assumed; ENGINE-NOTES 7ac says what an orthographic
// camera gets instead.
vec3 ViewPosition(vec2 uv, float linearDepth, vec2 invProjection)
{
	const vec2 ndc = uv * 2.0 - 1.0;
	return vec3(ndc * invProjection * linearDepth, linearDepth);
}

// The exact inverse of ViewPosition.
vec2 ViewToUv(vec3 position, vec2 invProjection)
{
	const vec2 ndc = position.xy / (invProjection * max(position.z, 1.0e-4));
	return ndc * 0.5 + 0.5;
}

// The surface attachment's octahedral normal, back to a unit vector. The
// inverse of OctEncode in pbr_fragment.glsl.
vec3 OctDecode(vec2 e)
{
	e = e * 2.0 - 1.0;
	vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
		n.xy = (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	return normalize(n);
}

// Whether a texel of the surface attachment is the clear rather than a
// surface -- sky, grid, particles, text: anything the PBR shaders did not
// draw.
//
// Tested on the *roughness* channel, not the normal. The PBR shader clamps
// roughness to 0.045 before writing it, so a real surface never stores a
// zero there and the clear always does. The normal channels cannot serve:
// the four corners of the octahedral square all decode to -z, and a normal
// pointing almost exactly along -z with the faintest negative x and y
// quantises to the same (0, 0) the clear holds. A wall facing that way
// would flicker between "surface" and "nothing" a texel at a time.
bool SurfaceIsEmpty(vec4 surface)
{
	return surface.b <= 0.0;
}

// A world-space normal into the reconstruction frame: the only place the
// map between the two is written down.
//
// Two steps that are one function so no pass can do half of it. First the
// view matrix's rotation, three rows, which is world to *view* space. Then
// view space to this frame: z negates, because the frame's z is the depth
// and view space looks down -z; and y negates on the backend that flips,
// because there texel rows run down the picture and view-space y runs up.
// On the other backend rows run up too and y stays.
//
// **This replaced a transform that was right only for normals with no
// x-component in view space.** It negated y on the unflipped backend and
// nothing on the flipped one, and had been measured on a floor seen without
// roll -- where the two agree, because reflect() cannot tell a normal from
// its negation and (x, -y, z) is the negation of the correct (x, y, -z) when
// x is zero. A mirror sphere told them apart: the block beside it reflected
// on the wrong side. check_ssr.py now has that sphere. ENGINE-NOTES 7ae.
vec3 ReconstructionNormal(vec3 worldNormal, vec3 viewRow0, vec3 viewRow1, vec3 viewRow2,
						  bool flipY)
{
	vec3 n = vec3(dot(viewRow0, worldNormal),
				  dot(viewRow1, worldNormal),
				  dot(viewRow2, worldNormal));
	n.z = -n.z;
	if (flipY)
		n.y = -n.y;
	return normalize(n);
}
