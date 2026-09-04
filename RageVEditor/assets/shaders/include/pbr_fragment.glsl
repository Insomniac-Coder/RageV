// No #version here: an include is spliced into a file that already has
// one, and GLSL requires it to be the first thing in the shader.
// Shared by every shader that lights a surface.
//
// Included rather than copied. There are two mesh shaders -- static and
// skinned -- differing only in how a vertex reaches world space, and the
// lighting underneath them has to be one implementation. Two copies of six
// hundred lines of PBR would drift, and the way that failure shows up is two
// objects in the same scene shaded differently, which reads as a material
// problem rather than as a duplicated shader.

// The whole of the lighting: clustered light lookup, Cook-Torrance,
// shadows and image-based lighting, ending in main().

const float PI = 3.14159265359;

const int MAP_BASE_COLOR         = 1 << 0;
const int MAP_NORMAL             = 1 << 1;
const int MAP_OCCLUSION          = 1 << 3;
const int MAP_EMISSIVE           = 1 << 4;
// Must match MaterialMap in Material.h. Separate roughness and metallic, which
// is how every texture library that is not a glTF ships them, plus dielectric
// reflectance.
const int MAP_ROUGHNESS          = 1 << 5;
const int MAP_METALLIC           = 1 << 6;
const int MAP_SPECULAR           = 1 << 7;
const int MAP_HEIGHT             = 1 << 8;

// --- macro variation ---------------------------------------------------------
//
// **What stops a tiled surface reading as one flat field at range.** A texture
// that repeats every two metres is fine close up; at four hundred metres the
// eye no longer resolves the tile and every repeat is byte-identical, so a
// large face becomes a perfectly regular field. Nothing authored *inside* the
// tile fixes that: anything bigger than a fraction of it appears once per
// repeat, in rows, and anything smaller averages to grey. The only thing that
// breaks it is a second field at a scale larger than the object, and that
// field can only come from world position at shading time.
//
// A formula rather than a texture, for three reasons: it costs no sampler on a
// path that has none spare, it needs no authoring, and being a pure function of
// position it gives the same answer to the raster shader, the reflection trace
// and the bounce ray -- so a macro-varied pier reflects and bounces as the same
// surface it draws as.
uint MacroHash(uint x)
{
	x ^= x >> 16; x *= 0x7FEB352Du;
	x ^= x >> 15; x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

float MacroLattice(ivec3 cell)
{
	uint h = MacroHash(uint(cell.x * 73856093 ^ cell.y * 19349663 ^ cell.z * 83492791));
	return float(h & 0x00FFFFFFu) / 16777216.0;
}

// Trilinear value noise, smoothstep interpolated. One octave.
float MacroNoise(vec3 p)
{
	vec3 base = floor(p);
	vec3 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	ivec3 c = ivec3(base);

	float n000 = MacroLattice(c + ivec3(0, 0, 0));
	float n100 = MacroLattice(c + ivec3(1, 0, 0));
	float n010 = MacroLattice(c + ivec3(0, 1, 0));
	float n110 = MacroLattice(c + ivec3(1, 1, 0));
	float n001 = MacroLattice(c + ivec3(0, 0, 1));
	float n101 = MacroLattice(c + ivec3(1, 0, 1));
	float n011 = MacroLattice(c + ivec3(0, 1, 1));
	float n111 = MacroLattice(c + ivec3(1, 1, 1));

	return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
			   mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// Three octaves, returned **centred on zero**. Mean-preserving is the whole
// point: a term that only darkened would make a material change its overall
// value the moment macro was switched on, and then every existing look would
// have to be re-tuned around it.
//
// Three and not more because the fourth octave is at an eighth of the scale
// and a quarter of the amplitude -- by then it is competing with the tile it
// exists to break up, and adding it would just be a second texture.
float MacroField(vec3 worldPos, float metres)
{
	vec3 p = worldPos / max(metres, 0.01);
	float n = MacroNoise(p) * 0.5333
			+ MacroNoise(p * 2.03 + 11.7) * 0.2667
			+ MacroNoise(p * 4.11 + 23.4) * 0.2000;
	return n - 0.5;
}

// The modulation itself. **Roughness moves against albedo**, which is not a
// stylistic choice: a darker patch on concrete or rock is a wetter or more
// stained one, and wet is smoother. Moving them together would read as a
// lighting change instead of a surface one.
void ApplyMacro(inout vec3 albedo, inout float roughness, vec3 worldPos,
				float metres, float strength)
{
	if (metres <= 0.0 || strength <= 0.0)
		return;
	float n = MacroField(worldPos, metres);
	albedo *= max(1.0 + strength * n * 2.0, 0.0);
	roughness = clamp(roughness * (1.0 - strength * n * 1.4), 0.045, 1.0);
}

// The same, and **also the relief** -- which is the half that actually matters
// on a mapped surface.
//
// Modulating brightness alone was not enough, and the reason is worth keeping:
// what reads as repetition on a normal-mapped wall is not tone, it is the
// *bump pattern* recurring, and no amount of darkening one copy hides the fact
// that the next one has the same dents in the same places. Fading the mapped
// normal towards the geometric one over the macro field varies how deep the
// relief looks from place to place, so the grid stops being a grid.
//
// Toward the geometric normal and never past it: this may flatten relief, never
// invert or exaggerate it, so a surface can lose its bumps in a patch but can
// never grow bumps the map does not have.
void ApplyMacro(inout vec3 albedo, inout float roughness, inout vec3 normal,
				vec3 geometricNormal, vec3 worldPos, float metres, float strength)
{
	if (metres <= 0.0 || strength <= 0.0)
		return;
	float n = MacroField(worldPos, metres);
	albedo *= max(1.0 + strength * n * 2.0, 0.0);
	roughness = clamp(roughness * (1.0 - strength * n * 1.4), 0.045, 1.0);
	float flatten = clamp(strength * (0.5 - n) * 1.6, 0.0, 0.85);
	normal = normalize(mix(normal, geometricNormal, flatten));
}

layout(set = 0, binding = 0) uniform SceneData
{
	mat4 ViewProjection;
	vec4 CameraPosition;
	vec4 Ambient;
	vec4 Environment;
	vec4 EnvironmentSize;
	vec4 ClusterGrid;
	vec4 ClusterDepth;
	mat4 CascadeLookup[4];
	vec4 CascadeSplits;
	vec4 CascadeTexel;
	vec4 CameraForward;
	vec4 ShadowParams;
	mat4 SpotLookup[4];
	int  LightCount;
	int  _pad0;
	int  _pad1;
	int  _pad2;

	// Not read in this stage. Declared anyway, because Jitter below has to
	// land on the offset the CPU wrote it to -- a stage may declare a shorter
	// view of a block, but not one whose members sit anywhere else. Leaving
	// this out is a shader reading the wrong sixteen bytes and no error.
	mat4 PreviousViewProjection;

	// xy = this frame's sub-pixel offset in NDC, zw = last frame's. Taken
	// back out of the motion vector at the top of main -- see there, and
	// ENGINE-NOTES 7r.
	vec4 Jitter;

	// Screen-space reflections. x = intensity, zero when there is no trace
	// to read -- the feature is off, this is a probe face or a shadow
	// caster, or the chain has not drawn a frame yet. y = the sign that
	// takes an NDC y-offset into the trace texture's row direction: -1 on
	// the backend whose row 0 is the top. ENGINE-NOTES 7af.
	vec4 ScreenReflections;

	// Ray-traced global illumination (ENGINE-NOTES 7at). x = intensity, zero
	// when the traced form is not running; y = a counter the bounce rays hash
	// so successive frames cast different directions. zw unused.
	//
	// Declared in *both* mirrors of this block -- pbr_fragment.glsl and
	// scene_vertex.glsl -- because OpenGL links the stages into one program
	// and two spellings of one uniform block is undefined ground: the first
	// draft added it to the fragment mirror alone and every OpenGL frame came
	// out different.
	vec4 GlobalIllumination;

	// Last frame's indirect diffuse (ENGINE-NOTES 7av). x = intensity, zero
	// when nothing is bound; y = the row sign. Mirrored in scene_vertex.glsl.
	vec4 Indirect;

	// **Where the reflection probes stand.** xyz the position, w the influence
	// radius, in world units; ProbeSlot[i].x is that probe's index into the
	// irradiance and environment cube arrays.
	//
	// Here rather than behind a binding of their own: set 0's bindings run 0
	// to 17 with none free, and every pipeline family allocates its own set
	// against its own layout, so a new binding would have to be declared and
	// written in each. This block is already bound everywhere and had room.
	//
	// Read by the lit shader, which blends the two that cover a fragment
	// instead of taking the one the CPU chose for the whole object. That is
	// what stops a mesh flipping its entire ambient and reflection in one
	// frame as it crosses an influence boundary.

	// **The irradiance atlas.** Centre.w is the atlas's depth -- the stride
	// from one tile of the texture to the next -- and Extents.w is how many
	// volumes it holds; zero means every reader falls back to the flat ambient
	// above. (Both carried a single composed box until 2026-08-27.)
	vec4 IrradianceCentre;
	vec4 IrradianceExtents;
	// Five rows a volume, up to eight: centre|zOffset, extents|spacing, and
	// the three rows that take a world direction into that box's own axes,
	// each carrying one of its cell counts in w.
	vec4 IrradianceBox[48];
	vec4 ProbeCount;              // x = how many rows are real
	vec4 ProbePlacement[15];
	vec4 ProbeSlot[15];

	// **WR-17: shadow rays thin with distance.** x = the falloff shape (0 off,
	// 1 hard, 2 linear, 3 smoothstep, 4 log, 5 share; +16 = a skipped ray
	// counts its light lit, the measurement arm), y = the distance from the
	// shaded point at which a light's rays start thinning, z = where the
	// thinning reaches its floor, w = under the share shape the share of the
	// pixel's direct light below which a light earns no ray, under the
	// distance shapes the fraction of rays still traced past the end.
	// Render settings, so one dial drives every light. Appended; mirrored
	// by hand in scene_block.glsl and Renderer3D.cpp.
	vec4 ShadowRayFade;

	// **WR-18: the water's ray rate and reach.** x = 1 when every water pixel
	// traces its own mirror and refraction rays, 2 when one lane of each 2x2
	// quad traces and the other three take its answer (half size in each
	// direction, inside the shader that has the exact normal). y = the
	// transmittance under which the refraction ray stops looking (0 = the
	// 300 m it always had). z = 1 skips the traced hit's light walk (a
	// measurement flag, --hit-lights=off). w unused. Appended; mirrored in scene_block.glsl
	// and Renderer3D.cpp.
	vec4 RayRates;
} u_Scene;

// Every light in the scene, however many that is.
//
// A storage buffer rather than an array in the scene block, and that is the
// whole of what removes the eight-light cap: a uniform block has to declare a
// fixed length, and the length it declared was eight. Nothing else about the
// lighting changed with this -- the loop below still reads every light -- so
// the picture is a strict check that the move itself was faithful.
struct GpuLight
{
	// xyz position, w = 1 for positional and 0 for directional
	vec4 Position;
	// xyz forward axis: the direction a directional light travels and the cone
	// axis of a spot. Not derivable from the position, which is why it is here.
	// w = the emitter's radius in metres (Light::SourceRadius); 0 is a point.
	vec4 Direction;
	// rgb colour, a intensity
	vec4 Color;
	// x range, y cos(inner cone), z cos(outer cone), w mobility: 0 realtime,
	// 1 half bake, 2 full bake, 3 plus the half-bake radius for a hybrid light
	// (see BakedShare)
	vec4 Params;
	// x kind of shadow map (0 none), y slot, z far distance, w texel scale
	vec4 Shadow;
};

layout(std430, set = 0, binding = 8) readonly buffer LightBlock
{
	GpuLight Lights[];
} u_Lights;

// **How much of a light the bake owns at a point** (ENGINE-NOTES 7cx, Hybrid
// Full Bake): none of a realtime or half-baked light, all of a fully baked
// one, and of a hybrid light -- Params.w is 3 plus its half-bake radius in
// metres -- a smooth step from none within the radius of the lamp to all a
// short band beyond it. The fill stores exactly this share and the live
// loops light the rest, so the two sum to one at every distance: the bright
// spot under a lamp and the glow it feeds stay live and exact, the far light
// that is most of the cost is the field's. A hybrid directional light has no
// distance to measure and is lit live, as Half bake.
float BakedShare(GpuLight light, vec3 position)
{
	if (light.Params.w < 1.5)
		return 0.0;
	if (light.Params.w < 2.5)
		return 1.0;
	if (light.Position.w == 0.0)
		return 0.0;
	const float radius = light.Params.w - 3.0;
	// A metre or two of blend, a tenth of the radius at most.
	const float band = max(radius * 0.1, 1.0);
	return smoothstep(radius - band, radius + band, distance(light.Position.xyz, position));
}

// The cluster grid: which lights reach which cell of the view frustum.
//
// Cells hold a range into the index list rather than the lights themselves,
// because a light reaching several cells would otherwise be stored several
// times.
struct LightCell
{
	uint Offset;
	uint Count;
	// WR-16 S2: the cell's live sublist -- the lamps a static surface deep
	// inside the irradiance field still has to walk, in the full list's own
	// order. Mirrors LightGrid::Cell.
	uint LiveOffset;
	uint LiveCount;
};

layout(std430, set = 0, binding = 9) readonly buffer CellBlock
{
	LightCell Cells[];
} u_Cells;

layout(std430, set = 0, binding = 10) readonly buffer CellIndexBlock
{
	uint Indices[];
} u_CellIndices;

// Arrays, not single cubes, so which environment a surface reflects is an
// index the instance carries rather than a binding the draw carries. Slot 0 is
// the sky in both, which is what lets "no probe reaches this object" be an
// index rather than a branch -- and what keeps a scene with no probes at all
// costing exactly what it did before.
layout(set = 0, binding = 1) uniform samplerCubeArray u_Environment;
layout(set = 0, binding = 5) uniform samplerCubeArray u_Irradiance;
layout(set = 0, binding = 6) uniform sampler2D u_BRDF;

// Last frame's screen-space reflection trace: RGB the radiance the ray found
// in this surface's reflected direction, A how far to trust it (zero: it
// found nothing, or nothing was traced). Read where the probe's reflected
// radiance is read, and mixed with it there, so the traced answer goes
// through exactly the weight the probe's does. ENGINE-NOTES 7af.
//
// Binding 12, not 11: this file is included by the skinned variant too, and
// its vertex stage keeps the bone buffer at set 0 binding 11. One set, one
// binding, two descriptor types is a pipeline that will not build -- and on
// the first attempt it did not, as a device loss the moment the fox drew.
layout(set = 0, binding = 12) uniform sampler2D u_ScreenReflections;

// Last frame's indirect diffuse, albedo-free, A the confidence (7av). Added
// to the probe's irradiance *before* the diffuse term multiplies by albedo,
// which is the whole point: the bounce is tinted by what this surface is.
// Binding 16 for the reason 12 is not 11 -- see above.
layout(set = 0, binding = 16) uniform sampler2D u_Indirect;

// Comparison samplers: the hardware compares against the reference and filters
// the answers, which is a 2x2 percentage-closer filter for one fetch. Four
// separate maps rather than one array texture, because indexing an array of
// samplers by a value that varies per fragment is not dynamically uniform --
// the switch below keeps every index constant, which is always legal.
layout(set = 0, binding = 2) uniform sampler2DShadow u_ShadowMaps[4];
layout(set = 0, binding = 3) uniform sampler2DShadow u_SpotShadows[4];
layout(set = 0, binding = 4) uniform samplerCubeShadow u_PointShadows[4];

// Shared with ShadowMap.cpp. A point light's faces are all rendered with the
// same near plane, so it does not need storing per light -- but the two have to
// agree, or every comparison is against a depth from a different projection.
const float POINT_SHADOW_NEAR = 0.05;

// Ray-traced shadows (ENGINE-NOTES 7am, 7an): the frame's acceleration
// structure, which the light loop traces into for every casting light --
// along L to infinity for a directional one, along L to the light for a
// spot or a point -- instead of looking a map up. Compiled in only when the
// project asks for traced shadows on a device that can; the map path is the
// same file with this block absent.
#ifdef RV_RAY_SHADOWS
#extension GL_EXT_ray_query : require
layout(set = 0, binding = 14) uniform accelerationStructureEXT u_SceneAS;
#endif

// **WR-16 S2: the light's cull record, sixteen bytes** (ENGINE-NOTES 7cz;
// GpuLightCull on the CPU side). The hit walk was reading a lamp's eighty
// bytes to learn it was out of range or fully baked -- 84 to 156 lamps per
// hit, 8 to 12 ms a frame on the bridge, most of it reads of lamps that
// contribute nothing. This record is what a loop needs to drop a lamp:
// its position, its range, and its class. xyz the position; w's bits, as
// an integer: 31..16 the range as a half float rounded UP, 15..14 the
// class (0 live: realtime or half baked; 1 fully baked; 2 hybrid), 13 a
// moving object inside its range this frame, 12..0 the hybrid lamp's
// radius plus its blend band in eighths of a metre, rounded UP. Every
// rounding is upward, so a lamp this record rejects is one the full record
// would have found contributing exactly zero: the walk is bit-identical
// with and without it. Under the same define as the structure; not in the
// fill, which walks every baked lamp by design.
#if defined(RV_RAY_SHADOWS) && !defined(RV_IRRADIANCE_FILL)
#define RV_LIGHT_CULL
layout(std430, set = 0, binding = 23) readonly buffer LightCullBlock
{
	vec4 Cull[];
} u_LightCull;

const uint LIGHT_CULL_LIVE   = 0u;
const uint LIGHT_CULL_FULL   = 1u;
const uint LIGHT_CULL_HYBRID = 2u;

// **Whether the full record can be skipped**, from the cull record alone.
// `insideField` is "a static surface with the field's weight at one" --
// where a fully baked lamp's live share is exactly zero and a hybrid lamp's
// is zero beyond its radius and band -- and `onScreen` says whether the
// subtractive ray for a moving object applies (it does not at a hit).
// Both tests reject only what the full loop would have found to be nothing.
bool LightCullRejects(uint index, vec3 position, bool insideField, bool onScreen)
{
	const vec4 cull = u_LightCull.Cull[index];
	const uint packed = floatBitsToUint(cull.w);
	const vec3 toLight = cull.xyz - position;
	const float distance2 = dot(toLight, toLight);
	// The range, rounded up when it was packed: past it the falloff window
	// is exactly zero. A directional light packs infinity here.
	const float range = unpackHalf2x16(packed).y;
	if (distance2 >= range * range)
		return true;
	if (!insideField)
		return false;
	const uint cls = (packed >> 14u) & 3u;
	if (cls == LIGHT_CULL_LIVE)
		return false;
	if (onScreen && (packed & (1u << 13u)) != 0u)
		return false;   // the subtractive ray, traced on screen
	if (cls == LIGHT_CULL_FULL)
		return true;
	const float reject = float(packed & 0x1FFFu) * 0.125;
	return distance2 >= reject * reject;
}
#endif

// **WR-16 S0: the rays are counted where they are cast** (ENGINE-NOTES 7cy;
// RayCounters on the CPU side, whose Lane enum this order mirrors). Every
// launch site below adds one to a per-invocation register; main adds the
// registers into the frame's counter buffer once, at its end -- a subgroup
// reduction and a single atomic per lane per wave, from a lane that is not
// a helper, because a helper invocation's stores have no effect and an
// electee that happened to be one would drop the wave's count. Under the
// same define as the structure, so the OpenGL layouts never see the
// binding; not in the irradiance fill, whose rays are bake time and whose
// set is its own.
#if defined(RV_RAY_SHADOWS) && !defined(RV_IRRADIANCE_FILL)
#define RV_RAY_COUNTERS
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_ballot : require

// **Sixty-four copies of the sixteen lanes, one per screen tile, summed on
// the CPU after the readback.** Every wave in the frame adds into this
// buffer -- 1.4 million atomics a frame on Headland -- and atomics to one
// address serialise in the L2, so the lanes are spread over sixty-four
// lines by the wave's position on screen; the 4 KB the CPU sums is
// nothing. Measured honestly: the spread changed nothing on this GPU (the
// 13 ms the flush first cost was the lost early depth test, below), and it
// is kept as the right shape for a counter every wave touches rather than
// as a fix for anything seen. (RayCounters::kSlots on the CPU side; the
// two must agree.)
layout(std430, set = 0, binding = 21) buffer RayCounterBlock
{
	uint Counts[64 * 16];
} u_RayCounters;
const uint RAY_COUNTER_SLOTS = 64u;

// **The depth test stays in front of the shader.** A fragment shader with a
// memory side effect -- these atomics -- may no longer be culled by depth
// before it runs, because the side effect would be observable; so without
// this line every fragment the prepass had already rejected is shaded
// again, and the frame pays for the counting with its overdraw: measured
// +11 ms on Headland's 59, with the contention and the registers both ruled
// out first. Forcing the early test is exact for the families that neither
// discard nor write depth -- the opaque and the water -- and counts only the
// fragments the picture keeps, which is what a per-pixel figure means. The
// alpha-cutout family cannot take it: an early depth write survives its
// discard and a picket would leave depth where its holes are. It counts
// late, and its overdraw is the price of counting there.
#if !defined(RV_ALPHA_CUTOUT) && !defined(RV_TRACE_ONLY)
layout(early_fragment_tests) in;
#endif

// Which copy this wave adds into: the 8x4 block of pixels the leader lane
// sits in, rows offset so neighbouring waves in x and in y both land on
// different lines. Any assignment is correct -- the CPU sums them all --
// this one merely keeps waves that run together apart.
uint RayCounterSlot()
{
	const uvec2 px = uvec2(gl_FragCoord.xy);
	return ((px.x >> 3u) + (px.y >> 2u) * 7u) & (RAY_COUNTER_SLOTS - 1u);
}

const uint RAY_LANE_SHADOW      = 0u;   // every shadow ray, on screen and at hits
const uint RAY_LANE_WATER       = 1u;   // the water's mirror and refraction rays
const uint RAY_LANE_REFLECTION  = 2u;   // the opaque surfaces' mirror rays
const uint RAY_LANE_GI          = 3u;   // the traced bounce's rays
const uint RAY_LANE_AO          = 4u;   // the occlusion pass's taps (counted there)
const uint RAY_LANE_LIT         = 5u;   // fragments this shader shaded
const uint RAY_LANE_LIGHTS      = 6u;   // lights those fragments walked, summed
const uint RAY_LANE_LIGHTS_MAX  = 7u;   // the largest list any fragment walked
const uint RAY_LANE_HITS        = 8u;   // traced surfaces shaded
const uint RAY_LANE_HIT_LIGHTS  = 9u;   // lights those hits walked, summed

// Which lane TraceSurface's rays belong to is a property of the compile,
// not of the call: the bounce's trace includes this file under
// RV_TRACE_ONLY, the water under RV_WATER, and the opaque families under
// neither.
#if defined(RV_TRACE_ONLY)
const uint RAY_LANE_SURFACE = RAY_LANE_GI;
#elif defined(RV_WATER)
const uint RAY_LANE_SURFACE = RAY_LANE_WATER;
#else
const uint RAY_LANE_SURFACE = RAY_LANE_REFLECTION;
#endif

// **Two words, packed, live through the whole of main.** The first form of
// this was an array of five counters and three more words, and it cost the
// Headland frame 26% (59 to 74 ms): eight registers held across a shader
// that was already at its occupancy edge, and an array indexed by a loop
// variable at the flush, which is an array in local memory. The counts a
// fragment can reach are small -- shadow rays under 65,536, surface rays
// and hits under 256, lights under 65,536 -- so they share two registers:
//   A = shadow rays (low 16 bits) | lights this fragment walks (high 16)
//   B = surface rays (low 8) | traced hits (next 8) | lights those hits walked (high 16)
// Which lane the surface rays belong to is decided at compile time
// (RAY_LANE_SURFACE), so no per-lane register is needed for them.
uint g_CountA = 0u;
uint g_CountB = 0u;

#define RV_COUNT_SHADOW_RAY()  (g_CountA += 1u)
#define RV_COUNT_SURFACE_RAY() (g_CountB += 1u)
#define RV_COUNT_HIT(lights)   (g_CountB += (1u << 8) | (uint(max(lights, 0)) << 16))
#define RV_COUNT_LIGHTS(total) (g_CountA |= uint(max(total, 0)) << 16)

// One reduction and at most one atomic per lane per wave. `leader` is the
// lowest active lane that is not a helper; a wave of helpers alone adds
// nothing, which is right -- no pixel of theirs is in the frame. `base` is
// the wave's slot times the lane count.
void CountLane(uint base, uint lane, uint value, bool leader)
{
	const uint sum = subgroupAdd(value);
	if (leader && sum > 0u)
		atomicAdd(u_RayCounters.Counts[base + lane], sum);
}

// Called once, at the end of main, in control flow every live lane of the
// wave reaches: a lane that discarded is inactive and simply absent from
// the reductions, and the discards in this file all happen before any ray.
// `shaded` says whether this invocation counts as a lit fragment (the
// bounce's trace passes false: its pixels are not surfaces).
void FlushRayCounters(bool shaded)
{
	const bool real = !gl_HelperInvocation;
	const uvec4 realLanes = subgroupBallot(real);
	const bool leader = real && gl_SubgroupInvocationID == subgroupBallotFindLSB(realLanes);
	const uint a = real ? g_CountA : 0u;
	const uint b = real ? g_CountB : 0u;
	const uint base = RayCounterSlot() * 16u;

	CountLane(base, RAY_LANE_SHADOW, a & 0xFFFFu, leader);
	CountLane(base, RAY_LANE_SURFACE, b & 0xFFu, leader);
	CountLane(base, RAY_LANE_LIT, (real && shaded) ? 1u : 0u, leader);
	CountLane(base, RAY_LANE_LIGHTS, a >> 16u, leader);
	const uint most = subgroupMax(a >> 16u);
	if (leader && most > 0u)
		atomicMax(u_RayCounters.Counts[base + RAY_LANE_LIGHTS_MAX], most);
	CountLane(base, RAY_LANE_HITS, (b >> 8u) & 0xFFu, leader);
	CountLane(base, RAY_LANE_HIT_LIGHTS, b >> 16u, leader);
}
#else
#define RV_COUNT_SHADOW_RAY()
#define RV_COUNT_SURFACE_RAY()
#define RV_COUNT_HIT(lights)
#define RV_COUNT_LIGHTS(total)
#endif

// **`--debug-view=rays|lights`**: the same two numbers per pixel, added into
// a buffer the debug composite draws as a heat map. Adds rather than stores,
// so a water pixel shows its own rays *and* the deck's beneath it -- what
// the pixel cost, not what the last surface cost. Compiled in only under
// the flag (Renderer3D::kDebugCountsBinding); the header is written by the
// CPU once per size and the planes are zeroed by a fill every frame.
#ifdef RV_DEBUG_VIEW
layout(std430, set = 0, binding = 22) buffer DebugCountBlock
{
	uint Width;
	uint Height;
	uint Pixels;
	uint _pad;
	uint Counts[];
} u_DebugCounts;

void DebugCountsStore()
{
	if (gl_HelperInvocation)
		return;
	const uvec2 px = uvec2(gl_FragCoord.xy);
	const uint index = px.y * u_DebugCounts.Width + px.x;
	if (px.x >= u_DebugCounts.Width || index >= u_DebugCounts.Pixels)
		return;
#ifdef RV_RAY_COUNTERS
	atomicAdd(u_DebugCounts.Counts[index], (g_CountA & 0xFFFFu) + (g_CountB & 0xFFu));
	atomicAdd(u_DebugCounts.Counts[u_DebugCounts.Pixels + index], g_CountA >> 16u);
#endif
}
#endif

// Ray-traced reflections (ENGINE-NOTES 7ao): a hit is shaded, so the hit's
// mesh and material have to be reachable from a shader that never bound
// them. The mesh by *address* -- buffer references over the vertex and index
// buffers the structures were built from -- and the material through the
// heap, by the record index the instance table names. One row per structure
// instance in build order; the hit's custom index is the row. Mirrored by
// hand in Renderer3D.cpp (GpuRayInstance), std430.
#if defined(RV_RAY_REFLECTIONS) || defined(RV_RAY_GI) || defined(RV_RAY_REFRACTION)
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_buffer_reference_uvec2 : require

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RayWords  { uint  Words[]; };
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RayFloats { float Floats[]; };

struct RayInstance
{
	uvec2 PositionAddress;    // the mesh's vertices, or a posed caster's compute-written positions
	uvec2 AttributeAddress;   // the mesh's vertices: normal and uv live here either way
	uvec2 IndexAddress;
	uint  PositionStrideWords;
	uint  AttributeStrideWords;
	uint  MaterialIndex;      // row of u_Materials
	uint  Flags;              // bit 0: posed; bit 1: an area emitter answers for this surface; bit 2: alpha-tested
	// Below this a candidate triangle is not there. Read only when the masked
	// bit is set.
	float AlphaCutoff;
	uint  _pad1;
	vec4  BaseColor;
	vec4  EmissiveColor;
	vec4  Surface;            // metallic, roughness, occlusion, normal scale
};
const uint RAY_INSTANCE_POSED = 1u;
// The area-emitter list holds a rectangle standing for this instance, so a
// shadow ray aimed at that rectangle already counted its emissive and a
// hemisphere hit here must not count it again. Absent for everything the
// list left out -- below the strength threshold, degenerate, or past the cap
// -- whose emissive the hemisphere term is the only estimator of.
const uint RAY_INSTANCE_EMITTER = 2u;

// **This instance is alpha-tested**, so traversal reports its hits as
// candidates and the loop below decides. Set together with the acceleration
// instance's FORCE_NO_OPAQUE: that flag makes the hardware ask, this one tells
// the shader what to answer. Anything without it is committed by the hardware
// and never reaches the test.
const uint RAY_INSTANCE_MASKED = 4u;

// **This instance never moves** (MeshComponent::Static, ENGINE-NOTES 7cx): a
// hit on it takes the fully baked lights from the field, as the surface
// itself does on screen; a hit on a moving instance walks them live.
const uint RAY_INSTANCE_STATIC = 8u;

layout(std430, set = 0, binding = 15) readonly buffer RayInstanceBlock
{
	RayInstance Instances[];
} u_RayInstances;
#endif

// The material: two front doors, one shading (ENGINE-NOTES 7al) -- and a
// third kind of surface behind either door (7aq).
//
// Everything below this block -- every texture(u_BaseColorMap, uv), every
// u_Material.MapFlags -- compiles unchanged in both variants. That is the
// point of forking at the preprocessor: the six hundred lines that could
// drift do not exist twice. RV_LAYERED changes what set 1 *holds* -- a
// block of four layers and, on the bound path, their samplers instead of
// one material's -- and what SampleSurface below assembles from it; on the
// bindless path the heap and the record buffer are declared as ever, since
// the hit shading of a traced reflection still reads them.
#ifdef RV_BINDLESS

// The bindless heap: every texture the scene has registered, in one array
// the shader indexes at runtime. Set 2 by convention, in every shader that
// reads it (TextureHeap::kSet); set 1 is empty in this variant.
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 2, binding = 0) uniform sampler2D u_Textures[];

// What the material set used to say, minus the scalars -- those were already
// per instance. One record per distinct material this frame, indexed by the
// instance. Mirrored by hand in Material.h (GpuMaterial), std430.
struct GpuMaterial
{
	// Heap slots: base colour, normal, occlusion, emissive.
	uvec4 Maps0;
	// Roughness, metallic, specular, height.
	uvec4 Maps1;
	// xy scale, zw offset. See MaterialParams::UvTransform.
	vec4  UvTransform;
	int   MapFlags;
	float Specular;
	float HeightScale;
	// See MaterialParams::AlphaCutoff. Per material rather than per pipeline,
	// which is what lets one masked draw carry many different cutouts.
	float AlphaCutoff;
	// x metres per macro cycle, y strength. See MaterialParams::Macro.
	vec4  Macro;
	// Four floats and a vec3+float, which in std140 is two vec4s worth. See
	// MaterialParams: named on both sides so the inspector can label them.
	float Clearcoat;
	float ClearcoatRoughness;
	float Anisotropy;
	float Subsurface;
	vec3  SheenColor;
	float SheenRoughness;
};

layout(std430, set = 0, binding = 13) readonly buffer MaterialBlock
{
	GpuMaterial Materials[];
} u_Materials;

// Fetched once at the top of main from the instance's record index, then
// read everywhere the bound variant reads its uniform block. A global so
// the helpers below main can see it, and a #define so they need not know
// which variant they are in.
GpuMaterial g_Material;
#define u_Material g_Material

// nonuniformEXT on the *descriptor* index, and only there. Once runs merge
// across materials the slot differs between the instances of one draw, and
// indexing a descriptor array by a value that is not dynamically uniform
// without saying so is undefined behaviour that works on most hardware.
#define u_BaseColorMap u_Textures[nonuniformEXT(g_Material.Maps0.x)]
#define u_NormalMap    u_Textures[nonuniformEXT(g_Material.Maps0.y)]
#define u_OcclusionMap u_Textures[nonuniformEXT(g_Material.Maps0.z)]
#define u_EmissiveMap  u_Textures[nonuniformEXT(g_Material.Maps0.w)]
#define u_RoughnessMap u_Textures[nonuniformEXT(g_Material.Maps1.x)]
#define u_MetallicMap  u_Textures[nonuniformEXT(g_Material.Maps1.y)]
#define u_SpecularMap  u_Textures[nonuniformEXT(g_Material.Maps1.z)]
#define u_HeightMap    u_Textures[nonuniformEXT(g_Material.Maps1.w)]

#elif !defined(RV_LAYERED)

// The bound material set, in its own include because the voxeliser declares
// the same set 1 (ENGINE-NOTES 7bc): Material::Bind writes every one of these
// bindings, so a second shader that binds a material has to declare every
// one of them, and two spellings of one set would drift.
#include "material_bound.glsl"

#endif

// A layered surface (ENGINE-NOTES 7aq): four materials, each an ordinary
// .rmat, in proportions read from a weight map. Set 1 is this block and --
// on the bound path -- thirteen samplers: the weights, and each layer's base
// colour, normal and roughness as arrays of four, so a layer's index is a
// constant wherever a sampler is named. On the bindless path the same maps
// are heap slots inside the block. Three maps per layer and not eight,
// because the shared set 0 already spends sixteen of the thirty-two texture
// units OpenGL gives a fragment stage, and both paths read the same three so
// the pixel comparison between them stays exact. Mirrored by hand in
// Material.h (LayeredParams), std140.
#ifdef RV_LAYERED

const int LAYER_ACTIVE = 1 << 15;
const int LAYERS = 4;

layout(set = 1, binding = 0) uniform LayeredData
{
	vec4  BaseColor[LAYERS];
	vec4  EmissiveColor[LAYERS];
	// metallic, roughness, occlusion, normal scale
	vec4  Surface[LAYERS];
	// xy scale, zw offset
	vec4  UvTransform[LAYERS];
	// One dielectric reflectance per layer.
	vec4  Specular;
	// Parallax depth per layer, in uv units; 0 is no march.
	vec4  HeightScale;
	// Metres per macro cycle and strength, per layer. See
	// MaterialParams::Macro.
	vec4  MacroScale;
	vec4  MacroStrength;
	// The layer's MAP_ flags for the three maps read, plus LAYER_ACTIVE.
	ivec4 MapFlags;
	// v_TexCoord * xy + zw is the weight map's coordinate.
	vec4  WeightUv;
	// Heap slots, bindless only; zero and unread on the bound path.
	uvec4 BaseColorSlots;
	uvec4 NormalSlots;
	uvec4 RoughnessSlots;
	uvec4 WeightSlot;
} u_Layered;

#ifdef RV_BINDLESS
// The slot comes from a uniform block, so it is uniform across the draw and
// the qualifier is not needed; kept for the same reason the material path
// keeps it -- the index into the descriptor array is what it decorates.
#define u_Weights            u_Textures[nonuniformEXT(u_Layered.WeightSlot.x)]
#define LAYER_BASE_COLOR(i)  u_Textures[nonuniformEXT(u_Layered.BaseColorSlots[i])]
#define LAYER_NORMAL(i)      u_Textures[nonuniformEXT(u_Layered.NormalSlots[i])]
#define LAYER_ROUGHNESS(i)   u_Textures[nonuniformEXT(u_Layered.RoughnessSlots[i])]
#else
layout(set = 1, binding = 1) uniform sampler2D u_Weights;
layout(set = 1, binding = 2) uniform sampler2D u_LayerBaseColor[LAYERS];
layout(set = 1, binding = 3) uniform sampler2D u_LayerNormal[LAYERS];
layout(set = 1, binding = 4) uniform sampler2D u_LayerRoughness[LAYERS];
#define LAYER_BASE_COLOR(i)  u_LayerBaseColor[i]
#define LAYER_NORMAL(i)      u_LayerNormal[i]
#define LAYER_ROUGHNESS(i)   u_LayerRoughness[i]
#endif

#endif   // RV_LAYERED

// **Everything below this line needs a vertex stage**, and RV_TRACE_ONLY is how
// a pass without one borrows the tracing above it.
//
// The traced GI pass is a fullscreen triangle: it reconstructs its position and
// normal from the depth and surface attachments rather than receiving them
// interpolated, and it writes one colour rather than the four a lit fragment
// does. It still wants TraceSurface, ShadeTraced and the whole set 0 that feeds
// them, which is why it includes this file instead of copying them -- two
// copies of a light loop is how two renderers end up disagreeing about what a
// hit looks like.
#ifndef RV_TRACE_ONLY
layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;
// Per instance, so the scalar surface parameters come from the instance stream
// rather than the material block. Only MapFlags is still read from the
// material, because which maps exist is a property of the texture set -- and
// the texture set is what decides whether two objects can share a draw.
layout(location = 3) flat in vec4 v_BaseColor;
layout(location = 4) flat in vec4 v_EmissiveColor;
layout(location = 5) flat in vec4 v_Surface;
layout(location = 6) in vec4 v_ClipPos;
// x: which cube of u_Environment and u_Irradiance. Flat and per instance: it
// is constant across the object, and interpolating it would put fragments in
// the middle of a triangle between two probes.
// y: MeshComponent::Static -- 1 when this object never moves (ENGINE-NOTES
// 7cx). A static surface reads the fully baked lights from the field and
// skips them live; a moving one is lit live by them and reads no stored
// direct light. The skinned and water vertex stages write 0 here whatever the
// instance says: a pose moves, and the water stays live for the streak.
layout(location = 7) flat in vec2 v_Instance;
layout(location = 8) in vec4 v_PrevClipPos;
// Which record in u_Materials this instance's material is. Read only by the
// bindless variant; declared in both so the two stages agree.
layout(location = 9) flat in float v_MaterialIndex;

#ifdef RV_WATER
// x = the instantaneous Jacobian (the wet-crest signal), y = where this
// fragment sits between trough and crest, zw = the 0..1 coordinate over the
// body's rectangle that addresses the foam buffer. Written by
// include/water_vertex.glsl, which explains what each is for.
layout(location = 10) in vec4 v_Water;

// The body's two colours, forwarded by the vertex stage rather than read here.
// **The fragment declares no push-constant block on purpose**: the vertex
// stage's would have to be repeated byte for byte, and two declarations that
// must agree are two that eventually do not -- what that looks like is one
// dial moving the wrong thing.
layout(location = 11) flat in vec4 v_WaterShallow;   // rgb + foam
layout(location = 12) flat in vec4 v_WaterDeep;      // rgb + wind direction
// x = the body's clock, y = grid spacing, z = gradient depth in metres,
// w = the backdrop flag and NDC sign (see water_params.glsl).
layout(location = 13) flat in vec4 v_WaterMisc;
// x = whether the sea's lamps came from their own passes (WR-16 S4b).
layout(location = 14) flat in vec4 v_WaterLamps;

// **The water pipeline's own set, and that is the lesson of the reverted
// first attempt.** The foam buffer went through Material::SetOcclusionMap
// once, because that needed no layout change -- and it collided with the
// occlusion term, forced a material per body, and still read as zero through
// the bindless records. Set 3 is free in every pipeline family (0 the scene,
// 1 the material, 2 the heap), so the water surface binds what only water
// needs at a number nothing else claims.
//
//   0  what the opaque passes rendered, for refraction to bend
//   1  that image's view depth in metres, for absorption to measure through
//   2  the foam accumulation buffer: r fresh, g residual
//   3  a tileable detail normal map, the waves below the geometry floor
//   4  a tileable foam pattern, so the buffer's smooth mask breaks into lace
layout(set = 3, binding = 0) uniform sampler2D u_WaterBackdrop;
layout(set = 3, binding = 1) uniform sampler2D u_WaterBackdropDepth;
layout(set = 3, binding = 2) uniform sampler2D u_WaterFoamBuffer;
layout(set = 3, binding = 3) uniform sampler2D u_WaterDetailNormal;
layout(set = 3, binding = 4) uniform sampler2D u_WaterFoamPattern;
// WR-16 S4b: the lamp light, chosen and shaded in its own two passes. Two
// pictures because the sea holds the glitter out of its own absorption --
// it reflected before the light ever entered the water.
layout(set = 3, binding = 5) uniform sampler2D u_WaterLampDiffuse;
layout(set = 3, binding = 6) uniform sampler2D u_WaterLampSpecular;
#endif

#if defined(RV_WATER_SURFACE)

// **WR-16 S4b: the sea's surface, written once so its lamps can be shaded
// somewhere else.** Tested before RV_TRANSPARENT, not after: this variant
// compiles with that define too -- it must, so every branch above the cut
// is the one the real water pass takes -- and an #elif after it would
// never be reached.
layout(location = 0) out vec4 o_SurfaceWater;    // octahedral N, roughness, wind angle
layout(location = 1) out vec4 o_MaterialWater;   // albedo rgb, the specular dial
// **Where the surface is, in full floats.** Not reconstructed from the depth
// buffer, because water writes no depth: the buffer under it holds the
// seabed and the pier, not the wave. And not packed into a half, because a
// bay is a kilometre across and a half float's step out there is half a
// metre. The w lane is the mask -- one where a wave was drawn, zero where
// the clear left it.
layout(location = 2) out vec4 o_PositionWater;

#elif defined(RV_TRANSPARENT)

// **The transparent variant writes two targets and nothing else.** The pass it
// is drawn in binds attachments 1 and 2 of the scene target -- accumulation and
// revealage -- and a shader that declared four outputs would be writing into
// attachments that pass has not bound.
//
// So velocity, the surface description and the traced indirect are all absent
// here, and that is a statement about what transparency costs rather than an
// omission: a blended fragment has no single depth, so it cannot be reprojected
// for TAA, cannot be a screen-space reflection's surface, and cannot be a
// denoiser's sample. Glass is drawn, composited, and not otherwise known about.
layout(location = 0) out vec4 o_Accumulate;
layout(location = 1) out float o_Revealage;


#else

layout(location = 0) out vec4 o_Color;

// Screen-space motion, in UV units: where this pixel is now minus where the
// same surface point was last frame.
//
// Exactly zero for anything that did not move -- when the projection is not
// jittered. PreviousModel equals Model there and the two projections are the
// same expression, so the subtraction cancels rather than nearly cancelling.
//
// Under TAA the two projections differ by the frame's sub-pixel offset, which
// main subtracts back out; that leaves a residual of the order of 1e-7 NDC,
// from two matrices that agree mathematically being multiplied out
// separately. In UV units that is a ten-thousandth of a pixel, below what a
// half float can represent, so it stores as zero anyway.
layout(location = 1) out vec2 o_Velocity;

// The surface description SSR reads (ENGINE-NOTES 7ad): the shading normal in
// *world* space, octahedral-encoded into RG, roughness in B, metallic in A.
// World rather than view space because this stage has no view matrix -- only
// the composed ViewProjection -- and the one pass that reads this has both.
layout(location = 2) out vec4 o_Surface;

// Traced indirect diffuse, raw irradiance, **albedo-free** (ENGINE-NOTES 7av).
// It goes to an attachment rather than into `irradiance` directly because a
// denoiser needs the GI term to exist on its own: mixed into the lit pixel it
// cannot be filtered without filtering the frame. Zero in every variant that
// does not trace.
layout(location = 3) out vec4 o_Indirect;

#endif   // RV_TRANSPARENT
#endif   // !RV_TRACE_ONLY

// Octahedral encoding, and its inverse, shared with everything that reads what
// this shader writes into the surface attachment.
#include "octahedral.glsl"

#ifndef RV_LAYERED
bool HasMap(int flag) { return (u_Material.MapFlags & flag) != 0; }
#endif

// Which cell of the grid this fragment falls in.
//
// The tile comes from the fragment's normalised device coordinate and the slice
// from how far down the view axis it is. The slice mapping is the inverse of the
// geometric series the grid was built with -- scale and bias arrive already
// folded, so this is a log and a multiply-add rather than a search.
//
// It has to agree with LightGrid::Build exactly. Where they disagree a fragment
// reads another cell's lights, which looks like lighting that snaps as the
// camera moves rather than like an indexing bug.
#ifndef RV_TRACE_ONLY
uint ClusterIndexFor(vec3 worldPos)
{
	vec2 tileCount = u_Scene.ClusterGrid.xy;
	float sliceCount = u_Scene.ClusterGrid.z;

	// The fragment's own normalised device coordinate, from the interpolated
	// clip position. Independent of the target's size and of which corner the
	// backend calls the origin, both of which gl_FragCoord would drag in.
	vec2 ndc = v_ClipPos.xy / max(abs(v_ClipPos.w), 1e-6) * sign(v_ClipPos.w);
	vec2 unit = clamp(ndc * 0.5 + 0.5, vec2(0.0), vec2(0.9999));

	uvec2 tile = uvec2(unit * tileCount);

	// Distance along the camera's forward axis, which is what the slices were
	// cut against -- not distance to the camera, which would make the slice
	// boundaries spheres instead of planes.
	float viewDepth = dot(worldPos - u_Scene.CameraPosition.xyz, u_Scene.CameraForward.xyz);

	float slice = 0.0;
	if (viewDepth > u_Scene.ClusterDepth.x)
		slice = log(viewDepth) * u_Scene.ClusterDepth.z + u_Scene.ClusterDepth.w;

	uint z = uint(clamp(slice, 0.0, sliceCount - 1.0));

	return (z * uint(tileCount.y) + tile.y) * uint(tileCount.x) + tile.x;
}
#endif

float SampleCascade(int cascade, vec3 coordinate)
{
	if (cascade == 0) return texture(u_ShadowMaps[0], coordinate);
	if (cascade == 1) return texture(u_ShadowMaps[1], coordinate);
	if (cascade == 2) return texture(u_ShadowMaps[2], coordinate);
	return texture(u_ShadowMaps[3], coordinate);
}

// One cascade's answer. Split out so the band between two of them can ask both.
float CascadeFactor(int cascade, vec3 worldPos, vec3 N, vec3 L)
{
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	// tan of the angle to the light, clamped: at grazing incidence this runs
	// away, and an offset larger than the geometry detaches the shadow.
	float slope = sqrt(1.0 - NdotL * NdotL) / max(NdotL, 0.15);
	float offset = u_Scene.CascadeTexel[cascade] * u_Scene.ShadowParams.y *
				   (1.0 + min(slope, 4.0));

	vec4 lightSpace = u_Scene.CascadeLookup[cascade] * vec4(worldPos + N * offset, 1.0);
	vec3 coordinate = lightSpace.xyz / lightSpace.w;

	// Beyond the far plane of the cascade there is nothing recorded, and the
	// honest answer is lit. **Under reverse-Z the far plane is 0**, so past it
	// is below zero rather than above one -- and the test left as `> 1.0`
	// would never fire, quietly shadowing everything past the last cascade
	// with whatever stale depth the map happened to hold.
	if (coordinate.z < 0.0)
		return 1.0;

	// **And beyond its sides, for the same reason.** The test above catches a
	// fragment past the cascade's far plane; nothing caught one outside its
	// lateral footprint. The lookup then landed off the edge of the map, the
	// sampler clamped, and whatever depth sat on that border was compared
	// against a surface hundreds of metres away -- which draws as a hard-edged
	// band of false shadow: straight, because it is the map's own edge
	// projected onto the ground, and sliding as the camera moves, because the
	// cascades are fitted to the camera.
	//
	// It only shows where geometry is outside *every* cascade, so it takes a
	// scene bigger than ShadowDistance to see: a 4 km terrain under the default
	// 40 m puts the whole ground out there, which is how it was found.
	if (any(lessThan(coordinate.xy, vec2(0.0))) || any(greaterThan(coordinate.xy, vec2(1.0))))
		return 1.0;

	// 3x3 of hardware 2x2 taps. Nine fetches is the point at which the edge
	// stops looking like a staircase and starts looking like a penumbra.
	float texel = u_Scene.ShadowParams.z;
	float sum = 0.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
			sum += SampleCascade(cascade, vec3(coordinate.xy + vec2(x, y) * texel, coordinate.z));
	}

	return sum / 9.0;
}

// How lit a point is, from 0 (fully shadowed) to 1.
//
// Two biases, because one is not enough and the reason is geometric. The
// shadow pass writes back faces, which pushes each caster's recorded depth
// behind it by its own thickness -- free, and proportional to the object. That
// does nothing for a surface nearly edge-on to the light, where one shadow
// texel spans a lot of depth, so the sample is also moved along the surface
// normal by a texel scaled by the slope. Pure depth bias is the option that
// was not taken: there is no constant that avoids both acne and shadows
// detaching from their casters.
#ifdef RV_RAY_SHADOWS
// One ray toward a light, for every kind of light (ENGINE-NOTES 7am, 7an):
// along L, no further than tMax -- effectively forever for a directional
// light, the distance to the light for a spot or a point, so a caster
// beyond the light does not shadow. Opaque and terminate-on-first-hit: a
// shadow ray does not care what it hit or where, only whether. The origin is
// pushed off the surface along the *geometric* normal by a few millimetres,
// scaled by the slope -- the same shape as the maps' normal offset and for
// the same reason, self-intersection, but a hundredth of the size, because a
// ray has no texel to clear. Too small and every surface shadows itself in a
// moire; too large and the shadow detaches. The result is hard: a light has
// a size and a real penumbra needs several rays and a filter across frames,
// which this is not.
//
// ShadowParams.x is a flag under rays: one when a structure was built this
// frame, zero when not (a probe capture, a frame before the first
// RenderShadows), and zero means lit -- the same reading the maps give a
// count of zero cascades.
// The engine's shadow rays, the alpha test inside their traversal, and the
// point on a sized source a soft shadow aims at: include/ray_shadow_trace.glsl,
// its own file since WR-16 S4b because the pass that shades the sea's lamps
// casts the same rays and they must be the same rays.
#include "ray_shadow_trace.glsl"

// The surface being shaded: its own geometric normal. The tracing above takes
// its normal as an argument and is what a pass with no varyings uses.
#ifndef RV_TRACE_ONLY
float TraceShadow(vec3 worldPos, vec3 L, float tMax)
{
	return TraceShadowFrom(worldPos, normalize(v_Normal), L, tMax);
}

float TraceShadowSoft(vec3 worldPos, vec3 L, float tMax,
					  float sourceRadius, uint light)
{
	return TraceShadowSoftFrom(worldPos, normalize(v_Normal), L, tMax,
							   sourceRadius, light);
}

// **WR-17: the farther a light, the fewer of its shadow rays a pixel
// traces.** A water pixel under the bridge walks up to 178 lamps and traced a
// ray to every one of them, and at 1440p that was a third of the frame --
// for shadows that, from a lamp half a kilometre away, are a picket's width
// projected to nothing. The owner's rule: the farther a light, the less it
// contributes, so the farther it is the more of its rays may go.
//
// Returns the fraction of this pixel's rays to the light that are skipped
// and counted lit, from 0 (trace as always) to 1 (never trace). The shape
// between the start and end distances is the render setting under test
// (docs/RENDERING-REVAMP.md WR-17): hard, linear, smoothstep, log -- or the
// light's *share* of the pixel's direct light, which is the physical form
// of the rule and needs no end distance, only a floor under which nothing
// is skipped so the deck keeps its own lamps' shadows.
// The shape lane's bit 16: a skipped ray counts its light lit instead of
// borrowing (below). The measurement arm; the flag's `lit` token sets it.
bool ShadowRaySkippedLit()
{
	return u_Scene.ShadowRayFade.x >= 16.0;
}

float ShadowRaySkip(float distance, float share)
{
	const float shape = mod(u_Scene.ShadowRayFade.x, 16.0);
	if (shape < 0.5)
		return 0.0;

	const float start = u_Scene.ShadowRayFade.y;
	const float end = max(u_Scene.ShadowRayFade.z, start + 1.0e-3);

	if (shape > 4.5)
	{
		if (distance < start)
			return 0.0;
		return 1.0 - clamp(share / max(u_Scene.ShadowRayFade.w, 1.0e-6), 0.0, 1.0);
	}

	// **The floor: past the end, this fraction of a light's rays is still
	// traced.** Found by the matrix: linear 300/600 with the borrow moved
	// 0.18% of Headland and nothing over six levels, while every 150/300
	// shape moved five percent -- because past the end no far ray is
	// traced at all, and the borrowed pool then holds only mid-distance
	// lamps, lit where the far ones are blocked. A traced fraction that
	// never reaches zero keeps the pool representative however far the
	// light is; it is the aggressive dial, not the end distance.
	const float ceiling = 1.0 - clamp(u_Scene.ShadowRayFade.w, 0.0, 1.0);
	if (shape < 1.5)
		return distance >= end ? ceiling : 0.0;

	const float t = clamp((distance - start) / (end - start), 0.0, 1.0);
	if (shape < 2.5)
		return t * ceiling;
	if (shape < 3.5)
		return t * t * (3.0 - 2.0 * t) * ceiling;
	// Log: most of the thinning happens early, so the far end is reached
	// aggressively -- log2(1 + t) is 0.58 at the halfway point.
	return log2(1.0 + t) * ceiling;
}

// Whether this pixel traces this light's ray at the fraction above. A fixed
// per-pixel dither, walked per frame only while a temporal filter is there
// to integrate it -- exactly WR-15's rule, learned the hard way: a random
// draw here blinks under no AA and MSAA. Its own offset into the gradient
// noise, so the skip pattern and the soft-shadow sample do not line up.
bool ShadowRayKept(float skip, uint light)
{
	if (skip <= 0.0)
		return true;
	if (skip >= 1.0)
		return false;

	float u = InterleavedGradientNoise(gl_FragCoord.xy + vec2(17.0, 31.0));
	if (any(notEqual(u_Scene.Jitter, vec4(0.0))))
		u += mod(u_Scene.GlobalIllumination.y, 1024.0) * 0.61803398875;
	u = fract(u + float(light) * 0.75487766625);
	return u >= skip;
}

#ifdef RV_RAY_SKY
// **How much sky this point can actually see.**
//
// The sky term is a light like any other, and a light is occluded by the
// things in front of it. Until now that occlusion came only from a baked
// irradiance volume, so it was 1.0 -- full, unobstructed sky -- everywhere a
// volume does not reach: under the deck, inside the truss, beside a cliff,
// and on anything that has moved since the bake. This asks the structure
// instead, per pixel, which is right in all four cases.
//
// Its own hash and sampler rather than GiHash/CosineDirection, which live
// under RV_RAY_GI and are not compiled when only shadows are on. Same
// lowbias32 constants, so the two agree where both exist.
uint SkyHash(uint x)
{
	x ^= x >> 16; x *= 0x7FEB352Du;
	x ^= x >> 15; x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

// Cosine-weighted about `n`, which is the distribution the diffuse sky
// integral wants: the mean of the samples *is* the visibility, with no
// per-sample cosine left to divide back out.
vec3 SkyDirection(vec3 n, inout uint seed)
{
	seed = SkyHash(seed);
	float u1 = float(seed & 0x00FFFFFFu) / 16777216.0;
	seed = SkyHash(seed);
	float u2 = float(seed & 0x00FFFFFFu) / 16777216.0;

	vec3 axis = abs(n.x) < 0.7 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	vec3 t = normalize(cross(axis, n));
	vec3 b = cross(n, t);

	float r = sqrt(u1);
	float phi = 6.2831853 * u2;
	return normalize(t * (r * cos(phi)) + b * (r * sin(phi))
					+ n * sqrt(max(1.0 - u1, 0.0)));
}

// **Four rays, and the temporal filter does the rest.** Four is not enough
// to resolve this on its own -- it is enough that TAA's accumulation over a
// held camera converges it, which is the same bargain RTGI takes. The seed
// carries the frame so consecutive frames draw different directions; a fixed
// seed would converge to its own four rays and stay wrong.
//
// The rays run to `far`, not to a short AO radius: the question is whether
// the *sky* is blocked, and a tower blocks it from a kilometre away. That is
// also what makes this a different quantity from the AO term, which stays a
// contact darkening and is applied separately.
float TraceSkyVisibility(vec3 worldPos, vec3 Ng, vec3 N, float far)
{
	if (u_Scene.ShadowParams.x <= 0.0)
		return 1.0;

	// GlobalIllumination.y is a per-BeginScene counter -- advanced whether or
	// not the traced bounce is running, and advanced per *view* so a probe
	// face and the main camera do not draw the same four rays.
	uint seed = SkyHash(uint(gl_FragCoord.x) * 73856093u
					  ^ SkyHash(uint(gl_FragCoord.y) * 19349663u
					  ^ SkyHash(uint(u_Scene.GlobalIllumination.y))));

	// The dial, carried by the define itself: Quarter/Half/Full are 2/4/8.
	const int kRays = RV_RAY_SKY;
	float open = 0.0;
	for (int i = 0; i < kRays; i++)
	{
		vec3 dir = SkyDirection(N, seed);
		// Never sample into the geometry: a shading normal can lean past the
		// true surface, and a ray starting below it reports the surface it
		// started on as the sky being blocked.
		if (dot(dir, Ng) <= 0.0)
			dir = reflect(dir, Ng);
		open += TraceShadowFrom(worldPos, Ng, dir, far);
	}
	return open / float(kRays);
}
#endif
#endif
#else
float ShadowFactor(vec3 worldPos, vec3 N, vec3 L)
{
	int count = int(u_Scene.ShadowParams.x);
	if (count <= 0)
		return 1.0;

	float viewDepth = dot(worldPos - u_Scene.CameraPosition.xyz, u_Scene.CameraForward.xyz);

	int cascade = count - 1;
	for (int i = 0; i < count; ++i)
	{
		if (viewDepth < u_Scene.CascadeSplits[i])
		{
			cascade = i;
			break;
		}
	}

	float lit = CascadeFactor(cascade, worldPos, N, L);

	// Fade into the next cascade over the last tenth of this one.
	//
	// Selection is a step function, and two cascades disagree slightly at their
	// boundary -- different texel sizes, different bias. Without a band the
	// disagreement is a line across the ground that slides as the camera moves,
	// which reads as a rendering artefact rather than as a shadow.
	if (cascade + 1 < count)
	{
		float start = cascade == 0 ? 0.0 : u_Scene.CascadeSplits[cascade - 1];
		float end = u_Scene.CascadeSplits[cascade];
		float band = (end - start) * 0.1;

		if (band > 0.0 && viewDepth > end - band)
		{
			float t = clamp((viewDepth - (end - band)) / band, 0.0, 1.0);
			lit = mix(lit, CascadeFactor(cascade + 1, worldPos, N, L), t);
		}
	}

	return lit;
}

float SampleSpot(int slot, vec3 coordinate)
{
	if (slot == 0) return texture(u_SpotShadows[0], coordinate);
	if (slot == 1) return texture(u_SpotShadows[1], coordinate);
	if (slot == 2) return texture(u_SpotShadows[2], coordinate);
	return texture(u_SpotShadows[3], coordinate);
}

float SamplePoint(int slot, vec4 coordinate)
{
	if (slot == 0) return texture(u_PointShadows[0], coordinate);
	if (slot == 1) return texture(u_PointShadows[1], coordinate);
	if (slot == 2) return texture(u_PointShadows[2], coordinate);
	return texture(u_PointShadows[3], coordinate);
}

// A spot light's own frustum, so there is nothing to fit and nothing to select.
float SpotShadow(int slot, vec3 lightPos, float texelScale, vec3 worldPos, vec3 N, vec3 L)
{
	float NdotL = clamp(dot(N, L), 0.0, 1.0);
	float slope = sqrt(1.0 - NdotL * NdotL) / max(NdotL, 0.15);

	// A texel's world size grows with distance from the light, unlike a
	// directional cascade where it is constant, so the offset has to as well.
	float distanceToLight = length(worldPos - lightPos);
	float offset = texelScale * distanceToLight * u_Scene.ShadowParams.y *
				   (1.0 + min(slope, 4.0));

	vec4 lightSpace = u_Scene.SpotLookup[slot] * vec4(worldPos + N * offset, 1.0);
	if (lightSpace.w <= 0.0)
		return 1.0;

	vec3 coordinate = lightSpace.xyz / lightSpace.w;
	if (coordinate.z < 0.0)
		return 1.0;

	return SampleSpot(slot, coordinate);
}

// Six faces of one, addressed by direction. No matrix: the reference depth is
// rebuilt from the distance along whichever axis the face was chosen by, which
// is what the projection did when the face was rendered.
float PointShadow(int slot, vec3 lightPos, float farClip, float texelScale,
				  vec3 worldPos, vec3 N)
{
	vec3 toFragment = worldPos - lightPos;
	float distanceToLight = length(toFragment);

	// Offset before the direction is taken, so a surface facing the light is
	// pushed out of its own shadow rather than sideways along the cube.
	vec3 offsetPos = worldPos + N * (texelScale * distanceToLight * u_Scene.ShadowParams.y);
	toFragment = offsetPos - lightPos;

	float major = max(abs(toFragment.x), max(abs(toFragment.y), abs(toFragment.z)));
	if (major <= POINT_SHADOW_NEAR || major >= farClip)
		return 1.0;

	// The depth the projection would have written for a point this far down
	// the face's axis, in [0, 1] -- **reverse-Z, so the near plane is 1 and
	// the far plane 0**, matching Math::Perspective exactly. This is the one
	// place a projection's depth is rebuilt by hand rather than rasterised,
	// which makes it the one place the flip has to be repeated: at `major`
	// = near it is 1, and at `major` = farClip it is 0.
	float depth = (POINT_SHADOW_NEAR / (farClip - POINT_SHADOW_NEAR)) *
				  (farClip / major - 1.0);

	return SamplePoint(slot, vec4(toFragment, depth));
}
#endif // RV_RAY_SHADOWS: the map lookups above are the path the rays replace

// Turns a direction into the sky's own frame. The sky pass folds the same
// rotation into its matrix on the CPU; a reflection that skipped it would show
// a different sky from the one visible behind the surface.
vec3 RotateIntoSky(vec3 v)
{
	float c = u_Scene.Environment.z;
	float s = u_Scene.Environment.w;
	return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

// **The scene's irradiance field**: one volume texture, three tiles stacked
// along z -- red, then green, then blue, each Depth slices tall.
//
// Binding 18 is the first free one in set 0. Declared here rather than in one
// shader because every lit pipeline family reflects this include, and a set 0
// whose layout differs between families is a set that cannot be shared.
//
// **It was three textures, and three samplers is what broke OpenGL.** A
// fragment shader gets thirty-two there, and the layered terrain variant --
// twelve shadow maps, twelve layer maps, and the six every lit shader needs --
// was at thirty before this field existed. Three more made thirty-three:
// "profile doesn't support more than 32 samplers", no layered pipeline, and
// terrain drawn as a black frame. One texture is one sampler, and thirty-one
// leaves room for the depth term this field still owes.
// **Three tiles of light, eight of distance.** Both ends of the texture agree
// on this number: IrradianceVolume::kTiles is the same eleven.
//
// The thirty-two are an octahedral map of sixty-four directions per cell, two
// to a texel: how far geometry is that way, and the mean of its square beside
// it. This has to be *sharp*, and each coarser version was measured and found
// wanting: a first-order fit says "about nine metres, more to the left" and
// left 3.4 levels of leak, and sixteen directions -- a cone forty-five degrees
// wide, mixing a wall at eight hundred millimetres with a room twenty metres
// deep -- left 3.6. What stops a wall leaking is a bin narrow enough that its
// mean means something.
#define RV_IRRADIANCE_TILES 7
#define RV_IRRADIANCE_OCT   8
#define RV_IRRADIANCE_BINS  (RV_IRRADIANCE_OCT * RV_IRRADIANCE_OCT)

// Which of the sixteen a direction belongs to, and the direction at the centre
// of one. The fill weights every ray against every bin with these; the lookup
// asks for the one bin it needs.
int OctBin(vec3 direction)
{
	const vec2 uv = OctEncode(direction);   // already zero to one
	const ivec2 cell = clamp(ivec2(uv * float(RV_IRRADIANCE_OCT)),
							 ivec2(0), ivec2(RV_IRRADIANCE_OCT - 1));
	return cell.y * RV_IRRADIANCE_OCT + cell.x;
}

vec3 OctBinDirection(int bin)
{
	const vec2 cell = vec2(bin % RV_IRRADIANCE_OCT, bin / RV_IRRADIANCE_OCT);
	return OctDecode((cell + 0.5) / float(RV_IRRADIANCE_OCT));
}

layout(set = 0, binding = 18) uniform sampler3D u_IrradianceField;

// **What indirect light arrives at a point, from a direction.**
//
// The question the renderer could not previously ask. A reflection probe
// answers it once per object -- so a long surface gets one answer and anything
// crossing a boundary changes all at once -- and the traced bounce answers it
// only where a ray happens to land. A field answers it anywhere inside its box,
// and slides between stored samples rather than switching.
//
// **The convention, because a stored SH is meaningless without one.** Each
// texel holds four coefficients for its channel: x is the constant term and yzw
// lean the answer toward an axis, so
//
//     irradiance(n) = L0 + dot(L1, n)
//
// evaluated per channel. First order: enough that the sky side of an object is
// lit by sky and the ground side by ground, which is where the cost-to-quality
// curve bends, and four coefficients rather than nine.
//
// Returns false outside the box, leaving the caller on whatever it did before.
// `shadowed` asks for the stored visibility to be honoured, which costs the
// hardware's trilinear filter: the eight cells have to be weighted one at a
// time, and each one costs a fetch of its distances on top of its light.
//
// **Measured, and it is why this is a parameter rather than always on.** In the
// lit pass it is worth it and nearly free -- that path reads the field only
// where the screen-space bounce has no answer, which is a small part of a
// frame. Terminating every traced ray in it is a different bargain: 32 fetches
// against 3, +0.55 ms on a 1.5 ms frame, to take a sealed room from 0.15 levels
// of leak to nothing. The bounce takes the cheap road and the picture takes the
// careful one.
// **Sky visibility out of a light tile's alpha lane.**
//
// That lane used to be a constant 1.0 for a live cell and 0.0 for a buried
// one -- sixteen bits carrying one bit. It now carries the cosine-weighted
// fraction of sky a surface facing this axis can see, packed as
// plain V -- the lane carries sky alone. Aliveness lives in tile 6's .y
// now, so a dead cell can store 1.0 here: "no data, do not darken",
//
// **A bake from before the change decodes to 1.0** -- fully open, darken
// nothing -- so a stale field is a no-op rather than a scene that quietly
// lost its ambient. That is what lets this ship without a version bump.
float SkyFromAlpha(float stored)
{
	return clamp(stored, 0.0, 1.0);
}

// **The field's basis: real spherical harmonics to second order, nine
// coefficients, in the volume's own frame.** A cell stores, per
// coefficient, the bounce's irradiance over pi in rgb and the sky's
// cosine-weighted visibility in alpha; a reader evaluates both with the
// surface normal in one weighted sum. The ambient cube this replaced was
// three fetches to nine, and could not hold which way a single lamp came
// from. Tile 9 carries the neighbour-visibility bits and the alive flag.
#define RV_FIELD_COEFFICIENTS 9
// Tiles 9-17: the fully baked lights' direct light, its own nine
// coefficients, read only where u_Scene.IrradianceExtents.x says a scene
// has such a light.
#define RV_FIELD_DIRECT_TILE 9
#define RV_FIELD_VISIBILITY_TILE 18

void FieldBasis(vec3 d, out float y[9])
{
	y[0] = 0.282095;
	y[1] = 0.488603 * d.y;
	y[2] = 0.488603 * d.z;
	y[3] = 0.488603 * d.x;
	y[4] = 1.092548 * d.x * d.y;
	y[5] = 1.092548 * d.y * d.z;
	y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
	y[7] = 1.092548 * d.x * d.z;
	y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

// The cosine lobe's convolution per band, over pi: what turns a radiance
// coefficient into an irradiance-over-pi one (Ramamoorthi and Hanrahan).
float FieldBandOverPi(int k)
{
	return k == 0 ? 1.0 : (k < 4 ? 2.0 / 3.0 : 0.25);
}

// **The dominant lamp, recovered from the direct tiles' first band.** A
// stored irradiance carries no highlight -- it is how much light arrives,
// not the direction a glossy surface needs -- which is why a fully baked
// showroom read its matte walls at 0.99 and its car's paint at 0.65. But
// the coefficients do carry a direction: for one lamp the zeroth is
// E * Y00 and the first three are (2/3) * E * 0.4886 * (y, z, x) of the
// lamp's direction, so the first band's vector points at the lamp and its
// length over the zeroth says how much of the light is one lamp's. Several
// lamps average into one broader, weaker direction, which is the honest
// answer for a highlight too. Unity's directional lightmaps and Source's
// ambient cube plus one light are the same idea.
void DerivedLamp(vec3 rawC0, vec3 l1, vec3 axisX, vec3 axisY, vec3 axisZ, float edgeFade,
				 out vec3 direction, out vec3 coherentLight)
{
	direction = vec3(0.0);
	coherentLight = vec3(0.0);
	const float lumC0 = dot(rawC0, vec3(0.2126, 0.7152, 0.0722));
	const float len = length(l1);
	if (lumC0 <= 1.0e-6 || len <= 1.0e-6)
		return;
	// (2/3) * 0.488603: what the first band's length is for a single lamp.
	const float coherence = clamp(len / (0.325735 * lumC0), 0.0, 1.0);
	const vec3 inBox = l1 / len;
	direction = normalize(axisX * inBox.x + axisY * inBox.y + axisZ * inBox.z);
	// Irradiance at normal incidence, from the zeroth coefficient (E * Y00),
	// scaled by how much of it points one way.
	coherentLight = max(rawC0 / 0.282095, vec3(0.0)) * coherence * edgeFade;
}

// **Which volume covers this point, and how deep inside it the lookup lands**
// -- the head of VolumeIrradiance, shared with IrradianceFieldWeight so that
// the live loop's complement of the field's edge fade *is* the fade and never
// an estimate of it (ENGINE-NOTES 7cx). `box` is the chosen volume's row in
// u_Scene.IrradianceBox; `local` the lookup point in that box's [-1, 1] frame
// after the half-cell step off the surface (and the further step off a buried
// anchor, below); `edgeFade` what every stored quantity is scaled by;
// `singleCell` whether the solve should read one cell rather than blend. False
// where no volume covers the point.
bool FieldLocate(vec3 position, vec3 normal, out int box, out vec3 local,
				 out float edgeFade, out bool singleCell)
{
	box = -1;
	local = vec3(0.0);
	edgeFade = 0.0;
	singleCell = false;

	const int volumeCount = int(u_Scene.IrradianceExtents.w + 0.5);
	if (volumeCount <= 0)
		return false;               // no field in the scene

	int chosen = -1;
	float chosenDepth = 0.0;
	vec3 chosenLocal = vec3(0.0);
	for (int v = 0; v < volumeCount; v++)
	{
		const vec4 boxCentre = u_Scene.IrradianceBox[v * 6 + 0];
		const vec4 boxExtents = u_Scene.IrradianceBox[v * 6 + 1];
		const vec3 axisX = u_Scene.IrradianceBox[v * 6 + 2].xyz;
		const vec3 axisY = u_Scene.IrradianceBox[v * 6 + 3].xyz;
		const vec3 axisZ = u_Scene.IrradianceBox[v * 6 + 4].xyz;

		const vec3 boxHalf = max(boxExtents.xyz, vec3(1.0e-3));
		const vec3 d = position - boxCentre.xyz;
		// Into this box's own frame; the bias along the normal is applied
		// after a volume is chosen, because it needs that volume's cell size.
		const vec3 p = vec3(dot(axisX, d), dot(axisY, d), dot(axisZ, d));
		const vec3 l = p / boxHalf;
		if (any(greaterThan(abs(l), vec3(1.0))))
			continue;               // outside this one

		const vec3 boxGrid = max(vec3(u_Scene.IrradianceBox[v * 6 + 2].w,
									  u_Scene.IrradianceBox[v * 6 + 3].w,
									  u_Scene.IrradianceBox[v * 6 + 4].w) - 1.0,
								 vec3(1.0));
		const vec3 inCells = (1.0 - abs(l)) * boxGrid * 0.5;
		const float depth = min(inCells.x, min(inCells.y, inCells.z));
		if (chosen < 0 || depth > chosenDepth)
		{
			chosen = v;
			chosenDepth = depth;
			chosenLocal = l;
		}
	}

	if (chosen < 0)
		return false;               // in none of them

	box = chosen * 6;
	const vec3 extents = max(u_Scene.IrradianceBox[box + 1].xyz, vec3(1.0e-3));

	// **Half a cell along the normal, before anything else** -- the cheap half
	// of not leaking through walls.
	//
	// The eight cells the hardware blends between are chosen by position alone,
	// and one of them can easily be on the far side of the wall this fragment
	// is on the near side of. That cell is bright, this surface is not, and
	// trilinear interpolation does not know a wall is in the way: the dark side
	// of a partition lights up. Measured on a sealed room beside a lit one --
	// the correct answer is black, and at two-metre cells it read 19.7 levels.
	//
	// Stepping the lookup off the surface along its own normal moves the sample
	// away from whatever is behind it and into the room it faces, which is
	// where its light comes from. Half a cell is the smallest step that
	// reliably crosses out of the cell pair straddling a wall, and irradiance
	// is low frequency enough that moving the question half a cell does not
	// change the answer anywhere the geometry does not.
	// The field's own shape, from the texture: its z is five tiles deep -- three
	// of light and two of distance -- so a tile is a fifth of it, and nothing
	// has to be told the grid separately.
	// **The atlas, and this volume's window onto it.** `tile` is the stride
	// from one tile of the texture to the next -- the atlas's whole depth,
	// not this volume's -- and `zbase` is where this volume's slices start
	// inside each tile. A reader that uses its own depth as the stride lands
	// in a neighbour's cells, which is the one way this layout goes wrong.
	const ivec3 texels = textureSize(u_IrradianceField, 0);
	const int tile = max(int(u_Scene.IrradianceCentre.w + 0.5), 1);
	const int zbase = int(u_Scene.IrradianceBox[box + 0].w + 0.5);
	// The box's corner in the atlas (7cx): regions pack side by side in x and
	// y as well as along z, so every texel address starts here.
	const ivec2 xybase = ivec2(u_Scene.IrradianceBox[box + 5].xy + 0.5);
	const vec3 cells = vec3(u_Scene.IrradianceBox[box + 2].w,
							u_Scene.IrradianceBox[box + 3].w,
							u_Scene.IrradianceBox[box + 4].w);
	const vec3 grid = max(cells - 1.0, vec3(1.0));

	// **Into the box's own frame first**, because a volume may be turned. The
	// three rows take a world vector onto the box's axes, and every question
	// below -- which cell, how far, which way -- is asked there. The rotation
	// is orthonormal, so a dot product means the same thing on either side of
	// it and the facing test does not care which frame it is in.
	const vec3 axisX = u_Scene.IrradianceBox[box + 2].xyz;
	const vec3 axisY = u_Scene.IrradianceBox[box + 3].xyz;
	const vec3 axisZ = u_Scene.IrradianceBox[box + 4].xyz;

	const vec3 delta = position - u_Scene.IrradianceBox[box + 0].xyz;
	const vec3 placed = vec3(dot(axisX, delta), dot(axisY, delta), dot(axisZ, delta));
	const vec3 facing = vec3(dot(axisX, normal), dot(axisY, normal), dot(axisZ, normal));

	// Half a cell along the normal, in the frame the cells are spaced in.
	//
	// **And a whole cell further when that lands on a buried cell**, which is
	// the difference between asking a question and asking someone who can
	// answer it. The mask a fragment judges its eight corners by belongs to
	// its nearest cell; a cell inside the floor was never asked what it can
	// see, publishes an empty mask, and an empty mask cannot refuse the
	// bright corner across a divider -- measured on the two-metre sealed room,
	// where the whole room holds one live layer and the lit room's cell sits
	// one corner away. Stepping the lookup off the surface until it reaches a
	// cell that is genuinely in the room gets a mask with something in it,
	// and the refusal works again. It costs one fetch, in the case that was
	// already the expensive one, and it blurs the read by a cell exactly
	// where the nearest cell was rubble -- which had no detail to lose.
	local = (placed + facing * (extents / grid)) / extents;
	if (any(greaterThan(abs(local), vec3(1.0))))
		return false;               // outside it

	// **And fade out before the edge, rather than stopping at it.**
	//
	// A field ends somewhere, and what lies past it gets whatever the shading
	// did before -- the probe, the flat ambient. Switching between the two at
	// the boundary draws a *line* across every surface that crosses it: one
	// pixel with a room's worth of bounced light, the next with none. It is the
	// most visible thing a volume can do wrong, and no average of the frame
	// shows it, because a hard edge is a small number of pixels being
	// completely wrong rather than every pixel being slightly wrong.
	//
	// **One cell wide, not a tenth of the box.** The step this hides is an
	// interpolation artefact, so the interpolation scale is its natural
	// width. The tenth-of-half-extent band it replaces was measured doing
	// real damage: a room's walls sit at the box its author drew, the sample
	// bias only steps half a *cell* off a surface, so at fine spacings every
	// wall landed deep inside a 40 cm fade band and read 40-60% of its
	// stored light -- and the finer the bake, the darker the walls, which is
	// exactly backwards. (1 - |local|) is the distance to the face in local
	// units; times grid/2 it is that distance in cells. The solve also pads
	// the box a cell past the authored extents -- see
	// Scene::UpdateIrradianceVolumes -- so a surface *on* the authored
	// boundary sits a full cell from the real edge and reads full strength.
	// Set where the lookup lands on a cell nothing surveyed; see below.
	singleCell = false;

	// The anchor, and the step off a buried one described above. Done before
	// the edge fade so the fade measures the sample actually taken.
	{
		const vec3 firstBase = (local * 0.5 + 0.5) * grid;
		const ivec3 firstAnchor = clamp(ivec3(floor(firstBase + 0.5)),
										ivec3(0), ivec3(grid));
		const int firstMask = int(texelFetch(u_IrradianceField,
											 firstAnchor + ivec3(xybase, tile * RV_FIELD_VISIBILITY_TILE + zbase), 0).x + 0.5);
		if (firstMask == 0)
		{
			const vec3 stepped = (placed + facing * (extents / grid) * 3.0) / extents;
			// Only if the step stays in the box: past the edge the fade below
			// is the honest answer, not a sample from outside it.
			if (all(lessThanEqual(abs(stepped), vec3(1.0))))
				local = stepped;
#ifdef RV_IRRADIANCE_FILL
			// **The solve reads one cell here, and does not blend.**
			//
			// A buried anchor means "nobody at this end of the lookup was
			// asked what they can see". The frame answers that by stepping
			// off and interpolating anyway -- a slightly optimistic wall
			// beats a black one, and a frame's guess dies with the frame.
			// The solve cannot be so relaxed: what it reads is *stored*, and
			// the next sweep reads the store as light, so an interpolation
			// that reaches across a wall is a loop with a gain. Measured on
			// the sealed room at two-metre cells, where every floor and
			// ceiling cell is buried: blending after the step took it from
			// black at one pass to fifteen levels at eight.
			//
			// Refusing outright also works and is what this did first, but
			// it throws away real light with the false: the GI fixture's
			// walls, whose inner cell layer is legitimately buried in
			// 200 mm of wall, lost three levels of honest bounce that way.
			//
			// So: step to a cell that was asked, and take its answer alone.
			// One cell cannot interpolate across anything, which is the
			// whole risk; and if the stepped cell is buried too, there is
			// genuinely nothing to report.
			singleCell = true;
#endif
		}
	}

	const vec3 fromEdge = (1.0 - abs(local)) * grid * 0.5;
	edgeFade = clamp(min(fromEdge.x, min(fromEdge.y, fromEdge.z)), 0.0, 1.0);
	if (edgeFade <= 0.0)
		return false;
	return true;
}

// **The field's weight at a point, for the live loop's complement** (7cx): 0
// where no volume covers it, the edge fade inside one -- exactly what
// VolumeIrradiance scales its direct light by. So a fully baked lamp lit live
// by (1 - weight) and read from the field by weight is one lamp counted once,
// everywhere: deep inside a volume, outside every volume (the beach beyond the
// deck's box, which used to get nothing), and across a volume's edge band.
float IrradianceFieldWeight(vec3 position, vec3 normal)
{
	int box;
	vec3 local;
	float fade;
	bool single;
	return FieldLocate(position, normal, box, local, fade, single) ? fade : 0.0;
}

bool VolumeIrradiance(vec3 position, vec3 normal, bool shadowed, out vec3 irradiance,
					  out float skyVisibility, out vec3 directLight,
					  out vec3 directDirection, out vec3 directCoherent)
{
	irradiance = vec3(0.0);
	directLight = vec3(0.0);
	directDirection = vec3(0.0);
	directCoherent = vec3(0.0);
	const bool hasDirect = u_Scene.IrradianceExtents.x > 0.5;

	// **One, meaning unoccluded, until a cell says otherwise.** Every early
	// return below is a fragment with no field over it, and the honest answer
	// there is "do not darken anything" -- a scene with no volume authored
	// must not lose its sky.
	skyVisibility = 1.0;

	// **Which volume is this fragment standing in?**
	//
	// A scene's volumes used to be merged into one box before anything got
	// here, so this function had exactly one to read and needed no choosing.
	// They are independent now -- own grid, own spacing, own rotation, packed
	// side by side into one texture -- so the first job is picking one.
	//
	// **The deepest containment wins**, measured as the distance to the
	// nearest face in cells rather than in metres: a fragment inside two
	// overlapping volumes should read the one it is furthest *inside*, and
	// comparing in metres would hand a coarse volume the argument simply for
	// being large. It is also exactly the quantity the edge fade below wants,
	// so it is computed once and used twice.
	int box;
	vec3 local;
	float edgeFade;
	bool singleCell;
	if (!FieldLocate(position, normal, box, local, edgeFade, singleCell))
		return false;

	// The chosen box's shape and frame again, from its rows -- cheap uniform
	// reads, and the one place the two halves of the lookup could disagree if
	// they were typed twice, which is why FieldLocate is the only place the
	// choice is made.
	const vec3 extents = max(u_Scene.IrradianceBox[box + 1].xyz, vec3(1.0e-3));
	const ivec3 texels = textureSize(u_IrradianceField, 0);
	const int tile = max(int(u_Scene.IrradianceCentre.w + 0.5), 1);
	const int zbase = int(u_Scene.IrradianceBox[box + 0].w + 0.5);
	// The box's corner in the atlas (7cx): regions pack side by side in x and
	// y as well as along z, so every texel address starts here.
	const ivec2 xybase = ivec2(u_Scene.IrradianceBox[box + 5].xy + 0.5);
	const vec3 cells = vec3(u_Scene.IrradianceBox[box + 2].w,
							u_Scene.IrradianceBox[box + 3].w,
							u_Scene.IrradianceBox[box + 4].w);
	const vec3 grid = max(cells - 1.0, vec3(1.0));
	const vec3 axisX = u_Scene.IrradianceBox[box + 2].xyz;
	const vec3 axisY = u_Scene.IrradianceBox[box + 3].xyz;
	const vec3 axisZ = u_Scene.IrradianceBox[box + 4].xyz;
	const vec3 delta = position - u_Scene.IrradianceBox[box + 0].xyz;
	const vec3 placed = vec3(dot(axisX, delta), dot(axisY, delta), dot(axisZ, delta));
	const vec3 facing = vec3(dot(axisX, normal), dot(axisY, normal), dot(axisZ, normal));

	// **The eight cells around it, each asked whether it can see this surface
	// at all.** This is where the shadow half of the bake is spent.
	//
	// Trilinear alone blends the eight by position, which is how a bright cell
	// on the far side of a wall lights the dark side of it -- interpolation
	// cannot see a wall. Three weights fix that between them, and a cell keeps
	// only what all three allow:
	//
	//  * the **trilinear** weight, kept exactly, so a fragment with everything
	//    visible reads what the hardware filter would have read;
	//  * the **facing** weight, zero for a cell behind this surface, because
	//    light does not arrive through the surface it is lighting;
	//  * the **visibility** weight, which is the distance the fill stored. The
	//    cell knows how far geometry is in the direction of this fragment; if
	//    the fragment is further away than that, something is in between.
	//
	// The last one is Chebyshev's inequality, the test DDGI uses: with a mean
	// and a mean square, the fraction of the distribution beyond a distance is
	// bounded, and that bound is a soft visibility rather than a hard yes or no
	// -- which is what keeps a wall's edge from becoming a hard line in the
	// lighting. Cubed to sharpen it, as DDGI does.
	const vec3 base = (local * 0.5 + 0.5) * grid;
	const ivec3 corner = ivec3(floor(base));
	const vec3 frac = base - vec3(corner);
	const ivec3 last = ivec3(grid);

	// **The cell this fragment is nearest, and what the bake says it can see.**
	// One fetch for the whole lookup rather than one per corner: the mask
	// belongs to where the fragment *is*, and every corner is judged by it.
	const ivec3 anchorCell = clamp(ivec3(floor(base + 0.5)), ivec3(0), last);
	const int reachable = int(texelFetch(u_IrradianceField,
										 anchorCell + ivec3(xybase, tile * RV_FIELD_VISIBILITY_TILE + zbase), 0).x + 0.5);


	// **Nothing in the way of any of the six, so nothing to weigh.** This is
	// the ordinary case -- a point in an open room -- and it is why the careful
	// path costs what it costs only where it has to. All six bits set means no
	// corner of the blend is behind a wall, and the hardware's filter is then
	// exactly the answer the loop below would spend 24 fetches arriving at.
	// **A cell inside a wall knows nothing about who can see whom**, and an
	// empty mask is that ignorance rather than a report of six walls. A
	// fragment on a floor or against a wall very often has exactly such a
	// cell as its nearest, and reading the zero as "nothing is reachable"
	// collapses the lookup onto a dead cell and the surface goes black --
	// measured at 4.6 levels darker than the realtime bounce on the GI
	// corner, which is where a field is supposed to be at its best.
	//
	// **But "no information" is not a licence to blend through walls**, and
	// for a while it was: an empty mask took the hardware path below, which
	// weighs by distance alone -- no visibility, no facing, no aliveness --
	// and is the most permissive read in this function. That was invisible
	// while the fill's buried-cell test was broken (see
	// TracedSurface::Backface: no cell was ever dead, so no mask was ever
	// empty). With the test working, the sealed room at two-metre cells went
	// straight through it: 7.8 levels where the answer is black.
	//
	// So an empty mask now falls through to the careful loop, which keeps the
	// facing weight and the aliveness test and simply has no bits to consult.
	// Facing alone is most of what was wanted anyway -- a cell on the far
	// side of the wall a fragment faces lies *behind* that fragment, and a
	// negative dot refuses it without needing to know a wall is there.
	if (!shadowed || reachable == 0x3F)
	{
		// **The three faces this normal actually faces.** An ambient cube's
		// weights are the squared components of the normal in the box's frame,
		// and they sum to one for a unit vector -- so this is a blend, not a sum
		// that needs normalising, and it costs the same three fetches the
		// spherical harmonics did.
		const vec3 uvw = (local * 0.5 + 0.5) * grid + 0.5;
		// **Against the atlas's own size, not this volume's.** The hardware
		// filter samples in texture space, so the coordinate has to be scaled
		// by the texture -- a volume narrower than the widest one would
		// otherwise stretch its cells across the whole width.
		const vec2 uv = (uvw.xy + vec2(xybase)) / vec2(texels.xy);
		const float depth = float(texels.z);
		float basis[9];
		FieldBasis(facing, basis);

		// Nine hardware-filtered fetches, one per coefficient; the sky's
		// visibility rides in the alpha of each. Both are linear in the
		// stored values, so the filter and the evaluation commute.
		irradiance = vec3(0.0);
		float cubeSky = 0.0;
		for (int k = 0; k < RV_FIELD_COEFFICIENTS; k++)
		{
			const vec4 stored = textureLod(u_IrradianceField,
					vec3(uv, (uvw.z + float(tile * k + zbase)) / depth), 0.0);
			irradiance += basis[k] * stored.rgb;
			cubeSky += basis[k] * stored.a;
		}
		vec3 directHere = vec3(0.0);
		vec3 rawC0 = vec3(0.0);
		vec3 rawL1 = vec3(0.0);
		if (hasDirect)
		{
			for (int k = 0; k < RV_FIELD_COEFFICIENTS; k++)
			{
				const vec3 c = textureLod(u_IrradianceField,
					vec3(uv, (uvw.z + float(tile * (RV_FIELD_DIRECT_TILE + k) + zbase)) / depth), 0.0).rgb;
				directHere += basis[k] * c;
				const float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
				if (k == 0) rawC0 = c;
				else if (k == 1) rawL1.y = lum;
				else if (k == 2) rawL1.z = lum;
				else if (k == 3) rawL1.x = lum;
			}
		}
		// **Divided by the filtered alive fraction.** A buried cell holds
		// zero light, and the hardware filter blends it in by distance like
		// any other -- so a surface against a wall or floor, half of whose
		// eight neighbours are inside the geometry, read half its light.
		// The alive flag filtered the same way is the weight those zeros
		// took; dividing by it leaves the mean of the live cells alone. For
		// the bounce this was a loss too small to see; with direct light in
		// the field it halved the showroom's walls. Sky is left as it is: a
		// buried cell stores "fully open" there on purpose.
		const float aliveWeight = textureLod(u_IrradianceField,
				vec3(uv, (uvw.z + float(tile * RV_FIELD_VISIBILITY_TILE + zbase)) / depth), 0.0).y;
		irradiance /= max(aliveWeight, 0.125);
		directHere /= max(aliveWeight, 0.125);
		rawC0 /= max(aliveWeight, 0.125);
		rawL1 /= max(aliveWeight, 0.125);
		// Second-order harmonics can ring a little below zero on the far
		// side of a lamp; the clamps are the whole of the correction.
		irradiance = max(irradiance, vec3(0.0)) * edgeFade;
		directLight = max(directHere, vec3(0.0)) * edgeFade;
		DerivedLamp(rawC0, rawL1, axisX, axisY, axisZ, edgeFade, directDirection, directCoherent);
		skyVisibility = clamp(cubeSky, 0.0, 1.0);
		return true;
	}

	float basis[9];
	FieldBasis(facing, basis);

	// **One cell, unblended** -- the solve's answer where the lookup landed on
	// a buried anchor. See the note at the step above: interpolation is the
	// part that can reach across a wall, so the case with no visibility
	// information to steer it takes the nearest surveyed cell whole.
	if (singleCell)
	{
		const ivec3 one = clamp(ivec3(floor(base + 0.5)), ivec3(0), last);
		float cubeSky = 0.0;
		vec3 here = vec3(0.0);
		for (int k = 0; k < RV_FIELD_COEFFICIENTS; k++)
		{
			const vec4 stored = texelFetch(u_IrradianceField,
										   one + ivec3(xybase, tile * k + zbase), 0);
			here += basis[k] * stored.rgb;
			cubeSky += basis[k] * stored.a;
		}
		vec3 directHere = vec3(0.0);
		vec3 rawC0 = vec3(0.0);
		vec3 rawL1 = vec3(0.0);
		if (hasDirect)
		{
			for (int k = 0; k < RV_FIELD_COEFFICIENTS; k++)
			{
				const vec3 c = texelFetch(u_IrradianceField,
					one + ivec3(xybase, tile * (RV_FIELD_DIRECT_TILE + k) + zbase), 0).rgb;
				directHere += basis[k] * c;
				const float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
				if (k == 0) rawC0 = c;
				else if (k == 1) rawL1.y = lum;
				else if (k == 2) rawL1.z = lum;
				else if (k == 3) rawL1.x = lum;
			}
		}
		// Aliveness from the visibility tile's .y -- the alpha lanes carry
		// sky, and a dead cell's sky evaluates to 1.0 there ("no data, do not
		// darken"), so testing it would call every buried cell alive.
		const bool alive = texelFetch(u_IrradianceField,
								   one + ivec3(xybase, tile * RV_FIELD_VISIBILITY_TILE + zbase), 0).y > 0.5;
		if (!alive)
			return false;   // buried there as well: nothing honest to report
		irradiance = max(here, vec3(0.0)) * edgeFade;
		directLight = max(directHere, vec3(0.0)) * edgeFade;
		DerivedLamp(rawC0, rawL1, axisX, axisY, axisZ, edgeFade, directDirection, directCoherent);
		skyVisibility = clamp(cubeSky, 0.0, 1.0);
		return true;
	}

	vec3 accumulated = vec3(0.0);
	vec3 directAccum = vec3(0.0);
	vec3 rawC0Accum = vec3(0.0);
	vec3 rawL1Accum = vec3(0.0);
	float total = 0.0;
	// **Accumulated out here, not per corner, and divided by the same total.**
	// The light beside it is a weighted mean -- summed against `total` and
	// divided at the end -- and sky visibility has to be the same or it is not
	// a visibility at all, just a sum of weights. Declared inside the corner
	// loop it reset every iteration and never left the last corner; multiplied
	// by the weight and never divided, it came out scaled by whatever the
	// weights happened to sum to. That is why the floor read black while the
	// wall beside it, which takes the fast path, read correctly.
	float cubeAccum = 0.0;
	// **Sky keeps its own total, because it counts corners light discards.**
	// A buried corner is skipped for light -- blending its zero is how a
	// surface against a wall loses three quarters of its illumination. For
	// sky visibility the same corner means something different: not
	// "darkness", but "no data", and the honest contribution of a cell that
	// knows nothing is "do not darken anything". Skipping it instead is what
	// made the floor black: a point on the floor has most of its corners
	// inside the floor, so the loop threw away seven of eight and averaged
	// whatever the last one held. Measured at 0.26 live corners of 8 against
	// the wall's 4.86, which is exactly where the black stopped.
	float skyTotal = 0.0;

	for (int c = 0; c < 8; c++)
	{
		const ivec3 offset = ivec3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
		const ivec3 index = clamp(corner + offset, ivec3(0), last);

		const vec3 axisWeight = mix(1.0 - frac, frac, vec3(offset));
		float weight = axisWeight.x * axisWeight.y * axisWeight.z;
		if (weight <= 0.0)
			continue;

		// Where that cell is, and which way this fragment lies from it -- both
		// in the box's frame, where the cells are.
		const vec3 cellPosition = (vec3(index) / grid * 2.0 - 1.0) * extents;
		const vec3 toCell = cellPosition - placed;
		const float span = length(toCell);
		const vec3 towardCell = span > 1.0e-4 ? toCell / span : facing;

		weight *= max(dot(towardCell, facing), 0.0);
		if (weight <= 0.0)
			continue;

		// **What the bake already decided: can the cell this fragment sits in
		// see the cell being blended in?** A corner reached by stepping along
		// one axis needs that axis's bit; a diagonal one needs every axis it
		// steps along, which is the right way round -- it can refuse a corner
		// light could have reached, and it cannot admit one behind a wall.
		//
		// **An empty mask is skipped rather than obeyed**: it means the
		// anchor is a cell inside geometry, which was never asked the
		// question, and refusing every corner on the strength of an answer
		// nobody gave is how a surface against a wall goes black. Facing and
		// aliveness below still apply, and between them they are most of the
		// test -- what is left is the diagonal case a mask would have caught.
		if (reachable != 0)
		{
			const ivec3 stepping = index - anchorCell;
			bool allowed = true;
			for (int axis = 0; axis < 3; axis++)
			{
				if (stepping[axis] == 0)
					continue;
				const int bit = axis * 2 + (stepping[axis] > 0 ? 0 : 1);
				if ((reachable & (1 << bit)) == 0)
					allowed = false;
			}
			if (!allowed)
				continue;
		}

		// The nine coefficients of this cell, evaluated for this normal.
		float cubeSky = 0.0;
		vec3 here = vec3(0.0);
		// One extra fetch per corner: the visibility tile's .y is the
		// aliveness flag, so the light's dead-cell skip keeps working while
		// the alpha lanes mean sky for the hardware filter.
		const bool alive = texelFetch(u_IrradianceField,
						  index + ivec3(xybase, tile * RV_FIELD_VISIBILITY_TILE + zbase), 0).y > 0.5;
		for (int k = 0; k < RV_FIELD_COEFFICIENTS; k++)
		{
			const vec4 stored = texelFetch(u_IrradianceField,
										   index + ivec3(xybase, tile * k + zbase), 0);
			here += basis[k] * stored.rgb;
			cubeSky += basis[k] * stored.a;
		}
		cubeSky = clamp(cubeSky, 0.0, 1.0);
		vec3 directHere = vec3(0.0);
		vec3 rawC0 = vec3(0.0);
		vec3 rawL1 = vec3(0.0);
		if (hasDirect && alive)
		{
			for (int k = 0; k < RV_FIELD_COEFFICIENTS; k++)
			{
				const vec3 c = texelFetch(u_IrradianceField,
					index + ivec3(xybase, tile * (RV_FIELD_DIRECT_TILE + k) + zbase), 0).rgb;
				directHere += basis[k] * c;
				const float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
				if (k == 0) rawC0 = c;
				else if (k == 1) rawL1.y = lum;
				else if (k == 2) rawL1.z = lum;
				else if (k == 3) rawL1.x = lum;
			}
		}

		// **A dead cell is skipped, not averaged in.** It holds zero because it
		// is buried in geometry, and blending that zero is the difference
		// between a surface against a wall reading the room's light and reading
		// three quarters of it.
		// Sky first, and unconditionally: a dead corner contributes "fully
		// open" rather than nothing, which is the same reading tile 7 gives
		// a buried cell and the same direction the mask's zero means.
		cubeAccum += (alive ? cubeSky : 1.0) * weight;
		skyTotal += weight;

		if (!alive)
			continue;

		accumulated += here * weight;
		directAccum += directHere * weight;
		rawC0Accum += rawC0 * weight;
		rawL1Accum += rawL1 * weight;
		total += weight;
	}

	// Nothing visible: the probe alone is a better answer than a wrong one.
	if (total <= 0.0)
		return false;

	irradiance = max(accumulated / total, vec3(0.0)) * edgeFade;
	directLight = max(directAccum / total, vec3(0.0)) * edgeFade;
	DerivedLamp(rawC0Accum / total, rawL1Accum / total, axisX, axisY, axisZ, edgeFade,
				directDirection, directCoherent);
	skyVisibility = skyTotal > 0.0 ? clamp(cubeAccum / skyTotal, 0.0, 1.0) : 1.0;
	return true;
}

// **Which probes cover this point, and how much each is worth.**
//
// The CPU picks one probe per object, against the object's bounds centre, and
// that is the right answer for the emitter list and for a small prop. For a
// surface it is not: a floor spanning two rooms gets one room's probe for all
// of it, and an object walking across an influence boundary flips its whole
// ambient and reflection in a single frame. Per fragment there is no boundary
// to cross -- the weights slide.
//
// Two probes and the sky, never more: the blend costs a cube fetch per slot in
// each of two arrays, and a third contributor buys less than it costs. The
// weight falls linearly to zero at the influence radius, so a probe stops
// mattering exactly where the CPU's own test says it stops applying, and
// whatever the two do not claim goes to the sky. That is what makes the edge
// of a probe's reach a fade rather than a step.
// How much of a probe's radius is fade rather than full strength. A quarter:
// wide enough that a walking character crosses it over many frames, narrow
// enough that most of a probe's volume is still exactly what the CPU's binary
// test would have given.
const float kProbeBlendShell = 0.25;

void ProbeBlendAt(vec3 position, out float slotA, out float slotB,
				  out float weightA, out float weightB)
{
	slotA = 0.0; slotB = 0.0;          // the sky
	weightA = 0.0; weightB = 0.0;

	const int count = clamp(int(u_Scene.ProbeCount.x + 0.5), 0, 15);

	for (int i = 0; i < count; i++)
	{
		const vec4 placement = u_Scene.ProbePlacement[i];
		const float radius = max(placement.w, 1.0e-4);

		// **Full weight until the outer shell, then a fade.** Falling off from
		// the probe's centre instead was the first attempt and it is wrong by
		// a lot: the CPU's rule is binary -- inside the influence you get the
		// probe -- so a linear falloff from the middle leaves a fragment at
		// three quarters of the radius taking three quarters of its light from
		// the *sky*. Measured on the showroom that darkened the room by 35%.
		//
		// The shell is where the pop was, and the only place a fade is owed.
		// Inside it this agrees with ProbeSlotFor exactly, which is the
		// property that matters: the lit pass and the traced bounce have to
		// light the same square metre the same way.
		const float shell = radius * kProbeBlendShell;
		const float distance = length(position - placement.xyz);
		const float weight = clamp((radius - distance) / max(shell, 1.0e-4), 0.0, 1.0);
		if (weight <= 0.0)
			continue;

		const float slot = u_Scene.ProbeSlot[i].x;
		if (weight > weightA)
		{
			slotB = slotA; weightB = weightA;
			slotA = slot;  weightA = weight;
		}
		else if (weight > weightB)
		{
			slotB = slot;  weightB = weight;
		}
	}

	// Normalised only when they would over-claim. Under one they are left
	// alone and the remainder is the sky's, which is the fade.
	const float total = weightA + weightB;
	if (total > 1.0)
	{
		weightA /= total;
		weightB /= total;
	}
}

// **A cube captured at a point is only right at that point.**
//
// Everywhere else the reflected ray should be traced to where it actually
// leaves the probe's volume and looked up from *there*, or a wall's reflection
// slides with the camera instead of staying put. The volume here is the
// influence sphere the probe already carries, which is the same shape the
// blend weights use -- so a probe is one radius, not two.
//
// A point outside the sphere has nothing to correct against and keeps its
// direction; so does a ray that never leaves it, which cannot happen for a
// point inside but is cheap to be safe about.
vec3 ProbeParallax(vec3 position, vec3 direction, float slot)
{
	const int count = clamp(int(u_Scene.ProbeCount.x + 0.5), 0, 15);

	for (int i = 0; i < count; i++)
	{
		if (abs(u_Scene.ProbeSlot[i].x - slot) > 0.5)
			continue;

		const vec4 placement = u_Scene.ProbePlacement[i];
		const vec3 offset = position - placement.xyz;
		const float b = dot(offset, direction);
		const float c = dot(offset, offset) - placement.w * placement.w;
		const float h = b * b - c;
		if (h <= 0.0)
			return direction;          // outside the sphere: nothing to say

		const float t = -b + sqrt(h);
		if (t <= 0.0)
			return direction;

		return normalize(position + direction * t - placement.xyz);
	}

	return direction;                  // the sky, which has no position
}


#if defined(RV_RAY_REFLECTIONS) || defined(RV_RAY_GI) || defined(RV_RAY_REFRACTION)
// The radiance the mirror ray from `origin` along `direction` finds
// (ENGINE-NOTES 7ao): the sky where it misses, and a *simplified* shade of
// the surface where it hits -- base colour and emissive through the hit's
// material and the heap, the interpolated vertex normal (flat for a posed
// caster, whose normals were not posed), every light in the buffer
// unclustered with one shadow ray toward the sun, and the sky's irradiance
// for ambient. No normal map, no parallax, no local-light shadows, no
// occlusion: a mirror of a brick wall shows the wall's colour and lighting,
// not its mortar's bump. Exact for emissive geometry, which is what the
// check that judges it is made of.
#ifdef RV_RAY_GI
// The bounce rays' die (7at). The same integer hash the post passes use, kept
// here rather than shared because a shader include that exists for one caller
// is a file to find rather than a line to read.
uint GiHash(uint x)
{
	x ^= x >> 16; x *= 0x7FEB352Du;
	x ^= x >> 15; x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

// One uniform draw in [0, 1), advancing the seed.
//
// Pulled out because next-event estimation needs single draws -- which emitter,
// and where on it -- rather than the pairs CosineDirection consumes, and two
// samplers drawing from one stream have to advance it the same way or they
// correlate.
float GiRandom(inout uint seed)
{
	seed = GiHash(seed);
	return float(seed & 0x00FFFFFFu) / 16777216.0;
}

// One cosine-weighted direction about `n`, advancing the seed by two draws.
//
// Cosine-weighted is the distribution the diffuse integral wants, so the mean
// of the samples *is* the irradiance with no per-sample cosine to divide back
// out -- which is also why one sample of it substitutes directly for the
// probe's irradiance at a hit (7ax).
//
// A function rather than two copies, because the second bounce draws from the
// same sequence: `seed` is inout so the caller's stream advances, and the
// caller perturbs it between bounces so the two rays are not the same ray.
vec3 CosineDirection(vec3 n, inout uint seed)
{
	seed = GiHash(seed);
	float u1 = float(seed & 0x00FFFFFFu) / 16777216.0;
	seed = GiHash(seed);
	float u2 = float(seed & 0x00FFFFFFu) / 16777216.0;

	vec3 axis = abs(n.x) < 0.7 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
	vec3 t = normalize(cross(axis, n));
	vec3 b = cross(n, t);

	float r = sqrt(u1);
	float phi = 6.2831853 * u2;
	return normalize(t * (r * cos(phi)) + b * (r * sin(phi))
					+ n * sqrt(max(1.0 - u1, 0.0)));
}
#endif

// What a ray found, before anything decides how to light it (ENGINE-NOTES
// 7ax). Splitting the tracer here is what lets the GI path run it twice: GLSL
// has no recursion, so the alternative is a second copy of the vertex fetch
// and the light loop below, which 7au refused for the terrain query for the
// same reason.
struct TracedSurface
{
	bool Missed;
	vec3 Sky;        // where it missed: the environment along the ray
	vec3 Position;   // where it hit, for a ray that continues from there
	vec3 Normal;
	vec3 Diffuse;    // albedo, metals removed
	vec3 Direct;     // every light, shadowed toward the sun
	vec3 Emissive;
	// Whether an area emitter answers for this surface's emissive. The
	// bounce subtracts Emissive only where this is true; see
	// RAY_INSTANCE_EMITTER.
	bool IsEmitter;
	// **Whether the ray struck the back of the surface** -- that is, whether
	// it started inside the solid this surface bounds.
	//
	// Recorded because `Normal` cannot answer it: the normal below is turned
	// to face the ray, which every shading path depends on, and the turn
	// destroys the only evidence. A caller that asks
	// `dot(surface.Normal, direction) > 0` is asking a question whose answer
	// is always no, and the irradiance fill asked exactly that for its
	// buried-cell test -- so the test never once fired, no cell was ever
	// classified as inside geometry, and cells sitting in walls stored the
	// light they saw through them. Both the sealed-room glow and the
	// divider's bright edge were that.
	bool Backface;
	// MeshComponent::Static of the instance hit (RAY_INSTANCE_STATIC, 7cx):
	// whether the fully baked lights are in the field for this surface, or
	// were walked live into Direct.
	bool Static;
};

// `reach` is how far the ray may travel, in world metres. A reflection wants
// the whole scene; a diffuse bounce does not -- see Renderer::SetGiReach.
TracedSurface TraceSurface(vec3 origin, vec3 Ng, vec3 direction, float reach)
{
	TracedSurface surface;
	surface.Missed = false;
	surface.Sky = vec3(0.0);
	// The origin and the incoming normal, so a miss still describes somewhere
	// real -- a caller that reads Position or Normal after a miss gets the ray's
	// own start rather than whatever was on the stack.
	surface.Position = origin;
	surface.Normal = Ng;
	surface.Diffuse = vec3(0.0);
	surface.Direct = vec3(0.0);
	surface.Emissive = vec3(0.0);
	surface.IsEmitter = false;
	surface.Backface = false;
	surface.Static = false;

	// Off the surface along its geometric normal, the shadow ray's offset,
	// for the shadow ray's reason.
	float NgdotD = clamp(dot(Ng, direction), 0.0, 1.0);
	float slope = sqrt(1.0 - NgdotD * NgdotD) / max(NgdotD, 0.15);
	float offset = 0.002 * (1.0 + min(slope, 4.0));

	RV_COUNT_SURFACE_RAY();
	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_SceneAS, RV_RAY_BASE_FLAGS, RV_RAY_MASK_SCENE,
						  origin + Ng * offset, 0.0, direction, reach);
	RayTraverse(q);

	if (rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT)
	{
		// The sky, unfiltered: a mirror ray sees the sky at full sharpness.
		surface.Missed = true;
		surface.Sky = textureLod(u_Environment, vec4(RotateIntoSky(direction), 0.0), 0.0).rgb *
				      u_Scene.Environment.x;
		return surface;
	}

	// What was hit, and where on it.
	uint instance = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(q, true));
	uint primitive = uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, true));
	vec2 bary = rayQueryGetIntersectionBarycentricsEXT(q, true);
	float w0 = 1.0 - bary.x - bary.y;
	mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(q, true);

	RayInstance hit = u_RayInstances.Instances[instance];
	surface.Static = (hit.Flags & RAY_INSTANCE_STATIC) != 0u;
	RayWords indices = RayWords(hit.IndexAddress);
	RayFloats positions = RayFloats(hit.PositionAddress);
	RayFloats attributes = RayFloats(hit.AttributeAddress);

	uint i0 = indices.Words[primitive * 3u + 0u];
	uint i1 = indices.Words[primitive * 3u + 1u];
	uint i2 = indices.Words[primitive * 3u + 2u];

	// Position and normal at the first three floats of a vertex, texture
	// coordinate at the next two: MeshVertex and SkinnedVertex agree on that
	// much, and the strides say the rest.
	uint ps = hit.PositionStrideWords;
	uint as_ = hit.AttributeStrideWords;
	vec2 uv0 = vec2(attributes.Floats[i0 * as_ + 6u], attributes.Floats[i0 * as_ + 7u]);
	vec2 uv1 = vec2(attributes.Floats[i1 * as_ + 6u], attributes.Floats[i1 * as_ + 7u]);
	vec2 uv2 = vec2(attributes.Floats[i2 * as_ + 6u], attributes.Floats[i2 * as_ + 7u]);

	// **Where the ray stopped, from the ray.** The traversal already solved
	// this: the committed hit distance along a world-space ray is the world
	// position, with no vertex positions, no object-to-world matrix and no
	// barycentric reconstruction behind it.
	//
	// Those three position loads were nine scalar reads scattered across the
	// vertex buffer -- a different triangle per lane, so effectively
	// uncached -- plus a twelve-word matrix fetch and nine multiply-adds, on
	// every hit of every hemisphere and mirror ray. They are still loaded for
	// a *posed* hit, which needs the triangle's own plane, and that path is a
	// minority of hits in every scene here.
	//
	// Not bit-identical with the barycentric form, and arguably the more
	// consistent of the two: this is the point the traversal itself
	// intersected, while the interpolated one can sit a float's width off the
	// triangle it came from.
	vec3 hitPosition = origin + Ng * offset
					 + direction * rayQueryGetIntersectionTEXT(q, true);
	vec2 hitUv = uv0 * w0 + uv1 * bary.x + uv2 * bary.y;

	vec3 objectNormal;
	if ((hit.Flags & RAY_INSTANCE_POSED) != 0u)
	{
		// Posed positions, unposed normals: the triangle's own plane is the
		// honest normal. The only reader of the position buffer left.
		vec3 p0 = vec3(positions.Floats[i0 * ps + 0u], positions.Floats[i0 * ps + 1u], positions.Floats[i0 * ps + 2u]);
		vec3 p1 = vec3(positions.Floats[i1 * ps + 0u], positions.Floats[i1 * ps + 1u], positions.Floats[i1 * ps + 2u]);
		vec3 p2 = vec3(positions.Floats[i2 * ps + 0u], positions.Floats[i2 * ps + 1u], positions.Floats[i2 * ps + 2u]);
		objectNormal = cross(p1 - p0, p2 - p0);
	}
	else
	{
		vec3 n0 = vec3(attributes.Floats[i0 * as_ + 3u], attributes.Floats[i0 * as_ + 4u], attributes.Floats[i0 * as_ + 5u]);
		vec3 n1 = vec3(attributes.Floats[i1 * as_ + 3u], attributes.Floats[i1 * as_ + 4u], attributes.Floats[i1 * as_ + 5u]);
		vec3 n2 = vec3(attributes.Floats[i2 * as_ + 3u], attributes.Floats[i2 * as_ + 4u], attributes.Floats[i2 * as_ + 5u]);
		objectNormal = n0 * w0 + n1 * bary.x + n2 * bary.y;
	}
	// A normal transforms by the inverse transpose: the world-to-object
	// matrix's rotation, transposed, which is applying its rows as columns.
	vec3 hitNormal = normalize(vec3(dot(worldToObject[0].xyz, objectNormal),
									dot(worldToObject[1].xyz, objectNormal),
									dot(worldToObject[2].xyz, objectNormal)));
	// Toward the ray, whichever face was hit -- and the fact of the turn
	// carried out, because it is the only place that knows. See
	// TracedSurface::Backface.
	const bool hitBackface = dot(hitNormal, direction) > 0.0;
	if (hitBackface)
		hitNormal = -hitNormal;

	// The field's weight at this hit (7cx): a static hit takes the fully
	// baked lights from the field only where a volume covers it, live for
	// the rest -- the beach in the water's mirror is lit by the lamps like
	// the beach on screen. The solve never reads it: there every baked light
	// is walked live, which is what fills the field.
	float hitFieldWeight = 0.0;
#ifndef RV_IRRADIANCE_FILL
	if (surface.Static && u_Scene.IrradianceExtents.w > 0.0)
		hitFieldWeight = IrradianceFieldWeight(hitPosition, hitNormal);
#endif

	// The material: the record, then the maps through the heap.
	GpuMaterial material = u_Materials.Materials[hit.MaterialIndex];
	vec2 uv = hitUv * material.UvTransform.xy + material.UvTransform.zw;

	vec3 albedo = hit.BaseColor.rgb;
	if ((material.MapFlags & MAP_BASE_COLOR) != 0)
		albedo *= textureLod(u_Textures[nonuniformEXT(material.Maps0.x)], uv, 0.0).rgb;
	// **The same variation the raster shader applies.** Without it a
	// macro-varied pier reflects back perfectly even, and a bounce off it
	// carries a colour the surface does not have -- which is exactly the tell
	// that gives a screen-space trick away. Roughness is unused here: a hit is
	// shaded Lambert, so only the albedo moves.
	{
		float ignored = 1.0;
		ApplyMacro(albedo, ignored, hitPosition, material.Macro.x, material.Macro.y);
	}

	vec3 emissive = hit.EmissiveColor.rgb;
	if ((material.MapFlags & MAP_EMISSIVE) != 0)
		emissive *= textureLod(u_Textures[nonuniformEXT(material.Maps0.w)], uv, 0.0).rgb;
	float metallic = hit.Surface.x;
	if ((material.MapFlags & MAP_METALLIC) != 0)
		metallic *= textureLod(u_Textures[nonuniformEXT(material.Maps1.y)], uv, 0.0).r;

	// Lambert only, every light, no clustering -- a hit is not on screen and
	// has no cluster -- and a shadow ray for the sun alone.
	vec3 diffuse = albedo * (1.0 - metallic);
	vec3 lit = vec3(0.0);
	// **A measurement, not a feature: `--hit-lights=off` (RayRates.z) skips
	// this walk**, so the ray's own cost and the walk's can be told apart
	// before WR-10 decides what to build. Uniform, so the loop is not
	// compiled out; the picture with it off is wrong on purpose.
	const int hitLightCount = u_Scene.RayRates.z > 0.5 ? 0 : u_Scene.LightCount;
	// WR-16 S4's sizing, as on screen: at most N positional lamps are read in
	// full and shaded at this hit. Read from RayRates.w's high bits here too,
	// so one flag covers both halves of the lamp cost.
	const int hitShadeLimit = ((int(u_Scene.RayRates.w + 0.5) >> 8) & 255) - 1;
	int hitShaded = 0;
#ifndef RV_TRACE_ONLY
	// **WR-10: the hit takes the cluster it falls in.** The cluster grid is
	// cut through the camera's view, and this loop assumed a hit had no
	// cluster because a hit is not on screen -- but nearly every hit is
	// *inside the view*: the reflected bridge, the seabed under the water.
	// So project the hit, and if it lands inside the frustum, walk that
	// cell's list instead of every light in the scene. A hit outside the
	// view walks every light as before. Exact where a cell holds every light
	// in range of it, which the CPU's binning guarantees. Measured before
	// this: the whole-scene walk was ~40 ms of a 114 ms frame at 1440p.
	const int hitDirectional = int(u_Scene.ClusterGrid.w);
	uint hitCellOffset = 0u;
	int hitCellCount = -1;   // -1: no cell, every light
	{
		const vec4 clip = u_Scene.ViewProjection * vec4(hitPosition, 1.0);
		const float viewDepth = dot(hitPosition - u_Scene.CameraPosition.xyz,
									u_Scene.CameraForward.xyz);
		if (clip.w > 0.0 && abs(clip.x) <= clip.w && abs(clip.y) <= clip.w
			&& viewDepth > u_Scene.ClusterDepth.x && viewDepth < u_Scene.ClusterDepth.y)
		{
			const vec2 unit = clamp(clip.xy / clip.w * 0.5 + 0.5, vec2(0.0), vec2(0.9999));
			const uvec2 tile = uvec2(unit * u_Scene.ClusterGrid.xy);
			const float slice = log(viewDepth) * u_Scene.ClusterDepth.z + u_Scene.ClusterDepth.w;
			const uint z = uint(clamp(slice, 0.0, u_Scene.ClusterGrid.z - 1.0));
			const uint cell = (z * uint(u_Scene.ClusterGrid.y) + tile.y)
							* uint(u_Scene.ClusterGrid.x) + tile.x;
			hitCellOffset = u_Cells.Cells[cell].Offset;
			hitCellCount = int(u_Cells.Cells[cell].Count);
#ifdef RV_LIGHT_CULL
			// WR-16 S2: a static hit deep inside the field walks the cell's
			// live sublist alone, in the full list's order; the lamps it
			// leaves out are ones it would have read and dropped. The
			// sublist's on-screen-only entries (a fully baked lamp with a
			// moving object under it) fall to the cull record below.
			if (surface.Static && hitFieldWeight >= 1.0)
			{
				hitCellOffset = u_Cells.Cells[cell].LiveOffset;
				hitCellCount = int(u_Cells.Cells[cell].LiveCount);
			}
#endif
		}
	}
	const int hitTotal = hitLightCount == 0 ? 0
					   : (hitCellCount < 0 ? hitLightCount : hitDirectional + hitCellCount);
	RV_COUNT_HIT(hitTotal);
	for (int entry = 0; entry < hitTotal; ++entry)
	{
		const int i = hitCellCount < 0 ? entry
					: (entry < hitDirectional ? entry
					   : int(u_CellIndices.Indices[hitCellOffset + uint(entry - hitDirectional)]));
#else
	RV_COUNT_HIT(hitLightCount);
	for (int i = 0; i < hitLightCount; ++i)
	{
#endif
#ifdef RV_LIGHT_CULL
		// WR-16 S2: sixteen bytes decide whether the eighty are read.
		if (LightCullRejects(uint(i), hitPosition, surface.Static && hitFieldWeight >= 1.0, false))
			continue;
#endif
		if (hitShadeLimit >= 0 && i >= int(u_Scene.ClusterGrid.w))
		{
			if (hitShaded >= hitShadeLimit)
				continue;
			++hitShaded;
		}
		GpuLight light = u_Lights.Lights[i];
		float hitLiveShare = 1.0;
#ifndef RV_IRRADIANCE_FILL
		// A fully baked light's direct light is in the field this hit adds --
		// for a static surface, where the field covers it (7cx). A moving
		// surface, the car in the water's mirror, is not in the field's world
		// and takes the light live here, as it does on screen; so does a
		// static one outside every volume.
		if (light.Params.w > 1.5 && u_Scene.IrradianceExtents.w > 0.0 && surface.Static)
		{
			hitLiveShare = 1.0 - hitFieldWeight * BakedShare(light, hitPosition);
			if (hitLiveShare <= 0.0)
				continue;
		}
#endif
#ifdef RV_IRRADIANCE_FILL
		// **The solve shades its hits without the realtime lights.**
		// Params.w carries Light::Mobility; a light the lighting hash skips
		// must not put its bounce into a file its toggles cannot rename --
		// that is the whole mobility contract. Every frame compile keeps the
		// loop as it was: realtime forms light everything, always.
		if (light.Params.w < 0.5)
			continue;
#endif
		vec3 lightColor = light.Color.rgb * light.Color.a;
		vec3 L;
		float attenuation = 1.0;
		float shadow = 1.0;
		if (light.Position.w == 0.0)
		{
			// Already unit length: Scene.cpp normalises it when the light is
			// gathered, and Renderer3D copies it through untouched. Stated
			// here because it makes that an invariant this shader depends on.
			L = -light.Direction.xyz;
			if (int(light.Shadow.x) != 0)
				shadow = TraceShadowFrom(hitPosition, hitNormal, L, 1.0e4);
		}
		else
		{
			vec3 toLight = light.Position.xyz - hitPosition;
			float distance2 = dot(toLight, toLight);
			float range = max(light.Params.x, 0.0001);

			// A light past its range contributes exactly zero -- the ratio
			// below clamps to it -- so skipping it is an identity, not an
			// approximation: the showroom and the camp render bit-identical
			// with and without it. A traced hit has no cluster to consult,
			// which is why this loop runs every light in the scene.
			//
			// Measured honestly: in the showroom this buys nothing (7.2 ms
			// against 7.2 up close, interleaved) because the fill panels'
			// ranges cover the whole room and almost nothing ever culls. It
			// stays because it is exact and free, and it bounds the loop for
			// the scene this loop has not met yet -- many short-range lights,
			// a street of lamp posts -- where every hit otherwise pays for
			// all of them.
			if (distance2 >= range * range)
				continue;

			// **And the lights behind this surface**, which the line at the
			// bottom of the loop multiplies to exactly zero anyway. It did so
			// only after a sqrt, a divide, a pow and a spot cone with its own
			// normalize -- roughly forty operations to reach a number that
			// was already known to be nothing.
			//
			// Normalising cannot change the sign of the dot, so this is the
			// same test line `lit +=` makes, made before the work instead of
			// after it. Bit-identical: X * 0.0 is 0.0 for every finite X, and
			// the epsilons below rule out an infinity reaching it.
			//
			// It pays into both hot passes at once -- the traced bounce calls
			// this per hemisphere ray, and the lit pass calls it per mirror
			// ray -- and it pays most where clustering helps least, which is
			// here: a traced hit has no cluster to consult, so this loop runs
			// every light in the scene.
			if (dot(hitNormal, toLight) <= 0.0)
				continue;

			// **All of this in distance-squared**, which is the form the
			// caller already has. Every use of `distance` had a squared
			// equivalent: 1 - (d/r)^4 is 1 - ((d/r)^2)^2, the normalise is an
			// inverse square root, and the falloff's denominator was squaring
			// it straight back. What goes is a sqrt, a full-precision divide
			// and a pow -- and pow lowers to log2, a multiply and exp2, so
			// that is four special-function operations out of roughly six in
			// the whole iteration, on a unit that issues at a quarter rate.
			//
			// Not bit-identical: (x*x)*(x*x) and pow(x,4) differ in the last
			// ulp or two, as do inversesqrt and sqrt-then-divide. Sub-1e-6 on
			// a value that scales a colour.
			const float t = distance2 / (range * range);
			const float ratio = clamp(1.0 - t * t, 0.0, 1.0);
			L = toLight * inversesqrt(max(distance2, 1.0e-8));
			attenuation = (ratio * ratio) / max(distance2, 0.0001);

			// **Out of range, and outside the cone** -- the two remaining ways a
			// light contributes exactly nothing to this hit, tested before the
			// ray rather than multiplied in after it.
			//
			// This is the argument the back-face test above already makes, and
			// it was left half made. Both numbers were being computed anyway --
			// the falloff here, the cone below the shadow ray -- and a lamp on
			// the far side of the room, or a spot aimed somewhere else, still
			// paid for a full traversal of the acceleration structure before
			// its contribution was multiplied by zero.
			//
			// **The cone moves above the ray; it does not change.** attenuation
			// is still built by the same two multiplies in the same order, and
			// nothing between them touched it, so this is bit-identical for the
			// reason the back-face test is: X * 0.0 is 0.0 for every finite X.
			//
			// It pays where clustering cannot help at all -- a traced hit is not
			// on screen and has no cluster, so this loop runs every light in the
			// scene. The showroom has twenty, with ranges of six to eleven
			// metres, in a room eleven metres by seventeen.
			if (ratio <= 0.0)
				continue;

			float cosInner = light.Params.y;
			float cosOuter = light.Params.z;
			if (cosOuter < cosInner)
			{
				float theta = dot(L, -light.Direction.xyz);
				float cone = clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
				if (cone <= 0.0)
					continue;
				attenuation *= cone;
			}

			// **And a shadow ray for this one too, not the sun alone.**
			//
			// Without it every hit in the level is lit by every lamp in it,
			// through walls, floors and closed doors -- and what reads that
			// answer is the bounce, so a sealed room beside a lit one glowed:
			// 165 levels of 255 where the right answer was black. Measured on
			// the leak scene, and it is the largest single error either the
			// bounce or the field has had.
			//
			// Bounded by the distance to the light rather than by the sky:
			// what is behind a lamp cannot shadow it, and a ray that stops at
			// the source is the cheaper half of the same question.
			if (int(light.Shadow.x) != 0)
			{
				shadow = TraceShadowFrom(hitPosition, hitNormal, L,
										 sqrt(max(distance2, 1.0e-8)));
			}
		}
		lit += diffuse / PI * lightColor * attenuation * max(dot(hitNormal, L), 0.0) * shadow
			 * hitLiveShare;
	}

	surface.Position = hitPosition;
	surface.Normal = hitNormal;
	surface.Diffuse = diffuse;
	surface.Direct = lit;
	surface.Emissive = emissive;
	surface.IsEmitter = (hit.Flags & RAY_INSTANCE_EMITTER) != 0u;
	surface.Backface = hitBackface;
	return surface;
}

// The probe's answer for indirect light arriving at a surface facing `normal`.
// One bounce shades every hit with this; the second bounce replaces it with a
// traced ray and lets *that* hit have it instead, which is why the recursion
// ends at depth two by construction and not by a counter.
// `probe` is which cube of the array answers. A parameter rather than the
// varying it used to read: the tracing below is shared with a pass that has no
// vertex stage and so no per-instance probe, and a function that reaches for a
// varying cannot be shared with one.
vec3 ProbeIrradiance(vec3 normal, float probe)
{
	return textureLod(u_Irradiance, vec4(RotateIntoSky(normal), probe), 0.0).rgb *
			  u_Scene.Environment.x;
}

#ifdef RV_IRRADIANCE_FILL
// Which flavour the solve is producing, set from the push constants at the
// top of the fill's main -- a global because ShadeTraced is shared with
// compiles that have no push-constant block to read it from.
bool g_FillFeedback = false;
#endif

// A described hit plus what arrives at it. Callers check `Missed` first: this
// deliberately does not, so a miss does not pay for an irradiance fetch it
// throws away.
vec3 ShadeTraced(TracedSurface surface, vec3 arriving)
{
	// **Added to the flat ambient, not put in its place.** The field stores
	// bounced light only, and `arriving` is the probe -- the sky half. Each of
	// the three is a different part of what reaches this hit, and every one of
	// them is counted once. Replacing the constant here is what an earlier
	// version did, when a cell still held sky as well, and it made the field
	// stand in for terms it was already standing beside.
	vec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;
#ifdef RV_IRRADIANCE_FILL
	// **Only the solve reads the field at a hit, and only for the traced
	// flavour -- this is what turns its passes into bounces.** No frame
	// compile enters this block: a Baked source does not trace at all -- the
	// frame reads the field per pixel and the saving is the whole point --
	// and Realtime answers without the bake by definition. What is left is
	// the solve shading its own rays' hits, where the sampled field is the
	// *previous* sweep's completed answer (the solve writes a second texture
	// and the two swap at each sweep boundary; see
	// Renderer3D::SolvePendingIrradiance). Sweep k's rays therefore inherit
	// k-1 bounces of stored transport, and the fixed point the sweeps
	// converge to is the full multi-bounce answer -- bought once, at bake
	// time, and read back per frame for the cost of a fetch.
	//
	// `g_FillFeedback` is the flavour switch, set from the push constants at
	// the top of the fill's main: the screen flavour skips this read and
	// stores a converged single bounce, matching what the gather and the
	// voxel form estimate; the traced flavour takes it and matches realtime
	// RTGI. Uniform across the draw, so the branch costs nothing.
	//
	// **The visibility test is not optional here.** Feedback amplifies
	// whatever it reads: a sealed room beside a lit one measured 0.15 levels
	// of leak after one unguarded sweep and 6.3 after eight, because five per
	// cent of a leak fed back eight times is not five per cent. The stored
	// visibility is what makes the loop safe to close, so this reader always
	// takes the careful path -- at bake time it costs bake time, which is the
	// cheap currency.
	if (g_FillFeedback)
	{
		vec3 stored;
		// **Deliberately not sky-occluded**, and the discard is the point: this
		// is the solve reading its own previous sweep, and the visibility it
		// would apply is the number this very pass is producing. Feeding it back
		// would occlude the sky twice over by the time the sweeps converge. The
		// lit pass applies it once, at the end, which is where it belongs.
		float solveSky;
		vec3 solveDirect;
		vec3 solveDirection;
		vec3 solveCoherent;
		if (VolumeIrradiance(surface.Position, surface.Normal, true, stored, solveSky, solveDirect,
							 solveDirection, solveCoherent))
			ambientLight += stored;
	}
#else
	// **A reflection or refraction hit on a static surface takes the fully
	// baked lights' direct light from the field**, where the live walk above
	// skipped them; a moving surface walked them live and reads nothing here
	// (7cx). Not in the solve: there the fully baked lights are walked live
	// at hits, which is what puts their bounce into the field in the first
	// place.
	if (u_Scene.IrradianceExtents.x > 0.5 && surface.Static)
	{
		vec3 hitBounce;
		float hitSky;
		vec3 hitDirect;
		vec3 hitDirection;
		vec3 hitCoherent;
		if (VolumeIrradiance(surface.Position, surface.Normal, true, hitBounce, hitSky, hitDirect,
							 hitDirection, hitCoherent))
			ambientLight += hitDirect;
	}
#endif
	return surface.Direct + surface.Diffuse * (ambientLight + arriving) + surface.Emissive;
}

// Unchanged in what it returns (7ax): find the surface, shade it with the
// probe. The reflection ray stays one bounce deep even while the diffuse
// around it is two -- a mirror ray is not a light-transport estimate, nothing
// accumulates it, and doubling its cost would buy an agreement nobody can see.
vec3 TraceReflection(vec3 origin, vec3 Ng, vec3 direction, float probe)
{
	TracedSurface surface = TraceSurface(origin, Ng, direction, 1.0e4);
	if (surface.Missed)
		return surface.Sky;
	return ShadeTraced(surface, ProbeIrradiance(surface.Normal, probe));
}
#endif

// The split-sum environment BRDF, read from the table.
//
// The scale and bias to apply to a material's F0, given how directly the
// surface faces the viewer and how rough it is. It depends on nothing else --
// not the environment, not the material's colour -- which is what makes one
// table serve every surface in every scene.
//
// This replaced Lazarov's analytic fit, which is within a couple of percent
// across most of the range and visibly wrong at grazing angles on smooth
// metal, which is the one case anybody looks at.
vec2 EnvBRDF(float NoV, float roughness)
{
	return texture(u_BRDF, vec2(clamp(NoV, 0.0, 1.0), clamp(roughness, 0.0, 1.0))).rg;
}

// Trowbridge-Reitz GGX: the microfacet distribution.
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
	// Squaring roughness makes the perceptual slider behave linearly, which is
	// the convention glTF and every DCC tool assume.
	float a  = roughness * roughness;
	float a2 = a * a;
	float NdotH = max(dot(N, H), 0.0);
	float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * denom * denom, 0.0001);
}

// Smith geometry term with the Schlick-GGX approximation.
float GeometrySchlickGGX(float NdotV, float roughness)
{
	float r = roughness + 1.0;
	float k = (r * r) / 8.0;   // direct lighting remapping
	return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
	return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
		   GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

#ifdef RV_WATER
// The sea's lobe and its masking term, shared with the pass that scores its
// lamps (WR-16 S4b): see include/water_lobe.glsl for why Beckmann rather
// than GGX, why it is anisotropic about the wind, and why the roughness it
// takes is the RMS slope itself.
#include "water_lobe.glsl"

// The web of light a rippled surface focuses onto whatever sits beneath it.
//
// Real caustics are the refracted sun concentrated by the surface's small
// curvature -- centimetre-to-metre ripple, not the swell -- so the pattern
// reads at the detail ripple's own metre scale, fixed, rather than scaling
// with the wavelength the way the foam does. Two reads of the lace tile
// drifting against each other, multiplied: the product of two independent
// webs is a cellular field that never shows either tile's repeat. Sharpened
// above one so the bright strands overshoot, which is what focused light
// does and flat texture never quite fakes.
//
// Masked to the first stretch of depth, quoted against the gradient dial:
// deeper than a few metres the focus spreads back out and the pattern
// washes away, and the absorption is already eating the light besides.
float WaterCaustics(vec2 bottomXZ, float through, float gradientDepth,
					vec2 wind, float time)
{
	const float web1 = texture(u_WaterFoamPattern,
							   bottomXZ / 2.9 + wind * (time * 0.050)).r;
	const float web2 = texture(u_WaterFoamPattern,
							   bottomXZ / 1.9 - wind * (time * 0.037)).r;
	const float focus = clamp(1.0 - through / max(gradientDepth, 0.5), 0.0, 1.0);
	return web1 * web2 * 2.2 * focus * focus;
}
#endif

// --- the extended lobes -------------------------------------------------------
//
// **One GGX lobe is most of what a hard surface is, and not all of it.** What
// it cannot express is a second interface, a fuzzy one, a directional one, or a
// translucent one. Each of the four below is off at zero, so a material that
// does not ask for them shades exactly as it did.

// Anisotropic GGX: the roughness split into two axes about the tangent, which
// is what stretches a highlight along a grain -- brushed metal, hair, vinyl.
// Water already does this with its own Beckmann lobe a few functions up; this
// is the same idea for everything else.
float DistributionGGXAniso(float NdotH, float TdotH, float BdotH, float ax, float ay)
{
	const float a2 = ax * ay;
	const vec3 d = vec3(ay * TdotH, ax * BdotH, a2 * NdotH);
	const float d2 = dot(d, d);
	if (d2 <= 0.0)
		return 0.0;
	const float b2 = a2 / d2;
	return a2 * b2 * b2 * (1.0 / PI);
}

// **Charlie, not GGX, and the difference is the whole point.** A GGX lobe falls
// to nothing at the silhouette; cloth does the opposite -- velvet is brightest
// at its rim, because the fibres stand up and catch light the surface beneath
// cannot. Estevez & Kulla's distribution is the one that rises there.
float DistributionCharlie(float NdotH, float roughness)
{
	const float inverse = 1.0 / max(roughness, 0.07);
	const float sin2 = max(1.0 - NdotH * NdotH, 0.0);
	return (2.0 + inverse) * pow(sin2, inverse * 0.5) / (2.0 * PI);
}

// Ashikhmin's visibility, which pairs with Charlie. Smith does not: it is
// derived for the same microfacet model GGX is, and using it here puts the
// sheen back where GGX would have had it.
float VisibilityAshikhmin(float NdotV, float NdotL)
{
	return 1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV) + 1.0e-4);
}

// Kelemen's, for the clearcoat. A coat is smooth and thin enough that the full
// Smith height-correlated term is spending arithmetic on a difference nothing
// can see.
float VisibilityKelemen(float VdotH)
{
	return 0.25 / max(VdotH * VdotH, 1.0e-4);
}

// **A wrap, not a diffusion profile, and it says so.** Real subsurface
// scattering traces light through the medium and comes out somewhere else;
// this lets the diffuse term reach past the terminator instead. It buys the
// soft edge of skin, marble, wax and leaves -- which is most of what the eye
// reads as translucent -- and it will not buy the glow of a hand held over a
// torch. Anything wanting that needs a real profile, and that is a feature and
// not a constant.
float WrapDiffuse(float NdotL, float wrap)
{
	return clamp((NdotL + wrap) / ((1.0 + wrap) * (1.0 + wrap)), 0.0, 1.0);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	vec3 F90 = max(vec3(1.0 - roughness), F0);
	return F0 + (F90 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Screen-space derivative TBN. Avoids needing tangents in the vertex format,
// which means generated primitives and imported meshes work the same way.
// The tangent frame from screen-space derivatives (Schuler's construction),
// so meshes need no precomputed tangents. Built once per fragment and shared
// by parallax and normal mapping -- two frames that drifted would shade one
// direction and displace another, which reads as the texture swimming.
mat3 TangentFrame(vec3 N, vec3 worldPos, vec2 uv)
{
	vec3 dp1 = dFdx(worldPos);
	vec3 dp2 = dFdy(worldPos);
	vec2 duv1 = dFdx(uv);
	vec2 duv2 = dFdy(uv);

	vec3 dp2perp = cross(dp2, N);
	vec3 dp1perp = cross(N, dp1);
	vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
	vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

	// The construction inherits the orientation of the screen: under Vulkan's
	// negative-height viewport dFdy is the negative of GL's, both terms of T
	// and of B flip, and the frame comes out rotated 180 degrees about N --
	// bumps lit from the wrong side, parallax marching the wrong way, on
	// exactly one backend. det(dp1, dp2, N) carries that orientation and
	// nothing else the frame should keep, so divide its sign out. Handedness
	// correction could not do this: negating both T and B is a rotation, and
	// cross(T, B) does not move.
	if (dot(dp1, dp2perp) < 0.0)
	{
		T = -T;
		B = -B;
	}

	float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
	return mat3(T * invmax, B * invmax, N);
}

// A normal-map texel to a world-space normal through the frame. Z is
// reconstructed from XY, never read -- one path for every normal map. A
// two-channel (BC5) map has no Z to read; for an RGB map whose normals are
// unit length the reconstruction *is* the stored Z, and one that was not unit
// length gets renormalized, which is a correction. A shader that branched on
// the texture's format here would be two shading paths waiting to drift.
vec3 UnpackNormal(mat3 TBN, vec2 xy, float scale)
{
	xy = xy * 2.0 - 1.0;
	vec3 tangentNormal = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
	tangentNormal.xy *= scale;
	return normalize(TBN * tangentNormal);
}

// **From here down is the lit fragment.** The maps it samples, the surface it
// assembles and the shading over it all read interpolated per-instance values
// -- v_Surface, v_BaseColor, v_MaterialIndex -- which a fullscreen pass does
// not have. The tracing above this line does not.
#ifndef RV_TRACE_ONLY

#ifndef RV_LAYERED
vec3 PerturbNormal(mat3 TBN, vec2 uv)
{
	return UnpackNormal(TBN, texture(u_NormalMap, uv).xy, v_Surface.w);
}

// Parallax occlusion: march the view ray through the height field and use the
// texel where the ray actually meets the surface.
//
// This, not the normal map, is what makes a surface read as *deep*. A normal
// map changes how a texel is shaded; parallax changes which texel is there at
// all as the view moves, and that motion cue is most of what the eye calls
// depth on a ground plane seen at an angle. Without it a bumpy floor is a
// photograph of a bumpy floor.
vec2 Parallax(vec2 uv, vec3 viewTS)
{
	float scale = u_Material.HeightScale;
	if (scale <= 0.0)
		return uv;

	// More steps at grazing angles, where the ray crosses more surface. The
	// clamp on viewTS.z stops the offset exploding at the silhouette, where a
	// grazing ray would otherwise sample texels from the far side of the tile.
	float steps = mix(28.0, 10.0, clamp(viewTS.z, 0.0, 1.0));
	float layer = 1.0 / steps;
	vec2 delta = (viewTS.xy / max(viewTS.z, 0.15)) * scale / steps;

	// textureLod, not texture: the loop's trip count varies per fragment, and
	// implicit derivatives inside divergent control flow are undefined.
	vec2 cur = uv;
	float depth = 0.0;
	float h = 1.0 - textureLod(u_HeightMap, cur, 0.0).r;
	float prevH = h;

	while (depth < h && depth < 1.0)
	{
		prevH = h;
		cur -= delta;
		depth += layer;
		h = 1.0 - textureLod(u_HeightMap, cur, 0.0).r;
	}

	// One secant step between the sample above the surface and the one below,
	// which removes the visible layering the raw march leaves on slopes.
	float after = h - depth;
	float before = prevH - (depth - layer);
	float t = clamp(after / (after - before + 1e-5), 0.0, 1.0);
	return cur + delta * t;
}
#endif   // !RV_LAYERED

// What the lighting below is given: the surface at this fragment, with every
// map already read. One struct, filled by one of two SampleSurface bodies --
// the single material every mesh has, or the four layers a terrain chunk has
// (ENGINE-NOTES 7aq) -- and nothing after it knows which. That is the whole
// of the layered fork: the lights, the shadows by map or by ray, the probe,
// the reflection mix and the emissive term run over this struct unchanged.
struct Surface
{
	vec4  BaseColor;
	float Metallic;
	float Roughness;
	float Occlusion;
	float Specular;
	// The shading normal, after any normal map.
	vec3  N;
	vec3  Emissive;

	// **The extended lobes arrive here, not from u_Material.** The lighting
	// below is shared with the layered variant, whose set 1 has no such
	// fields -- reading them directly would fail to compile for terrain. This
	// is the same reason everything else about a surface comes through this
	// struct: nothing after it knows which body filled it.
	//
	// The shading tangent, for the anisotropic lobe. A zero vector means
	// isotropic, which is what the layered path fills.
	vec3  Tangent;
	// x clearcoat weight, y clearcoat roughness, z anisotropy, w subsurface.
	vec4  Coat;
	// rgb sheen colour, a sheen roughness. Black is off.
	vec4  Sheen;
};

#ifndef RV_LAYERED

Surface SampleSurface(vec3 Ngeo, vec3 V)
{
	Surface s;

	// The material's tiling, applied once. Every map has to use the same
	// coordinate or the normals stop lining up with the colour they belong to,
	// which reads as a lighting bug rather than as a UV one.
	vec2 uv = v_TexCoord * u_Material.UvTransform.xy + u_Material.UvTransform.zw;

	mat3 TBN = TangentFrame(Ngeo, v_WorldPos, uv);

	// Parallax before any map is sampled, so colour, normal, roughness and
	// occlusion all agree about which texel is under this pixel.
	if (HasMap(MAP_HEIGHT))
		uv = Parallax(uv, transpose(TBN) * V);

	s.BaseColor = v_BaseColor;
	if (HasMap(MAP_BASE_COLOR))
		s.BaseColor *= texture(u_BaseColorMap, uv);

	s.Metallic  = v_Surface.x;
	s.Roughness = v_Surface.y;
	// Separate greyscale maps, read from red. glTF packs these two into one
	// texture; the importer splits it, so by the time a material exists there
	// is no packed form left to handle.
	if (HasMap(MAP_ROUGHNESS))
		s.Roughness *= texture(u_RoughnessMap, uv).r;
	if (HasMap(MAP_METALLIC))
		s.Metallic *= texture(u_MetallicMap, uv).r;

	s.Occlusion = v_Surface.z;
	if (HasMap(MAP_OCCLUSION))
		s.Occlusion *= texture(u_OcclusionMap, uv).r;

	s.N = Ngeo;
	if (HasMap(MAP_NORMAL))
		s.N = PerturbNormal(TBN, uv);

	// Dielectric reflectance, on the convention everyone uses: F0 = 0.08 *
	// specular, so the default 0.5 is the 4% that was hardcoded here. Metals
	// ignore it entirely -- their F0 *is* their albedo, which is what the mix
	// in main says.
	s.Specular = u_Material.Specular;
	if (HasMap(MAP_SPECULAR))
		s.Specular *= texture(u_SpecularMap, uv).r;

	s.Emissive = v_EmissiveColor.rgb;
	if (HasMap(MAP_EMISSIVE))
		s.Emissive *= texture(u_EmissiveMap, uv).rgb;

	// **Last, and on the assembled surface.** It modulates what every map
	// added up to rather than any one of them, so no later multiply can undo
	// it and a material with no colour map varies exactly as one with.
	ApplyMacro(s.BaseColor.rgb, s.Roughness, s.N, Ngeo, v_WorldPos,
			   u_Material.Macro.x, u_Material.Macro.y);

	// The tangent the anisotropic lobe turns about, and the four lobes
	// themselves. TBN[0] is the surface's own u direction, so a brushed metal's
	// grain follows its uv rather than the world.
	s.Tangent = TBN[0];
	s.Coat = vec4(u_Material.Clearcoat, u_Material.ClearcoatRoughness,
				  u_Material.Anisotropy, u_Material.Subsurface);
	s.Sheen = vec4(u_Material.SheenColor, u_Material.SheenRoughness);

	return s;
}

#else   // RV_LAYERED

// Parallax occlusion for one layer, marched through the blue channel of the
// packed surface map.
//
// **Height rides in blue** because the layered variant binds three samplers a
// layer and cannot grow a fourth, so roughness, occlusion and height share one
// texture (TextureLoader::PackChannels). The march is the same one the
// single-material path runs, reading `.b` instead of a height map's `.r`.
//
// `textureLod`, not `textureGrad`: the trip count varies per fragment, so this
// is divergent control flow and an implicit derivative in it is undefined.
// Level zero, because a mip of a height field is a shallower height field and
// the march would terminate early against it.
vec2 ParallaxLayer(sampler2D surface, vec2 uv, vec3 viewTS, float scale)
{
	if (scale <= 0.0)
		return uv;

	// More steps at grazing angles, where the ray crosses more surface, and
	// the clamp on viewTS.z stops the offset exploding at the silhouette.
	float steps = mix(24.0, 8.0, clamp(viewTS.z, 0.0, 1.0));
	float step = 1.0 / steps;
	vec2 delta = (viewTS.xy / max(viewTS.z, 0.15)) * scale / steps;

	vec2 cur = uv;
	float depth = 0.0;
	float h = 1.0 - textureLod(surface, cur, 0.0).b;
	float prevH = h;

	while (depth < h && depth < 1.0)
	{
		prevH = h;
		cur -= delta;
		depth += step;
		h = 1.0 - textureLod(surface, cur, 0.0).b;
	}

	// One secant step between the sample above the surface and the one below,
	// which removes the layering the raw march leaves on slopes.
	float after = h - depth;
	float before = prevH - (depth - step);
	float t = clamp(after / (after - before + 1e-5), 0.0, 1.0);
	return cur + delta * t;
}

// One layer's contribution, at a constant index so every sampler it names
// is a constant. A macro rather than a function so the bound path's sampler
// arrays are indexed by a literal -- and unrolled by hand, four times, for
// the same reason.
//
// textureGrad, not texture: the body sits under a per-fragment branch on a
// weight read from a texture, and an implicit derivative inside divergent
// control flow is undefined. The coordinate's derivatives are taken once
// outside and scaled by the layer's own tiling here. The tangent frame is
// built per layer, outside the weight branch for the same reason, because
// the frame is a function of the transformed coordinate and a layer's
// tiling may be non-uniform or mirrored; four frames are four derivative
// pairs and cost nothing.
//
// **Two frames per layer, because the wall projection needs its own.** When
// the terrain gained cliffs, the colour and the roughness were given a side
// projection and the normal was not -- it faded to the geometric normal as
// the surface stood up, which discards the normal map outright on anything
// steep. A 400 ft headland came out as smooth clay: correctly projected
// colour, correctly projected roughness, and no relief at all.
//
// Fading it was the safe thing rather than a wrong one. `TBN` is built from
// the planar x/z coordinate, which is degenerate on a vertical face, so
// unpacking through it gives a garbage direction -- and garbage is worse
// than flat. The answer is not to unpack through that frame but to build the
// frame the *wall* coordinate implies and unpack through that, then blend
// two real normals. `TangentFrame` takes the uv it is meant for, so this is
// the same call with the other coordinate.
//
// **One fetch, three maps, and the condition is "any of the three".** The
// surface texture carries roughness, occlusion and height in r, g and b, so
// gating the fetch on MAP_ROUGHNESS alone meant a material with occlusion and
// no roughness map sampled nothing at all. Each channel is applied under its
// own flag, so an absent map leaves its scalar alone rather than multiplying
// it by a neutral.
//
// **And nothing inside the macro may be a // comment.** Every line of it ends
// in a backslash, and a line continuation at the end of a // comment makes the
// next line part of that comment -- which silently deletes the rest of the
// macro, all the way to the one line that carries no backslash. It compiles;
// it just stops shading. That is why all of this prose lives out here.
#define SHADE_LAYER(i)                                                                      \
	{                                                                                       \
		int   flags = u_Layered.MapFlags[i];                                                \
		float w     = weight[i];                                                            \
		vec2 uvL  = v_TexCoord * u_Layered.UvTransform[i].xy + u_Layered.UvTransform[i].zw; \
		vec2 ddxL = ddx * u_Layered.UvTransform[i].xy;                                      \
		vec2 ddyL = ddy * u_Layered.UvTransform[i].xy;                                      \
		vec2 uvW  = wallUv  * u_Layered.UvTransform[i].xy + u_Layered.UvTransform[i].zw;    \
		vec2 ddxW = wallDdx * u_Layered.UvTransform[i].xy;                                  \
		vec2 ddyW = wallDdy * u_Layered.UvTransform[i].xy;                                  \
		mat3 TBN  = TangentFrame(Ngeo, v_WorldPos, uvL);                                    \
		mat3 TBNW = TangentFrame(Ngeo, v_WorldPos, uvW);                                    \
		if (w > 0.0)                                                                        \
		{                                                                                   \
			float pom = (flags & MAP_HEIGHT) != 0                                           \
				? u_Layered.HeightScale[i] * smoothstep(0.5, 0.8, w) : 0.0;                 \
			if (pom > 0.0 && wall < 1.0)                                                    \
				uvL = ParallaxLayer(LAYER_ROUGHNESS(i), uvL,                                \
									normalize(transpose(TBN) * V), pom * (1.0 - wall));     \
			if (pom > 0.0 && wall > 0.0)                                                    \
				uvW = ParallaxLayer(LAYER_ROUGHNESS(i), uvW,                                \
									normalize(transpose(TBNW) * V), pom * wall);            \
			vec4 baseL = u_Layered.BaseColor[i];                                            \
			if ((flags & MAP_BASE_COLOR) != 0)                                              \
			{                                                                               \
				vec4 floorT = textureGrad(LAYER_BASE_COLOR(i), uvL, ddxL, ddyL);            \
				baseL *= wall > 0.0                                                         \
					? mix(floorT, textureGrad(LAYER_BASE_COLOR(i), uvW, ddxW, ddyW), wall)  \
					: floorT;                                                               \
			}                                                                               \
			float roughL = u_Layered.Surface[i].y;                                          \
			float aoL    = u_Layered.Surface[i].z;                                          \
			if ((flags & (MAP_ROUGHNESS | MAP_OCCLUSION | MAP_HEIGHT)) != 0)                \
			{                                                                               \
				vec3 floorRAH = textureGrad(LAYER_ROUGHNESS(i), uvL, ddxL, ddyL).rgb;       \
				vec3 rah = wall > 0.0                                                       \
					? mix(floorRAH,                                                         \
						  textureGrad(LAYER_ROUGHNESS(i), uvW, ddxW, ddyW).rgb, wall)       \
					: floorRAH;                                                             \
				if ((flags & MAP_ROUGHNESS) != 0)                                           \
					roughL *= rah.r;                                                        \
				if ((flags & MAP_OCCLUSION) != 0)                                           \
					aoL *= rah.g;                                                           \
			}                                                                               \
			vec3 nL = Ngeo;                                                                 \
			if ((flags & MAP_NORMAL) != 0)                                                  \
			{                                                                               \
				vec3 nF = UnpackNormal(TBN,                                                 \
						textureGrad(LAYER_NORMAL(i), uvL, ddxL, ddyL).xy,                   \
						u_Layered.Surface[i].w);                                            \
				vec3 nW = UnpackNormal(TBNW,                                                \
						textureGrad(LAYER_NORMAL(i), uvW, ddxW, ddyW).xy,                   \
						u_Layered.Surface[i].w);                                            \
				nL = wall > 0.0 ? normalize(mix(nF, nW, wall)) : nF;                        \
			}                                                                               \
			ApplyMacro(baseL.rgb, roughL, nL, Ngeo, v_WorldPos,                             \
					   u_Layered.MacroScale[i], u_Layered.MacroStrength[i]);                \
			s.BaseColor += w * baseL;                                                       \
			s.Metallic  += w * u_Layered.Surface[i].x;                                      \
			s.Roughness += w * roughL;                                                      \
			s.Occlusion += w * aoL;                                                         \
			s.Specular  += w * u_Layered.Specular[i];                                       \
			s.Emissive  += w * u_Layered.EmissiveColor[i].rgb;                              \
			s.N         += w * nL;                                                          \
		}                                                                                   \
	}


Surface SampleSurface(vec3 Ngeo, vec3 V)
{
	// The paint under this fragment. Inactive layers weigh nothing whatever
	// the map says; the rest are normalised so a half-painted map is not a
	// darker one; and where nothing is painted at all -- a zero sum, which is
	// also every texel of an unpainted terrain -- layer 0 is the surface.
	vec2 wuv = v_TexCoord * u_Layered.WeightUv.xy + u_Layered.WeightUv.zw;
	vec4 weight = texture(u_Weights, wuv);
	for (int i = 0; i < LAYERS; ++i)
		if ((u_Layered.MapFlags[i] & LAYER_ACTIVE) == 0)
			weight[i] = 0.0;
	float sum = weight.x + weight.y + weight.z + weight.w;
	weight = sum > 1e-4 ? weight / sum : vec4(1.0, 0.0, 0.0, 0.0);

	// The derivatives every layer's fetch scales, taken here where control
	// flow is still uniform.
	vec2 ddx = dFdx(v_TexCoord);
	vec2 ddy = dFdy(v_TexCoord);

	// --- the wall projection -------------------------------------------------
	//
	// **A planar uv has nothing to say about a wall.** On terrain the uv is
	// local metres over the texture scale -- a function of x and z alone --
	// so on a face where the height runs and x and z barely move, every map
	// is stretched into vertical stripes. On a 400 ft cliff those stripes
	// *are* the surface, and no amount of painting fixes it: the only answer
	// is to stop projecting from above.
	//
	// So a second projection, from whichever side the face turns towards,
	// blended in as the surface stands up. Two planes rather than the
	// textbook three -- the third is the one the fragment is most nearly
	// parallel to, and it would contribute nothing but a sample.
	//
	// **Flat ground pays nothing.** `wall` is zero for anything under about
	// twenty degrees, which is all of a rolling terrain, and the extra
	// fetches sit behind a branch on it. textureGrad is what makes that
	// branch legal: every derivative is taken here, while control flow is
	// still uniform.
	// **Widened 2026-08-31.** At 0.35/0.78 the blend only reached full side
	// projection past 69.5 degrees, so a 56-degree sea cliff -- which is most of
	// the Marin coast -- ran at wall = 0.54 and kept half of the planar uv's
	// vertical smear. 0.50/0.88 is fully side-projected by 60 degrees and still
	// costs a rolling terrain nothing: anything under 28 degrees is untouched.
	float wall = 1.0 - smoothstep(0.50, 0.88, abs(Ngeo.y));

	// Metres per unit of uv, read off the mapping rather than passed in: for
	// terrain the uv *is* position over the texture scale, so the ratio of
	// the two derivatives is that scale, and the wall then tiles at the same
	// rate as the floor instead of at some unrelated one.
	vec2 dPos = dFdx(v_WorldPos.xz);
	float uvMetres = dot(ddx, ddx) > 1e-12 ? length(dPos) / length(ddx) : 1.0;
	float invMetres = 1.0 / max(uvMetres, 1e-4);

	// Down is +v on both planes, so a cliff's texture runs down it rather
	// than standing on its head on one side of the hill.
	vec2 wallUvX = vec2(v_WorldPos.z, -v_WorldPos.y) * invMetres;
	vec2 wallUvZ = vec2(v_WorldPos.x, -v_WorldPos.y) * invMetres;
	bool faceX   = abs(Ngeo.x) > abs(Ngeo.z);
	vec2 wallUv  = faceX ? wallUvX : wallUvZ;
	vec2 wallDdx = faceX ? dFdx(wallUvX) : dFdx(wallUvZ);
	vec2 wallDdy = faceX ? dFdy(wallUvX) : dFdy(wallUvZ);

	Surface s;
	s.BaseColor = vec4(0.0);
	s.Metallic  = 0.0;
	s.Roughness = 0.0;
	s.Occlusion = 0.0;
	s.Specular  = 0.0;
	s.N         = vec3(0.0);
	s.Emissive  = vec3(0.0);

	SHADE_LAYER(0)
	SHADE_LAYER(1)
	SHADE_LAYER(2)
	SHADE_LAYER(3)

	// The weighted sum of unit normals is not unit; the geometric normal is
	// the answer where every layer's cancels, which cannot happen for
	// weights above zero over normals in one hemisphere but costs nothing to
	// state.
	float length2 = dot(s.N, s.N);
	s.N = length2 > 1e-8 ? s.N * inversesqrt(length2) : Ngeo;

	// **Off for terrain, and that is not a stub.** Ground is not lacquered,
	// fuzzy, brushed or translucent; the four lobes would be four branches
	// never taken on the most pixel-heavy surface in the scene.
	s.Tangent = vec3(0.0);
	s.Coat = vec4(0.0, 0.1, 0.0, 0.0);
	s.Sheen = vec4(0.0, 0.0, 0.0, 0.3);
	return s;
}

#endif   // RV_LAYERED

#if defined(RV_WATER) && (defined(RV_RAY_REFLECTIONS) || defined(RV_RAY_REFRACTION))
#extension GL_KHR_shader_subgroup_basic : require
#extension GL_KHR_shader_subgroup_quad : require

// **WR-18: one ray per 2x2 quad on the water.** The mirror ray and the
// refraction ray were the second and third largest items in the night frame
// (about 15 and 33 ms of 114 at 1440p on Headland), and each is cast per
// pixel from inside this shader because the water's normal -- waves plus the
// detail map -- exists nowhere else, so a half-resolution pass cannot be
// handed it. This is half size in each direction without a pass: one lane
// of each quad traces, and the other three take its radiance through a quad
// broadcast, with the exact normal and none of the precision the screen
// buffer would lose. Under TAA the tracing lane walks the four positions
// frame by frame, so a still quad fills back in over four frames; without a
// temporal filter it holds at lane 0 and the picture stays put. Water only:
// an opaque quad may hold a discarded lane (the thin-member fade), and a
// broadcast from a dead lane is undefined.
uint QuadTraceLane()
{
	if (u_Scene.RayRates.x < 1.5)
		return 4u;   // every lane traces its own
	return any(notEqual(u_Scene.Jitter, vec4(0.0)))
		 ? (uint(u_Scene.GlobalIllumination.y) & 3u)
		 : 0u;
}

bool QuadTraces(uint lane)
{
	return lane == 4u || (gl_SubgroupInvocationID & 3u) == lane;
}

// Every lane of the quad must reach this call: a quad broadcast from inside
// a branch some lanes skipped is undefined.
vec3 QuadShare(vec3 mine, uint lane)
{
	switch (lane)
	{
		case 0u: return subgroupQuadBroadcast(mine, 0u);
		case 1u: return subgroupQuadBroadcast(mine, 1u);
		case 2u: return subgroupQuadBroadcast(mine, 2u);
		case 3u: return subgroupQuadBroadcast(mine, 3u);
		default: return mine;
	}
}
#endif

void main()
{
#ifdef RV_BINDLESS
	// The material, once, before anything reads it. The index is flat and
	// per instance; the record buffer is ordinary memory, so this indexing
	// needs no qualifier -- only the descriptor lookups the record feeds do.
	g_Material = u_Materials.Materials[int(v_MaterialIndex)];
#endif

	// Screen motion, in UV units: where this surface point is now, minus where
	// it was last frame. The halving turns an NDC difference into a
	// texture-coordinate one, which is what a history fetch is addressed in.
	//
	// The jitter comes back out of both terms. Each projection carried the
	// sub-pixel offset of the frame that built it, and a motion vector is
	// meant to say where the *surface* went -- a camera dithered by half a
	// pixel has not moved anything, and leaving that in would send every
	// history fetch half a pixel wrong on a scene standing perfectly still.
	// Zero for every mode but TAA, where both terms are zero and this is the
	// expression it always was.
	//
	// First in main, and unconditionally: a fragment that leaves the velocity
	// attachment unwritten leaves whatever the target's memory held.
	//
	// `thenNDC` outlives the block: where this surface point was last frame
	// is also where last frame's reflection trace has its answer for it.
	vec2 thenNDC;
	{
		vec2 nowNDC = v_ClipPos.xy     / max(abs(v_ClipPos.w), 1e-6)     * sign(v_ClipPos.w);
		thenNDC     = v_PrevClipPos.xy / max(abs(v_PrevClipPos.w), 1e-6) * sign(v_PrevClipPos.w);

#ifndef RV_TRANSPARENT
		// `thenNDC` is computed either way -- the reflection trace below reads
		// it -- but only the opaque variant has an attachment to put it in.
		o_Velocity = ((nowNDC - u_Scene.Jitter.xy) - (thenNDC - u_Scene.Jitter.zw)) * 0.5;
#endif
	}

	vec3 Ngeo = normalize(v_Normal);
	vec3 V = normalize(u_Scene.CameraPosition.xyz - v_WorldPos);

	// The surface, every map read, from whichever body this variant has.
	Surface surface = SampleSurface(Ngeo, V);

#ifdef RV_ALPHA_CUTOUT
	// **The cutout, as early as the alpha is known** -- before the lighting,
	// the shadows and the rays, none of which a discarded fragment should pay
	// for.
	//
	// Behind a define rather than a cutoff of zero on every other material,
	// because the mere *presence* of `discard` in a shader is what makes the
	// hardware fall back from early-z to late-z, whether or not the branch is
	// ever taken. Compiled into the shared opaque shader it would have cost
	// every opaque surface in the frame its early rejection -- and undone the
	// depth prepass, which is worth 27% here. So masked geometry is its own
	// pipeline, drawn from its own bucket, and the opaque shader stays clean.
	//
	// The cutoff is read from the material rather than the define, so one
	// masked pipeline still draws a railing and a leaf with different cutoffs
	// in a single indirect call.
	if (surface.BaseColor.a < u_Material.AlphaCutoff)
		discard;

	// **The thin-member fade** (MaterialParams::Macro.zw). A member thinner
	// than a pixel is rasterised in the frames a sample lands on it and not
	// in the rest, and it blinks under every anti-aliasing mode the moment
	// anything moves -- measured on the bridge's lamp posts, pickets and
	// truss webs from the headland at 23% of the deck band's pixels after
	// every other cause was gone. No resolve can average an input that is
	// on or off, so past the material's FadeEnd the member is simply not
	// drawn, and between FadeStart and FadeEnd it dissolves on a per-pixel
	// dither: interleaved gradient noise, whose error sits in the highest
	// frequencies, walked by the golden ratio per frame only while a
	// temporal filter is running to integrate it (the jitter is the signal,
	// as in the soft shadow). The depth prepass never draws masked geometry,
	// so a discarded fragment leaves no depth behind.
	{
		const float fadeStart = u_Material.Macro.z;
		const float fadeEnd = u_Material.Macro.w;
		if (fadeEnd > fadeStart)
		{
			const float away = length(v_WorldPos - u_Scene.CameraPosition.xyz);
			const float keep = 1.0 - smoothstep(fadeStart, fadeEnd, away);
			float threshold = fract(52.9829189
				* fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
			if (any(notEqual(u_Scene.Jitter, vec4(0.0))))
				threshold = fract(threshold + mod(u_Scene.GlobalIllumination.y, 1024.0) * 0.61803398875);
			if (keep <= threshold)
				discard;
		}
	}
#endif

	vec4 baseColor = surface.BaseColor;
	vec3 albedo = baseColor.rgb;

	float roughness = clamp(surface.Roughness, 0.045, 1.0);   // fully smooth aliases badly

#ifdef RV_WATER
	// **Three states off one number, not two.** The Jacobian of the horizontal
	// displacement says how much the surface is being pulled apart. Below about
	// 0.4 it has folded through itself, which physically *is* a wave breaking --
	// that is foam. Between 0.4 and 1.0 it is stretched but still intact: a wet
	// crest on the point of breaking, and on a real sea under a low sun that
	// band is the brightest thing in frame. Reading only the folded case, which
	// is what a plain foam value does, throws the glint away and leaves the sea
	// looking uniformly matte.
	const float jacobian = v_Water.x;

	// The wind, reconstructed once: the detail normals scroll along it, the
	// foam pattern drifts with it, and the specular lobe stretches along it.
	const float waterTime = v_WaterMisc.x;
	const vec2 waterWind = vec2(cos(v_WaterDeep.w), sin(v_WaterDeep.w));

	// **Detail normals below the geometry floor.** At a 3 m grid the shortest
	// wave the vertices can carry is 12 m, and a real sea's slope lives almost
	// entirely under that -- centimetre chop the footprint-roughness block
	// further down can only average into a wider highlight. Two reads of one
	// tileable map at unrelated scales, scrolled at different speeds along the
	// wind so the pattern never repeats in time, perturb the wave normal in
	// world XZ -- legitimate because a body of water is horizontal, which is
	// also why no TBN is built for it.
	//
	// Faded with distance for the same reason the footprint grows: once the
	// ripple is smaller than a pixel it cannot be drawn, only aliased, and its
	// slope energy is already accounted for in the roughness. Without the fade
	// the horizon shimmers.
	{
		const vec2 detailUv1 = v_WorldPos.xz / 6.1 + waterWind * (waterTime * 0.110);
		const vec2 detailUv2 = v_WorldPos.xz / 2.3 - waterWind * (waterTime * 0.067);
		const vec2 slope1 = texture(u_WaterDetailNormal, detailUv1).xy * 2.0 - 1.0;
		const vec2 slope2 = texture(u_WaterDetailNormal, detailUv2).xy * 2.0 - 1.0;

		const float viewDistance = length(u_Scene.CameraPosition.xyz - v_WorldPos);
		const float detailFade = exp(-viewDistance * 0.010);

		surface.N = normalize(surface.N + vec3(slope1.x + slope2.x * 0.6, 0.0,
											   slope1.y + slope2.y * 0.6)
										  * (0.32 * detailFade));
	}

	// **Foam comes off the accumulation buffer now, not off this instant's
	// wave.** The buffer is injected where the surface folds, decays
	// exponentially, drifts downwind, and its fresh channel ages into a
	// residual one -- so a whitecap leaves a trail that outlives the crest
	// that dropped it, which is most of what separates foam from painted foam.
	// The instantaneous Jacobian still contributes underneath it: the sim grid
	// is metres wide and the fold is sharper than that, so the live term is
	// what keeps the onset crisp while the buffer supplies the memory.
	//
	// The pattern texture is what stops the mask being airbrush: fresh foam is
	// dense sheet whitewater and barely cut by it; residual foam is the lace
	// that is left when the sheet drains through.
	// **How much water the view ray passes through, off the real scene.** The
	// backdrop pass leaves the opaque frame's view depth in metres beside its
	// colour; this surface's own view depth is the clip w. The difference is
	// the thickness of water behind this pixel -- zero at the waterline where
	// a pier breaks the surface, hundreds of metres out in the bay -- and it
	// is what the contact foam below, the colour gradient, and the absorption
	// in the transparent block all measure through. Flags of zero means no
	// backdrop is bound this pass, and everything below falls back to the
	// crest-based forms that predate it.
	// The screen coordinate through this frame's own projection, mapped the
	// way the lit shader already maps last frame's reflection trace: NDC with
	// the y-sign that takes it into the texture's row direction -- the sign
	// rides in the flags, because which way rows run is the backend's fact
	// and the renderer's to state.
	const float waterFlags = v_WaterMisc.w;
	const float waterGradient = max(v_WaterMisc.z, 0.05);
	vec2 waterScreenUV = vec2(0.0);
	float waterThickness = 0.0;
	if (waterFlags != 0.0)
	{
		const vec2 ndcHere = v_ClipPos.xy / max(v_ClipPos.w, 1.0e-4);
		waterScreenUV = vec2(ndcHere.x, ndcHere.y * sign(waterFlags)) * 0.5 + 0.5;
		const float eyeBehind = texture(u_WaterBackdropDepth, waterScreenUV).r;
		waterThickness = max(eyeBehind - v_ClipPos.w, 0.0);
	}

	const vec2 foamAccum = texture(u_WaterFoamBuffer, v_Water.zw).rg;

	// **The foam's features are sized by the waves that make them.** Every
	// read of the lace used to sit at a fixed metre scale, so a 48 m swell
	// dropped 5 m flecks -- foam and sea at unrelated scales is a decal, and
	// it was one of the strongest wrong notes. The dominant wavelength rides
	// in v_WaterMisc.y and every foam read is quoted against it: patches at
	// about a third of a wavelength, macro variation at about a wavelength
	// and a half, and the residual's windrows stretched a wavelength long
	// and a few metres wide -- old foam on a real sea collects into streaks
	// running downwind, and that anisotropy is most of what separates
	// weathered foam from splatter.
	const float waterWaveLength = max(v_WaterMisc.y, 1.0);
	const vec2 windPerp = vec2(-waterWind.y, waterWind.x);
	const float lace1 = texture(u_WaterFoamPattern,
								v_WorldPos.xz / (waterWaveLength * 0.22)
								+ waterWind * (waterTime * 0.02)).r;
	const float lace2 = texture(u_WaterFoamPattern,
								v_WorldPos.xz / (waterWaveLength * 1.6)).r;
	const float foamLace = clamp(lace1 * (0.35 + lace2 * 0.9), 0.0, 1.0);
	const float foamStreak = texture(u_WaterFoamPattern,
									 vec2(dot(v_WorldPos.xz, waterWind)
										  / (waterWaveLength * 1.6),
										  dot(v_WorldPos.xz, windPerp)
										  / (waterWaveLength * 0.22))).r;

	// **Open water is nearly bare.** Measured whitecap coverage at a fresh
	// breeze is one to three percent of the sea, on the largest crests only
	// -- so the live term is gated by where this fragment sits on the swell,
	// and the buffer (whose injection is gated the same way at the source)
	// carries the trails. Soft thresholds still, because a hard cut pops.
	const float foamNow = clamp((0.88 - jacobian) * 2.6, 0.0, 1.0)
						* smoothstep(0.55, 0.85, v_Water.y);
	float foam = clamp(max(foamAccum.r * mix(foamLace, 1.0, 0.5) * 0.5
						 + foamAccum.g * foamLace * foamStreak * 0.35,
						   foamNow * 0.22 * mix(foamLace, 1.0, 0.4)),
					   0.0, 1.0);

	// **Contact foam: where the water meets anything.** The thickness goes
	// to zero against a pier leg, a rock, a shoreline -- and that is where a
	// real sea is white, because every wave that arrives there breaks. Driven
	// by the measured thickness, so it needs no tagging and follows whatever
	// geometry enters the water; the two lace reads at a finer scale keep the
	// edge ragged, and the slow pulse is the surge a standing waterline has.
	// Nothing without the backdrop -- there is no thickness to read.
	if (waterFlags != 0.0)
	{
		// Within about a metre of the contact, not across the whole shallow
		// apron -- the first cut covered every submerged step in sheet white
		// and read as a glow stain (the owner circled it). Lace-dominated,
		// so it is strands clinging to the geometry rather than a wash, and
		// the surge trough goes near zero so it visibly comes and goes.
		const float shallow = 1.0 - smoothstep(0.1, 1.2, waterThickness);
		const float surge = 0.55 + 0.35 * sin(waterTime * 1.1
											  + waterThickness * 3.0);
		const float contactLace = texture(u_WaterFoamPattern,
										  v_WorldPos.xz / (waterWaveLength * 0.08)
										  - waterWind * (waterTime * 0.035)).r;
		// The one place foam is COMMON. The owner's rule, stated 2026-08-31:
		// rare everywhere except where the water touches something.
		foam = clamp(foam + shallow * surge * contactLace * 0.85, 0.0, 1.0);
	}

	foam *= v_WaterShallow.w;
	const float wetness = smoothstep(1.0, 0.45, jacobian) * (1.0 - foam);

	// **Two colours, then foam, in that order.** The gradient is the body of the
	// water and the foam sits on top of it; the other way round tints the
	// whitewater with the sea under it, and foam is not tinted -- it is air, and
	// air is white whatever it floats on.
	//
	// With the backdrop bound the gradient is driven by the *measured* depth --
	// shallow over the pier footing, deep in the channel beside it, which is
	// the owed depth-based colour -- and the crest term becomes a modifier: a
	// crest is thinner water whatever the bottom is doing. Without it, the
	// crest term is all there is, which is what this looked like before.
	//
	// The material's own base colour is deliberately ignored: a body of water
	// has no material to point at, so the colours a scene authors are the
	// component's and they arrive in the push constants.
	if (waterFlags != 0.0)
	{
		const float depthMix = 1.0 - exp(-waterThickness / waterGradient);
		albedo = mix(v_WaterShallow.rgb, v_WaterDeep.rgb, depthMix);
		albedo = mix(albedo, v_WaterShallow.rgb, v_Water.y * 0.2);
	}
	else
	{
		albedo = mix(v_WaterDeep.rgb, v_WaterShallow.rgb, v_Water.y);
	}

	// The wet crest: smoother than the water around it, and *not* lighter. What
	// makes it read is the sharpened reflection, not a change of colour -- a
	// crest painted brighter looks like a crest painted brighter.
	roughness = clamp(roughness * mix(1.0, 0.45, wetness), 0.045, 1.0);

	// Foam is rough, and that is most of what makes it read as foam rather than
	// as white paint: whitewater is a mass of bubbles scattering in every
	// direction, so the one thing it must not do is reflect the sky.
	albedo = mix(albedo, vec3(0.92, 0.95, 0.97), foam);
	roughness = clamp(mix(roughness, 0.72, foam), 0.045, 1.0);

	// **The sun track is a slope problem, not a highlight problem.** The width
	// of the glittering path a light lays across water is set by the *mean
	// square slope* of the surface, and almost all of that slope lives in waves
	// far too small for any grid to carry -- centimetres to decimetres. Measured
	// against Cox and Munk's aerial-photograph fit, geometry at this scale holds
	// under a tenth of a real sea's slope energy; the rest has to come out of
	// the roughness.
	//
	// So roughness grows with how much sea one pixel covers. Close up it stays
	// low and individual crests sparkle; toward the horizon it approaches the
	// full slope variance and the highlight becomes a broad track, which is what
	// a real one is. A single constant roughness cannot be right anywhere except
	// at one distance -- too rough near, too smooth far, and fireflies at the
	// horizon where the waves fall under a pixel.
	{
		const float distance = length(u_Scene.CameraPosition.xyz - v_WorldPos);
		const float grazing = max(abs(dot(surface.N, normalize(
							  u_Scene.CameraPosition.xyz - v_WorldPos))), 0.08);

		// The footprint stretches at grazing angles, which is exactly where the
		// horizon smears -- so it divides rather than being ignored.
		const float footprint = distance * 0.0016 / grazing;

		// Approaches the Cox-Munk variance for a fresh breeze rather than
		// running away: past the horizon there is no more slope energy to add.
		const float residual = 0.24 * (1.0 - exp(-footprint * 0.045));
		roughness = clamp(max(roughness, residual), 0.045, 1.0);
	}

	// **Crests are more opaque than troughs.** Thin water at a crest transmits
	// more, but the froth and aeration in it hide what is behind, and foam hides
	// it completely. Without this the whitewater is see-through, which is the
	// one thing it never is.
	baseColor.a = clamp(baseColor.a + foam * (1.0 - baseColor.a), 0.0, 1.0);
#endif
	float metallic  = clamp(surface.Metallic, 0.0, 1.0);
	float occlusion = surface.Occlusion;
	vec3 N = surface.N;

	// **WR-13: a highlight smaller than a pixel has to be widened, not
	// sampled harder.**
	//
	// The bridge is made of things thinner than a pixel at any distance worth
	// looking at it from -- main cables, suspender ropes, railing pickets,
	// truss webs, lamp standards -- and at night every one of them carries an
	// isolated, high-energy sodium glint. As the camera moves that glint
	// lands inside a pixel or misses it, and the pixel blinks. Measured on
	// the Headland camera before this: **14.17% of the deck-and-cables band
	// blinking over 16 frames**, mean swing 21 levels.
	//
	// **MSAA cannot fix it and it is worth being precise about why.** MSAA
	// takes several samples of *coverage* -- how much of the pixel the cable
	// fills -- and shades **once**. The flicker is in the shading, so more
	// coverage samples average nothing. The owner's own reading was that the
	// anti-aliasing was struggling with detail that small; it is the right
	// instinct about the cause and the wrong half of the pipeline.
	//
	// Tokuyoshi & Kaplanyan, "Improved Geometric Specular Antialiasing"
	// (I3D 2019): read how fast the shading normal turns across this pixel
	// from its screen derivatives, treat that as extra slope variance, and
	// add it to the surface's own. A pixel covering a whole cable's worth of
	// curvature gets a lobe wide enough to hold every direction inside it, so
	// the answer stops depending on where the sample happened to land.
	// KAPPA caps how much can be added, or a silhouette pixel -- where the
	// normal swings through ninety degrees in one step -- would go fully
	// rough and read as a grey smear along every edge.
	//
	// **The conventions have to line up or this does nothing.** The paper's
	// `roughness2` is the squared GGX alpha, and this engine's `roughness` is
	// the perceptual value that `DistributionGGX` squares on the way in. So
	// the filter is applied in alpha-squared and brought back through two
	// square roots, and what comes out is a perceptual roughness the existing
	// call sites can take unchanged.
	//
	// **Analytic lights only.** `o_Surface` keeps the unfiltered value, so
	// SSR and the traced paths are untouched -- they cannot use screen
	// derivatives anyway, and they already carry their own bounds. Water is
	// excluded too: its roughness is read as RMS slope rather than squared,
	// and its footprint block already does this job with a distance term
	// fitted to Cox-Munk. Two mechanisms widening the same lobe would
	// double-count.
#ifdef RV_WATER
	float shadingRoughness = roughness;
#else
	float shadingRoughness = roughness;
	{
		// The pixel filter's variance, 1/(2*pi).
		const float kSigma2 = 0.15915494;
		// The ceiling on what may be added. The paper's value.
		const float kKappa = 0.18;

		const vec3 dndx = dFdx(N);
		const vec3 dndy = dFdy(N);
		const float variance = kSigma2 * (dot(dndx, dndx) + dot(dndy, dndy));
		const float kernel = min(2.0 * variance, kKappa);

		const float alpha = roughness * roughness;
		const float filtered = min(alpha * alpha + kernel, 1.0);
		shadingRoughness = sqrt(sqrt(filtered));
	}
#endif

	// The surface as SSR will see it: the *shading* normal, after the normal
	// map, so a reflection off the brick floor follows the mortar and not a
	// flat plane. Written here, as soon as all three are final, and
	// unconditionally -- an unwritten attachment holds whatever the target
	// last held.
#ifndef RV_TRANSPARENT
	o_Surface = vec4(OctEncode(N), roughness, metallic);
#endif


	// Dielectrics reflect ~4% at normal incidence; metals use their albedo as
	// the reflectance and have no diffuse response at all: F0 = 0.08 *
	// specular for a dielectric, the albedo for a metal.
	vec3 F0 = mix(vec3(0.08 * clamp(surface.Specular, 0.0, 1.0)), albedo, metallic);

#ifdef RV_WATER_SURFACE
	// **The cut.** Everything above is the surface: the wave normal with its
	// detail ripples, the roughness the footprint block widened with distance,
	// the foam, and the colour the depth gradient chose. Everything below is
	// the lighting, which under this variant happens in the pass that reads
	// these two attachments. The wind angle rides along because the water's
	// lobe is anisotropic about it and the reader has no vertex to ask.
	// **The normal itself, not an encoding of it.** A sea's lobe is a few
	// hundredths of a radian wide and it is aimed by this vector; the two
	// horizontal components are enough, because a water normal always points
	// up, and the vertical one comes back exactly from them.
	o_SurfaceWater = vec4(N.xz, shadingRoughness, v_WaterDeep.w);
	o_MaterialWater = vec4(albedo, clamp(surface.Specular, 0.0, 1.0));
	o_PositionWater = vec4(v_WorldPos, 1.0);
	return;
#endif

	vec3 Lo = vec3(0.0);

#ifdef RV_WATER
	// The analytic specular on its own, beside the total. The transparent
	// block replaces the water's *scattered* light with the refracted scene
	// as the transmittance rises -- but the glitter is surface reflection,
	// bounced before the ray ever enters the water, and absorbing it with
	// the body would switch the sun track off exactly where the water goes
	// clear. Summed separately so it can be held out of that mix.
	vec3 waterSpecular = vec3(0.0);
#endif

	// MeshComponent::Static for this object (7cx) -- see v_Instance. A static
	// surface leaves the fully baked lights to the field; a moving one takes
	// every light live.
	const bool surfaceStatic = v_Instance.y > 0.5;
	// How much of this pixel the field answers for (7cx): 0 outside every
	// volume, the edge fade inside one. The fully baked lamps are lit live
	// for the rest -- a beach outside the deck's volume is lit by the lamps
	// as any surface is -- and the two shares sum to one everywhere.
	const float fieldWeight = surfaceStatic && u_Scene.IrradianceExtents.w > 0.0
							? IrradianceFieldWeight(v_WorldPos, N) : 0.0;
#ifdef RV_RAY_SHADOWS
	// What the moving objects' shadows take out of the fully baked light on
	// this static pixel, summed by the loop and applied where the field's own
	// direct light is read, further down.
	vec3 movingLossDiffuse = vec3(0.0);
	vec3 movingLossSpecular = vec3(0.0);
#endif

	// Directional lights first, unconditionally: they have no position, so they
	// reach every cell and binning them would put a copy in all 3456 of them.
	// Everything after them is positional and comes from this fragment's cell.
	int directionalCount = int(u_Scene.ClusterGrid.w);

	uint cell = ClusterIndexFor(v_WorldPos);
	uint cellOffset = u_Cells.Cells[cell].Offset;
	uint cellCount = u_Cells.Cells[cell].Count;
#ifdef RV_LIGHT_CULL
	// WR-16 S2: a static pixel deep inside the field walks the cell's live
	// sublist alone, in the full list's order; the lamps it leaves out are
	// ones it would have read and dropped. The sixteen-byte test below still
	// drops the out-of-range ones inside it.
	const bool cullInsideField = surfaceStatic && fieldWeight >= 1.0;
	if (cullInsideField)
	{
		cellOffset = u_Cells.Cells[cell].LiveOffset;
		cellCount = u_Cells.Cells[cell].LiveCount;
	}
#endif

	int total = directionalCount + int(cellCount);
	// What this fragment walks, whatever it traces: the budget's "lights per
	// pixel", the number the owner's document asks for first.
	RV_COUNT_LIGHTS(total);

#ifdef RV_RAY_SHADOWS
	// **WR-17, share shape only: what this pixel receives unshadowed from
	// every light in its cell**, so a light's share of it can say whether
	// its shadow ray is worth tracing. Diffuse irradiance -- colour times
	// falloff times cosine -- and not the BRDF, which is the expensive half
	// of the loop below; the same falloff and cone arithmetic as that loop,
	// copied rather than shared so the loop's own bits stay exactly what
	// they were. Uniform-branched off under every other shape.
	float directIrradiance = 0.0;
	if (mod(u_Scene.ShadowRayFade.x, 16.0) > 4.5)
	{
		for (int entry = 0; entry < total; ++entry)
		{
			int i = entry < directionalCount
				  ? entry
				  : int(u_CellIndices.Indices[cellOffset + uint(entry - directionalCount)]);
			GpuLight light = u_Lights.Lights[i];

			vec3 L;
			float attenuation = 1.0;
			if (light.Position.w == 0.0)
			{
				L = -light.Direction.xyz;
			}
			else
			{
				vec3 toLight = light.Position.xyz - v_WorldPos;
				float distance2 = dot(toLight, toLight);
				L = toLight * inversesqrt(max(distance2, 1.0e-8));
				float range = max(light.Params.x, 0.0001);
				float t = distance2 / (range * range);
				float ratio = clamp(1.0 - t * t, 0.0, 1.0);
				attenuation = (ratio * ratio) / max(distance2, 0.0001);
				if (light.Params.z < light.Params.y)
				{
					float theta = dot(L, -light.Direction.xyz);
					attenuation *= clamp((theta - light.Params.z)
										 / max(light.Params.y - light.Params.z, 0.0001), 0.0, 1.0);
				}
			}
			directIrradiance += dot(light.Color.rgb, vec3(0.2126, 0.7152, 0.0722))
							  * light.Color.a * attenuation * max(dot(N, L), 0.0);
		}
	}

	// **What a skipped ray borrows.** The first landing counted a skipped
	// light lit, and the Headland diff showed the water under the deck
	// brightening by seventy levels: each far lamp is negligible on its own,
	// but a hundred of them blocked by the same slab are not, and "lit" put
	// their whole sum back. The second landing borrowed the *mean*
	// visibility of every far ray the pixel had traced, and the foreground
	// water went darker instead: a pixel that sees the lamps on its side of
	// the tower and not the ones behind it averages the two groups, and the
	// lit lamps pay for the blocked ones.
	//
	// So the borrow is local. Consecutive light indices are consecutive
	// stations along the deck (the generator writes them in order and the
	// cluster list keeps it), and neighbouring lamps share their occluder --
	// the deck, the tower, the pier -- far more than the row as a whole
	// does. A skipped light takes the visibility of the last thinned light
	// this pixel traced, which the dither's R2 spacing puts within a few
	// stations of it; before any was traced it is lit.
	float lastFarVisible = 1.0;

	// **WR-16 S1, the fixed-budget pre-check: K shadow rays a pixel, to K
	// lamps chosen by importance** (`--shadow-budget=K`, RayRates.w; the
	// owner's Debug Pass C). Instead of one ray per casting lamp, thinned by
	// distance, each pixel keeps K reservoirs; every live positional lamp
	// offers itself to each with a weight -- its unshadowed irradiance, the
	// cheap target S4 will use, not the BRDF -- and the classic single-sample
	// weighted reservoir keeps one lamp per reservoir with probability
	// weight over total. After the loop the K survivors are traced and the
	// pixel's lamp light is the importance-sampling estimate
	//     sum over reservoirs of  term_j * V_j * W / (K * w_j)
	// which is unbiased for the sum over every lamp of term_i * V_i, and
	// exactly as noisy as K rays allow: the raw floor Part IV asks to see
	// before any reuse. The draws are interleaved gradient noise per pixel
	// shifted per reservoir, walked along the golden ratio per lamp index,
	// and along the frame only while a temporal filter exists to integrate
	// it (u_Scene.Jitter, the rule every stochastic term here follows): with
	// no filter the choice is fixed per pixel and the picture holds still.
	// Reservoirs are drawn with replacement -- independent samples -- which
	// keeps the estimator simple and the same lamp may be traced twice.
	//
	// A measurement, not the production sampler: it still shades every lamp
	// to build its terms (the shading lever is S4's), and eight reservoirs
	// of a term each are registers this shader does not have to spare, so
	// its frame time is an upper bound and its picture is the point.
	// RayRates.w carries K in the low four bits and the target's kind above
	// them: 0 the cheap target (unshadowed irradiance, what S4 would afford
	// for a hundred and forty candidates), 16 the full one (the lamp's whole
	// unshadowed term, BRDF and all -- affordable here only because this
	// measurement still shades every lamp). The two arms answer the S4
	// design question the water forces: a glitter lamp's specular is a
	// hundred times its irradiance, and a target that does not know it
	// samples the streak's lamps as if they were dim.
	const int budgetCode = int(u_Scene.RayRates.w + 0.5);
	const int shadowBudget = clamp(budgetCode & 15, 0, 8);
	const bool budgetFullTarget = (budgetCode & 16) != 0;
	uint  budgetIndex[8];
	vec3  budgetTerm[8];
#ifdef RV_WATER
	vec3  budgetSpec[8];
#endif
	float budgetWeight[8];
	uint  budgetSeed[8];
	float budgetTotal = 0.0;
	if (shadowBudget > 0)
	{
		// **Independent draws, hashed per pixel, reservoir and lamp.** The
		// first landing walked one golden-ratio sequence along the lamp
		// index, and the water came out darker the smaller K was: a weighted
		// reservoir is unbiased only when each item's uniform is independent
		// of the last, and consecutive terms of one sequence are not. White
		// noise across lamps is right here; the low-discrepancy set was for
		// the soft shadow's disc points, a different question. Fixed per
		// pixel without a temporal filter, salted by the frame under one.
		const uvec2 px = uvec2(gl_FragCoord.xy);
		const uint frameSalt = any(notEqual(u_Scene.Jitter, vec4(0.0)))
							 ? uint(mod(u_Scene.GlobalIllumination.y, 1024.0)) * 0x85EBCA6Bu : 0u;
		for (int r = 0; r < 8; ++r)
		{
			budgetIndex[r] = 0u;
			budgetTerm[r] = vec3(0.0);
#ifdef RV_WATER
			budgetSpec[r] = vec3(0.0);
#endif
			budgetWeight[r] = 0.0;
			budgetSeed[r] = BudgetHash(px.x ^ (px.y << 16u) ^ (uint(r) << 28u) ^ frameSalt);
		}
	}
#endif

	// **WR-16 S4's sizing** (`--shade-lights=N`, RayRates.w bits 8 and up,
	// N + 1 so zero is off). A measurement with no feature behind it: the
	// pixel walks every lamp's sixteen-byte cull record as it does today --
	// what S4's sampler would pay to score its candidates -- and shades only
	// the first N lamps that pass it. `--casting-lights` cuts the ray and
	// leaves the shading; this cuts the shading, which after S2 is the half
	// the water still pays in full. The lamps past N are simply absent, so
	// the picture is wrong on purpose and the frame time is the bound.
	const int shadeLimit = ((int(u_Scene.RayRates.w + 0.5) >> 8) & 255) - 1;
	int shadedLights = 0;

	// **WR-16 S4, the sampler** (`--light-sampling=K[,target]`, RayRates.w
	// bits 16-19 as K, bits 20-21 as the target's kind). The step S1 sized
	// and this builds: a pixel with more lamps reaching it than a budget can
	// pay for does not shade them all. It scores each candidate cheaply,
	// keeps K of them by weighted reservoir sampling, and shades and traces
	// only those K -- the same unbiased estimate S1 measured (the survivor's
	// term over its probability of having been chosen, averaged over K), so
	// the picture is right in the mean and as noisy as K allows. The reuse
	// and the reconstruction that follow this step are what quiet it.
	//
	// **The target is the whole design question**, which is why it is a dial
	// here and not a decision: S1 measured the cheap irradiance target
	// unusable on the water -- a glitter lamp's specular is about a hundred
	// times its irradiance, so a target blind to the highlight samples the
	// streak's lamps as if they were dim and the tonemapper clips the little
	// it does draw. `term` is the other arm and the one that won: the
	// unshadowed term's luminance, built from the same lobe the shading uses
	// -- see the score below, which took three landings to get right.
	//
	// **Only where it can win**: a live surface -- the sea, and anything that
	// moves -- whose cell list is longer than twice the budget. A static
	// pixel deep in the field already walks the short live sublist S2 gave
	// it, and sampling four of three lamps costs more than shading them. The
	// two measurement flags are exclusive with it: S1's budget still shades
	// every lamp, and the sizing flag shades none past N.
#ifdef RV_RAY_SHADOWS
	const int sampleCount  = clamp((budgetCode >> 16) & 15, 0, 8);
	const int sampleTarget = (budgetCode >> 20) & 3;
#else
	const int sampleCount  = 0;
	const int sampleTarget = 0;
#endif
	uint  sampleIndex[8];
	float sampleScale[8];
	bool  sampled = false;
#ifdef RV_RAY_SHADOWS
	// The field's weight, not the Static flag: what disqualifies a pixel is
	// the bake owing it light, not the mesh being still. The sea is marked
	// Static like everything else the marking pass touched, but no volume
	// covers the bay, so its weight is zero and every lamp reaching it is
	// live -- which is exactly the pixel this sampler is for. A weight above
	// zero also means a lamp may be *subtracted* here rather than added (a
	// moving object inside a baked lamp's range), and a subtraction taken
	// from a sampled estimate would not be the same quantity.
	if (sampleCount > 0 && shadowBudget == 0 && shadeLimit < 0 && fieldWeight <= 0.0
#ifdef RV_WATER
		&& v_WaterLamps.x == 0.0
#endif
		&& int(cellCount) > 2 * sampleCount)
	{
		float sampleWeight[8];
		uint  sampleSeed[8];
		float sampleTotal = 0.0;
		// The rule every stochastic choice in this shader follows: fixed per
		// pixel with no temporal filter to integrate it, walking along the
		// frame under one. Hashed per pixel, reservoir and lamp, because a
		// weighted reservoir is unbiased only when each draw is independent
		// of the last -- the trap S1 paid for with a darker water.
		const uvec2 px = uvec2(gl_FragCoord.xy);
		const uint  salt = any(notEqual(u_Scene.Jitter, vec4(0.0)))
						 ? uint(mod(u_Scene.GlobalIllumination.y, 1024.0)) * 0xC2B2AE35u : 0u;
		for (int r = 0; r < sampleCount; ++r)
		{
			sampleIndex[r] = 0u;
			sampleWeight[r] = 0.0;
			sampleScale[r] = 0.0;
			sampleSeed[r] = BudgetHash(px.x ^ (px.y << 16u) ^ (uint(r) << 28u)
									   ^ salt ^ 0x2545F491u);
		}
		const vec3  kLum = vec3(0.2126, 0.7152, 0.0722);
		const float NdotVs = max(dot(N, V), 1.0e-3);
#ifdef RV_WATER
		// **The water's lobe is not GGX and its roughness is not a roughness**
		// (see WaterBeckmannD): it is the surface's RMS slope, and the lobe is
		// an anisotropic Beckmann in a frame that a sized lamp turns from the
		// wind to the view -- which is what draws the shaft toward the camera
		// instead of a pool along the wind. The first landing of this sampler
		// scored the sea with the GGX distribution and this number, and the
		// two disagree by enough that the lamps carrying the glitter were
		// scored as if dim: Pier came out at 10.7% of the frame over six
		// levels against the fully traced picture, against S1's 1.14% with
		// the true term as the target. So the score uses the same lobe the
		// shading does, with the shadowing term left at one. Everything here
		// is per pixel; only the half vector and the source's angular size
		// change from lamp to lamp.
		const vec3  windDir = vec3(waterWind.x, 0.0, waterWind.y);
		const vec3  windT = normalize(windDir - N * dot(windDir, N));
		const vec3  windB = cross(N, windT);
		vec3  viewT = V - N * dot(V, N);
		const float viewT2 = dot(viewT, viewT);
		const bool  hasStreak = viewT2 > 1.0e-6;
		viewT = hasStreak ? viewT * inversesqrt(max(viewT2, 1.0e-12)) : windT;
		const vec3  viewB = hasStreak ? cross(N, viewT) : windB;
		const float ax0 = max(shadingRoughness * 1.16, 0.02);
		const float ay0 = max(shadingRoughness * 0.86, 0.02);
		// The view's half of the masking: the same in both frames and the
		// same for every lamp, so it is paid once.
		const float g1View = hasStreak
						   ? WaterBeckmannG1X(V, N, viewT, viewB, ax0, ay0)
						   : WaterBeckmannG1(V, N, windT, windB, shadingRoughness);
#endif
		const float f0Lum  = dot(F0, kLum);
		const float albLum = max(dot(albedo, kLum), 1.0e-3);
		const float alpha  = shadingRoughness * shadingRoughness;
		const float alpha2 = max(alpha * alpha, 1.0e-6);
		for (int entry = directionalCount; entry < total; ++entry)
		{
			const int i = int(u_CellIndices.Indices[cellOffset + uint(entry - directionalCount)]);
#ifdef RV_LIGHT_CULL
			if (LightCullRejects(uint(i), v_WorldPos, false, true))
				continue;
#endif
			const GpuLight light = u_Lights.Lights[i];
			if (light.Position.w == 0.0)
				continue;
			// The falloff and the cone exactly as the loop below computes
			// them, so a lamp scores zero only where its term is zero and the
			// estimate stays unbiased.
			const vec3  toLight = light.Position.xyz - v_WorldPos;
			const float distance2 = dot(toLight, toLight);
			const vec3  L = toLight * inversesqrt(max(distance2, 1.0e-8));
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
			const float NdotL = max(dot(N, L), 0.0);
			if (attenuation <= 0.0 || NdotL <= 0.0)
				continue;
			const float lum = dot(light.Color.rgb, kLum) * light.Color.a * attenuation;
			// **What the target has to be, learned by measuring it three
			// times.** Irradiance alone (S1's cheap target) is 11.8% of Pier
			// over six levels; the distribution added, but GGX's and with the
			// water's slope read as a roughness, 10.7%; the water's own
			// anisotropic lobe with its streak frame, 5.5%. What was still
			// missing is the half of the term that varies most from lamp to
			// lamp on a low camera: the Fresnel, which at grazing angles is
			// fifty times its value at normal incidence, and the masking,
			// which cuts the lamps that lie along the surface. With those the
			// score is the unshadowed term's luminance -- what S1 measured as
			// the target that holds -- minus only the coat, the sheen and the
			// sized lamp's closest-point half vector. It is affordable
			// because it is paid for the candidates and not for the shading:
			// no ray, no closest-point solve, no coat, no sheen, and one
			// number instead of three channels.
			const vec3  H = normalize(V + L);
			const float VdotH = max(dot(H, V), 0.0);
			const float fresnel = f0Lum + (1.0 - f0Lum) * pow(1.0 - VdotH, 5.0);
			float target = lum * NdotL * albLum * (1.0 - fresnel) * (1.0 - metallic) / PI;
			if (sampleTarget != 0)
			{
#ifdef RV_WATER
				// The sized-lamp streak, or the wind frame for a point source:
				// the shading's own two branches, with its renormalisation.
				const float angular = light.Direction.w
									* inversesqrt(max(distance2, 1.0e-8));
				float ndf;
				float ndfScale;
				float g;
				if (light.Direction.w > 0.0 && hasStreak)
				{
					const float axS = min(ax0 * 3.0 + 4.0 * angular, 1.0);
					const float ayS = min(ay0 + 0.5 * angular, 1.0);
					ndfScale = sqrt((ax0 * ay0) / (axS * ayS));
					ndf = WaterBeckmannDX(H, N, viewT, viewB, axS, ayS);
					g = g1View * WaterBeckmannG1X(L, N, viewT, viewB, ax0, ay0);
				}
				else
				{
					const float widened = min(shadingRoughness + 0.5 * angular, 1.0);
					ndfScale = (shadingRoughness * shadingRoughness)
							 / max(widened * widened, 1.0e-12);
					ndf = WaterBeckmannD(H, N, windT, windB, widened);
					g = g1View * WaterBeckmannG1(L, N, windT, windB, shadingRoughness);
				}
				target += lum * ndf * ndfScale * g * fresnel * 0.25 / NdotVs;
#else
				const float NdotH = max(dot(N, H), 0.0);
				const float d = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
				target += lum * (alpha2 / (PI * d * d))
						* GeometrySmith(N, V, L, shadingRoughness)
						* fresnel * 0.25 / NdotVs;
#endif
			}
			target = max(target, 1.0e-9);
			sampleTotal += target;
			const float accept = target / sampleTotal;
			for (int r = 0; r < sampleCount; ++r)
			{
				const float draw = float(BudgetHash(sampleSeed[r] ^ (uint(i) * 0x9E3779B9u)) >> 8u)
								 * (1.0 / 16777216.0);
				if (draw < accept)
				{
					sampleIndex[r] = uint(i);
					sampleWeight[r] = target;
				}
			}
		}
		if (sampleTotal > 0.0)
		{
			// One over the probability this lamp had of being chosen, over K
			// samples: the whole weight of the cell divided among the
			// survivors. The loop below multiplies it into `liveShare`, which
			// is its one per-light scale.
			for (int r = 0; r < sampleCount; ++r)
			{
				sampleScale[r] = sampleWeight[r] > 0.0
							   ? sampleTotal / (float(sampleCount) * sampleWeight[r])
							   : 0.0;
			}
			sampled = true;
			total = directionalCount + sampleCount;
		}
	}
#endif

	// `entry`, not `slot`: the loop body already has a `slot`, which is the
	// shadow map this light was given.
	for (int entry = 0; entry < total; ++entry)
	{
#ifdef RV_WATER
		// WR-16 S4b: chosen, shaded and traced in their own passes, and added
		// once below. Nothing positional is walked here at all -- not the
		// eighty-byte record, not the term, not a ray.
		if (v_WaterLamps.x != 0.0 && entry >= directionalCount)
			continue;
#endif
		const bool survivor = sampled && entry >= directionalCount;
		int i = entry < directionalCount
			  ? entry
			  : (sampled ? int(sampleIndex[entry - directionalCount])
						 : int(u_CellIndices.Indices[cellOffset + uint(entry - directionalCount)]));
		// WR-16 S4: a survivor with no weight is a reservoir that never saw a
		// candidate -- there is no light behind it to add.
		if (survivor && sampleScale[entry - directionalCount] <= 0.0)
			continue;

#ifdef RV_LIGHT_CULL
		// WR-16 S2: sixteen bytes decide whether the eighty are read. A
		// directional light is never rejected (its range packs infinity, its
		// class is live). A survivor passed this test when it was scored.
		if (!survivor && LightCullRejects(uint(i), v_WorldPos, cullInsideField, true))
			continue;
#endif
		// The sizing flag's cap (above): directional lights are never capped
		// -- there are one or two of them and they are the sun -- so this
		// counts positional lamps alone, in the cell list's order.
		if (shadeLimit >= 0 && i >= directionalCount)
		{
			if (shadedLights >= shadeLimit)
				continue;
			++shadedLights;
		}
		GpuLight light = u_Lights.Lights[i];

		// **A fully baked light on a static surface is in the field** (7cx),
		// read with the bounce further down, and this loop leaves it alone --
		// unless a moving object stands inside its range (Shadow.y, set per
		// frame by the scene) and the light casts shadows at all. Then the
		// field's answer is right except where that object blocks the lamp,
		// and this iteration runs on to find out where: the term is computed
		// as for any light, the shadow ray is traced against the moving
		// objects alone, and what it finds blocked is *subtracted* below
		// instead of added. Without traced shadows there is no ray to ask and
		// the field's answer stands. A moving surface never enters this
		// block: it is lit live by every light, this one included.
		// The share of this light lit live here: all of it for a moving
		// surface or a light that is not fully baked; for a fully baked light
		// on a static surface, what the field does not cover -- one minus its
		// weight, zero deep inside a volume and one outside every volume.
		float liveShare = 1.0;
		float fieldShare = 0.0;
		bool subtractive = false;
		if (light.Params.w > 1.5 && u_Scene.IrradianceExtents.w > 0.0 && surfaceStatic)
		{
			// The field's share of this light here: how much of it the bake
			// owns at this distance (all of a fully baked lamp, the far part
			// of a hybrid one) times how much of this pixel the field covers.
			fieldShare = fieldWeight * BakedShare(light, v_WorldPos);
			liveShare = 1.0 - fieldShare;
#ifdef RV_RAY_SHADOWS
			subtractive = fieldShare > 0.0 && light.Shadow.y > 0.5 && light.Shadow.x > 0.5;
#endif
			if (liveShare <= 0.0 && !subtractive)
				continue;
		}
		// WR-16 S4: the survivor stands for the lamps that were not chosen --
		// its term over the probability it had of being chosen, averaged over
		// K. `liveShare` is the loop's one per-light scale, so the estimate
		// rides in on it and every consumer below inherits it. A sampled
		// pixel is never static, so this multiplies a one.
		if (survivor)
			liveShare *= sampleScale[entry - directionalCount];

		vec3  lightColor = light.Color.rgb * light.Color.a;
		float isPositional = light.Position.w;

		vec3  L;
		float attenuation = 1.0;

		if (isPositional == 0.0)
		{
			// Directional: L points towards the light, the opposite of travel.
			// Already unit length: Scene.cpp normalises it when the light is
			// gathered, and Renderer3D copies it through untouched. Stated
			// here because it makes that an invariant this shader depends on.
			L = -light.Direction.xyz;
		}
		else
		{
			vec3 toLight = light.Position.xyz - v_WorldPos;
			// Squared, for the reason TraceSurface's copy of this states: no
			// use of the distance here needs its square root.
			float distance2 = dot(toLight, toLight);
			L = toLight * inversesqrt(max(distance2, 1.0e-8));

			// Inverse-square falloff, windowed so the light reaches zero at its
			// range instead of trailing off forever.
			float range = max(light.Params.x, 0.0001);
			float t = distance2 / (range * range);
			float ratio = clamp(1.0 - t * t, 0.0, 1.0);
			attenuation = (ratio * ratio) / max(distance2, 0.0001);

			// Spot cone, when the inner and outer angles differ. Measured
			// against the light's own axis, not against its position -- those
			// only coincide when the light sits at the origin.
			float cosInner = light.Params.y;
			float cosOuter = light.Params.z;
			if (cosOuter < cosInner)
			{
				float theta = dot(L, -light.Direction.xyz);
				attenuation *= clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
			}
		}

		// **A light that reaches zero here reaches zero everywhere below.**
		// Out of range the windowed falloff is exactly zero, and outside a
		// spot's cone the cone factor is; `radiance` is lightColor times this,
		// and `Lo +=` multiplies the whole term by radiance. Under traced
		// shadows that zero was costing a ray into the scene.
		//
		// Clustering is supposed to be what spares this loop, and in this
		// scene it spares it nothing: the benchmark reports twenty lights and
		// a busiest cluster holding twenty. A cluster bounds which lights
		// *might* reach a cell, not which ones do.
		//
		// Bit-identical, by the same argument as the NdotL hoist below.
		if (attenuation <= 0.0)
			continue;

		// **Hoisted above the BRDF and the shadow ray**, because line `Lo +=`
		// below multiplies all of it by this and a back-facing light makes
		// the whole term zero. Reaching that zero used to cost a half-vector
		// normalize, a GGX distribution, a Smith geometry term with two
		// divides, a Fresnel pow, and -- under traced shadows -- an actual
		// ray into the scene.
		//
		// Bit-identical, for the same reason as the one in TraceSurface: the
		// products skipped are the ones that were multiplied by zero.
		float NdotL = max(dot(N, L), 0.0);
		if (NdotL <= 0.0)
			continue;

		vec3 H = normalize(V + L);
		vec3 radiance = lightColor * attenuation;

		// **A light with a radius is a sphere, and only the mirror image knows.**
		//
		// The representative point (Karis 2013): the specular half-vector is
		// built toward the point of the sphere nearest the reflection ray, so
		// the highlight has the source's extent instead of a delta's -- and the
		// lobe is widened by the sphere's angular radius with the energy
		// renormalised, so a bigger lamp spreads its image rather than
		// multiplying it. On water this is the difference between a streak and
		// a blinking speck: a facet no longer needs the exact mirror alignment,
		// only one within the lamp's disc, and at grazing angles the band of
		// facets that qualify runs vertically toward the viewer -- which is the
		// streak every night photograph of a lit shore is made of.
		//
		// Diffuse, cone, range and shadow all keep the true direction L: at
		// lamp radii the diffuse difference is beneath the range window's own
		// cut, so the cost stays in the one term the eye can see.
		//
		// specRoughness/specScale fold to exactly roughness/1.0 when the
		// radius is zero, so every light authored before this shades to the
		// bit it always did.
		// WR-13's filtered value, not the surface's own: everything below --
		// the sized-light widening, the water lobe, the GGX call -- works from
		// here, so the antialiasing lands on every analytic light including
		// the capsule and LTC forms when they arrive.
		float specRoughness = shadingRoughness;
		float specScale = 1.0;
		if (isPositional != 0.0 && light.Direction.w > 0.0)
		{
			const vec3 R = reflect(-V, N);
			const vec3 toCentre = light.Position.xyz - v_WorldPos;
			const vec3 centreToRay = dot(toCentre, R) * R - toCentre;
			const vec3 closest = toCentre + centreToRay *
				clamp(light.Direction.w *
					  inversesqrt(max(dot(centreToRay, centreToRay), 1.0e-8)),
					  0.0, 1.0);
			H = normalize(V + normalize(closest));

			const float angular = light.Direction.w *
								  inversesqrt(max(dot(toCentre, toCentre), 1.0e-8));
#ifdef RV_WATER
			// Water's roughness is the RMS slope itself (see WaterBeckmannD),
			// so the angular radius adds to it directly.
			const float widened = min(specRoughness + 0.5 * angular, 1.0);
			specScale = (specRoughness * specRoughness) / (widened * widened);
			specRoughness = widened;
#else
			// Everything else squares the perceptual value into alpha, so the
			// widening happens there and comes back through the square root.
			const float alpha = specRoughness * specRoughness;
			const float widenedAlpha = min(alpha + 0.5 * angular, 1.0);
			specScale = (alpha / widenedAlpha) * (alpha / widenedAlpha);
			specRoughness = sqrt(widenedAlpha);
#endif
		}

#ifdef RV_WATER
		// The anisotropic Beckmann lobe, in a frame aligned to the wind: the
		// tangent is the wind flattened onto the surface, which is what makes
		// the glitter a streak along it. See the functions for why Beckmann
		// and why the roughness is read as slope.
		vec3 waterT = normalize(vec3(waterWind.x, 0.0, waterWind.y)
								- N * dot(vec3(waterWind.x, 0.0, waterWind.y), N));
		vec3 waterB = cross(N, waterT);

		// **A sized light trades the wind frame for the streak frame.** The
		// reference photographs' shafts run toward the viewer, not along the
		// wind: at grazing incidence the facets that can still mirror the
		// source differ mostly in their toward-viewer tilt, so the lobe is
		// widened along the view direction projected onto the surface and
		// held near the Cox-Munk width across it. Isotropic widening spreads
		// the same energy into a pool; this stretches it into the shaft.
		// Point lights (radius zero) keep the wind frame bit-for-bit.
		float NDF;
		float G;
		if (isPositional != 0.0 && light.Direction.w > 0.0)
		{
			vec3 Tv = V - N * dot(V, N);
			const float tv2 = dot(Tv, Tv);
			// Looking straight down there is no toward-viewer axis and no
			// streak to draw; the wind frame answers as before.
			if (tv2 > 1.0e-6)
			{
				Tv *= inversesqrt(tv2);
				const vec3 Bv = cross(N, Tv);
				const float ax0 = max(specRoughness * 1.16, 0.02);
				const float ay0 = max(specRoughness * 0.86, 0.02);
				const float angular = light.Direction.w *
					inversesqrt(max(dot(light.Position.xyz - v_WorldPos,
										light.Position.xyz - v_WorldPos), 1.0e-8));
				// Along the view: the streak's length. The 3x is the artist
				// term that makes the shaft read at all; the angular term is
				// the physical one that scales it with the source. Across:
				// only the source's own width, so the shaft stays narrow.
				const float axS = min(ax0 * 3.0 + 4.0 * angular, 1.0);
				const float ayS = min(ay0 + 0.5 * angular, 1.0);
				// **Half the renormalisation, on purpose.** The exact factor
				// (ax0*ay0)/(axS*ayS) conserves energy and buries the shaft:
				// three times the length at a third the brightness reads as
				// nothing on a night sea. The square root keeps the streak
				// visibly lit while still paying for most of its spread --
				// the same licence every renderer takes with a sun glitter
				// path, because the alternative is a shaft nobody can see.
				specScale = sqrt((ax0 * ay0) / (axS * ayS));
				NDF = WaterBeckmannDX(H, N, Tv, Bv, axS, ayS);
				// **The masking stays at the surface's own width.** The
				// stretch stands in for the source's size, and a bigger lamp
				// does not make the sea shadow itself more -- widening G with
				// D collapsed G1(V) at exactly the grazing angles the shaft
				// lives at, and the streak died of its own correction.
				G   = WaterBeckmannG1X(V, N, Tv, Bv, ax0, ay0)
					* WaterBeckmannG1X(L, N, Tv, Bv, ax0, ay0);
			}
			else
			{
				NDF = WaterBeckmannD(H, N, waterT, waterB, specRoughness);
				G   = WaterBeckmannG1(V, N, waterT, waterB, specRoughness)
					* WaterBeckmannG1(L, N, waterT, waterB, specRoughness);
			}
		}
		else
		{
			NDF = WaterBeckmannD(H, N, waterT, waterB, specRoughness);
			G   = WaterBeckmannG1(V, N, waterT, waterB, specRoughness)
				* WaterBeckmannG1(L, N, waterT, waterB, specRoughness);
		}
#else
		// **Anisotropic only when asked for, and only with a tangent to turn
		// about.** The layered path fills a zero tangent, so terrain takes the
		// isotropic branch without needing to know the lobe exists.
		float NDF;
		if (abs(surface.Coat.z) > 0.001 && dot(surface.Tangent, surface.Tangent) > 0.0)
		{
			const vec3 T = normalize(surface.Tangent - N * dot(surface.Tangent, N));
			const vec3 B = cross(N, T);
			const float alpha = roughness * roughness;
			// The Disney remap: one dial from -1 to 1 stretching the lobe along
			// the tangent or across it, with the total area kept.
			const float aspect = sqrt(1.0 - abs(surface.Coat.z) * 0.9);
			float ax = max(alpha / aspect, 0.001);
			float ay = max(alpha * aspect, 0.001);
			if (surface.Coat.z < 0.0)
			{
				const float swap = ax;
				ax = ay;
				ay = swap;
			}
			NDF = DistributionGGXAniso(max(dot(N, H), 0.0), dot(T, H), dot(B, H), ax, ay);
		}
		else
		{
			NDF = DistributionGGX(N, H, specRoughness);
		}
		float G   = GeometrySmith(N, V, L, specRoughness);
#endif
		vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

		vec3 numerator = NDF * G * F * specScale;
		float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
		vec3 specular = numerator / denominator;

		// Energy conservation: what is not reflected is refracted, and metals
		// refract nothing.
		vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

		// **The clearcoat takes its share before the base gets any.** A coat is
		// a second interface *above* the surface, so what it reflects never
		// reaches what is underneath -- attenuating the base by the coat's
		// Fresnel is what keeps the two summing to no more than arrived. Adding
		// a coat on top without that is how a lacquered surface ends up
		// brighter than the light falling on it.
		if (surface.Coat.x > 0.0)
		{
			const float VdotH = max(dot(V, H), 0.0);
			const float coatRoughness = clamp(surface.Coat.y, 0.045, 1.0);
			const float Dc = DistributionGGX(N, H, coatRoughness);
			const float Vc = VisibilityKelemen(VdotH);
			// A coat is a dielectric film: 4%, always, whatever is beneath it.
			const float Fc = (0.04 + 0.96 * pow(1.0 - VdotH, 5.0)) * surface.Coat.x;

			specular = specular * (1.0 - Fc) + vec3(Dc * Vc * Fc);
			kD *= (1.0 - Fc);
		}

		// Sheen sits on top of both and is not attenuated by them: the fibres
		// that produce it stand above the surface rather than under a coat.
		if (dot(surface.Sheen.rgb, surface.Sheen.rgb) > 0.0)
		{
			const float Ds = DistributionCharlie(max(dot(N, H), 0.0),
												 clamp(surface.Sheen.a, 0.07, 1.0));
			const float Vs = VisibilityAshikhmin(max(dot(N, V), 0.0), NdotL);
			specular += surface.Sheen.rgb * Ds * Vs;
		}

		// Each light carries which kind of shadow it has, if any: under maps
		// which map, under rays whether the ray goes to infinity or to the
		// light (ENGINE-NOTES 7an).
		int kind = int(light.Shadow.x);
		int slot = int(light.Shadow.y);

		float shadow = 1.0;
#ifdef RV_RAY_SHADOWS
		// The moving objects alone, for the field's share (7cx): the static
		// ones are already in the field's shadow. No thinning and no
		// borrowing -- a ray toward a lamp with a car under it is the one ray
		// this pass exists for, and its answer is about that car, not about
		// the deck the far lamps share. A directional light reaches to
		// infinity as ever. The live share takes the ordinary ray below like
		// any light; both are traced only in a volume's edge band with a
		// moving object at hand.
		float shadowMoving = 1.0;
		if (subtractive)
		{
			shadowMoving = kind == 1
						 ? TraceShadowFromMasked(v_WorldPos, normalize(v_Normal), L, 1.0e4,
												 RV_RAY_MASK_MOVING)
						 : TraceShadowSoftFromMasked(v_WorldPos, normalize(v_Normal), L,
													 length(light.Position.xyz - v_WorldPos),
													 light.Direction.w, uint(i), RV_RAY_MASK_MOVING);
		}
		// WR-16 S1: under the fixed budget a live positional lamp is not traced
		// here; it offers itself to the reservoirs where its term is known,
		// below, and contributes nothing to Lo directly.
		bool budgeted = false;
		if (liveShare > 0.0 && kind == 1)
		{
			shadow = TraceShadow(v_WorldPos, L, 1.0e4);
		}
		else if (liveShare > 0.0 && kind != 0 && shadowBudget > 0)
		{
			budgeted = true;
		}
		else if (liveShare > 0.0 && kind != 0 && survivor)
		{
			// WR-16 S4: a survivor is traced, always. Choosing which lamps
			// deserve a ray is exactly what the thinning below was for, and
			// the sampler has just done it per pixel and by importance rather
			// than by distance.
			shadow = TraceShadowSoft(v_WorldPos, L,
									 length(light.Position.xyz - v_WorldPos),
									 light.Direction.w, uint(i));
		}
		else if (liveShare > 0.0 && kind != 0)
		{
			// WR-17: a far light's ray is traced by a fraction of the pixels
			// that decreases with its distance (see ShadowRaySkip); a pixel
			// that skips counts the light lit. Under the share shape the
			// fraction is the light's share of the pixel's direct light.
			const float distance = length(light.Position.xyz - v_WorldPos);
			const float share = directIrradiance > 0.0
				? dot(lightColor, vec3(0.2126, 0.7152, 0.0722)) * attenuation * NdotL
				  / directIrradiance
				: 1.0;
			const float skip = ShadowRaySkip(distance, share);
			if (ShadowRayKept(skip, uint(i)))
			{
				// WR-15: aimed at a point on the source rather than at its
				// centre, so a dense array of lamps casts a penumbra instead
				// of a picket fence. `light.Direction.w` is SourceRadius;
				// zero traces the ray this line always traced.
				shadow = TraceShadowSoft(v_WorldPos, L, distance,
										 light.Direction.w, uint(i));
				// Only the thinned lights lend: a near lamp's shadow is its
				// own picket, not the deck the far ones share.
				if (skip > 0.0)
					lastFarVisible = shadow;
			}
			else if (!ShadowRaySkippedLit())
			{
				shadow = lastFarVisible;
			}
		}
#else
		if (kind == 1)
			shadow = ShadowFactor(v_WorldPos, N, L);
		else if (kind == 2)
			shadow = SpotShadow(slot, light.Position.xyz,
								light.Shadow.w, v_WorldPos, N, L);
		else if (kind == 3)
			shadow = PointShadow(slot, light.Position.xyz,
								 light.Shadow.z, light.Shadow.w,
								 v_WorldPos, N);
#endif

		// **The wrap applies to the diffuse and not the specular.** Light that
		// entered the surface and came back out has forgotten which way it
		// arrived; light that bounced off the surface has not. Wrapping both
		// would put a highlight on the dark side of a face.
		const float diffuseCosine = surface.Coat.w > 0.0
			? WrapDiffuse(dot(N, L), surface.Coat.w) : NdotL;

#ifdef RV_RAY_SHADOWS
		if (subtractive)
		{
			// What the moving object takes away from the field's share of a
			// lamp the field has already paid in full. Held apart from Lo and
			// clamped against the field's own direct light once that is
			// read, further down.
			movingLossDiffuse += kD * albedo / PI * diffuseCosine * radiance
							   * (1.0 - shadowMoving) * fieldShare;
			movingLossSpecular += specular * NdotL * radiance * (1.0 - shadowMoving) * fieldShare;
		}
#endif
#ifdef RV_RAY_SHADOWS
		if (budgeted)
		{
			// The lamp's whole term, held for the reservoirs; its weight is
			// the cheap target -- luminance of the unshadowed irradiance --
			// so a bright near lamp is chosen often and a far dim one
			// rarely, and the estimate divides by that weight to stay fair.
			const vec3 term = (kD * albedo / PI * diffuseCosine + specular * NdotL)
							* radiance * liveShare;
			const vec3 lumWeights = vec3(0.2126, 0.7152, 0.0722);
			const float weight = max(budgetFullTarget ? dot(term, lumWeights)
													  : dot(radiance, lumWeights) * NdotL, 1.0e-6);
			budgetTotal += weight;
			const float accept = weight / budgetTotal;
			for (int r = 0; r < shadowBudget; ++r)
			{
				const float draw = float(BudgetHash(budgetSeed[r] ^ (uint(i) * 0x9E3779B9u)) >> 8u)
								 * (1.0 / 16777216.0);
				if (draw < accept)
				{
					budgetIndex[r] = uint(i);
					budgetTerm[r] = term;
#ifdef RV_WATER
					budgetSpec[r] = specular * radiance * NdotL;
#endif
					budgetWeight[r] = weight;
				}
			}
		}
		else
#endif
		{
		// `liveShare` is exactly 1.0 for every light that is not a fully baked
		// lamp on a static surface, so those shade to the bit they always did.
		Lo += (kD * albedo / PI * diffuseCosine + specular * NdotL) * radiance * shadow * liveShare;
#ifdef RV_WATER
		// `liveShare` here too (WR-16 S4). It is exactly one for the water
		// today -- the sea is never a static surface -- so this changes no
		// pixel; under the sampler it carries the survivor's estimate, and
		// without it the glitter would be the light of four lamps instead of
		// the light of all of them.
		waterSpecular += specular * radiance * NdotL * shadow * liveShare;
#endif
		}
	}

#ifdef RV_WATER
	// **The lamp light from its own passes** (WR-16 S4b), added exactly where
	// this loop's own would have gone: the whole of it into Lo, and the
	// glinting half into the water's specular sum as well, because that sum
	// is what the transmission below holds out of the sea's absorption.
	if (v_WaterLamps.x != 0.0)
	{
		const vec2 lampUv = gl_FragCoord.xy / vec2(textureSize(u_WaterLampDiffuse, 0));
		const vec3 lampDiffuse = textureLod(u_WaterLampDiffuse, lampUv, 0.0).rgb;
		const vec3 lampSpecular = textureLod(u_WaterLampSpecular, lampUv, 0.0).rgb;
		Lo += lampDiffuse + lampSpecular;
		waterSpecular += lampSpecular;
	}
#endif

#ifdef RV_RAY_SHADOWS
	// WR-16 S1: the K survivors are traced, and the lamps' light is the
	// importance-sampling estimate -- each survivor's term scaled by the
	// total weight over K times its own, which is one over its probability
	// of having been chosen, over K samples.
	if (shadowBudget > 0 && budgetTotal > 0.0)
	{
		for (int r = 0; r < shadowBudget; ++r)
		{
			if (budgetWeight[r] <= 0.0)
				continue;
			const GpuLight chosen = u_Lights.Lights[budgetIndex[r]];
			const vec3 toLight = chosen.Position.xyz - v_WorldPos;
			const float distance = length(toLight);
			const vec3 Lc = toLight / max(distance, 1.0e-6);
			const float visible = TraceShadowSoft(v_WorldPos, Lc, distance, chosen.Direction.w,
												  budgetIndex[r]);
			const float scale = visible * budgetTotal / (float(shadowBudget) * budgetWeight[r]);
			Lo += budgetTerm[r] * scale;
#ifdef RV_WATER
			waterSpecular += budgetSpec[r] * scale;
#endif
		}
	}
#endif

	// A constant environment standing in for IBL: irradiance is the same from
	// every direction, so one colour serves both the diffuse and specular
	// response. Still an approximation -- it cannot vary with view angle or
	// roughness the way a real environment does -- but it is driven by the
	// scene now rather than by two constants buried in this file, and it is
	// what IBL will fall back to when a scene has no environment map.
	// Diffuse image-based lighting: how much light actually arrives at a
	// surface facing this way, rather than one number for every direction.
	//
	// The flat ambient is still added, and is now a floor rather than the whole
	// answer -- it is what a scene with no sky at all is lit by, and what keeps
	// the inside of an unlit room from being pure black. The irradiance cube
	// carries the part that varies: the sky side of an object is lit by sky and
	// the ground side by ground, which is most of what makes a scene look like
	// it is somewhere.
	vec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;

	// **The stored field is read further down**, with the bounce it belongs
	// beside, and not here. It holds bounced light -- the same quantity
	// u_Indirect holds -- so adding it to the flat ambient would put the same
	// light in the picture twice over: measured on the GI corner, once as
	// +5.5 levels of red where a true second bounce is worth +2.2.

	// **Blended between the probes that cover this fragment**, rather than
	// taken wholly from the one the CPU chose for the object. v_Instance.x is still
	// what the emitter list and the traced bounce agree on for the object as a
	// whole; here the surface asks for itself. Whatever the probes do not
	// claim is the sky's, which is slot zero -- so the two fetches below are
	// the whole answer and there is never a third.
	float probeA, probeB, probeWa, probeWb;
	ProbeBlendAt(v_WorldPos, probeA, probeB, probeWa, probeWb);
	const float probeSky = max(1.0 - probeWa - probeWb, 0.0);

	const vec3 skyDirection = RotateIntoSky(N);
	// **The sky half, kept in its own name.** What follows adds *bounced*
	// light, and the two have to stay separable to the end: sky occlusion
	// scales this and must not touch that. The field's bounce already carries
	// its own occlusion -- it is what the cell measured -- so darkening it
	// here would count the same shadowing twice.
	const vec3 skyDiffuse =
		  (textureLod(u_Irradiance, vec4(skyDirection, probeA), 0.0).rgb * probeWa
			 + textureLod(u_Irradiance, vec4(skyDirection, probeB), 0.0).rgb * probeWb
			 + textureLod(u_Irradiance, vec4(skyDirection, 0.0), 0.0).rgb * probeSky) *
			   u_Scene.Environment.x;

	// Bounced light only, from here down.
	vec3 irradiance = vec3(0.0);

#ifdef RV_RAY_GI
	// **The bounce is traced in a pass of its own now** (ENGINE-NOTES 7bs).
	//
	// It used to be cast here: four cosine directions per shaded fragment,
	// which is four rays for every pixel of a full-resolution frame because an
	// attachment has only one size. Indirect light is the lowest-frequency
	// thing in the picture and was the only term paying full rate for itself --
	// ambient occlusion has done the same class of tracing at half resolution
	// since it existed. rtgi_trace.rvshader reconstructs the same position and
	// normal from this pass's depth and surface attachments and calls the same
	// TraceSurface, at whatever resolution the quality dial asks for.
	//
	// Zero rather than left unwritten: an attachment a pass declares and does
	// not write holds whatever the last frame left in it, and this one is read
	// by nothing now -- but "read by nothing" is a property of today's graph,
	// and undefined memory is a property of forever.
#ifndef RV_TRANSPARENT
	o_Indirect = vec4(0.0, 0.0, 0.0, 1.0);
#endif
#endif

	// Last frame's indirect diffuse (ENGINE-NOTES 7av), added to the probe's
	// irradiance so the diffuse term below multiplies it by *this* surface's
	// albedo. That multiply is the whole restructure: a post pass had to stand
	// the lit pixel in for an albedo it could not know, and here albedo is the
	// local variable two lines down.
	//
	// Reprojected through the same previous-frame NDC the reflections use --
	// the buffer was written at the end of last frame. Off the edge means
	// nothing was gathered for this point and the probe answers alone, which
	// is what clamp-to-edge would have got wrong by handing back a neighbour's
	// bounce.
	// Kept as well as added, because the screen-space gather has to subtract
	// it back off (ENGINE-NOTES 7ay): it reads this image, and an image that
	// already contains indirect light feeds a gather its own answer.
	vec3 indirectTerm = vec3(0.0);
	// How much of the bounce the screen answered for. One where the gather has
	// a confident sample for this fragment, zero where it has none at all --
	// off the edge of last frame's frame, freshly disoccluded, or the feature
	// off entirely.
	float bounceAnswered = 0.0;
	if (u_Scene.Indirect.x > 0.0)
	{
		vec2 previousIndirectNDC = thenNDC - u_Scene.Jitter.zw;
		vec2 indirectUV = vec2(previousIndirectNDC.x,
							   previousIndirectNDC.y * u_Scene.Indirect.y) * 0.5 + 0.5;

		if (all(greaterThanEqual(indirectUV, vec2(0.0))) &&
			all(lessThanEqual(indirectUV, vec2(1.0))))
		{
			vec4 bounced = texture(u_Indirect, indirectUV);
			indirectTerm = max(bounced.rgb, vec3(0.0)) * bounced.a * u_Scene.Indirect.x;
			bounceAnswered = clamp(bounced.a, 0.0, 1.0);
			irradiance += indirectTerm;
		}
	}

	// **And the field answers for the rest of it.**
	//
	// The two carry the same quantity -- bounced light arriving here -- so they
	// are alternatives and never a sum: adding both counts one bounce twice,
	// which is the whole of what made a field brighten a scene past where a
	// second traced bounce puts it. The screen-space answer is per pixel and
	// sharper wherever it exists, so it wins where it is confident; the field
	// is what a fragment gets where it does not, which is exactly the case the
	// bounce has never had an answer for -- a surface that was off screen last
	// frame, or one a moving object just uncovered.
	//
	// It costs nothing to say so: with the gather confident this multiplies by
	// zero, and with no field the fetch returns false.
	// **Asked for even where the bounce is not.** The field answers two
	// questions and only one of them depends on how confident the gather is:
	// the stored bounce is taken only where the gather fell short, but the
	// sky fraction applies to every fragment over a volume however its bounce
	// was found.
	float skyVisible = 1.0;
	// **The bounce only where the gather fell short; the fully baked lights'
	// direct light always.** The field used to be read only as the bounce's
	// fallback, weighted by one minus the gather's confidence -- right for
	// bounce light, and it threw a fully baked lamp's direct light away
	// with it wherever the gather was confident, which is nearly everywhere:
	// the showroom read 0.68 of its live twin, walls at half. The direct
	// light has its own coefficients now and its own, unweighted, read.
	vec3 bakedDirection = vec3(0.0);
	vec3 bakedCoherent = vec3(0.0);
	// The fully baked lights' stored direct light, kept in its own name: the
	// moving objects' shadows below are clamped against it.
	vec3 storedDirect = vec3(0.0);
	if (bounceAnswered < 1.0 || u_Scene.IrradianceExtents.x > 0.5)
	{
		vec3 storedBounce;
		float fieldSky;
		if (VolumeIrradiance(v_WorldPos, N, true, storedBounce, fieldSky, storedDirect,
							 bakedDirection, bakedCoherent))
		{
			if (bounceAnswered < 1.0)
			{
				irradiance += storedBounce * (1.0 - bounceAnswered);
				skyVisible = fieldSky;
			}
			// **Static surfaces only** (7cx): a moving object walked the fully
			// baked lights live in the loop above, and reading their stored
			// light as well would count each of them twice.
			if (surfaceStatic)
				irradiance += storedDirect;
			else
				storedDirect = vec3(0.0);
		}
		else
		{
			storedDirect = vec3(0.0);
		}
	}

	vec3 bakedHighlight = vec3(0.0);
#ifndef RV_WATER
	// **The fully baked lamps' highlight**, from the dominant lamp the field
	// recovers (DerivedLamp): one virtual light with that direction and
	// that coherent irradiance, through the same lobe a live lamp gets.
	// Diffuse from those lamps arrived through `irradiance` above; this is
	// the specular half the field could not carry. Not on water, whose
	// lamps stay live for the streak -- and not on a moving surface, whose
	// lamps were live in the loop (7cx).
	if (surfaceStatic && u_Scene.IrradianceExtents.x > 0.5
		&& dot(bakedCoherent, bakedCoherent) > 0.0)
	{
		const float NdotLb = max(dot(N, bakedDirection), 0.0);
		if (NdotLb > 0.0)
		{
			const vec3 Hb = normalize(V + bakedDirection);
			const float Db = DistributionGGX(N, Hb, shadingRoughness);
			const float Gb = GeometrySmith(N, V, bakedDirection, shadingRoughness);
			const vec3 Fb = FresnelSchlick(max(dot(Hb, V), 0.0), F0);
			bakedHighlight = (Db * Gb * Fb / (4.0 * max(dot(N, V), 0.0) * NdotLb + 0.0001))
						   * bakedCoherent * NdotLb;
			Lo += bakedHighlight;
		}
	}
#endif

#ifdef RV_RAY_SKY
	// **Traced, and it REPLACES the volume's number rather than joining it.**
	// Both measure the same thing -- the fraction of sky this point can see --
	// so multiplying them would darken twice for one occlusion. The traced one
	// wins because it is the one that is right outside a volume, right for
	// geometry that moved, and right per pixel instead of per cell.
	//
	// Deliberately not applied to `irradiance`: bounced light already arrived
	// having been occluded on its way in, and the comment below this is the
	// standing warning about exactly that double-count.
	// 1.0e4 is this shader's standing "as far as anything goes" -- the same
	// tMax the directional shadow ray uses, and the right one here: a tower
	// blocks the sky from a kilometre away, so this is not an AO radius.
	skyVisible = TraceSkyVisibility(v_WorldPos, normalize(v_Normal), N, 1.0e4);
#endif

	float NdotV = max(dot(N, V), 0.0);
	vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	// **Sky occlusion, on the sky half only.**
	//
	// `ambientLight` is the flat floor a scene with no sky is lit by and
	// `skyDiffuse` is the probe's answer for this normal: both are sky, and
	// both are what a recess under an eave should lose. `irradiance` is
	// bounced light, which already arrived having been occluded on the way,
	// and darkening it again is the double-count this is written to avoid.
	//
	// A multiply, not a subtraction, so a fully enclosed cell keeps exactly
	// its bounced light and nothing else, and an open one is bit-identical to
	// what it was before any of this existed.
	vec3 ambient = kD * albedo *
		   ((ambientLight + skyDiffuse) * skyVisible + irradiance) * occlusion;

#ifdef RV_RAY_SHADOWS
	// **The moving objects' shadows, taken out of the fully baked light**
	// (7cx). The loop traced each fully baked lamp with a moving object in
	// its range against those objects alone and summed the light they block;
	// here that light leaves the picture. Clamped to what the field put in --
	// the field is a cell's average and a pixel's live term can exceed it --
	// so a shadow can darken to the floor's unlit colour and never below.
	if (surfaceStatic)
	{
		ambient -= min(movingLossDiffuse, kD * albedo * storedDirect * occlusion);
		Lo -= min(movingLossSpecular, bakedHighlight);
	}
#endif

#ifndef RV_RAY_GI
	// **What this pixel's colour owes to last frame's indirect light**
	// (ENGINE-NOTES 7ay). Every factor is the one the line above applied, so
	// the screen-space gather's subtraction is exact rather than an estimate:
	// what it sees after taking this off is the directly lit image.
	//
	// The traced form writes its own estimate here instead, and the two forms
	// are mutually exclusive -- 7at's claim 5 is the check that keeps them so.
	// Every other shader that draws into this target writes zero here, which
	// is right: none of them received indirect light.
#ifndef RV_TRANSPARENT
	o_Indirect = vec4(kD * albedo * indirectTerm * occlusion, 1.0);
#endif
#endif

	// The environment, reflected. Roughness selects a mip rather than driving
	// a real GGX convolution -- the chain is box filtered, so this is an
	// approximation of the lobe rather than the lobe -- but it is monotonic in
	// roughness, which is what makes a rough metal read as rough.
	//
	// This is the specular half of image-based lighting. The diffuse half is
	// still the flat ambient above; 3.4 replaces it with real irradiance.
	vec3 reflection = RotateIntoSky(reflect(-V, N));

	// Roughness alone is not enough to pick a mip.
	//
	// Near a silhouette the reflection vector sweeps a large part of the
	// environment across a handful of pixels, so a smooth surface samples a
	// sharp mip at points that are far apart in the cube. Anything small and
	// bright in there -- a sun -- then lands in scattered single pixels, and
	// the bloom pass turns each one into a blob that appears to float in the
	// air beside the object. It is the same aliasing hardware mip selection
	// exists to prevent for ordinary textures, and the fix is the same
	// argument: measure how fast the coordinate changes across the screen.
	//
	// A cube face spans 90 degrees across its width, so the rate of change of
	// a unit direction converts to texels with 2/pi and the face size.
	vec3 dRdx = dFdx(reflection);
	vec3 dRdy = dFdy(reflection);
	float faceSize = max(u_Scene.EnvironmentSize.x, 1.0);
	float texelsPerPixel = max(length(dRdx), length(dRdy)) * (2.0 / PI) * faceSize;

	// The coarser of the two: what roughness asks for, and what the screen
	// demands. Taking the maximum means a mirror still looks like a mirror
	// everywhere it is not aliasing.
	//
	// The roughness term indexes a real GGX convolution now rather than a box
	// filtered chain, so a level means a lobe width rather than "blurrier than
	// the last one".
	float lod = max(roughness * u_Scene.Environment.y,
					log2(max(texelsPerPixel, 1.0)));

	// The same blend, and each slot's own parallax: a cube is only correct at
	// the point it was captured, so the reflected ray is carried out to where
	// it leaves that probe's sphere and looked up from there. Without it a
	// wall's reflection slides with the camera instead of staying on the wall.
	vec3 prefiltered = (textureLod(u_Environment,
								   vec4(ProbeParallax(v_WorldPos, reflection, probeA), probeA),
								   lod).rgb * probeWa
					  + textureLod(u_Environment,
								   vec4(ProbeParallax(v_WorldPos, reflection, probeB), probeB),
								   lod).rgb * probeWb
					  + textureLod(u_Environment, vec4(reflection, 0.0), lod).rgb * probeSky) *
					   u_Scene.Environment.x;

	// Screen-space reflections replace the probe *here* -- the radiance the
	// probe says is in the reflected direction, swapped for what last
	// frame's trace found there, wherever it found something. Everything
	// after this line -- the split-sum weight, the occlusion -- applies to
	// both alike, which is what makes the replacement exact instead of a
	// post-pass guess at how much of the pixel the probe was.
	//
	// Last frame's, and read at where this surface point *was* last frame,
	// through the same previous clip position the motion vector is built
	// from. The trace is written at the end of a frame from that frame's
	// depth and image; the lighting that reads it is the next frame's. One
	// frame late, reprojected. ENGINE-NOTES 7af says what that costs and
	// why it is the honest place.
	//
	// The row sign: thenNDC's y is up the picture, the trace's rows run up
	// on one backend and down on the other -- the same fact taa_resolve
	// states for the velocity, carried in as a uniform rather than
	// re-derived per shader.
#ifdef RV_RAY_REFLECTIONS
	// The traced form (7ao): the mirror ray from this surface, this frame,
	// weighted in where the surface is glossy enough for a mirror ray to be
	// the answer and out where the probe's blur is what many jittered rays
	// would converge to. The SSR passes do not run when this is compiled
	// in, so the block below is dead by its own gate.
	{
		// The window comes from the quality level (7bt) rather than being a
		// constant here: it is the only axis a level can move while the ray
		// is cast inside this shader. Zero would mean nothing traces, which
		// no level asks for, so it also stands in for "nobody set it".
		vec2 gloss = u_Scene.ScreenReflections.zw;
		if (gloss.y <= 0.0)
			gloss = vec2(0.25, 0.6);
		float mirror = 1.0 - smoothstep(gloss.x, gloss.y, roughness);
#ifdef RV_WATER
		// WR-18: one lane of the quad traces, all four share (see QuadShare).
		// The share runs for every lane, outside the mirror test, because a
		// quad op inside a branch some lanes skip is undefined.
		const uint quadLane = QuadTraceLane();
		vec3 quadTraced = vec3(0.0);
		if (mirror > 0.0 && QuadTraces(quadLane))
			quadTraced = TraceReflection(v_WorldPos, normalize(v_Normal), reflect(-V, N), v_Instance.x);
		quadTraced = QuadShare(quadTraced, quadLane);
#endif
		if (mirror > 0.0)
		{
#ifdef RV_WATER
			vec3 traced = quadTraced;
#else
			vec3 traced = TraceReflection(v_WorldPos, normalize(v_Normal), reflect(-V, N), v_Instance.x);
#endif

			// **Bound the ray by the probe it is replacing.**
			//
			// A mirror ray reads ONE unfiltered texel at mip 0 -- there are no
			// ray cones and no derivatives inside a ray query, so there is no
			// footprint to filter over. The probe answers the same lobe already
			// band-limited, by the LOD chosen from dFdx/dFdy of this very
			// reflection vector a few lines above. So the two are estimates of
			// the same quantity, and when the point sample stands many times
			// above the filtered one it is not a highlight the probe missed --
			// it is the one texel the ray happened to land on this frame. On a
			// smooth surface the direction drifts with the sub-pixel jitter, so
			// which texel that is changes every frame, and the pixel blinks.
			//
			// Generous, because a real highlight must survive: a chrome ball
			// reflecting a lamp reads far brighter than its blurred probe, and
			// eight times leaves that alone while removing the hundredfold
			// spikes. The additive term keeps the bound from collapsing to zero
			// where the probe is black.
			// **The floor is a setting now, and this is why.** The bound is
			// right whenever the probe is a fair estimate of the same lobe --
			// which is every daylit scene. At night the environment is black,
			// `prefiltered * 8` collapses, and the whole bound becomes this
			// floor: a sodium lamp's reflection in water, which is a genuine
			// highlight and most of what a night frame of a lit bridge is
			// made of, was being crushed to 0.05 along with the fireflies.
			//
			// Indirect.z, defaulting to the 0.05 this used to hardcode, so no
			// scene that does not raise it changes by a bit.
			const float kTracedBound = 8.0;
			const float floorLevel = max(u_Scene.Indirect.z, 0.0);
			traced = min(traced, prefiltered * kTracedBound + vec3(floorLevel));

			prefiltered = mix(prefiltered, traced, mirror);
		}
	}
#endif
	if (u_Scene.ScreenReflections.x > 0.0)
	{
		vec2 previousNDC = thenNDC - u_Scene.Jitter.zw;
		vec2 traceUV = vec2(previousNDC.x, previousNDC.y * u_Scene.ScreenReflections.y) * 0.5 + 0.5;

		// Off the edge last frame means nothing was traced for this point;
		// the probe answers, and clamp-to-edge would have answered with a
		// neighbour's reflection instead.
		if (all(greaterThanEqual(traceUV, vec2(0.0))) && all(lessThanEqual(traceUV, vec2(1.0))))
		{
			vec4 traced = texture(u_ScreenReflections, traceUV);
			float share = clamp(traced.a * u_Scene.ScreenReflections.x, 0.0, 1.0);
			prefiltered = mix(prefiltered, traced.rgb, share);
		}
	}

	vec2 envBRDF = EnvBRDF(NdotV, roughness);
	ambient += prefiltered * (F0 * envBRDF.x + envBRDF.y) * occlusion;

	vec3 color = ambient + Lo + surface.Emissive;

#ifdef RV_TRANSPARENT

	// Weighted-blended OIT, the same estimator the particles use and with the
	// same unit-weight distance, so a pane of glass and a plume of smoke at one
	// depth carry the same authority in the sum.
	//
	// **The weight is a depth ramp and it has to be one.** A constant weight
	// cancels in the resolve and turns the whole thing into a flat average,
	// which is the defect particle_weighted.rvshader records finding: near
	// glass would not cover far glass and a windscreen would show the far side
	// of the cabin as strongly as the near.
	const float kUnitWeightDistance = 20.0;

	// **Coverage is not transmission, and multiplying the highlight by alpha is
	// where naive transparency dies.** A pane of glass at alpha 0.2 transmits
	// four fifths of what is behind it *and still reflects the ceiling at full
	// strength* -- the two are different terms and only the first is what alpha
	// describes. Weighted-blended OIT has one coverage number per fragment, so
	// the reflectance has to be folded into it rather than kept separate:
	//
	//     coverage = alpha + reflectance * (1 - alpha)
	//
	// which is exactly "what it transmits, plus what it mirrors of the rest".
	// It also gives the Fresnel behaviour for nothing, because the reflectance
	// below already carries it: glass seen head-on stays mostly transparent and
	// glass at a grazing angle goes to a mirror, which is the single cue that
	// makes a windscreen read as glass rather than as a tinted hole.
	//
	// Without this the car's screen and lamp lenses came back flat and dull --
	// correctly lit and then attenuated to nothing.
	// **The reflection is not attenuated by the transmission**, and folding it
	// into the coverage alone was not enough. A dielectric reflects about four
	// percent head-on, so a coverage lifted by four percent is a four percent
	// change to a thing that was already invisible -- and the *reflected
	// radiance itself* was still being multiplied by alpha, which is what
	// deleted it.
	//
	// So the two terms are separated. What the glass reflects is added at full
	// strength; only what is seen *through* it is scaled by how much of it
	// there is:
	//
	//     premultiplied = transmitted * alpha + reflected
	//     coverage      = alpha + reflectance * (1 - alpha)
	//
	// Work the resolve through with one layer and it comes out as
	// `transmitted * alpha + reflected + background * (1 - coverage)`, which is
	// the thing a pane of glass actually does.
	//
	// `reflected` is the environment specular exactly as the line above
	// computed it, not an approximation of it -- so an opaque surface and a
	// blended one reflect the same room by construction.
	vec3 reflected = prefiltered * (F0 * envBRDF.x + envBRDF.y) * occlusion;
	vec3 transmitted = max(color - reflected, vec3(0.0));

	float reflectance = clamp(max(reflected.r, max(reflected.g, reflected.b)), 0.0, 1.0);

	float alpha = clamp(baseColor.a, 0.0, 1.0);

#ifdef RV_WATER
	// **The glassy see-through: what is behind the surface, bent and
	// absorbed.** Alpha blending can only fade the water towards what is
	// behind it; glass-clear water does something different -- it *shows* the
	// scene below, displaced by the surface slope and losing red first as the
	// path through it lengthens. So the transmitted term stops being the
	// water's own lit colour and becomes the scene behind, run through
	// Beer-Lambert, with the lit colour taking over exactly as the
	// transmittance dies -- which is what depth does to a real sea.
	//
	// The extinction is quoted against the gradient dial: at a thickness of
	// one GradientDepth the red channel is gone and blue is down to a
	// quarter, so the dial keeps meaning what its name says -- metres to
	// reach deep water -- for the absorption as well as the colour ramp. The
	// spectral shape (red dies ~5x faster than blue) is clear-ocean water's.
	//
	// Foam scales the transmittance down before the mix: whitewater floats
	// *on* the surface and a pixel the sheet covers shows foam, not the
	// refracted pier below it.
	{
		const vec3 waterSigma = vec3(4.6, 1.9, 1.3) / max(waterGradient, 0.05);
		const vec3 waterView = normalize(u_Scene.CameraPosition.xyz - v_WorldPos);

#ifdef RV_RAY_REFRACTION
		// **The traced form: the actual refracted ray, into the actual
		// scene.** Snell at the surface, then the same traversal and the same
		// simplified shade the mirror rays use -- so a pier leg seen through
		// the swell is the pier leg, displaced exactly as physics displaces
		// it, wherever it is and whether or not it is on screen. The hit
		// distance is the true path length through the water, which the
		// screen-space form can only approximate along the view axis.
		//
		// A miss is open sea with no bottom in reach: transmittance zero, the
		// lit colour answers, which is what a bottomless bay looks like.
		const vec3 refractedDir = refract(-waterView, N, 0.7519);

		// **WR-18: how far the refracted ray looks.** It looked 300 m, and
		// the water had absorbed the answer long before that: transmittance
		// is exp(-sigma * distance), so past -ln(floor) / sigma_min the
		// bottom, however lit, returns under `floor` of its light -- a 256th
		// at the presets' value, 51 m in this bay. A ray that stops there is
		// cheap whether it hits or misses, and the open channel, where every
		// ray used to travel the full 300 m to find nothing, is where the
		// frame was paying. Anything beyond could not show, by construction.
		float reach = 300.0;
		if (u_Scene.RayRates.y > 0.0)
		{
			const float sigmaMin = min(waterSigma.r, min(waterSigma.g, waterSigma.b));
			reach = min(reach, -log(u_Scene.RayRates.y) / max(sigmaMin, 1.0e-4));
		}

		// WR-18: one lane of the quad traces the refraction, all four share.
		// Two broadcasts -- the lit bottom already attenuated, and the
		// transmittance itself, which the mix below needs on its own.
		const uint refractLane = QuadTraceLane();
		vec3 quadBehind = vec3(0.0);
		vec3 quadT = vec3(0.0);
		if (dot(refractedDir, refractedDir) > 1.0e-6 && QuadTraces(refractLane))
		{
			TracedSurface behindHit = TraceSurface(v_WorldPos, -N, refractedDir, reach);
			if (!behindHit.Missed)
			{
				const float through = length(behindHit.Position - v_WorldPos);
				quadT = exp(-waterSigma * through) * (1.0 - foam);
				vec3 behind = ShadeTraced(behindHit,
										  ProbeIrradiance(behindHit.Normal, v_Instance.x));
				// The traced form knows exactly where the bottom is, so the
				// caustic web lands on the real hit point.
				behind *= 1.0 + WaterCaustics(behindHit.Position.xz, through,
											  waterGradient, waterWind, waterTime);
				quadBehind = behind * quadT;
			}
		}
		quadBehind = QuadShare(quadBehind, refractLane);
		quadT = QuadShare(quadT, refractLane);

		if (dot(refractedDir, refractedDir) > 1.0e-6)
		{
			// Only the *scattered* light gives way to the refracted scene:
			// the glitter is surface reflection and never entered the water,
			// so it rides over clear and murky alike.
			const vec3 scatter = max(transmitted - waterSpecular, vec3(0.0));
			transmitted = quadBehind + scatter * (1.0 - quadT) + waterSpecular;
			alpha = 1.0;
		}
#else
		// **The raster form: the opaque frame, sampled where the refracted
		// ray lands.** A point is pushed a little way down the refracted
		// direction and reprojected; the screen-space offset to it is the
		// distortion, so the bend scales correctly with distance and view
		// angle rather than being a fixed screen-space wobble. The push is
		// clamped to the measured thickness -- shallow water bends what is
		// visibly just below the surface, not something metres away.
		//
		// Two guards on the fetched sample, both standard and both earned:
		// an offset that lands outside the frame falls back to the straight
		// sample, and one that lands on something *nearer than the surface*
		// -- a pier standing out of the water smeared across it -- is
		// rejected the same way, because the backdrop holds no second layer
		// behind that pier to fetch.
		if (waterFlags != 0.0)
		{
			const vec3 refractedDir = refract(-waterView, N, 0.7519);
			const float push = clamp(waterThickness, 0.25, 4.0);
			const vec4 refractedClip = u_Scene.ViewProjection
									 * vec4(v_WorldPos + refractedDir * push, 1.0);

			// Through the same NDC-to-row mapping the straight sample took, so
			// the two agree about which way is down the image.
			const vec2 ndcRefracted = refractedClip.xy / max(refractedClip.w, 1.0e-4);
			vec2 refractedUV = vec2(ndcRefracted.x,
									ndcRefracted.y * sign(waterFlags)) * 0.5 + 0.5;
			float eyeBehind = texture(u_WaterBackdropDepth, refractedUV).r;
			const bool offFrame = any(lessThan(refractedUV, vec2(0.0)))
							   || any(greaterThan(refractedUV, vec2(1.0)));
			if (offFrame || eyeBehind < v_ClipPos.w - 0.05)
			{
				refractedUV = waterScreenUV;
				eyeBehind = texture(u_WaterBackdropDepth, refractedUV).r;
			}

			const float through = max(eyeBehind - v_ClipPos.w, 0.0);
			const vec3 waterT = exp(-waterSigma * through) * (1.0 - foam);
			vec3 behind = texture(u_WaterBackdrop, refractedUV).rgb;

			// The raster form has no hit point, so the bottom is estimated
			// along the refracted ray at the measured thickness -- exact for
			// a flat bottom seen steeply, close enough for the caustic web,
			// whose job is life rather than measurement.
			behind *= 1.0 + WaterCaustics((v_WorldPos + refractedDir * through).xz,
										  through, waterGradient,
										  waterWind, waterTime);

			// Only the *scattered* light gives way to the refracted scene:
			// the glitter is surface reflection and never entered the water,
			// so it rides over clear and murky alike.
			const vec3 scatter = max(transmitted - waterSpecular, vec3(0.0));
			transmitted = behind * waterT + scatter * (1.0 - waterT) + waterSpecular;

			// The pixel is owned outright: the background is already inside
			// the transmitted term, refracted, so letting the resolve show it
			// again through the revealage would draw it twice -- once bent
			// and once straight, a double exposure at every waterline.
			alpha = 1.0;
		}
#endif
	}
#endif

	float coverage = clamp(alpha + reflectance * (1.0 - alpha), 0.0, 1.0);
	float viewDepth = length(v_WorldPos - u_Scene.CameraPosition.xyz);
	float weight = coverage * clamp(kUnitWeightDistance / max(viewDepth, 1e-3), 1e-2, 3e3);

#ifndef RV_WATER_SURFACE
	// Not in the surface variant, which returned at the cut and declares
	// two attachments of its own: unreachable code is still compiled, and
	// an undeclared output is a compile error rather than dead weight.
	o_Accumulate = vec4(transmitted * alpha + reflected, coverage) * weight;
	o_Revealage = coverage;
#endif

#else

	// Linear, unbounded, untouched. Tone mapping and the transfer function
	// moved to the tonemap pass, for two reasons: bloom needs these values
	// before the curve compresses them, and every shader that wrote to the
	// screen used to have to agree on the display transform -- which only this
	// one did, so quads and meshes were being shown through different ones.
	o_Color = vec4(color, baseColor.a);

#endif

	// WR-16 S0: the frame's counts, once per invocation, last. Every live
	// lane of the wave reaches this line -- main has no early return, and
	// the discards above came before any ray was cast.
#ifdef RV_RAY_COUNTERS
	FlushRayCounters(true);
#endif
#ifdef RV_DEBUG_VIEW
	DebugCountsStore();
#endif
}

#endif   // !RV_TRACE_ONLY
