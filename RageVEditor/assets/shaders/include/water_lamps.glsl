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
	// history may carry, in frames. z, w: spare.
	vec4 History;
} u_Lamps;

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

WaterPoint FetchWaterPoint(vec2 uv)
{
	WaterPoint p;
	const vec4 position = textureLod(u_WaterPositionIn, uv, 0.0);
	p.Valid = position.w > 0.5;
	p.Position = position.xyz;
	const vec4 surface = textureLod(u_WaterSurfaceIn, uv, 0.0);
	p.N = OctDecode(surface.xy);
	p.Roughness = surface.z;
	p.Wind = surface.w;
	const vec4 material = textureLod(u_WaterMaterialIn, uv, 0.0);
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

	const vec3 H = normalize(p.V + L);
	const float VdotH = max(dot(H, p.V), 0.0);
	const float fresnel = f0 + (1.0 - f0) * pow(1.0 - VdotH, 5.0);

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
	const float ax0 = max(p.Roughness * 1.16, 0.02);
	const float ay0 = max(p.Roughness * 0.86, 0.02);
	const float NdotV = max(dot(p.N, p.V), 1.0e-3);
	const float angular = light.Direction.w * inversesqrt(max(distance2, 1.0e-8));

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
		const float widened = min(p.Roughness + 0.5 * angular, 1.0);
		ndfScale = (p.Roughness * p.Roughness) / max(widened * widened, 1.0e-12);
		ndf = WaterBeckmannD(H, p.N, windT, windB, widened);
		g = WaterBeckmannG1(p.V, p.N, windT, windB, p.Roughness)
		  * WaterBeckmannG1(L, p.N, windT, windB, p.Roughness);
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
