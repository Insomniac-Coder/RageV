// The engine's shadow rays, and the alpha test inside their traversal.
//
// **Its own file since WR-16 S4b**, because a second stage traces them now.
// The sea's lamps are chosen and shaded in their own pass, and a shadow ray
// there has to be the same ray the lit shader casts -- same traversal, same
// cutout test, same soft-shadow point on the source -- or a railing would
// shadow the water as a solid panel from one pass and as a railing from the
// other. Written once, included twice.
//
// **What it asks of whoever includes it**: the acceleration structure
// (u_SceneAS), the ray-instance table (u_RayInstances) and, under
// RV_BINDLESS, the material table and the texture heap; the ray masks; the
// counter macro; InterleavedGradientNoise; and the four facts below. The
// bindings themselves stay with the includer, which is what lets a pass
// number its own set however it likes.

// The camera, for the thin-member fade: a member the lit pass dissolved at
// this distance must not still be found by a ray.
#ifndef RV_TRACE_CAMERA
#define RV_TRACE_CAMERA u_Scene.CameraPosition.xyz
#endif
// Whether a temporal filter exists to integrate a walking sample. Every
// stochastic choice in this engine holds still without one.
#ifndef RV_TRACE_ANIMATED
#define RV_TRACE_ANIMATED any(notEqual(u_Scene.Jitter, vec4(0.0)))
#endif
// The frame counter the walk advances along.
#ifndef RV_TRACE_FRAME
#define RV_TRACE_FRAME mod(u_Scene.GlobalIllumination.y, 1024.0)
#endif
// Whether a structure was built this frame at all; without one every ray
// answers lit.
#ifndef RV_TRACE_READY
#define RV_TRACE_READY (u_Scene.ShadowParams.x > 0.0)
#endif

// **The alpha test, inside traversal.**
//
// This engine traces with ray *queries*, not a ray-tracing pipeline: there is
// no shader binding table and no any-hit stage to write. So the place a cutout
// gets to say "not here" is the traversal loop itself, which was empty --
// `while (rayQueryProceedEXT(q)) {}` -- because every instance was opaque and
// the hardware committed every hit without asking.
//
// The work is the fetch chain TraceSurface already does *after* the loop,
// hoisted in and asked of the *candidate* rather than the committed hit: the
// instance's record, its index and attribute buffers, the triangle's three
// texture coordinates, the barycentric blend, one texture fetch.
//
// **It runs only for instances that asked for it.** Everything else keeps
// VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE off, so the hardware commits its hits
// and never reports a candidate at all -- which is what keeps a scene with no
// cutouts paying nothing. That is the whole reason the flag is per instance:
// a texture fetch inside traversal is the known cliff of alpha-tested ray
// tracing, and the way to afford it is to spend it only on the geometry that
// needs it.
//
// **Only the variants that carry the ray-instance table can do this**, which
// is the same condition the table itself is declared under. A shadows-only
// build has no instance record to read a cutoff from and no heap to sample,
// so it forces every instance opaque instead: no candidate is reported, the
// loop stays empty, and a cutout shadows as its sheet exactly as it did
// before any of this. Saying so with the ray flag rather than with a `return
// true` keeps that variant paying nothing for a test it cannot run.
#if defined(RV_RAY_REFLECTIONS) || defined(RV_RAY_GI) || defined(RV_RAY_REFRACTION)
#define RV_RAY_BASE_FLAGS 0u

bool RayCandidateIsThere(rayQueryEXT query)
{
	const uint instance = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(query, false));
	RayInstance hit = u_RayInstances.Instances[instance];

	// Solid geometry that merely happens to be in a non-opaque instance --
	// and the fast exit for everything if the flags ever disagree.
	if ((hit.Flags & RAY_INSTANCE_MASKED) == 0u)
		return true;

#ifdef RV_BINDLESS
	GpuMaterial material = u_Materials.Materials[hit.MaterialIndex];

#ifndef RV_IRRADIANCE_FILL
	// **The thin-member fade, for rays too** (MaterialParams::Macro.zw). A
	// member the lit pass has dissolved at this distance must not still be
	// found by a mirror ray from the water: the first landing left the rays
	// alone and the owner saw "random red dots in the water" -- the
	// reflections of pickets and ropes that were no longer on screen to
	// explain them. Same distances, from the same camera, and a dither from
	// the hit point so a member in the fade band thins out in its reflection
	// the way it thins out on screen. Not in the irradiance solve, which has
	// no camera: the bake keeps every member.
	if (material.Macro.w > material.Macro.z)
	{
		const vec3 hitPoint = rayQueryGetWorldRayOriginEXT(query)
							+ rayQueryGetWorldRayDirectionEXT(query)
							  * rayQueryGetIntersectionTEXT(query, false);
		const float away = length(hitPoint - RV_TRACE_CAMERA);
		const float keep = 1.0 - smoothstep(material.Macro.z, material.Macro.w, away);
		if (keep <= 0.0)
			return false;
		if (keep < 1.0)
		{
			// The hit point on a 5 cm lattice, hashed: deterministic per
			// place rather than per frame, so a still frame is still.
			const uvec3 cell = uvec3(ivec3(floor(hitPoint * 20.0)) + ivec3(0x40000000));
			uint h = cell.x * 73856093u ^ cell.y * 19349663u ^ cell.z * 83492791u;
			h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
			if (keep <= float(h >> 8) * (1.0 / 16777216.0))
				return false;
		}
	}
#endif

	// No map, no fetch: the alpha is the material's scalar and the whole
	// triangle stands or falls together.
	if ((material.MapFlags & MAP_BASE_COLOR) == 0)
		return hit.BaseColor.a >= hit.AlphaCutoff;

	const uint primitive = uint(rayQueryGetIntersectionPrimitiveIndexEXT(query, false));
	const vec2 bary = rayQueryGetIntersectionBarycentricsEXT(query, false);
	const float w0 = 1.0 - bary.x - bary.y;

	RayWords indices = RayWords(hit.IndexAddress);
	RayFloats attributes = RayFloats(hit.AttributeAddress);
	const uint as_ = hit.AttributeStrideWords;

	const uint i0 = indices.Words[primitive * 3u + 0u];
	const uint i1 = indices.Words[primitive * 3u + 1u];
	const uint i2 = indices.Words[primitive * 3u + 2u];

	// Texture coordinate at the sixth and seventh floats of a vertex, as
	// TraceSurface reads them: MeshVertex and SkinnedVertex agree that far.
	const vec2 uv0 = vec2(attributes.Floats[i0 * as_ + 6u], attributes.Floats[i0 * as_ + 7u]);
	const vec2 uv1 = vec2(attributes.Floats[i1 * as_ + 6u], attributes.Floats[i1 * as_ + 7u]);
	const vec2 uv2 = vec2(attributes.Floats[i2 * as_ + 6u], attributes.Floats[i2 * as_ + 7u]);

	const vec2 uv = (uv0 * w0 + uv1 * bary.x + uv2 * bary.y) * material.UvTransform.xy
				  + material.UvTransform.zw;

	// Level zero: a candidate has no derivatives, and a cutout's edge is the
	// one thing a lower mip would move.
	const float alpha = hit.BaseColor.a *
						textureLod(u_Textures[nonuniformEXT(material.Maps0.x)], uv, 0.0).a;
	return alpha >= hit.AlphaCutoff;
#else
	// Without the heap there is no map to read, so the scalar decides.
	return hit.BaseColor.a >= hit.AlphaCutoff;
#endif
}

// Traversal, for every ray in this engine. Written once because the three
// call sites must agree: a shadow ray that believed a hole and a reflection
// ray that did not would put a fence's shadow where its reflection is not.
void RayTraverse(rayQueryEXT query)
{
	while (rayQueryProceedEXT(query))
	{
		if (rayQueryGetIntersectionTypeEXT(query, false) ==
				gl_RayQueryCandidateIntersectionTriangleEXT &&
			RayCandidateIsThere(query))
		{
			rayQueryConfirmIntersectionEXT(query);
		}
	}
}

#else   // no instance table in this variant

#define RV_RAY_BASE_FLAGS gl_RayFlagsOpaqueEXT

void RayTraverse(rayQueryEXT query)
{
	while (rayQueryProceedEXT(query)) {}
}

#endif

// **Which world a ray sees** (ENGINE-NOTES 7cx). Every instance in the
// structure carries exactly one of two mask bits -- RayShadows::kMaskStatic
// or kMaskMoving, mirrored here and nowhere else -- and a ray's cull mask
// chooses. The frame's rays see both: RV_RAY_MASK_SCENE is every bit. The
// bake's solve sees the static half alone, because a moving object is not
// baked -- its shadow must not be painted onto the floor it will drive off.
// And a static pixel under a fully baked lamp traces toward it with
// RV_RAY_MASK_MOVING alone, to find what the field could not know.
#define RV_RAY_MASK_STATIC 0x01u
#define RV_RAY_MASK_MOVING 0x02u
#ifdef RV_IRRADIANCE_FILL
#define RV_RAY_MASK_SCENE RV_RAY_MASK_STATIC
#else
#define RV_RAY_MASK_SCENE 0xFFu
#endif

float TraceShadowFromMasked(vec3 worldPos, vec3 Ng, vec3 L, float tMax, uint mask)
{
	if (!RV_TRACE_READY)
		return 1.0;
	RV_COUNT_SHADOW_RAY();

	float NgdotL = clamp(dot(Ng, L), 0.0, 1.0);
	float slope = sqrt(1.0 - NgdotL * NgdotL) / max(NgdotL, 0.15);
	float offset = 0.002 * (1.0 + min(slope, 4.0));

	rayQueryEXT q;
	// **No gl_RayFlagsOpaqueEXT.** That flag forces every instance opaque and
	// overrides the per-instance one, so a cutout would shadow like a sheet.
	// Dropping it costs nothing for solid geometry, which is still marked
	// opaque in the structure and still committed without a candidate.
	//
	// TerminateOnFirstHit stays: it ends traversal at the first *committed*
	// hit, and nothing is committed now without passing the test.
	rayQueryInitializeEXT(q, u_SceneAS,
						  RV_RAY_BASE_FLAGS | gl_RayFlagsTerminateOnFirstHitEXT,
						  mask, worldPos + Ng * offset, 0.0, L, tMax);
	RayTraverse(q);
	return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT
		 ? 1.0 : 0.0;
}

// The shadow ray every caller traced before masks existed: the whole scene
// under the frame, the static half under the solve.
float TraceShadowFrom(vec3 worldPos, vec3 Ng, vec3 L, float tMax)
{
	return TraceShadowFromMasked(worldPos, Ng, L, tMax, RV_RAY_MASK_SCENE);
}

// **WR-15: a lamp has a size, so its shadow has an edge.**
//
// The hard shadow above is right for one light and wrong for a hundred and
// twenty. The deck carries 120 sodium lamps at 45.7 m spacing, each of them a
// 0.6 m sphere the specular already knows about (`SourceRadius`, commit
// `dab692d`) and each of them a *point* to the shadow ray -- so the railing
// pickets cast 120 crisp shadows onto the water and the glitter path came out
// as a picket fence, dark inside each rectangle and bright between. The owner
// reported it twice from two different cameras before it was found, and the
// reason it survived that long is that it looks like a reflection artefact
// rather than a shadow one.
//
// **One ray, aimed at a point on the source rather than at its centre.** The
// obvious fix -- N rays per light and average -- multiplies the most expensive
// term in this frame by N, and with 120 casting lamps that is not a trade
// anybody would take. Instead each light's single ray is aimed at a point
// sampled uniformly over the source disc, so the shadow is still hard *per
// light* and the softness comes out of the array: where twenty lamps overlap,
// twenty independently displaced hard edges sum to the penumbra the real
// installation has. That is not a trick, it is what a penumbra *is* -- the
// partial visibility of an extended source -- and a dense array is the one
// case where one sample per light is enough to resolve it.
//
// **Seeded from the pixel and the light; from the frame only when something
// is there to integrate it.** The first landing hashed pixel, light and frame
// together as white noise, and that made the penumbra a property of the
// anti-aliasing mode: under TAA the resolve averaged the frames and the
// railing's shadow came out soft, under MSAA or no AA the same ray was
// re-rolled every frame with nothing to average it and the whole lit roadway
// and the water under it blinked -- measured with the clock pinned on the
// Headland camera, 14.7% of the deck's pixels swinging six levels or more
// with no AA, 0.3% with this ray forced hard. A shading term that only works
// under one AA mode is a defect in the term, not in the other modes.
//
// So the sample is built the other way round. Per pixel, an interleaved
// gradient noise value (Jimenez 2014) -- a fixed pattern whose error sits in
// the highest spatial frequencies, where the eye and every downstream filter
// average it, unlike a hash's white noise which reads as salt and pepper.
// Per light, the R2 sequence (Roberts 2018): the lights in range of a pixel
// take points of a low-discrepancy set on the disc rather than independent
// draws, so the array's own sum -- twenty or forty lamps at a water pixel --
// converges like a stratified estimate rather than a random one. The pixel's
// noise shifts the whole set toroidally, which keeps its structure and
// decorrelates neighbours.
//
// **The frame term is gated on the jitter**, because that is what a temporal
// accumulator announces itself with: `u_Scene.Jitter` is non-zero only while
// a TemporalHistory is being filled (FrameGraphBuilder: no history, no
// jitter). Under TAA the shift walks the golden ratio each frame and the
// resolve integrates it; under anything else it stands still and the
// picture is the same every frame, which is what every screenshot
// comparison in this repository already requires. The counter is
// `GlobalIllumination.y`, a frame index and not a clock, so frame N is
// reproducible either way.
//
// Falls through to the hard path when the source has no size, so every light
// authored before `SourceRadius` existed traces exactly the ray it always did.
// Jimenez's interleaved gradient noise: a plane-tiling pattern with almost
// no low-frequency energy, from two fracts and a dot. The constants are the
// published ones.
float InterleavedGradientNoise(vec2 pixel)
{
	return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// A permuted-congruential hash (O'Neill's PCG output step), for the draws
// that must be independent of one another -- the reservoir sampling of
// WR-16 S1. White noise, deterministic, one integer in and out.
uint BudgetHash(uint x)
{
	x = x * 747796405u + 2891336453u;
	x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
	return (x >> 22u) ^ x;
}

float TraceShadowSoftFromMasked(vec3 worldPos, vec3 Ng, vec3 L, float tMax,
								float sourceRadius, uint light, uint mask)
{
	// No size, or a directional light -- whose tMax is a stand-in for infinity
	// and whose radius in metres would mean nothing against it.
	if (sourceRadius <= 0.0 || tMax >= 1.0e4)
		return TraceShadowFromMasked(worldPos, Ng, L, tMax, mask);

	// The pixel's shift: one gradient-noise value per axis, the second read
	// at an offset so the two are not the same pattern.
	const vec2 pixel = gl_FragCoord.xy;
	vec2 shift = vec2(InterleavedGradientNoise(pixel),
					  InterleavedGradientNoise(pixel + vec2(5.588238, 5.588238)));
	// Walked by the golden ratio per frame, only while a temporal filter is
	// there to integrate it. mod keeps the product small enough that fract
	// below still has its precision after thousands of frames.
	if (RV_TRACE_ANIMATED)
	{
		const float frame = RV_TRACE_FRAME;
		shift += frame * vec2(0.61803398875, 0.38196601125);
	}
	// The R2 point for this light, shifted: the plastic constant's powers.
	const vec2 u = fract(shift + float(light) * vec2(0.75487766625, 0.56984029100));
	const float u1 = u.x;
	const float u2 = u.y;

	// Uniform over the disc: sqrt on the radius, or the samples crowd the
	// centre and the penumbra keeps a hard core.
	const float radius = sourceRadius * sqrt(u1);
	const float phi = 6.28318530718 * u2;

	// A frame about L. The guard is the usual one: a cross product with a
	// parallel vector is zero and normalising it is a NaN, which TAA would
	// then keep forever.
	const vec3 up = abs(L.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	const vec3 t = normalize(cross(up, L));
	const vec3 b = cross(L, t);

	const vec3 target = L * tMax + (t * cos(phi) + b * sin(phi)) * radius;
	// The sampled point is fractionally further than the centre; ending the
	// ray at the centre distance would let the source's own far edge occlude
	// it. sqrt is exact here and the term is one instruction.
	return TraceShadowFromMasked(worldPos, Ng, normalize(target),
								 sqrt(tMax * tMax + radius * radius), mask);
}

float TraceShadowSoftFrom(vec3 worldPos, vec3 Ng, vec3 L, float tMax,
						  float sourceRadius, uint light)
{
	return TraceShadowSoftFromMasked(worldPos, Ng, L, tMax, sourceRadius, light,
									 RV_RAY_MASK_SCENE);
}
