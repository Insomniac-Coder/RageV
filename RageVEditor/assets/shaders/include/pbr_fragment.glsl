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
	vec4 Direction;
	// rgb colour, a intensity
	vec4 Color;
	// x range, y cos(inner cone), z cos(outer cone)
	vec4 Params;
	// x kind of shadow map (0 none), y slot, z far distance, w texel scale
	vec4 Shadow;
};

layout(std430, set = 0, binding = 8) readonly buffer LightBlock
{
	GpuLight Lights[];
} u_Lights;

// The cluster grid: which lights reach which cell of the view frustum.
//
// Cells hold a range into the index list rather than the lights themselves,
// because a light reaching several cells would otherwise be stored several
// times.
struct LightCell
{
	uint Offset;
	uint Count;
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

// The material: two front doors, one shading (ENGINE-NOTES 7al).
//
// Everything below this block -- every texture(u_BaseColorMap, uv), every
// u_Material.MapFlags -- compiles unchanged in both variants. That is the
// point of forking at the preprocessor: the six hundred lines that could
// drift do not exist twice.
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
	int   _pad0;
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

#else

layout(set = 1, binding = 0) uniform MaterialData
{
	vec4  BaseColor;
	vec4  EmissiveColor;
	float Metallic;
	float Roughness;
	float Occlusion;
	float NormalScale;
	int   MapFlags;
	// F0 = 0.08 * Specular for anything that is not metal. 0.5 is the 4% this
	// used to hardcode.
	float Specular;
	// Depth of the parallax displacement, in UV units.
	float HeightScale;
	// std140 pads out to a vec4 boundary, so this must be declared or the
	// offsets here and in MaterialParams disagree -- and a uniform block that
	// disagrees does not fail, it reads the wrong sixteen bytes.
	int   _pad0;
	// xy scale, zw offset. See MaterialParams::UvTransform.
	vec4  UvTransform;
} u_Material;

layout(set = 1, binding = 1) uniform sampler2D u_BaseColorMap;
layout(set = 1, binding = 2) uniform sampler2D u_NormalMap;
// Binding 3 held the packed metallic-roughness map. glTF was the only source
// of one and the importer splits it now, so nothing writes this -- the slot
// stays declared because the descriptor set layout is built from the shader.
layout(set = 1, binding = 3) uniform sampler2D u_Unused3;
layout(set = 1, binding = 4) uniform sampler2D u_OcclusionMap;
layout(set = 1, binding = 5) uniform sampler2D u_EmissiveMap;
// Separate greyscale roughness and metallic, read from red. Every texture
// library that is not a glTF ships them this way.
layout(set = 1, binding = 6) uniform sampler2D u_RoughnessMap;
layout(set = 1, binding = 7) uniform sampler2D u_MetallicMap;
layout(set = 1, binding = 8) uniform sampler2D u_SpecularMap;
layout(set = 1, binding = 9) uniform sampler2D u_HeightMap;

#endif

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
// Which cube of u_Environment and u_Irradiance. Flat and per instance: it is
// constant across the object, and interpolating it would put fragments in the
// middle of a triangle between two probes.
layout(location = 7) flat in float v_Probe;
layout(location = 8) in vec4 v_PrevClipPos;
// Which record in u_Materials this instance's material is. Read only by the
// bindless variant; declared in both so the two stages agree.
layout(location = 9) flat in float v_MaterialIndex;

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

// Octahedral encoding, unit vector to [0,1]^2. Two 8-bit channels give about a
// degree of direction, which a reflection ray does not notice.
vec2 OctEncode(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
															 n.y >= 0.0 ? 1.0 : -1.0);
	return e * 0.5 + 0.5;
}

bool HasMap(int flag) { return (u_Material.MapFlags & flag) != 0; }

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
	// honest answer is lit.
	if (coordinate.z > 1.0)
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
float TraceShadow(vec3 worldPos, vec3 L, float tMax)
{
	if (u_Scene.ShadowParams.x <= 0.0)
		return 1.0;

	vec3 Ng = normalize(v_Normal);
	float NgdotL = clamp(dot(Ng, L), 0.0, 1.0);
	float slope = sqrt(1.0 - NgdotL * NgdotL) / max(NgdotL, 0.15);
	float offset = 0.002 * (1.0 + min(slope, 4.0));

	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_SceneAS,
						  gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
						  0xFFu, worldPos + Ng * offset, 0.0, L, tMax);
	while (rayQueryProceedEXT(q)) {}
	return rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT
		 ? 1.0 : 0.0;
}
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
	if (coordinate.z > 1.0)
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

	// The depth glm's perspective would have written for a point this far down
	// the face's axis, with GLM_FORCE_DEPTH_ZERO_TO_ONE.
	float depth = (farClip / (farClip - POINT_SHADOW_NEAR)) *
				  (1.0 - POINT_SHADOW_NEAR / major);

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

vec3 PerturbNormal(mat3 TBN, vec2 uv)
{
	// Z is reconstructed from XY, never read -- one path for every normal
	// map. A two-channel (BC5) map has no Z to read; for an RGB map whose
	// normals are unit length the reconstruction *is* the stored Z, and one
	// that was not unit length gets renormalized, which is a correction. A
	// shader that branched on the texture's format here would be two shading
	// paths waiting to drift.
	vec2 xy = texture(u_NormalMap, uv).xy * 2.0 - 1.0;
	vec3 tangentNormal = vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)));
	tangentNormal.xy *= v_Surface.w;
	return normalize(TBN * tangentNormal);
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

		o_Velocity = ((nowNDC - u_Scene.Jitter.xy) - (thenNDC - u_Scene.Jitter.zw)) * 0.5;
	}

	// The material's tiling, applied once. Every map has to use the same
	// coordinate or the normals stop lining up with the colour they belong to,
	// which reads as a lighting bug rather than as a UV one.
	vec2 uv = v_TexCoord * u_Material.UvTransform.xy + u_Material.UvTransform.zw;

	vec3 Ngeo = normalize(v_Normal);
	vec3 V = normalize(u_Scene.CameraPosition.xyz - v_WorldPos);
	mat3 TBN = TangentFrame(Ngeo, v_WorldPos, uv);

	// Parallax before any map is sampled, so colour, normal, roughness and
	// occlusion all agree about which texel is under this pixel.
	if (HasMap(MAP_HEIGHT))
		uv = Parallax(uv, transpose(TBN) * V);

	vec4 baseColor = v_BaseColor;
	if (HasMap(MAP_BASE_COLOR))
		baseColor *= texture(u_BaseColorMap, uv);

	vec3 albedo = baseColor.rgb;

	float metallic  = v_Surface.x;
	float roughness = v_Surface.y;
	// Separate greyscale maps, read from red. glTF packs these two into one
	// texture; the importer splits it, so by the time a material exists there
	// is no packed form left to handle.
	if (HasMap(MAP_ROUGHNESS))
		roughness *= texture(u_RoughnessMap, uv).r;
	if (HasMap(MAP_METALLIC))
		metallic *= texture(u_MetallicMap, uv).r;

	roughness = clamp(roughness, 0.045, 1.0);   // fully smooth aliases badly
	metallic  = clamp(metallic, 0.0, 1.0);

	float occlusion = v_Surface.z;
	if (HasMap(MAP_OCCLUSION))
		occlusion *= texture(u_OcclusionMap, uv).r;

	vec3 N = Ngeo;
	if (HasMap(MAP_NORMAL))
		N = PerturbNormal(TBN, uv);

	// The surface as SSR will see it: the *shading* normal, after the normal
	// map, so a reflection off the brick floor follows the mortar and not a
	// flat plane. Written here, as soon as all three are final, and
	// unconditionally -- an unwritten attachment holds whatever the target
	// last held.
	o_Surface = vec4(OctEncode(N), roughness, metallic);

	// Dielectrics reflect ~4% at normal incidence; metals use their albedo as
	// the reflectance and have no diffuse response at all.
	// Dielectric reflectance, on the convention everyone uses: F0 = 0.08 *
	// specular, so the default 0.5 is the 4% that was hardcoded here. Metals
	// ignore it entirely -- their F0 *is* their albedo, which is what the mix
	// below says.
	float specular = u_Material.Specular;
	if (HasMap(MAP_SPECULAR))
		specular *= texture(u_SpecularMap, uv).r;

	vec3 F0 = mix(vec3(0.08 * clamp(specular, 0.0, 1.0)), albedo, metallic);

	vec3 Lo = vec3(0.0);

	// Directional lights first, unconditionally: they have no position, so they
	// reach every cell and binning them would put a copy in all 3456 of them.
	// Everything after them is positional and comes from this fragment's cell.
	int directionalCount = int(u_Scene.ClusterGrid.w);

	uint cell = ClusterIndexFor(v_WorldPos);
	uint cellOffset = u_Cells.Cells[cell].Offset;
	uint cellCount = u_Cells.Cells[cell].Count;

	int total = directionalCount + int(cellCount);

	// `entry`, not `slot`: the loop body already has a `slot`, which is the
	// shadow map this light was given.
	for (int entry = 0; entry < total; ++entry)
	{
		int i = entry < directionalCount
			  ? entry
			  : int(u_CellIndices.Indices[cellOffset + uint(entry - directionalCount)]);

		GpuLight light = u_Lights.Lights[i];

		vec3  lightColor = light.Color.rgb * light.Color.a;
		float isPositional = light.Position.w;

		vec3  L;
		float attenuation = 1.0;

		if (isPositional == 0.0)
		{
			// Directional: L points towards the light, the opposite of travel.
			L = normalize(-light.Direction.xyz);
		}
		else
		{
			vec3 toLight = light.Position.xyz - v_WorldPos;
			float distance = length(toLight);
			L = toLight / max(distance, 0.0001);

			// Inverse-square falloff, windowed so the light reaches zero at its
			// range instead of trailing off forever.
			float range = max(light.Params.x, 0.0001);
			float ratio = clamp(1.0 - pow(distance / range, 4.0), 0.0, 1.0);
			attenuation = (ratio * ratio) / max(distance * distance, 0.0001);

			// Spot cone, when the inner and outer angles differ. Measured
			// against the light's own axis, not against its position -- those
			// only coincide when the light sits at the origin.
			float cosInner = light.Params.y;
			float cosOuter = light.Params.z;
			if (cosOuter < cosInner)
			{
				float theta = dot(L, normalize(-light.Direction.xyz));
				attenuation *= clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
			}
		}

		vec3 H = normalize(V + L);
		vec3 radiance = lightColor * attenuation;

		float NDF = DistributionGGX(N, H, roughness);
		float G   = GeometrySmith(N, V, L, roughness);
		vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

		vec3 numerator = NDF * G * F;
		float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
		vec3 specular = numerator / denominator;

		// Energy conservation: what is not reflected is refracted, and metals
		// refract nothing.
		vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

		float NdotL = max(dot(N, L), 0.0);

		// Each light carries which kind of shadow it has, if any: under maps
		// which map, under rays whether the ray goes to infinity or to the
		// light (ENGINE-NOTES 7an).
		int kind = int(light.Shadow.x);
		int slot = int(light.Shadow.y);

		float shadow = 1.0;
#ifdef RV_RAY_SHADOWS
		if (kind == 1)
			shadow = TraceShadow(v_WorldPos, L, 1.0e4);
		else if (kind != 0)
			shadow = TraceShadow(v_WorldPos, L, length(light.Position.xyz - v_WorldPos));
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

		Lo += (kD * albedo / PI + specular) * radiance * NdotL * shadow;
	}

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
	vec3 irradiance = textureLod(u_Irradiance, vec4(RotateIntoSky(N), v_Probe), 0.0).rgb *
					  u_Scene.Environment.x;

	float NdotV = max(dot(N, V), 0.0);
	vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	vec3 ambient = kD * albedo * (ambientLight + irradiance) * occlusion;

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

	vec3 prefiltered = textureLod(u_Environment, vec4(reflection, v_Probe), lod).rgb *
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

	vec3 emissive = v_EmissiveColor.rgb;
	if (HasMap(MAP_EMISSIVE))
		emissive *= texture(u_EmissiveMap, uv).rgb;

	vec3 color = ambient + Lo + emissive;

	// Linear, unbounded, untouched. Tone mapping and the transfer function
	// moved to the tonemap pass, for two reasons: bloom needs these values
	// before the curve compresses them, and every shader that wrote to the
	// screen used to have to agree on the display transform -- which only this
	// one did, so quads and meshes were being shown through different ones.
	o_Color = vec4(color, baseColor.a);
}
