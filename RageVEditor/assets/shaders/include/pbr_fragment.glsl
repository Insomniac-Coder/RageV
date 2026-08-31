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
	vec4 IrradianceBox[40];
	vec4 ProbeCount;              // x = how many rows are real
	vec4 ProbePlacement[15];
	vec4 ProbeSlot[15];
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
#endif

#ifdef RV_TRANSPARENT

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

float TraceShadowFrom(vec3 worldPos, vec3 Ng, vec3 L, float tMax)
{
	if (u_Scene.ShadowParams.x <= 0.0)
		return 1.0;

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
						  0xFFu, worldPos + Ng * offset, 0.0, L, tMax);
	RayTraverse(q);
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

bool VolumeIrradiance(vec3 position, vec3 normal, bool shadowed, out vec3 irradiance,
					  out float skyVisibility)
{
	irradiance = vec3(0.0);

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
	const int volumeCount = int(u_Scene.IrradianceExtents.w + 0.5);
	if (volumeCount <= 0)
		return false;               // no field in the scene

	int chosen = -1;
	float chosenDepth = 0.0;
	vec3 chosenLocal = vec3(0.0);
	for (int v = 0; v < volumeCount; v++)
	{
		const vec4 boxCentre = u_Scene.IrradianceBox[v * 5 + 0];
		const vec4 boxExtents = u_Scene.IrradianceBox[v * 5 + 1];
		const vec3 axisX = u_Scene.IrradianceBox[v * 5 + 2].xyz;
		const vec3 axisY = u_Scene.IrradianceBox[v * 5 + 3].xyz;
		const vec3 axisZ = u_Scene.IrradianceBox[v * 5 + 4].xyz;

		const vec3 boxHalf = max(boxExtents.xyz, vec3(1.0e-3));
		const vec3 d = position - boxCentre.xyz;
		// Into this box's own frame; the bias along the normal is applied
		// after a volume is chosen, because it needs that volume's cell size.
		const vec3 p = vec3(dot(axisX, d), dot(axisY, d), dot(axisZ, d));
		const vec3 l = p / boxHalf;
		if (any(greaterThan(abs(l), vec3(1.0))))
			continue;               // outside this one

		const vec3 boxGrid = max(vec3(u_Scene.IrradianceBox[v * 5 + 2].w,
									  u_Scene.IrradianceBox[v * 5 + 3].w,
									  u_Scene.IrradianceBox[v * 5 + 4].w) - 1.0,
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

	const int box = chosen * 5;
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
	vec3 local = (placed + facing * (extents / grid)) / extents;
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
	bool singleCell = false;

	// The anchor, and the step off a buried one described above. Done before
	// the edge fade so the fade measures the sample actually taken.
	{
		const vec3 firstBase = (local * 0.5 + 0.5) * grid;
		const ivec3 firstAnchor = clamp(ivec3(floor(firstBase + 0.5)),
										ivec3(0), ivec3(grid));
		const int firstMask = int(texelFetch(u_IrradianceField,
											 firstAnchor + ivec3(0, 0, tile * 6 + zbase), 0).x + 0.5);
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
	const float edgeFade = clamp(min(fromEdge.x, min(fromEdge.y, fromEdge.z)), 0.0, 1.0);
	if (edgeFade <= 0.0)
		return false;

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
										 anchorCell + ivec3(0, 0, tile * 6 + zbase), 0).x + 0.5);


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
		const vec2 uv = uvw.xy / vec2(texels.xy);
		const float depth = float(texels.z);
		const vec3 square = facing * facing;

		irradiance = vec3(0.0);
		float cubeSky = 0.0;
		for (int axis = 0; axis < 3; axis++)
		{
			const int slot = axis * 2 + (facing[axis] >= 0.0 ? 0 : 1);
			const vec4 stored = textureLod(u_IrradianceField,
					vec3(uv, (uvw.z + float(tile * slot + zbase)) / depth), 0.0);
			irradiance += square[axis] * stored.rgb;
			// **The ambient cube of sky rides in the same fetch.** A face's alpha
			// is the visibility for a surface pointing that way, and the three
			// square weights sum to one for a unit normal -- so this is a convex
			// combination: it cannot leave [0,1], cannot ring, and reconstructs a
			// constant field to that constant from any blend of any cells. No
			// extra texture read and nothing blended that is not linear.
			cubeSky += square[axis] * SkyFromAlpha(stored.a);
		}
		irradiance = max(irradiance, vec3(0.0)) * edgeFade;
		skyVisibility = cubeSky;
		return true;
	}

	const vec3 square = facing * facing;

	// **One cell, unblended** -- the solve's answer where the lookup landed on
	// a buried anchor. See the note at the step above: interpolation is the
	// part that can reach across a wall, so the case with no visibility
	// information to steer it takes the nearest surveyed cell whole.
	if (singleCell)
	{
		const ivec3 one = clamp(ivec3(floor(base + 0.5)), ivec3(0), last);
		float cubeSky = 0.0;
		vec3 here = vec3(0.0);
		for (int axis = 0; axis < 3; axis++)
		{
			const int slot = axis * 2 + (facing[axis] >= 0.0 ? 0 : 1);
			const vec4 stored = texelFetch(u_IrradianceField,
										   one + ivec3(0, 0, tile * slot + zbase), 0);
			here += square[axis] * stored.rgb;
			cubeSky += square[axis] * SkyFromAlpha(stored.a);
		}
		// Aliveness from tile 6's .y -- the alpha lane carries sky now, and a
		// dead cell's alpha is deliberately 1.0 there, so testing it would call
		// every buried cell alive and let its black light back into the blend.
		const bool alive = texelFetch(u_IrradianceField,
								   one + ivec3(0, 0, tile * 6 + zbase), 0).y > 0.5;
		if (!alive)
			return false;   // buried there as well: nothing honest to report
		irradiance = max(here, vec3(0.0)) * edgeFade;
		// Already an average: the three square weights sum to one.
		skyVisibility = clamp(cubeSky, 0.0, 1.0);
		return true;
	}

	vec3 accumulated = vec3(0.0);
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

		// The three faces this normal faces, from this cell.
		float cubeSky = 0.0;
		vec3 here = vec3(0.0);
		// One extra fetch per corner, and it buys the fix: tile 6's .y is
		// the aliveness flag now, so the light's dead-cell skip keeps
		// working while the alpha lane means sky for the hardware filter.
		const bool alive = texelFetch(u_IrradianceField,
						  index + ivec3(0, 0, tile * 6 + zbase), 0).y > 0.5;
		for (int axis = 0; axis < 3; axis++)
		{
			const int slot = axis * 2 + (facing[axis] >= 0.0 ? 0 : 1);
			const vec4 stored = texelFetch(u_IrradianceField,
										   index + ivec3(0, 0, tile * slot + zbase), 0);
			here += square[axis] * stored.rgb;
			cubeSky += square[axis] * SkyFromAlpha(stored.a);
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
		total += weight;
	}

	// Nothing visible: the probe alone is a better answer than a wrong one.
	if (total <= 0.0)
		return false;

	irradiance = max(accumulated / total, vec3(0.0)) * edgeFade;
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

	// Off the surface along its geometric normal, the shadow ray's offset,
	// for the shadow ray's reason.
	float NgdotD = clamp(dot(Ng, direction), 0.0, 1.0);
	float slope = sqrt(1.0 - NgdotD * NgdotD) / max(NgdotD, 0.15);
	float offset = 0.002 * (1.0 + min(slope, 4.0));

	rayQueryEXT q;
	rayQueryInitializeEXT(q, u_SceneAS, RV_RAY_BASE_FLAGS, 0xFFu,
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
#ifdef RV_IRRADIANCE_FILL
		// **The solve shades its hits without the realtime lights.**
		// Params.w carries Light::IsBaked; a light the lighting hash skips
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
		lit += diffuse / PI * lightColor * attenuation * max(dot(hitNormal, L), 0.0) * shadow;
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
		if (VolumeIrradiance(surface.Position, surface.Normal, true, stored, solveSky))
			ambientLight += stored;
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
// **Beckmann, not GGX, for the water's analytic lights -- and anisotropic.**
// Cox and Munk photographed the sun's glitter from the air and fitted the sea
// surface's slope distribution: it is a Gaussian, which is exactly Beckmann's
// microfacet density, where GGX's heavy tails smear the glitter into a haze
// over the whole sea instead of a defined track. And they fitted it *twice*,
// because the variances differ along and across the wind -- which is what
// turns the highlight from a disc into the streak a low light actually lays
// on wind-driven water.
//
// **The roughness here is the RMS slope itself, not squared.** The footprint
// block chose its 0.24 ceiling as Cox-Munk's slope at a fresh breeze, so
// feeding it through the perceptual alpha = r*r convention would collapse the
// horizon variance to a hundredth of the measured sea. The two conventions
// meet at the numbers this shader actually runs -- water stays under 0.3 --
// and the environment split-sum keeps the perceptual mapping it was built
// with, so only these two functions read it this way.
//
// The ratio 1.16 : 0.86 is the square root of Cox-Munk's asymptotic variance
// ratio (3.16 : 1.92 per unit wind), applied about the shared mean so the
// total slope energy the footprint block budgeted is preserved.
float WaterBeckmannD(vec3 H, vec3 N, vec3 T, vec3 B, float roughness)
{
	const float ax = max(roughness * 1.16, 0.02);
	const float ay = max(roughness * 0.86, 0.02);

	const float NdotH = dot(N, H);
	if (NdotH <= 0.0)
		return 0.0;

	const float th = dot(T, H);
	const float bh = dot(B, H);
	const float n2 = NdotH * NdotH;

	const float slope = ((th * th) / (ax * ax) + (bh * bh) / (ay * ay)) / n2;
	return exp(-slope) / (PI * ax * ay * n2 * n2);
}

// Walter's rational fit of the Beckmann Smith shadowing term, one direction,
// with the roughness projected into that direction's azimuth so the term
// matches the anisotropic lobe above rather than an isotropic stand-in.
float WaterBeckmannG1(vec3 v, vec3 N, vec3 T, vec3 B, float roughness)
{
	const float NdotV = dot(N, v);
	if (NdotV <= 0.0)
		return 0.0;

	const float ax = max(roughness * 1.16, 0.02);
	const float ay = max(roughness * 0.86, 0.02);

	const float tv = dot(T, v);
	const float bv = dot(B, v);
	const float azimuth2 = max(tv * tv + bv * bv, 1.0e-8);
	const float alpha = sqrt((tv * tv * ax * ax + bv * bv * ay * ay) / azimuth2);

	const float tanTheta = sqrt(max(1.0 - NdotV * NdotV, 0.0)) / max(NdotV, 1.0e-4);
	const float a = 1.0 / max(alpha * tanTheta, 1.0e-4);
	if (a >= 1.6)
		return 1.0;
	return (3.535 * a + 2.181 * a * a) / (1.0 + 2.276 * a + 2.577 * a * a);
}

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
		vec2 uvW  = wallUv  * u_Layered.UvTransform[i].xy + u_Layered.UvTransform[i].zw;   \
		vec2 ddxW = wallDdx * u_Layered.UvTransform[i].xy;                                 \
		vec2 ddyW = wallDdy * u_Layered.UvTransform[i].xy;                                 \
		mat3 TBN  = TangentFrame(Ngeo, v_WorldPos, uvL);                                   \
		if (w > 0.0)                                                                       \
		{                                                                                  \
			vec4 baseL = u_Layered.BaseColor[i];                                           \
			if ((flags & MAP_BASE_COLOR) != 0)                                             \
			{                                                                              \
				vec4 floorT = textureGrad(LAYER_BASE_COLOR(i), uvL, ddxL, ddyL);           \
				baseL *= wall > 0.0                                                        \
					? mix(floorT, textureGrad(LAYER_BASE_COLOR(i), uvW, ddxW, ddyW), wall) \
					: floorT;                                                              \
			}                                                                              \
			float roughL = u_Layered.Surface[i].y;                                         \
			if ((flags & MAP_ROUGHNESS) != 0)                                              \
			{                                                                              \
				float floorR = textureGrad(LAYER_ROUGHNESS(i), uvL, ddxL, ddyL).r;         \
				roughL *= wall > 0.0                                                       \
					? mix(floorR,                                                          \
						  textureGrad(LAYER_ROUGHNESS(i), uvW, ddxW, ddyW).r, wall)         \
					: floorR;                                                              \
			}                                                                              \
			vec3 nL = Ngeo;                                                                \
			if ((flags & MAP_NORMAL) != 0)                                                 \
				nL = mix(UnpackNormal(TBN,                                                 \
									  textureGrad(LAYER_NORMAL(i), uvL, ddxL, ddyL).xy,    \
									  u_Layered.Surface[i].w), Ngeo, wall);                \
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
	float wall = 1.0 - smoothstep(0.35, 0.78, abs(Ngeo.y));

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

#ifdef RV_WATER
		// The anisotropic Beckmann lobe, in a frame aligned to the wind: the
		// tangent is the wind flattened onto the surface, which is what makes
		// the glitter a streak along it. See the functions for why Beckmann
		// and why the roughness is read as slope.
		vec3 waterT = normalize(vec3(waterWind.x, 0.0, waterWind.y)
								- N * dot(vec3(waterWind.x, 0.0, waterWind.y), N));
		vec3 waterB = cross(N, waterT);

		float NDF = WaterBeckmannD(H, N, waterT, waterB, roughness);
		float G   = WaterBeckmannG1(V, N, waterT, waterB, roughness)
				  * WaterBeckmannG1(L, N, waterT, waterB, roughness);
#else
		float NDF = DistributionGGX(N, H, roughness);
		float G   = GeometrySmith(N, V, L, roughness);
#endif
		vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

		vec3 numerator = NDF * G * F;
		float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
		vec3 specular = numerator / denominator;

		// Energy conservation: what is not reflected is refracted, and metals
		// refract nothing.
		vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

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
#ifdef RV_WATER
		waterSpecular += specular * radiance * NdotL * shadow;
#endif
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

	// **The stored field is read further down**, with the bounce it belongs
	// beside, and not here. It holds bounced light -- the same quantity
	// u_Indirect holds -- so adding it to the flat ambient would put the same
	// light in the picture twice over: measured on the GI corner, once as
	// +5.5 levels of red where a true second bounce is worth +2.2.

	// **Blended between the probes that cover this fragment**, rather than
	// taken wholly from the one the CPU chose for the object. v_Probe is still
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
	if (bounceAnswered < 1.0)
	{
		vec3 storedBounce;
		if (VolumeIrradiance(v_WorldPos, N, true, storedBounce, skyVisible))
			irradiance += storedBounce * (1.0 - bounceAnswered);
	}

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
		if (mirror > 0.0)
		{
			vec3 traced = TraceReflection(v_WorldPos, normalize(v_Normal), reflect(-V, N), v_Probe);

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
			const float kTracedBound = 8.0;
			traced = min(traced, prefiltered * kTracedBound + vec3(0.05));

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
		if (dot(refractedDir, refractedDir) > 1.0e-6)
		{
			TracedSurface behindHit = TraceSurface(v_WorldPos, -N, refractedDir, 300.0);
			vec3 waterT = vec3(0.0);
			vec3 behind = vec3(0.0);
			if (!behindHit.Missed)
			{
				const float through = length(behindHit.Position - v_WorldPos);
				waterT = exp(-waterSigma * through) * (1.0 - foam);
				behind = ShadeTraced(behindHit,
									 ProbeIrradiance(behindHit.Normal, v_Probe));
				// The traced form knows exactly where the bottom is, so the
				// caustic web lands on the real hit point.
				behind *= 1.0 + WaterCaustics(behindHit.Position.xz, through,
											  waterGradient, waterWind, waterTime);
			}
			// Only the *scattered* light gives way to the refracted scene:
			// the glitter is surface reflection and never entered the water,
			// so it rides over clear and murky alike.
			const vec3 scatter = max(transmitted - waterSpecular, vec3(0.0));
			transmitted = behind * waterT + scatter * (1.0 - waterT) + waterSpecular;
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

	o_Accumulate = vec4(transmitted * alpha + reflected, coverage) * weight;
	o_Revealage = coverage;

#else

	// Linear, unbounded, untouched. Tone mapping and the transfer function
	// moved to the tonemap pass, for two reasons: bloom needs these values
	// before the curve compresses them, and every shader that wrote to the
	// screen used to have to agree on the display transform -- which only this
	// one did, so quads and meshes were being shown through different ones.
	o_Color = vec4(color, baseColor.a);

#endif
}

#endif   // !RV_TRACE_ONLY
