// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
//
// Cone tracing through the voxel radiance clipmap (8.1, ENGINE-NOTES 7bc),
// shared by the gather (voxelgi_gather.rvshader) and the injection's
// multi-bounce (voxel_inject.rvshader). One tracer, two callers -- the same
// rule 7ax applied to the ray tracer, for the same reason.
//
// The includer declares, *before* this include:
//
//   sampler3D u_VoxelRadiance  -- level 0 of the lit chain, isotropic
//   sampler3D u_VoxelFaces     -- levels 1 and up, six faces stacked along Y
//   int   VoxelResolution()    -- N, voxels along a cascade's side
//   int   VoxelCascadeCount()  -- C
//   vec3  VoxelOrigin(int k)   -- cascade k's minimum corner, world space
//   float VoxelSize(int k)     -- cascade k's voxel size, metres
//   float VoxelMaxMip()        -- the coarsest level a cone may read
//
// The atlas: cascades side by side along X, so cascade k occupies
// [k/C, (k+1)/C] of u; in the face chain the six faces are stacked along
// Y as well, -X, +X, -Y, +Y, -Z, +Z. A sample clamps itself inside its own
// cascade and face by half a texel at the level it reads, which is the
// atlas-padding rule from every texture atlas ever built.

// The directional chain at one level, for one direction: the three faces
// the direction looks into, weighted by the squares of its components.
vec4 VoxelFaceSample(vec3 local, int cascade, vec3 direction, float level)
{
	const int n = VoxelResolution();
	const int count = VoxelCascadeCount();

	// Texels per cascade side at this level of the face chain, whose level
	// 0 is the isotropic chain's level 1.
	const float texels = max(float(n) / exp2(floor(level) + 1.0), 1.0);
	const float halfU = 0.5 / (texels * float(count));
	const float halfV = 0.5 / (texels * 6.0);

	const float u = clamp((float(cascade) + local.x) / float(count),
						  float(cascade) / float(count) + halfU,
						  float(cascade + 1) / float(count) - halfU);
	const vec3 d2 = direction * direction;

	vec4 result = vec4(0.0);
	for (int axis = 0; axis < 3; axis++)
	{
		const float weight = d2[axis];
		if (weight <= 0.0)
			continue;
		const int face = axis * 2 + (direction[axis] > 0.0 ? 1 : 0);
		const float v = clamp((float(face) + local.y) / 6.0,
							  float(face) / 6.0 + halfV,
							  float(face + 1) / 6.0 - halfV);
		result += textureLod(u_VoxelFaces, vec3(u, v, local.z), level) * weight;
	}
	return result;
}

// Whether a point is inside cascade k, with half a voxel's margin: the
// edge texel's centre is the last place a sample reads it whole, and a
// surface that sits on a cascade's boundary -- every wall on a round
// coordinate does, with the origin snapped to a round one -- is in that
// cascade's edge voxel rather than the next cascade's, which is twice as
// coarse.
bool VoxelInside(vec3 p, int k, out vec3 local)
{
	const int n = VoxelResolution();
	const float extent = float(n) * VoxelSize(k);
	local = (p - VoxelOrigin(k)) / extent;
	const float margin = 0.5 / float(n);
	return all(greaterThanEqual(local, vec3(margin))) && all(lessThanEqual(local, vec3(1.0 - margin)));
}

// The finest cascade containing a point, or the outermost where none does:
// what decides how far a cone has to lift off its surface and how far its
// first step is, because the surface is as thick as *that* cascade's voxel.
int VoxelCascadeAt(vec3 p)
{
	const int count = VoxelCascadeCount();
	vec3 local;
	for (int k = 0; k < count; k++)
		if (VoxelInside(p, k, local))
			return k;
	return count - 1;
}

// Radiance and occupancy at a world point, for a cone travelling along
// `direction` with a footprint of `diameter`: read from the finest cascade
// that contains it and still resolves that footprint. Level 0 is the
// isotropic chain; from level 1 the directional one. Returns zero --
// nothing there, nothing blocking -- outside every cascade.
vec4 VoxelSample(vec3 p, vec3 direction, float diameter)
{
	const int n = VoxelResolution();
	const int count = VoxelCascadeCount();
	const float maxMip = VoxelMaxMip();

	for (int k = 0; k < count; k++)
	{
		const float size = VoxelSize(k);
		vec3 local;
		if (!VoxelInside(p, k, local))
			continue;

		// The level whose texel is the footprint's size. A footprint finer
		// than this cascade's voxel reads level 0; one coarser than the
		// cascade resolves moves on to the next cascade out.
		const float mip = log2(max(diameter / size, 1.0));
		if (mip > maxMip && k + 1 < count)
			continue;
		const float level = min(mip, maxMip);

		if (level <= 0.0)
		{
			const float half_ = 0.5 / (float(n) * float(count));
			const float u = clamp((float(k) + local.x) / float(count),
								  float(k) / float(count) + half_,
								  float(k + 1) / float(count) - half_);
			return textureLod(u_VoxelRadiance, vec3(u, local.y, local.z), 0.0);
		}
		if (level < 1.0)
		{
			// Between the isotropic level and the first directional one.
			const float half_ = 0.5 / (float(n) * float(count));
			const float u = clamp((float(k) + local.x) / float(count),
								  float(k) / float(count) + half_,
								  float(k + 1) / float(count) - half_);
			const vec4 fine = textureLod(u_VoxelRadiance, vec3(u, local.y, local.z), 0.0);
			const vec4 coarse = VoxelFaceSample(local, k, direction, 0.0);
			return mix(fine, coarse, level);
		}
		return VoxelFaceSample(local, k, direction, level - 1.0);
	}

	return vec4(0.0);
}

// One cone from `origin` along `direction`, whose diameter at distance d is
// `ratio * d`, accumulating front to back until it is occluded or leaves the
// outermost cascade. `voxel` is the voxel size where the cone starts -- the
// origin's cascade's -- which is how far out the first footprint is taken
// and the smallest a footprint is. RGB is what the cone saw, A how much of
// it was blocked -- and **a cone that leaves the volume having seen nothing
// returns zero** (7bb): the probe is the sky's to supply.
vec4 TraceCone(vec3 origin, vec3 direction, float ratio, float maxDistance, float voxel)
{
	vec3 colour = vec3(0.0);
	float alpha = 0.0;

	// Start one voxel out, so the first footprint clears the surface the
	// cone stands on; the caller has already lifted the origin off it.
	float d = voxel;
	for (int i = 0; i < 64 && d < maxDistance && alpha < 0.95; i++)
	{
		const float diameter = max(ratio * d, voxel);
		const vec4 sample_ = VoxelSample(origin + direction * d, direction, diameter);

		// Premultiplied: rgb is radiance times occupancy already, so the
		// front-to-back blend adds it scaled only by what is still visible.
		// The occupancy is taken as read, with no correction to the step:
		// these are surfaces, not a medium, and a thin wall sampled twice
		// over should block as a wall does. Measured against the traced
		// form on the GI fixture, this lands within five per cent of it.
		colour += (1.0 - alpha) * sample_.rgb;
		alpha += (1.0 - alpha) * sample_.a;

		d += diameter * 0.5;
	}

	return vec4(colour, alpha);
}

// The gather every surface takes (7bc): six cones about the normal -- one
// along it, five around it at sixty degrees, rotated about the normal by
// `rotation` -- weighted by the cosine of each direction and normalised so
// the answer is the cosine-weighted mean radiance over the hemisphere: the
// quantity the traced form writes and the screen gather writes, so the three
// land in one buffer in one unit.
//
// Each cone's diameter is 0.577 of its distance -- a half-angle of about
// sixteen degrees, not the thirty that would tile the hemisphere. Measured
// on the GI fixture: the wider cone reads a nearby wall through a footprint
// that already spans it and the floor beside it, and the red bounce on the
// wall next to the red one came out at a third of the traced form's; the
// narrower one reads the wall as the wall, and lands at three quarters.
// Six narrow cones are a sparser quadrature of the hemisphere than six wide
// ones, and what that costs is noise the per-pixel rotation, the blur and
// GI denoise after this absorb.
//
// `surface` is the point on the surface; the cones start a voxel and a half
// above it along the normal -- the voxel of whichever cascade the point is
// in, because that is how thick the surface is there -- so no cone reads
// the voxel its own surface was written into. Lifting by the finest
// cascade's voxel alone left every wall on a coarser cascade's edge reading
// itself through the first sample, which on the GI fixture doubled the
// bounce a wall received and buried the red that came from next door.
vec3 GatherCones(vec3 surface, vec3 normal, float rotation, float maxDistance)
{
	const float kRatio = 0.577;
	const float kSideCos = 0.5;           // cos 60
	const float kSideSin = 0.8660254;     // sin 60
	const int kSides = 5;

	const float voxel = VoxelSize(VoxelCascadeAt(surface));
	const vec3 origin = surface + normal * (1.5 * voxel);

	const vec3 axis = abs(normal.x) < 0.7 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	const vec3 tangent = normalize(cross(axis, normal));
	const vec3 bitangent = cross(normal, tangent);

	vec3 gathered = TraceCone(origin, normal, kRatio, maxDistance, voxel).rgb;
	float weight = 1.0;

	for (int i = 0; i < kSides; i++)
	{
		const float theta = rotation + float(i) * (6.2831853 / float(kSides));
		const vec3 direction = normalize(tangent * (cos(theta) * kSideSin)
									   + bitangent * (sin(theta) * kSideSin)
									   + normal * kSideCos);
		gathered += TraceCone(origin, direction, kRatio, maxDistance, voxel).rgb * kSideCos;
		weight += kSideCos;
	}

	return gathered / weight;
}
