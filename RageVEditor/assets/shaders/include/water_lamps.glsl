// What the sea's two lamp passes share: what a water pixel is, which lamps
// reach it, what a lamp is worth there, and how a choice is packed.
//
// **Nothing here declares a light, a cell or a structure.** Both passes
// include pbr_fragment.glsl under RV_TRACE_ONLY, which is how the traced
// bounce borrows the lit shader's set 0, its material heap and its rays --
// "a second copy of that light loop is how two renderers start disagreeing
// about what a hit looks like", as rtgi_trace puts it. So the lights, the
// cluster lists, the sixteen-byte cull records, the acceleration structure
// and the shadow ray with its cutout test are all the lit shader's own, and
// what is below is only what the sea's lamp passes add.
//
// **The score is the one S4a measured**, and it is why the sampler works:
// the unshadowed term's luminance, from the water's own anisotropic lobe
// with its Fresnel and its masking. Three cheaper scores were tried and each
// lost -- the irradiance alone by ten times, the same with GGX's
// distribution by eight, the right lobe without the Fresnel by four.
// RAY-BUDGET-DESIGN Part IV, "S4a, the sampler, measured", has the table.

// The sea's surface, from the pass that drew it. Set 3 rather than a spare
// binding in set 0, for the reason rtgi_trace gives: set 0 is the scene's,
// shared with every lit pipeline, and adding to it would change a layout
// four other shaders reflect.
layout(set = 3, binding = 0) uniform sampler2D u_WaterSurfaceIn;    // oct N, roughness, wind
layout(set = 3, binding = 1) uniform sampler2D u_WaterMaterialIn;   // albedo, specular
layout(set = 3, binding = 2) uniform sampler2D u_WaterPositionIn;   // world position, mask

// The choices: four lamps a pixel, the light's index and the confidence in
// one texture, the weight in the other. Two textures because a weight is a
// float and an index is not, and packing the weight into a half would
// quantise the one number the estimate divides by.
layout(set = 3, binding = 3) uniform usampler2D u_ReservoirIndex;
layout(set = 3, binding = 4) uniform usampler2D u_ReservoirWeight;

layout(push_constant) uniform LampParams
{
	// Where a point was on screen last frame. The sea writes no velocity and
	// its waves are not a rigid body, so the camera's own motion is all there
	// is to reproject by -- which is what TAA already does for it.
	mat4 PreviousViewProjection;
	// x: last frame's choices are there to reuse. y: the most confidence a
	// history may carry, in frames. z: whether the reuse runs at all. w: one
	// where a texture row runs the other way up from a normalised coordinate.
	vec4 History;
	// **The pixel under the microscope** (--lamp-probe=x,y): xy the pixel, z
	// non-zero where one was asked for. A picture cannot show the two numbers
	// this estimate turns on -- the sum of the scores a pixel swept, and the
	// score of the lamp it kept -- so one pixel writes them into a buffer the
	// CPU reads back and prints.
	vec4 Probe;
	// WR-16 S5, for the mirror pass alone: x the probe whose irradiance a
	// traced hit is lit by -- the water draw takes it per instance and a
	// fullscreen pass has no instance -- and y how many surface texels one
	// of its own covers. Zero on every other pass here.
	vec4 Trace;
} u_Lamps;

// Thirty-two floats, one pixel, written once a frame. Slot by slot:
//   0 candidates swept   1 the sum of their scores   2 the cell's length
//   3 K                  4..7 the chosen lamps       8..11 their scores
//   12..15 their weights 16..19 their unshadowed terms
//   20..23 their visibility
//   24 the estimate      25 the full walk with rays  26 the full walk without
//   27 the sum of scores recomputed here (against slot 1)
//   28 the pixel's own screen position, packed
layout(std430, set = 3, binding = 5) buffer LampProbeBlock
{
	float Values[];
} u_LampProbe;

// **The one place a projected coordinate becomes a framebuffer row.** The
// reprojected history arrives in normalised device coordinates while the
// attachments are addressed by row, and which way the rows run is the
// backend's business -- so it rides in the push constant, as it does for
// every other fullscreen pass in this engine.
ivec2 LampTexelFromUv(vec2 uv)
{
	const vec2 size = vec2(textureSize(u_WaterPositionIn, 0));
	const float row = u_Lamps.History.w > 0.5 ? 1.0 - uv.y : uv.y;
	return ivec2(clamp(vec2(uv.x, row), vec2(0.0), vec2(0.9999)) * size);
}

bool LampProbeHere()
{
	return u_Lamps.Probe.z > 0.5
		&& uvec2(gl_FragCoord.xy) == uvec2(u_Lamps.Probe.xy + 0.5);
}

const uint RV_LAMP_RESERVOIRS = 4u;

// K and the score's kind ride in the lane the forward sampler reads, so one
// flag drives both: RayRates.w, bits 16-19 and 20-21 (see EngineConfig's
// --light-sampling).
int LampBudget()
{
	return clamp((int(u_Scene.RayRates.w + 0.5) >> 16) & 15, 0, int(RV_LAMP_RESERVOIRS));
}

bool LampScoreIsTerm()
{
	return ((int(u_Scene.RayRates.w + 0.5) >> 20) & 3) != 0;
}

// The third arm: the score is the shaded term itself.
bool LampScoreIsExact()
{
	return ((int(u_Scene.RayRates.w + 0.5) >> 20) & 3) == 2;
}

// A water pixel, as both passes read it.
struct WaterPoint
{
	vec3  Position;
	vec3  N;
	vec3  V;
	vec3  Albedo;
	float Roughness;    // the RMS slope, not a perceptual roughness
	float Specular;
	float Wind;         // radians
	bool  Valid;
};

// **By framebuffer texel, never by coordinate.** These three attachments are
// written by a raster pass at its own fragment position and read by the water
// shader at its own -- both framebuffer-addressed. A fullscreen pass in
// between is the one place where a *coordinate* enters, and on Vulkan a
// fullscreen triangle's v_UV runs the other way up: the pass standing at row
// y read the sea at row H-1-y, chose and shaded for that point, and wrote the
// answer at row y. Nothing was dim and nothing was mis-weighted -- the whole
// band was reflected about the middle of the screen, which is why it looked
// like the glitter had walked away from the bridge. The engine has the trap
// written down in PostProcess.cpp: the negative-height viewport fixes where a
// fragment writes, not where a coordinate reads.
WaterPoint FetchWaterPoint(ivec2 texel)
{
	WaterPoint p;
	const vec4 position = texelFetch(u_WaterPositionIn, texel, 0);
	p.Valid = position.w > 0.5;
	p.Position = position.xyz;
	const vec4 surface = texelFetch(u_WaterSurfaceIn, texel, 0);
	// The two horizontal components, with the vertical one recovered: a sea's
	// normal always points up, so nothing is lost and nothing is quantised.
	p.N = vec3(surface.x, sqrt(max(1.0 - dot(surface.xy, surface.xy), 0.0)), surface.y);
	p.Roughness = surface.z;
	p.Wind = surface.w;
	const vec4 material = texelFetch(u_WaterMaterialIn, texel, 0);
	p.Albedo = material.rgb;
	p.Specular = material.a;
	p.V = normalize(u_Scene.CameraPosition.xyz - p.Position);
	return p;
}

// The cell this point falls in. The lit shader takes the tile from its clip
// position; a fullscreen pass takes it from the coordinate it is shading,
// which is the same tile. The slice is distance along the camera's forward
// axis, because that is what the slices were cut against.
uint WaterClusterCell(vec3 worldPos, vec2 uv)
{
	const vec2 tileCount = u_Scene.ClusterGrid.xy;
	// **Through the projection, not through the pixel's own coordinate.** The
	// lit shader takes its tile from its clip position, and the two have to
	// land in the same cell or this pass scores a list of lamps that do not
	// reach the point it is shading. Deriving it the same way removes the
	// question of whether a screen coordinate and a normalised device
	// coordinate agree about which way is up on this backend.
	const vec4 clip = u_Scene.ViewProjection * vec4(worldPos, 1.0);
	const vec2 ndc = clip.xy / max(abs(clip.w), 1.0e-6) * sign(clip.w);
	const uvec2 tile = uvec2(clamp(ndc * 0.5 + 0.5, vec2(0.0), vec2(0.9999)) * tileCount);
	const float viewDepth = dot(worldPos - u_Scene.CameraPosition.xyz,
								u_Scene.CameraForward.xyz);
	float slice = 0.0;
	if (viewDepth > u_Scene.ClusterDepth.x)
		slice = log(viewDepth) * u_Scene.ClusterDepth.z + u_Scene.ClusterDepth.w;
	const uint z = uint(clamp(slice, 0.0, u_Scene.ClusterGrid.z - 1.0));
	return (z * uint(tileCount.y) + tile.y) * uint(tileCount.x) + tile.x;
}

// WR-16 S2's record again: sixteen bytes decide whether the eighty are read.
// The sea is never inside the irradiance field -- no volume covers the bay,
// which is why these passes exist -- so only the range test applies.
bool LampOutOfRange(uint index, vec3 position)
{
	const vec4 cull = u_LightCull.Cull[index];
	const vec3 toLight = cull.xyz - position;
	const float range = unpackHalf2x16(floatBitsToUint(cull.w)).y;
	return dot(toLight, toLight) >= range * range;
}

// One lamp, shaded in full, in the order the lit shader does it: the sized
// source's closest point for the half vector, the widening its angular size
// adds to the slope, then the streak frame it turns the lobe into.
void ShadeLamp(WaterPoint p, uint index, out vec3 diffuse, out vec3 specular,
			   out vec3 L, out float toward)
{
	diffuse = vec3(0.0);
	specular = vec3(0.0);
	L = vec3(0.0, 1.0, 0.0);
	toward = 0.0;

	const GpuLight light = u_Lights.Lights[index];
	const vec3 toLight = light.Position.xyz - p.Position;
	const float distance2 = dot(toLight, toLight);
	toward = sqrt(max(distance2, 1.0e-8));
	L = toLight / toward;

	const float range = max(light.Params.x, 0.0001);
	const float t = distance2 / (range * range);
	const float ratio = clamp(1.0 - t * t, 0.0, 1.0);
	float attenuation = (ratio * ratio) / max(distance2, 0.0001);
	if (light.Params.z < light.Params.y)
	{
		const float theta = dot(L, -light.Direction.xyz);
		attenuation *= clamp((theta - light.Params.z)
							 / max(light.Params.y - light.Params.z, 0.0001), 0.0, 1.0);
	}
	const float NdotL = max(dot(p.N, L), 0.0);
	if (attenuation <= 0.0 || NdotL <= 0.0)
	{
		toward = 0.0;
		return;
	}

	const vec3 radiance = light.Color.rgb * light.Color.a * attenuation;
	const vec3 N = p.N;
	const vec3 V = p.V;
	const float NdotV = max(dot(N, V), 0.0);

	// The half vector a sized source deserves: the point on the sphere
	// closest to the mirror direction, not its centre.
	vec3 H = normalize(V + L);
	float specRoughness = p.Roughness;
	float specScale = 1.0;
	const float radius = light.Direction.w;
	if (radius > 0.0)
	{
		const vec3 R = reflect(-V, N);
		const vec3 centreToRay = dot(toLight, R) * R - toLight;
		const vec3 closest = toLight + centreToRay *
			clamp(radius * inversesqrt(max(dot(centreToRay, centreToRay), 1.0e-8)), 0.0, 1.0);
		H = normalize(V + normalize(closest));

		// Water's roughness is the RMS slope itself, so the angular radius
		// adds to it directly.
		const float angular = radius * inversesqrt(max(distance2, 1.0e-8));
		const float widened = min(specRoughness + 0.5 * angular, 1.0);
		specScale = (specRoughness * specRoughness) / max(widened * widened, 1.0e-12);
		specRoughness = widened;
	}

	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - N * dot(windDir, N));
	const vec3 windB = cross(N, windT);

	float NDF;
	float G;
	vec3 Tv = V - N * dot(V, N);
	const float tv2 = dot(Tv, Tv);
	if (radius > 0.0 && tv2 > 1.0e-6)
	{
		Tv *= inversesqrt(tv2);
		const vec3 Bv = cross(N, Tv);
		const float ax0 = max(specRoughness * 1.16, 0.02);
		const float ay0 = max(specRoughness * 0.86, 0.02);
		const float angular = radius * inversesqrt(max(distance2, 1.0e-8));
		const float axS = min(ax0 * 3.0 + 4.0 * angular, 1.0);
		const float ayS = min(ay0 + 0.5 * angular, 1.0);
		specScale = sqrt((ax0 * ay0) / (axS * ayS));
		NDF = WaterBeckmannDX(H, N, Tv, Bv, axS, ayS);
		G   = WaterBeckmannG1X(V, N, Tv, Bv, ax0, ay0)
			* WaterBeckmannG1X(L, N, Tv, Bv, ax0, ay0);
	}
	else
	{
		NDF = WaterBeckmannD(H, N, windT, windB, specRoughness);
		G   = WaterBeckmannG1(V, N, windT, windB, specRoughness)
			* WaterBeckmannG1(L, N, windT, windB, specRoughness);
	}

	const float f0 = 0.08 * clamp(p.Specular, 0.0, 1.0);
	const float VdotH = max(dot(H, V), 0.0);
	const vec3 F = vec3(f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0));
	const vec3 spec = (NDF * G * F * specScale) / (4.0 * NdotV * NdotL + 0.0001);
	const vec3 kD = vec3(1.0) - F;

	diffuse = kD * p.Albedo / PI * NdotL * radiance;
	specular = spec * NdotL * radiance;
}

// **What a lamp is worth at this point.** Zero where it contributes nothing,
// which is what keeps the estimate unbiased: a score of zero has to mean a
// term of zero, or the lamps a score cannot see would be missing rather than
// noisy.
float ScoreLamp(WaterPoint p, uint index, out vec3 L, out float NdotL)
{
	L = vec3(0.0, 1.0, 0.0);
	NdotL = 0.0;

	const GpuLight light = u_Lights.Lights[index];
	if (light.Position.w == 0.0)
		return 0.0;

	const vec3 toLight = light.Position.xyz - p.Position;
	const float distance2 = dot(toLight, toLight);
	L = toLight * inversesqrt(max(distance2, 1.0e-8));
	const float range = max(light.Params.x, 0.0001);
	const float t = distance2 / (range * range);
	const float ratio = clamp(1.0 - t * t, 0.0, 1.0);
	float attenuation = (ratio * ratio) / max(distance2, 0.0001);
	if (light.Params.z < light.Params.y)
	{
		const float theta = dot(L, -light.Direction.xyz);
		attenuation *= clamp((theta - light.Params.z)
							 / max(light.Params.y - light.Params.z, 0.0001), 0.0, 1.0);
	}
	NdotL = max(dot(p.N, L), 0.0);
	if (attenuation <= 0.0 || NdotL <= 0.0)
		return 0.0;

	const vec3 kLum = vec3(0.2126, 0.7152, 0.0722);
	const float lum = dot(light.Color.rgb, kLum) * light.Color.a * attenuation;
	const float f0 = 0.08 * clamp(p.Specular, 0.0, 1.0);
	const float albLum = max(dot(p.Albedo, kLum), 1.0e-3);

	// **The half vector a sized lamp deserves, here too.** The shading
	// aims at the point on the lamp's sphere closest to the mirror
	// direction, not at its centre; a score that aims at the centre is
	// asking a different question of a very narrow lobe, and the lamps
	// it calls bright are not the ones the shading finds bright. On a
	// sea whose lobe is a few hundredths of a radian wide that is not a
	// small difference -- it moves the streak.
	vec3 H = normalize(p.V + L);
	if (light.Direction.w > 0.0)
	{
		const vec3 R = reflect(-p.V, p.N);
		const vec3 centreToRay = dot(toLight, R) * R - toLight;
		const vec3 closest = toLight + centreToRay *
			clamp(light.Direction.w
				  * inversesqrt(max(dot(centreToRay, centreToRay), 1.0e-8)), 0.0, 1.0);
		H = normalize(p.V + normalize(closest));
	}
	const float VdotH = max(dot(H, p.V), 0.0);
	const float fresnel = f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);

	// **The score can be the term itself** (--light-sampling=K,exact), which
	// is what S1's winning arm used: when the score is proportional to what
	// the lamp actually contributes, every choice returns the same estimate
	// and four samples are nearly noiseless. Every approximation to it
	// widens the spread instead, and on a lobe this narrow the spread is
	// the difference between a sheet of light and a scatter of dots.
	if (LampScoreIsExact())
	{
		vec3 ed, es, eL;
		float et;
		ShadeLamp(p, index, ed, es, eL, et);
		return max(dot(ed + es, kLum), 1.0e-9);
	}

	float score = lum * NdotL * albLum * (1.0 - fresnel) / PI;
	if (!LampScoreIsTerm())
		return max(score, 1.0e-9);   // the irradiance arm, kept as the arm it lost as

	// The sea's own lobe, in the frame a sized lamp turns toward the viewer.
	const vec3 windDir = vec3(cos(p.Wind), 0.0, sin(p.Wind));
	const vec3 windT = normalize(windDir - p.N * dot(windDir, p.N));
	const vec3 windB = cross(p.N, windT);
	vec3 viewT = p.V - p.N * dot(p.V, p.N);
	const float viewT2 = dot(viewT, viewT);
	const bool hasStreak = viewT2 > 1.0e-6;
	viewT = hasStreak ? viewT * inversesqrt(max(viewT2, 1.0e-12)) : windT;
	const vec3 viewB = hasStreak ? cross(p.N, viewT) : windB;
	const float NdotV = max(dot(p.N, p.V), 1.0e-3);
	const float angular = light.Direction.w * inversesqrt(max(distance2, 1.0e-8));
	// **The lamp's size widens the slope before the lobe is built**, which
	// is what the shading does and what the score has to do with it. A
	// score built on the unwidened slope calls a lamp a little off the
	// mirror direction dim, while the shading finds it bright: the sampler
	// then picks it rarely and pays it an enormous weight when it does,
	// and a four-sample estimate of a heavy tail reads dark however
	// unbiased it is in the limit.
	const float widened = light.Direction.w > 0.0
						? min(p.Roughness + 0.5 * angular, 1.0) : p.Roughness;
	const float ax0 = max(widened * 1.16, 0.02);
	const float ay0 = max(widened * 0.86, 0.02);

	float ndf, ndfScale, g;
	if (light.Direction.w > 0.0 && hasStreak)
	{
		const float axS = min(ax0 * 3.0 + 4.0 * angular, 1.0);
		const float ayS = min(ay0 + 0.5 * angular, 1.0);
		ndfScale = sqrt((ax0 * ay0) / (axS * ayS));
		ndf = WaterBeckmannDX(H, p.N, viewT, viewB, axS, ayS);
		g = WaterBeckmannG1X(p.V, p.N, viewT, viewB, ax0, ay0)
		  * WaterBeckmannG1X(L, p.N, viewT, viewB, ax0, ay0);
	}
	else
	{
		ndfScale = (p.Roughness * p.Roughness) / max(widened * widened, 1.0e-12);
		ndf = WaterBeckmannD(H, p.N, windT, windB, widened);
		g = WaterBeckmannG1(p.V, p.N, windT, windB, widened)
		  * WaterBeckmannG1(L, p.N, windT, windB, widened);
	}
	score += lum * ndf * ndfScale * g * fresnel * 0.25 / NdotV;
	return max(score, 1.0e-9);
}

// A choice, unpacked: which lamp, how many frames of confidence stand behind
// it, and the weight its term is multiplied by.
struct Reservoir
{
	uint  Index;
	uint  M;
	float W;
};

uvec4 PackReservoirIndices(uint idx[4], uint m[4])
{
	return uvec4((idx[0] & 0xFFFFu) | (min(m[0], 0xFFFFu) << 16),
				 (idx[1] & 0xFFFFu) | (min(m[1], 0xFFFFu) << 16),
				 (idx[2] & 0xFFFFu) | (min(m[2], 0xFFFFu) << 16),
				 (idx[3] & 0xFFFFu) | (min(m[3], 0xFFFFu) << 16));
}

Reservoir UnpackReservoir(uvec4 indices, uvec4 weights, uint slot)
{
	Reservoir r;
	const uint packed = indices[slot];
	r.Index = packed & 0xFFFFu;
	r.M = packed >> 16;
	r.W = uintBitsToFloat(weights[slot]);
	return r;
}

// The hash every draw here comes from: independent per pixel, per reservoir
// and per lamp, because a weighted reservoir is unbiased only when each draw
// is independent of the last -- the trap S1 paid for with a sea that came
// out darker the fewer rays it had.
uint LampHash(uint x)
{
	x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
	return x;
}

float LampRandom(uint seed)
{
	return float(LampHash(seed) >> 8u) * (1.0 / 16777216.0);
}

// The frame the walk advances along, and only while something exists to
// integrate it: the rule every stochastic term in this engine follows, and
// the reason a still frame holds still.
uint LampFrameSalt()
{
	return any(notEqual(u_Scene.Jitter, vec4(0.0)))
		 ? uint(mod(u_Scene.GlobalIllumination.y, 1024.0)) * 0xC2B2AE35u : 0u;
}
