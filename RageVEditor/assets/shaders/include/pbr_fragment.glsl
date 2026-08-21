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

// Ray-traced reflections (ENGINE-NOTES 7ao): a hit is shaded, so the hit's
// mesh and material have to be reachable from a shader that never bound
// them. The mesh by *address* -- buffer references over the vertex and index
// buffers the structures were built from -- and the material through the
// heap, by the record index the instance table names. One row per structure
// instance in build order; the hit's custom index is the row. Mirrored by
// hand in Renderer3D.cpp (GpuRayInstance), std430.
#if defined(RV_RAY_REFLECTIONS) || defined(RV_RAY_GI)
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
	uint  Flags;              // bit 0: positions are posed, take the flat normal
	uint  _pad0;
	uint  _pad1;
	vec4  BaseColor;
	vec4  EmissiveColor;
	vec4  Surface;            // metallic, roughness, occlusion, normal scale
};
const uint RAY_INSTANCE_POSED = 1u;

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

// Traced indirect diffuse, raw irradiance, **albedo-free** (ENGINE-NOTES 7av).
// It goes to an attachment rather than into `irradiance` directly because a
// denoiser needs the GI term to exist on its own: mixed into the lit pixel it
// cannot be filtered without filtering the frame. Zero in every variant that
// does not trace.
layout(location = 3) out vec4 o_Indirect;
#endif   // !RV_TRACE_ONLY

// Octahedral encoding, unit vector to [0,1]^2. Two 8-bit channels give about a
// degree of direction, which a reflection ray does not notice.
vec2 OctEncode(vec3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	vec2 e = n.z >= 0.0 ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0,
															 n.y >= 0.0 ? 1.0 : -1.0);
	return e * 0.5 + 0.5;
}

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
float TraceShadowFrom(vec3 worldPos, vec3 Ng, vec3 L, float tMax)
{
	if (u_Scene.ShadowParams.x <= 0.0)
		return 1.0;

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

// The surface being shaded: its own geometric normal. The tracing above takes
// its normal as an argument and is what a pass with no varyings uses.
#ifndef RV_TRACE_ONLY
float TraceShadow(vec3 worldPos, vec3 L, float tMax)
{
	return TraceShadowFrom(worldPos, normalize(v_Normal), L, tMax);
}
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

#if defined(RV_RAY_REFLECTIONS) || defined(RV_RAY_GI)
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

	// Off the surface along its geometric normal, the shadow ray's offset,
	// for the shadow ray's reason.
	float NgdotD = clamp(dot(Ng, direction), 0.0, 1.0);
	float slope = sqrt(1.0 - NgdotD * NgdotD) / max(NgdotD, 0.15);
	float offset = 0.002 * (1.0 + min(slope, 4.0));

	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_SceneAS, gl_RayFlagsOpaqueEXT, 0xFFu,
						  origin + Ng * offset, 0.0, direction, reach);
	while (rayQueryProceedEXT(q)) {}

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
	mat4x3 objectToWorld = rayQueryGetIntersectionObjectToWorldEXT(q, true);
	mat4x3 worldToObject = rayQueryGetIntersectionWorldToObjectEXT(q, true);

	RayInstance hit = u_RayInstances.Instances[instance];
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
	vec3 p0 = vec3(positions.Floats[i0 * ps + 0u], positions.Floats[i0 * ps + 1u], positions.Floats[i0 * ps + 2u]);
	vec3 p1 = vec3(positions.Floats[i1 * ps + 0u], positions.Floats[i1 * ps + 1u], positions.Floats[i1 * ps + 2u]);
	vec3 p2 = vec3(positions.Floats[i2 * ps + 0u], positions.Floats[i2 * ps + 1u], positions.Floats[i2 * ps + 2u]);
	vec2 uv0 = vec2(attributes.Floats[i0 * as_ + 6u], attributes.Floats[i0 * as_ + 7u]);
	vec2 uv1 = vec2(attributes.Floats[i1 * as_ + 6u], attributes.Floats[i1 * as_ + 7u]);
	vec2 uv2 = vec2(attributes.Floats[i2 * as_ + 6u], attributes.Floats[i2 * as_ + 7u]);

	vec3 objectPosition = p0 * w0 + p1 * bary.x + p2 * bary.y;
	vec3 hitPosition = objectToWorld * vec4(objectPosition, 1.0);
	vec2 hitUv = uv0 * w0 + uv1 * bary.x + uv2 * bary.y;

	vec3 objectNormal;
	if ((hit.Flags & RAY_INSTANCE_POSED) != 0u)
	{
		// Posed positions, unposed normals: the triangle's own plane is the
		// honest normal.
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
	// Toward the ray, whichever face was hit.
	if (dot(hitNormal, direction) > 0.0)
		hitNormal = -hitNormal;

	// The material: the record, then the maps through the heap.
	GpuMaterial material = u_Materials.Materials[hit.MaterialIndex];
	vec2 uv = hitUv * material.UvTransform.xy + material.UvTransform.zw;

	vec3 albedo = hit.BaseColor.rgb;
	if ((material.MapFlags & MAP_BASE_COLOR) != 0)
		albedo *= textureLod(u_Textures[nonuniformEXT(material.Maps0.x)], uv, 0.0).rgb;
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
	for (int i = 0; i < u_Scene.LightCount; ++i)
	{
		GpuLight light = u_Lights.Lights[i];
		vec3 lightColor = light.Color.rgb * light.Color.a;
		vec3 L;
		float attenuation = 1.0;
		float shadow = 1.0;
		if (light.Position.w == 0.0)
		{
			L = normalize(-light.Direction.xyz);
			if (int(light.Shadow.x) != 0)
				shadow = TraceShadowFrom(hitPosition, hitNormal, L, 1.0e4);
		}
		else
		{
			vec3 toLight = light.Position.xyz - hitPosition;
			float distance = length(toLight);
			L = toLight / max(distance, 0.0001);
			float range = max(light.Params.x, 0.0001);
			float ratio = clamp(1.0 - pow(distance / range, 4.0), 0.0, 1.0);
			attenuation = (ratio * ratio) / max(distance * distance, 0.0001);
			float cosInner = light.Params.y;
			float cosOuter = light.Params.z;
			if (cosOuter < cosInner)
			{
				float theta = dot(L, normalize(-light.Direction.xyz));
				attenuation *= clamp((theta - cosOuter) / max(cosInner - cosOuter, 0.0001), 0.0, 1.0);
			}
		}
		lit += diffuse / PI * lightColor * attenuation * max(dot(hitNormal, L), 0.0) * shadow;
	}

	surface.Position = hitPosition;
	surface.Normal = hitNormal;
	surface.Diffuse = diffuse;
	surface.Direct = lit;
	surface.Emissive = emissive;
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

// A described hit plus what arrives at it. Callers check `Missed` first: this
// deliberately does not, so a miss does not pay for an irradiance fetch it
// throws away.
vec3 ShadeTraced(TracedSurface surface, vec3 arriving)
{
	vec3 ambientLight = u_Scene.Ambient.rgb * u_Scene.Ambient.a;
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

	return s;
}

#else   // RV_LAYERED

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
#define SHADE_LAYER(i)                                                                     \
	{                                                                                      \
		int   flags = u_Layered.MapFlags[i];                                               \
		float w     = weight[i];                                                           \
		vec2 uvL  = v_TexCoord * u_Layered.UvTransform[i].xy + u_Layered.UvTransform[i].zw; \
		vec2 ddxL = ddx * u_Layered.UvTransform[i].xy;                                     \
		vec2 ddyL = ddy * u_Layered.UvTransform[i].xy;                                     \
		mat3 TBN  = TangentFrame(Ngeo, v_WorldPos, uvL);                                   \
		if (w > 0.0)                                                                       \
		{                                                                                  \
			vec4 baseL = u_Layered.BaseColor[i];                                           \
			if ((flags & MAP_BASE_COLOR) != 0)                                             \
				baseL *= textureGrad(LAYER_BASE_COLOR(i), uvL, ddxL, ddyL);                \
			float roughL = u_Layered.Surface[i].y;                                         \
			if ((flags & MAP_ROUGHNESS) != 0)                                              \
				roughL *= textureGrad(LAYER_ROUGHNESS(i), uvL, ddxL, ddyL).r;              \
			vec3 nL = Ngeo;                                                                \
			if ((flags & MAP_NORMAL) != 0)                                                 \
				nL = UnpackNormal(TBN, textureGrad(LAYER_NORMAL(i), uvL, ddxL, ddyL).xy,   \
								  u_Layered.Surface[i].w);                                 \
			s.BaseColor += w * baseL;                                                      \
			s.Metallic  += w * u_Layered.Surface[i].x;                                     \
			s.Roughness += w * roughL;                                                     \
			s.Occlusion += w * u_Layered.Surface[i].z;                                     \
			s.Specular  += w * u_Layered.Specular[i];                                      \
			s.Emissive  += w * u_Layered.EmissiveColor[i].rgb;                             \
			s.N         += w * nL;                                                         \
		}                                                                                  \
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
	return s;
}

#endif   // RV_LAYERED

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

	vec3 Ngeo = normalize(v_Normal);
	vec3 V = normalize(u_Scene.CameraPosition.xyz - v_WorldPos);

	// The surface, every map read, from whichever body this variant has.
	Surface surface = SampleSurface(Ngeo, V);

	vec4 baseColor = surface.BaseColor;
	vec3 albedo = baseColor.rgb;

	float roughness = clamp(surface.Roughness, 0.045, 1.0);   // fully smooth aliases badly
	float metallic  = clamp(surface.Metallic, 0.0, 1.0);
	float occlusion = surface.Occlusion;
	vec3 N = surface.N;

	// The surface as SSR will see it: the *shading* normal, after the normal
	// map, so a reflection off the brick floor follows the mortar and not a
	// flat plane. Written here, as soon as all three are final, and
	// unconditionally -- an unwritten attachment holds whatever the target
	// last held.
	o_Surface = vec4(OctEncode(N), roughness, metallic);


	// Dielectrics reflect ~4% at normal incidence; metals use their albedo as
	// the reflectance and have no diffuse response at all: F0 = 0.08 *
	// specular for a dielectric, the albedo for a metal.
	vec3 F0 = mix(vec3(0.08 * clamp(surface.Specular, 0.0, 1.0)), albedo, metallic);

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
	o_Indirect = vec4(0.0, 0.0, 0.0, 1.0);
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
			irradiance += indirectTerm;
		}
	}

	float NdotV = max(dot(N, V), 0.0);
	vec3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
	vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
	vec3 ambient = kD * albedo * (ambientLight + irradiance) * occlusion;

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
	o_Indirect = vec4(kD * albedo * indirectTerm * occlusion, 1.0);
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
		if (mirror > 0.0)
		{
			vec3 traced = TraceReflection(v_WorldPos, normalize(v_Normal), reflect(-V, N), v_Probe);
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

	// Linear, unbounded, untouched. Tone mapping and the transfer function
	// moved to the tonemap pass, for two reasons: bloom needs these values
	// before the curve compresses them, and every shader that wrote to the
	// screen used to have to agree on the display transform -- which only this
	// one did, so quads and meshes were being shown through different ones.
	o_Color = vec4(color, baseColor.a);
}

#endif   // !RV_TRACE_ONLY
