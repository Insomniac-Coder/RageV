#include <rvpch.h>
#include "Renderer3D.h"
#include "LightGlow.h"
#include "Renderer.h"
#include "RageV/Renderer/RHI/ShaderCompiler.h"
#include "TextureLoader.h"
#include "TextureHeap.h"
#include "Water.h"
#include "RayShadows.h"
#include "VoxelGI.h"
#include "ShadowMap.h"
#include "EnvironmentIBL.h"
#include "LightGrid.h"
#include "RageV/Core/EngineConfig.h"
#include "Meshlet.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	using namespace RageV::RHI;

	namespace
	{
		// Mirrors GpuLight in pbr.rvshader, std430.
		//
		// There is no cap on how many of these a frame may carry. There was --
		// eight, because the scene's uniform block declared arrays of eight and
		// a uniform block must declare a length. A storage buffer does not.
		struct GpuLight
		{
			Vec4 Position;    // xyz, w = 1 positional / 0 directional
			Vec4 Direction;   // xyz forward axis
			Vec4 Color;       // rgb, a = intensity
			Vec4 Params;      // range, cos(inner), cos(outer), IsBaked
			Vec4 Shadow;      // kind, slot, far, texel scale
		};
		static_assert(sizeof(GpuLight) == 80, "Must match GpuLight in pbr.rvshader");

		// Mirrors RayInstance in pbr_fragment.glsl, std430 (ENGINE-NOTES 7ao):
		// what a ray's hit needs to shade the instance it hit, one per TLAS
		// instance in build order so the hit's custom index is the row.
		// Addresses rather than bindings: the mesh nothing bound is reached
		// through GL_EXT_buffer_reference.
		struct GpuRayInstance
		{
			uint64_t PositionAddress;    // the mesh's vertices, or a posed caster's compute-written positions
			uint64_t AttributeAddress;   // the mesh's vertices: normal and uv live here either way
			uint64_t IndexAddress;
			uint32_t PositionStrideWords;
			uint32_t AttributeStrideWords;
			uint32_t MaterialIndex;      // row of the GpuMaterial table at binding 13
			uint32_t Flags;              // bit 0: posed, bit 1: emitter, bit 2: alpha-tested
			// Below this the traversal loop rejects a candidate triangle.
			// Read only when the masked bit is set, so it costs nothing for
			// the geometry that is simply there.
			float    AlphaCutoff;
			uint32_t _pad1;
			Vec4 BaseColor;
			Vec4 EmissiveColor;
			Vec4 Surface;                // metallic, roughness, occlusion, normal scale
		};
		static_assert(sizeof(GpuRayInstance) == 96, "Must match RayInstance in pbr_fragment.glsl");
		static_assert(offsetof(GpuRayInstance, BaseColor) == 48, "RayInstance vec4s begin at 48");
		constexpr uint32_t kRayInstanceBinding = 15;
		// Where the visible-index buffer binds (roadmap 8.16). Declared by
		// every scene vertex stage through scene_vertex.glsl, so all three
		// lit sets carry it -- unlike the material and ray-instance bindings
		// above, which only the bindless variants declare.
		constexpr uint32_t kVisibleBinding = 17;
		constexpr uint32_t kRayInstancePosed = 1u;
		// This instance is one the area-emitter list answers for, so a
		// hemisphere hit on it must not count its emissive a second time.
		// Per instance rather than "any emitter exists at all", which is what
		// it was: membership is filtered -- by the strength threshold, by the
		// degenerate-rectangle drop, and by the cap -- so the global form
		// subtracted the emissive of surfaces no shadow ray ever aimed at,
		// and their light simply vanished.
		constexpr uint32_t kRayInstanceEmitter = 2u;

		// **This instance is alpha-tested**, so a ray that hits it has to
		// sample the base colour before believing the hit. Set from the
		// material, and paired with the acceleration instance's
		// `ForceNoOpaque` -- the flag tells traversal to *ask*, and this tells
		// the shader what to answer.
		constexpr uint32_t kRayInstanceMasked = 4u;

		// Mirrors the std140 SceneData block in pbr.rvshader.
		struct SceneUniforms
		{
			Mat4 ViewProjection;
			Vec4 CameraPosition;
			// rgb = ambient colour, a = ambient intensity
			Vec4 Ambient;
			// x = environment intensity, y = its highest mip, zw = cos and sin
			// of the sky's rotation
			Vec4 Environment;
			// x = the environment's mip-0 face size, in texels.
			Vec4 EnvironmentSize;

			// xyz = tiles across, tiles down, depth slices. w = how many
			// directional lights sit at the front of the light buffer.
			Vec4 ClusterGrid;
			// x near, y far, zw the scale and bias that map a view depth to a
			// slice.
			Vec4 ClusterDepth;

			// World space straight to shadow lookup coordinates, per cascade.
			Mat4 CascadeLookup[4];
			// Far view-space distance of each cascade, for selection.
			Vec4 CascadeSplits;
			// World size of one texel in each, for normal-offset bias.
			Vec4 CascadeTexel;
			// The camera's forward axis: cascade selection needs a view depth
			// and the shader has no view matrix, only a view-projection.
			Vec4 CameraForward;
			// x = cascades rendered (0 = no shadows), y = normal offset scale,
			// z = one cascade texel in lookup coordinates, w unused
			Vec4 ShadowParams;

			Mat4 SpotLookup[4];

			int32_t   LightCount;
			int32_t   _padding[3];

			// Last frame's ViewProjection, for motion vectors. Appended so
			// every offset above is unchanged -- this struct is mirrored by
			// hand in include/scene_vertex.glsl, and the two disagreeing is a
			// wrong picture rather than a failed build. ENGINE-NOTES 7r.
			Mat4 PreviousViewProjection;

			// xy = this frame's sub-pixel offset in NDC, zw = last frame's.
			//
			// Both, because both projections above carry one and the velocity
			// is the difference of the two. Subtracting only the current one
			// would leave last frame's offset in every motion vector, which is
			// half a pixel of velocity on a scene where nothing moved.
			Vec4 Jitter;

			// Screen-space reflections, read from last frame's trace inside
			// the lighting. x = intensity, zero when there is nothing to read;
			// y = the sign that takes an NDC y-offset into the trace texture's
			// row direction (-1 on the backend whose row 0 is the top). zw
			// unused. ENGINE-NOTES 7af.
			Vec4 ScreenReflections;

			// Ray-traced global illumination (ENGINE-NOTES 7at). x = the
			// profile's intensity, zero when the traced form is not running;
			// y = a frame counter the bounce rays hash, so each frame draws a
			// different four directions and TAA has something to average. zw
			// unused.
			//
			// **A field added here must be added to EVERY shader that mirrors
			// this block** -- pbr_fragment.glsl *and* scene_vertex.glsl. On
			// Vulkan a stage that declares fewer fields is harmless; on
			// OpenGL the two stages link into one program, and one uniform
			// block declared two ways is a layout the linker resolves as it
			// likes. Adding this to the fragment mirror alone made OpenGL
			// render *differently on every run* -- two identical runs differed
			// by 67 levels -- which is what check_ssao's "off is off to the
			// byte" caught. See 7at.
			Vec4 GlobalIllumination;

			// Indirect diffuse from last frame's buffer (ENGINE-NOTES 7av).
			// x = intensity, zero when nothing is bound; y = the row sign,
			// exactly as ScreenReflections carries it; zw unused.
			//
			// REMINDER, and 7at paid for it: this block is mirrored BY HAND in
			// include/pbr_fragment.glsl AND include/scene_vertex.glsl. A field
			// added to one only is undefined on OpenGL, which links the stages
			// into one program -- and it does not fail, it renders differently
			// every run.
			Vec4 Indirect;

			// Mirrors the block in scene_block.glsl and pbr_fragment.glsl --
			// both, by hand, because OpenGL links the two stages into one
			// program and two spellings of one uniform block is undefined
			// ground. See the note there.
			// xyz the field's centre, w unused; and its half-extents, with w
			// carrying **how many volumes the atlas holds** -- zero means every
			// reader falls back to the flat ambient. It was a nought-or-one
			// flag while a scene had one composed field; a count reads the same
			// way at zero and says more above it.
			Vec4 IrradianceCentre{ 0.0f };
			Vec4 IrradianceExtents{ 1.0f, 1.0f, 1.0f, 0.0f };
			// The rows of the field's inverse rotation. Mirrored by hand in
			// scene_block.glsl and pbr_fragment.glsl, as everything in this
			// block is.
			// **Every volume in the atlas, one box each.** Five rows apiece,
			// laid out so a reader can reject a box on the first two and only
			// pay for the rest once it has found the one it is standing in:
			//
			//   0  centre.xyz          | z offset into each tile
			//   1  half-extents.xyz    | metres between cells
			//   2  axis X.xyz          | cells across
			//   3  axis Y.xyz          | cells up
			//   4  axis Z.xyz          | cells deep
			//
			// The same shape ProbePlacement takes above and for the same
			// reason: the selection is per fragment, so the table has to be
			// somewhere a fragment can read cheaply.
			Vec4 IrradianceBox[Renderer3D::kMaxIrradianceVolumes * 5]{};
			Vec4 ProbeCount{ 0.0f };
			Vec4 ProbePlacement[15]{};
			Vec4 ProbeSlot[15]{};

			// WR-17: shadow rays thin with distance. x = the falloff shape,
			// y = start, z = end, w = the share floor. Appended, so every
			// offset above is unchanged; mirrored in scene_block.glsl and
			// pbr_fragment.glsl.
			Vec4 ShadowRayFade{};
		};

		// Where a batch starts in the instance buffer. The model matrix used to
		// be here, one push per object; it is per instance now, and this is all
		// that is left.
		struct ObjectPushConstants
		{
			int32_t BaseInstance;
		};
		static_assert(sizeof(ObjectPushConstants) == 4, "Push constant block must stay within 128 bytes");

		// The water pipeline's, which is the one above plus the body's dials.
		//
		// **The three pad words are not optional.** A vec4 aligns to sixteen
		// bytes in the shader's block; without them the C++ struct would put
		// the first colour at offset 4 and the shader would read it from 16, so
		// every dial would land one lane out -- foam would be read as the wave
		// direction and the sea would look broken in a way that points at the
		// shader rather than at this struct.
		struct WaterPushConstants
		{
			int32_t BaseInstance;
			int32_t Pad0 = 0;
			int32_t Pad1 = 0;
			int32_t Pad2 = 0;
			Vec4 Shallow;
			Vec4 Deep;
			Vec4 Wave;
			Vec4 Extra;
			Vec4 Size;
		};
		static_assert(sizeof(WaterPushConstants) == 96,
					  "Must match ObjectData under RV_WATER in scene_block.glsl, "
					  "and stay within the 128 bytes every device guarantees");

		// The skinned depth pass needs one more: where this caster's bones sit.
		//
		// Two structs rather than one with an unused member, because an unused
		// push constant is optimised out of the shader and the reflected range
		// shrinks with it -- writing eight bytes into a four-byte range is a
		// validation error rather than a harmless extra.
		struct SkinnedShadowPushConstants
		{
			int32_t BaseInstance;
			int32_t BoneBase;
		};
		static_assert(sizeof(SkinnedShadowPushConstants) == 8, "Must match shadow_depth_skinned.rvshader");

		// Mirrors InstanceData in pbr.rvshader, std430.
		struct InstanceData
		{
			Mat4 Model;

			// The same matrix on the previous frame. The difference between
			// where a vertex projects under the two is the motion vector, and
			// it belongs per instance for the same reason Model does: two
			// objects moving differently still batch into one instanced draw.
			//
			// Equal to Model for anything that did not move, which is most of
			// a scene, and produces exactly zero velocity there rather than
			// something small and wrong.
			Mat4 PreviousModel;
			// transpose(inverse(mat3(Model))), as a mat4 because std430 pads a
			// mat3 to the same size anyway and a mat4 has no surprises in it.
			// Computed once per instance rather than once per vertex, which is
			// where it used to happen.
			Mat4 NormalMatrix;
			Vec4 BaseColor;
			Vec4 EmissiveColor;
			// metallic, roughness, occlusion, normal scale
			Vec4 Surface;
			// x = where this instance's bones start in the bone buffer, zero
			// for anything the skinned pipeline does not draw.
			// y = which cube of the reflection-probe arrays this object
			//     reflects. Slot 0 is the sky, so zero is a real answer and
			//     not a missing one.
			//
			// Per instance, and that is the point: the probe rides along with
			// the model matrix, so two objects choosing different probes still
			// batch into one instanced draw. Selecting per draw instead would
			// mean the probe had to be part of the sort key, and a run would
			// split every time the answer changed.
			Vec4 Indices{ 0.0f };
		};
		static_assert(sizeof(InstanceData) == 256,
					  "Must match InstanceData in include/scene_vertex.glsl");

		// One submitted mesh, held until EndScene can sort them.
		//
		// Drawing immediately is what made every mesh its own draw call. The
		// order objects arrive in is the registry's, which is neither grouped
		// by mesh nor sorted by depth, so nothing can be batched without first
		// having all of them.
		// Which of the lit pipelines a draw needs. Static and skinned differ in
		// vertex layout; layered (ENGINE-NOTES 7aq) in what set 1 holds. A run
		// is entirely one kind, and the sort puts the kinds in this order.
		// **Water is a fourth kind and not a fourth material.** The kind is the
		// pipeline -- that is what this enum has always meant -- and water needs
		// its own because its vertices move: the grid is displaced into waves in
		// the vertex stage. Nothing below the rasteriser differs, which is why
		// it is a kind rather than a second renderer.
		enum class DrawKind : uint8_t { Static, Skinned, Layered, Water };

		// Which pass a draw belongs to. Ordered: the sort packs this in the
		// key's top bits, so the values *are* the drawing order.
		enum class DrawBucket : uint8_t { Opaque, Masked, Blended };

		struct PendingDraw
		{
			// Sort key: the bound state a draw needs. Meshes first because
			// changing vertex buffers is the more expensive of the two, and
			// materials within a mesh so a run is contiguous in both.
			const Mesh* MeshKey = nullptr;
			uint64_t MaterialKey = 0;
			// First in the sort key: the three kinds are three pipelines, and
			// a run has to be one of them.
			DrawKind Kind = DrawKind::Static;

			// **Above the kind in the sort key**, so each bucket lands in one
			// contiguous block and the opaque pass can simply stop where the
			// blended one begins. Nothing else about the record differs -- the
			// same instance table, the same material, the same batching --
			// which is what keeps the second pass a second loop rather than a
			// second renderer.
			//
			// **The order of the values is the drawing order and is load-
			// bearing.** Opaque first; then Masked, so every cutout tests
			// against a depth buffer the opaque geometry has already filled and
			// its discarded fragments cost nothing; then Blended last, over a
			// finished opaque image.
			//
			// This was a bool until cutouts existed, and every site that asked
			// about it asked `!= Opaque` -- exact for two modes and silently
			// wrong for three, since it would have routed every cutout into the
			// blended pass and stopped it writing depth.
			DrawBucket Bucket = DrawBucket::Opaque;

			Ref<Mesh> MeshRef;
			Ref<Material> MaterialRef;
			// The layered kind's set 1, which binds itself; null otherwise.
			Ref<LayeredMaterial> LayeredRef;
			// The water kind's dials, pushed with the draw. Carried on every
			// record so one kind can use them, which is the price of keeping
			// the record a single type; the alternative is a side table keyed
			// by draw index and a second thing to keep in step with the sort.
			Renderer3D::WaterDraw Water;
			// How many of the mesh's indices this draw covers, from the first:
			// the mesh's count for everything but a terrain chunk drawn without
			// its skirts (7ap). Part of what a run must agree on.
			uint32_t IndexCount = 0;
			// **Where this draw's instance data lives, not the data itself**
			// (roadmap 8.16). It used to be the 256-byte InstanceData inline,
			// which made this record 336 bytes -- and both of the sorts below
			// move these records, so every comparison swap shifted a third of
			// a kilobyte. At sixty thousand objects the front-to-back sort
			// alone shuffled about twenty megabytes and cost 12.2 ms of CPU to
			// save 0.6 ms of GPU.
			//
			// An index instead. The record is a quarter of the size, the sorts
			// move a quarter as much, and the instance data does not move at
			// all -- it is read once, in sorted order, when the buffer is
			// filled. Every draw has one, so this is never invalid.
			uint32_t Instance = 0;
			// Distance from the eye, for ordering batches front to back.
			float ViewDepth = 0.0f;
		};

		// One draw's place in the order, as the sort sees it: everything the
		// comparison needs, and the index of the record it belongs to. Sorted
		// as a flat array of these, so no comparison touches a PendingDraw.
		struct SortEntry
		{
			uint64_t Key = 0;
			uint32_t Index = 0;
		};

		// A maximal span of Pending sharing one pipeline, mesh and material --
		// exactly what becomes one instanced draw. Ordering these rather than
		// the draws inside them is what lets depth sorting and batching
		// coexist.
		struct DrawRun
		{
			size_t Begin = 0;
			size_t End = 0;
			// The nearest member, which is the one whose depth writes would
			// occlude the most.
			float Nearest = 0.0f;
		};

		// The depth pass has no material, so its only key is the mesh.
		struct PendingShadowDraw
		{
			const Mesh* MeshKey = nullptr;
			Ref<Mesh> MeshRef;
			// Light view-projection times model, already multiplied out.
			Mat4 LightMVP{ 1.0f };
			bool Skinned = false;
			// Where this caster's bones start. -1 when it has none.
			int32_t BoneBase = -1;

			// **Null unless this caster is alpha-tested.** A depth pass has no
			// surface properties and wants none -- except that a cutout's
			// shape is its geometry minus its alpha, so the one masked caster
			// in a scene has to carry the material whose alpha says which
			// parts of it are there. Non-null is also what routes the draw to
			// the masked pipeline, so the two cannot disagree.
			const Material* MaterialKey = nullptr;
			Ref<Material> MaterialRef;
		};

		struct Renderer3DData
		{
			RHIDevice* Device = nullptr;

			Ref<RHIShader>   Shader;
			Ref<RHIPipeline> Pipeline;
			// The same lighting through a wider vertex. Its own pipeline
			// because the vertex layout differs, and its own shader because the
			// vertex stage does; everything below that is a shared include.
			Ref<RHIShader>   SkinnedShader;
			Ref<RHIPipeline> SkinnedPipeline;
			// The same lighting over a surface assembled from four layers
			// (7aq): the static vertex stage, and a fragment stage whose set 1
			// is the layered block and its samplers instead of a material's.
			Ref<RHIShader>   LayeredShader;
			Ref<RHIPipeline> LayeredPipeline;
			// The same lighting written into the accumulate and revealage pair
			// instead of the colour target, for materials whose blend mode is
			// not Opaque. Drawn in a pass of its own, after everything opaque.
			Ref<RHIShader>   TransparentShader;
			Ref<RHIPipeline> TransparentPipeline;

			// Water's own transparent pipeline: the same lighting again, over a
			// vertex stage that displaces the grid into waves. There is no
			// opaque twin on purpose -- water that is not see-through is a dark
			// floor, which is the thing this exists to stop being.
			Ref<RHIShader>   WaterShader;
			Ref<RHIPipeline> WaterPipeline;

			// **The same lit shader with the cutout test compiled in**, for
			// alpha-tested materials. A separate variant and not a uniform
			// branch: `discard` present in a shader costs early-z whether or
			// not it is reached, so compiling it into the shared opaque shader
			// would charge every opaque surface in the frame -- and undo the
			// depth prepass. Masked geometry is its own bucket, its own run,
			// and its own pipeline; the opaque shader stays clean.
			Ref<RHIShader>   MaskedShader;
			Ref<RHIPipeline> MaskedPipeline;
			// Where the transparent block begins in Pending, which the sort put
			// at the end. Equal to the size when there is nothing blended,
			// which is every scene this project had until the showroom.
			uint32_t         TransparentBegin = 0;

			// The blended table's cull result, held until the transparent pass
			// runs -- the same arrangement DrawSceneIndirect has with the
			// opaque pass, and for the same reason: the instance table cannot
			// be filled until every submission is in.
			GpuCull::View                TransparentView;
			std::vector<GpuCull::Slot>   TransparentSlots;
			// The traced bounce, as a fullscreen pass rather than part of the
			// lit fragment (7bs). The same set 0 and the same material heap --
			// it includes the lit shader with RV_TRACE_ONLY -- plus a set of
			// its own holding the depth and surface it reconstructs from.
			Ref<RHIShader>   GiShader;
			Ref<RHIPipeline> GiPipeline;

			// The depth prepass: the lit pipeline's twin with an empty
			// fragment stage and colour writes masked off. Null when the
			// shader is absent, which is the whole of the on/off switch --
			// nothing else has to be gated, because the draw below asks for
			// the pipeline and skips when there is not one.
			Ref<RHIShader>   PrepassShader;
			Ref<RHIPipeline> PrepassPipeline;
			Ref<RHIShader>   IrradianceFillShader;
			Ref<RHIPipeline> IrradianceFillPipeline;
			// The target the fill rasterises into, and the set its outputs are
			// bound through. Both follow the largest field solved so far and
			// stay there, so a steady scene stops allocating.
			Ref<RHIRenderTarget> IrradianceFillTarget;
			uint32_t IrradianceFillWidth = 0;
			uint32_t IrradianceFillHeight = 0;
			// One set per frame in flight, and that is not tidiness: the set is
			// rewritten with the field being solved, and a descriptor set may
			// not be rewritten while a command buffer still refers to it. A
			// scene whose lighting changes every frame -- a moving light --
			// asks for a solve every frame, which is exactly the case that
			// would rewrite one still in use.
			std::vector<Ref<RHIResourceSet>> IrradianceFillSets;
			uint32_t IrradianceFrame = 0;

			// The emissive rectangles the bounce aims at, as the shader reads
			// them: centre and area, the two half-extents, the radiance, and
			// -- when the surface's light is not spread evenly over it -- the
			// map to aim by. Mirrors GiEmitter in rtgi_trace.rvshader, std430.
			struct GpuEmitter
			{
				Vec4 CentreArea{ 0.0f };
				Vec4 TangentU{ 0.0f };
				Vec4 TangentV{ 0.0f };
				Vec4 Radiance{ 0.0f };
				// x = where this emitter's aiming table starts in the shared
				// buffer, y = cells a side (zero: radiate evenly, and the
				// three rows here are unread), z = the map's heap slot.
				Vec4 Aim{ 0.0f };
				// Texture uv back to the rectangle's (su, sv); see
				// AreaEmitter::UvToSurface.
				Vec4 UvToSurface0{ 0.0f };
				Vec4 UvToSurface1{ 0.0f };
			};
			static_assert(sizeof(GpuEmitter) == 112,
						  "Must match GiEmitter in rtgi_trace.rvshader");
			std::vector<GpuEmitter> Emitters;
			// The owners of the rows above, in the same order. Not uploaded:
			// this is how a ray instance is told it is one of them.
			std::vector<uint64_t> EmitterOwners;
			// Every aiming table this frame's emitters use, end to end; a row
			// names its own by offset and side. One buffer rather than one
			// per emitter because sixteen bindings would be sixteen
			// descriptors for four kilobytes each.
			std::vector<float> EmitterCdf;

			// The probes the traced bounce may land inside, as the shader
			// reads them. Mirrors GiProbe in rtgi_trace.rvshader, std430.
			struct GpuProbe
			{
				// xyz where it stands, w how far it reaches.
				Vec4 Placement{ 0.0f };
				// x its slot in the irradiance array; the rest unused.
				Vec4 Slot{ 0.0f };
			};
			static_assert(sizeof(GpuProbe) == 32,
						  "Must match GiProbe in rtgi_trace.rvshader");
			std::vector<GpuProbe> Probes;

			// The scene's irradiance field and the box it covers. Held rather
			// than passed through because BeginScene clears the block these
			// bounds live in, so the copy has to happen on its far side.
			Ref<IrradianceVolume> Irradiance;
			// Every volume's box, guarded against degeneracy, in the order
			// they sit in the atlas. Uploaded into the scene block's
			// IrradianceBox table at BeginScene.
			std::vector<IrradianceVolume::Region> IrradianceRegions;

			// **A field waiting to be solved.** The two halves of the job are
			// pinned to opposite ends of the frame: sizing has to happen before
			// BeginScene, because the block carrying the box's bounds is
			// uploaded there, and the solve has to happen after the scene pass,
			// because it traces and nothing it traces against exists until
			// then. So the scene walk records the request and the frame graph's
			// fill pass runs it.
			//
			// It stands until a pass actually solves it, which is what lets a
			// first frame with no traced set defer rather than solve against
			// nothing and call the field done.
			Ref<IrradianceVolume> PendingIrradiance;
			// Which region of the atlas this frame is solving. The sweep is the
			// outer loop and this the inner one, deliberately: the swap that
			// ends a sweep flips the *whole* texture, so a region solved to
			// completion before its neighbours started would leave theirs
			// stale in whichever buffer ended up in front.
			uint32_t PendingIrradianceRegion = 0;
			// **How far through the field this solve has got.** A band of rows
			// a frame rather than the whole grid at once: solving a large field
			// in one pass is a hitch exactly when a scene loads, which is the
			// worst moment to have one. The row is into the unrolled grid, so
			// it is a run of (y, z) pairs.
			Mat3 PendingIrradianceRotation{ 1.0f };
			uint32_t PendingIrradianceRow = 0;
			// Sweeps finished. The first replaces what a field holds and the
			// rest converge onto it, which is what buys both a mean over more
			// rays than one pass can afford and a bounce of history per sweep.
			uint32_t PendingIrradianceSweep = 0;
			// The quality the volumes asked for, held with the request because
			// the solve runs frames after the ask.
			uint32_t PendingIrradiancePasses = 4;
			// Whether the pending solve's rays read the previous sweep's field
			// at their hits -- the traced flavour's bounces -- or not, the
			// screen flavour's converged single bounce.
			bool PendingIrradianceFeedback = false;
			uint32_t PendingIrradianceRays = 512;
			// **The runtime cache: the same solve, never finishing.**
			//
			// A bake solves a field to convergence and stops. A cache keeps
			// sweeping forever at a small fixed cost, so the stored light
			// follows a scene whose lights move -- and, the reason it is worth
			// having here, so the *frame* stops paying for indirect light per
			// pixel. Rays update cells; pixels interpolate cells. The ray count
			// is then a property of the grid and the budget, not of the screen
			// resolution, which is the decoupling every shipping engine makes
			// and the one this renderer did not.
			//
			// Continuous mode changes exactly three things about the solve
			// below: how many rays a frame may spend, what it blends with, and
			// whether finishing a sweep ends it.
			bool PendingIrradianceContinuous = false;
			// Rays a frame, runtime. Four orders below the bake's megaray: a
			// bake stretches a frame nobody is watching, this one is inside the
			// frame being looked at. Fixed, so it is budgetable -- the whole
			// point.
			uint32_t PendingIrradianceRayBudget = 1u << 16;
			// How much of a fresh estimate a cell takes each time it is
			// revisited. Small, because stability is the product: a cell is
			// revisited every time the sweep comes round, so a low weight still
			// converges quickly in wall-clock while never stepping visibly.
			// This is the one number that trades response time for stillness.
			float PendingIrradianceHysteresis = 0.05f;
			// The following cache's box as of this frame. The volume's own
			// region list is fixed at creation, and rebuilding the volume to
			// move it would throw away every cell solved so far -- seven frames
			// of darkness every time the view crossed a threshold. So the box
			// travels here instead and the texture stays put.
			IrradianceVolume::Region PendingIrradianceRegion2{};
			// Which field the line below was last written about, so a scene
			// whose lighting changes every frame -- a moving light -- says it
			// once rather than filling the log. Compared and never dereferenced.
			const IrradianceVolume* AnnouncedIrradiance = nullptr;
			Format           GiTargetColor = Format::Undefined;
			Ref<RHISampler>  PointSampler;
			Format TargetColor = Format::R8G8B8A8_UNORM;
			Format TargetDepth = Format::D32_SFLOAT;
		// Sample count, which has to equal the target's. A pipeline whose
		// rasterizationSamples disagrees with the attachment it draws into is
		// undefined behaviour rather than an error, so it travels with the
		// formats and gets compared with them.
			// The previous view-projection and its jitter used to live here,
			// one pair for the process. They are per frame chain and now live
			// with the history they reproject into -- CameraMotion, reached
			// through Renderer::GetCameraMotion(). See BeginScene.
			uint32_t TargetSamples = 1;

		// Where the scene writes its motion vectors, or Undefined for a pass
		// that has no velocity attachment bound.
		//
		// One shape for every pass that writes the scene target -- the scene
		// pass and the overlay both bind {colour, velocity}. 7q paid for the
		// alternative: pipelines built for one target shape being bound into a
		// pass with another is undefined behaviour rather than an error.
			Format TargetVelocity = Format::Undefined;
			Format TargetNormal = Format::Undefined;
			// Traced indirect diffuse (7av). Undefined everywhere the
			// scene target does not carry it.
			Format TargetIndirect = Format::Undefined;
			bool   PipelineDirty = true;
			bool   Wireframe = false;

			// One per scene *within* a frame, not one per frame in flight.
			//
			// A draw reads the scene block when the GPU runs it, not when it is
			// recorded, so two scenes in one frame -- the editor viewport and
			// the game viewport -- need separate blocks. Sharing one meant the
			// second BeginScene overwrote the view-projection the first
			// viewport's draws were about to use.
			struct SceneSlot
			{
				Ref<RHIBuffer>      Buffer;
				Ref<RHIResourceSet> Set;
				// The same set 0 again, differing in one binding: the visible
				// indices come from the cull pass rather than from the CPU.
				// A second set rather than a rebind, because both are used in
				// the same render pass and a descriptor set may not be
				// rewritten while a command buffer still refers to it.
				Ref<RHIResourceSet> GpuSet;
				// The traced bounce reads the same scene block through a set of
				// its own: a set is allocated against one pipeline's layout,
				// and this pipeline's set 0 is a subset -- no shadow maps, no
				// cluster grid, no instance stream, because a hit is not on
				// screen and has no cluster.
				Ref<RHIResourceSet> GiSet;
				// Its depth and surface inputs, which change per view rather
				// than per scene.
				Ref<RHIResourceSet> GiInputs;
				// The prepass's set 0. Its own object because its layout is a
				// subset of everything else's: the prepass shader has no
				// fragment stage worth the name, so only the three bindings
				// the *vertex* stage reads survive reflection.
				Ref<RHIResourceSet> PrepassSet;
				// The emissive rectangles, uploaded per frame. On the GI pass's
				// own set rather than set 0, which four other shaders reflect
				// and which the comment on GiInputs is about.
				Ref<RHIBuffer> GiEmitters;
				uint32_t GiProbeCapacity = 0;
				Ref<RHIBuffer> GiProbes;
				uint32_t GiEmitterCdfCapacity = 0;
				Ref<RHIBuffer> GiEmitterCdf;
				uint32_t GiEmitterCapacity = 0;
				// The batch's per-instance array. Grows to the largest scene
				// this slot has drawn and stays there, so a steady scene stops
				// allocating after its first frame.
				Ref<RHIBuffer>      Instances;
				uint32_t            InstanceCapacity = 0;
				// Which row of the array above each drawn instance reads, in
				// draw order (roadmap 8.16). Four bytes a draw against the
				// 256 the reordering used to move.
				Ref<RHIBuffer>      Visible;
				uint32_t            VisibleCapacity = 0;
				// Every light in this scene, however many that is.
				Ref<RHIBuffer>      Lights;
				uint32_t            LightCapacity = 0;
				// The cluster grid: a range per cell, and the indices those
				// ranges point into.
				Ref<RHIBuffer>      Cells;
				uint32_t            CellCapacity = 0;
				Ref<RHIBuffer>      CellIndices;
				uint32_t            CellIndexCapacity = 0;
				// Every skinned instance's bones, back to back. One buffer for
				// the scene rather than one per character: forty characters
				// would otherwise want forty bindings.
				Ref<RHIBuffer>      Bones;
				uint32_t            BoneCapacity = 0;
				// The skinned pipeline's layout declares one binding the static
				// one does not -- the bones -- so it needs a set of its own.
				// Writing a binding a shader never declared is the hazard
				// recorded in HANDOFF section 5, and the validation layer says
				// so immediately.
				Ref<RHIResourceSet> SkinnedSet;
				// And the layered pipeline's, for the same reason: its layout is
				// set 0 of a different pipeline object, and a set is allocated
				// against one layout.
				Ref<RHIResourceSet> LayeredSet;
				// **And the masked pipeline's**, whose layout is identical to
				// the static one -- the cutout variant adds a test, not a
				// binding. Identical is not the same as interchangeable: a set
				// is allocated against a pipeline, and OpenGL resolves its
				// bindings against that program rather than against a layout.
				// Reusing the static set here drew every cutout black on that
				// backend while Vulkan, where compatible layouts really are
				// interchangeable, showed nothing wrong at all.
				Ref<RHIResourceSet> MaskedSet;
				// The GPU-culled path's twin of it: those draws read their
				// instances through a different binding, and a masked one reads
				// them through a different pipeline.
				Ref<RHIResourceSet> MaskedGpuSet;
				// **And one for the transparent pipeline**, which is a fourth
				// layout and therefore a fourth set. Sharing the opaque one
				// looked like it worked -- the geometry drew, the lighting was
				// right -- and quietly returned nothing for the bindings past
				// the basics, so blended surfaces reflected pure black. A
				// windscreen with no reflection in it reads as a material
				// problem, which is where two hours went.
				Ref<RHIResourceSet> TransparentSet;
				// One per distinct static mesh drawn as meshlets this frame:
				// set 3 holds the cut and the vertices, which change per
				// mesh, and a set rewritten under a recorded bind invalidates
				// the command buffer. The shadow path keeps an identical pool
				// for the identical reason.
				std::vector<Ref<RHIResourceSet>> MeshletSets;
				uint32_t MeshletCursor = 0;
				// The water pipeline's set 3 -- backdrop, foam and the two
				// generated tiles. One per water *run*, pooled with a cursor
				// exactly like the meshlet sets and for the same reason: the
				// foam buffer differs per body, and a set rewritten under a
				// recorded bind invalidates the command buffer.
				std::vector<Ref<RHIResourceSet>> WaterSets;
				uint32_t WaterSetCursor = 0;
				// And the transparent pipeline's *GPU-driven* set: the same
				// instance table read through the indices the blended cull
				// wrote instead of the ones the sort produced. One binding
				// apart from TransparentSet, which is the whole difference
				// between the two paths at draw time.
				Ref<RHIResourceSet> TransparentGpuSet;
				// One GpuMaterial per distinct material this scene drew, on the
				// bindless path only (ENGINE-NOTES 7al). Rebuilt every frame;
				// materials x 64 bytes, and it means no second free list.
				Ref<RHIBuffer>      Materials;
				uint32_t            MaterialCapacity = 0;
				// The ray-instance table (7ao), written when reflections trace.
				Ref<RHIBuffer>      RayInstances;
				uint32_t            RayInstanceCapacity = 0;
			};

			// [frame in flight][scene within the frame]
			std::vector<std::vector<SceneSlot>> SceneSlots;
			uint32_t SceneCursor = 0;

			// The same idea for the depth pass, which needs one per shadow
			// render rather than one per scene: a frame opens a shadow pass per
			// cascade, per spot light and per face of every point light's cube,
			// and each reads its own instances when the GPU gets to it.
			struct ShadowSlot
			{
				Ref<RHIBuffer>      Instances;
				Ref<RHIResourceSet> Set;
				uint32_t            InstanceCapacity = 0;
				// The depth pass needs the same bones the lit pass used, or a
				// character casts the shadow of its bind pose.
				Ref<RHIBuffer>      Bones;
				uint32_t            BoneCapacity = 0;
				Ref<RHIResourceSet> SkinnedSet;
				// And one for the masked pipeline. Its set 0 is the same
				// instance buffer the opaque pass reads -- only set 1 and the
				// fragment stage differ -- but a set is allocated against a
				// pipeline, so it cannot be the same object.
				Ref<RHIResourceSet> MaskedSet;
				// One per distinct static mesh drawn as meshlets this pass --
				// the meshlet buffers change per mesh, and a set rewritten
				// under a recorded bind invalidates the command buffer
				// (HANDOFF section 5). Pooled with a cursor exactly like the
				// slots themselves.
				std::vector<Ref<RHIResourceSet>> MeshletSets;
				uint32_t            MeshletCursor = 0;
			};

			std::vector<std::vector<ShadowSlot>> ShadowSlots;
			uint32_t ShadowCursor = 0;

			// The depth pass's *culled* form needs a set and nothing else:
			// the instances it binds were written by the cull pass and live in
			// device memory, so there is no buffer here to grow and no upload
			// to make. Its own ring rather than a ShadowSlot's, which would
			// hand out a host-visible instance buffer per view that nothing
			// would ever write.
			std::vector<std::vector<Ref<RHIResourceSet>>> CulledShadowSets;
			uint32_t CulledShadowCursor = 0;

			// The slot BeginScene took, so EndScene reaches the same one
			// without recomputing an index from the cursor -- which is off by
			// one the moment BeginScene returns early.
			SceneSlot* ActiveScene = nullptr;

			// Built in BeginScene, uploaded there too.
			std::vector<GpuLight> LightScratch;
			// Original light indices, directional first. The shadow assignment
			// is keyed on the original order and has to be undone through this.
			std::vector<uint32_t> LightOrder;
			LightList Ordered;
			LightGrid Grid;

			// How many rows at the head of the instance pool belong to the
			// cull table rather than to a pending draw (roadmap 8.3). The CPU
			// path's own instances start after them.
			uint32_t ReservedInstances = 0;
			// The camera's culled result and the slot table that names each
			// slot's mesh, recorded by DrawSceneIndirect and issued by
			// EndScene once the instance table is uploaded and bound.
			//
			// The slot table is **copied, not borrowed**. It is a handful of
			// entries -- one per distinct mesh -- against the risk of holding
			// a pointer into a vector that Scene rebuilds on every draw-list
			// refresh, which is a dangling read the moment anything refreshes
			// between the two calls.
			GpuCull::View IndirectView;
			std::vector<GpuCull::Slot> IndirectSlots;

			// Accumulated between BeginScene and EndScene, then sorted.
			std::vector<PendingDraw> Pending;
			// The instance data those draws point into, in submission order
			// and never reordered. A draw finds its row by index, and the
			// index survives the sorts because it travels inside the record
			// being sorted.
			std::vector<InstanceData> Instances;
			// One row per draw, in the order the sorts settled on, naming the
			// instance each draw reads. Kept between frames for the allocation.
			std::vector<uint32_t> VisibleScratch;
			// The frame's material records and which record each material got,
			// bindless path only. Kept between frames for the allocation.
			std::vector<GpuMaterial> MaterialScratch;
			std::unordered_map<const Material*, uint32_t> MaterialIndex;
			// Kept between frames so the front-to-back reorder allocates
			// nothing on a stable scene.
			std::vector<DrawRun> Runs;
			std::vector<PendingDraw> SortScratch;
			// The permutation both sorts are decided over, and the two scratch
			// arrays they need. Indices and keys rather than records: see the
			// sorting section of EndScene.
			std::vector<uint32_t> SortOrder;
			std::vector<uint32_t> OrderScratch;
			std::vector<SortEntry> SortEntries;
			// Compact ids for the packed key, rebuilt each scene. Small: a
			// scene has a handful of each however many objects it has.
			std::vector<const Mesh*> MeshIds;
			std::vector<uint64_t> MaterialIds;
			// Said once, not per frame, when a scene has more of either than
			// the key's fifteen bits can number.
			bool PackWarned = false;
			// Said once per session, like the packed-key fallback above: a
			// non-static mesh wearing a blended material has no transparent
			// pipeline to draw it.
			bool SkinnedBlendWarned = false;

			// The depth pass carries only a matrix per caster.
			std::vector<PendingShadowDraw> ShadowPending;
			std::vector<Mat4> ShadowScratch;

			// Bone matrices for this scene and this shadow pass. Appended to as
			// skinned meshes are submitted; each draw remembers where its own
			// run began.
			std::vector<Mat4> BoneScratch;
			std::vector<Mat4> ShadowBoneScratch;

			Ref<Material> DefaultMaterial;

			// The bindless texture heap, or null on the bound path. Which path
			// this is was decided once, in Init, from the caps and --bindless:
			// the shaders were compiled for it, so it cannot change afterwards.
			std::unique_ptr<TextureHeap> Heap;
			bool Bindless = false;

			// Whether the lit shaders were compiled with RV_RAY_SHADOWS. Unlike
			// the heap this changes at runtime -- it is a project setting -- so
			// the shaders are recompiled and the pipelines rebuilt when it does.
			bool RayShadowsOn = false;
			bool RayReflectionsOn = false;
			// The sky's visibility, traced per pixel rather than baked. The
			// count is the dial: 0 is off, and 2/4/8 are Quarter/Half/Full.
			// Stored as the number the shader wants rather than the enum, so
			// the define below is one conversion and not two.
			int RaySkyVisibilityRays = 0;
			// The traced bounce (7at): same structures, same heap, its own
			// define, so it recompiles the lit shaders the way the other two
			// do.
			bool RayGlobalIlluminationOn = false;
			// Water's traced refraction: recompiles only the transparent pair
			// (RV_RAY_REFRACTION goes on both so their set-0 layouts stay one
			// layout), and needs the same structure and heap reflections do.
			bool RayWaterRefractionOn = false;

			// The glassy water's inputs for the current transparent pass,
			// set by the frame graph around it and cleared after -- null
			// means the backdrop pass did not run and the water shader is
			// told so through its flags lane.
			Ref<RHITexture> WaterBackdropColor;
			Ref<RHITexture> WaterBackdropDepth;
			// Clamped-linear for the backdrop and the foam buffer (both are
			// bounded rectangles), wrapping for the two generated tiles.
			Ref<RHISampler> WaterClampSampler;
			Ref<RHISampler> WaterWrapSampler;
			// **Whether a traced hit honours the field's stored visibility.**
			//
			// It always honours it where the picture is shaded directly -- that
			// path reads the field rarely and the test is nearly free there.
			// This is about the other reader, the one every bounce ray hits,
			// where weighting the eight cells one at a time costs +0.6 ms
			// against +0.055. Worth it when the dial is asking for the best
			// bounce there is, and not worth it below that.
			// Indirect light is read from the field rather than computed. Set
			// per frame by the scene, which is the only thing that knows
			// whether a bake exists to read.
			bool BakedIrradianceOnly = false;
			std::vector<GpuRayInstance> RayInstanceScratch;

			// The depth-only pipeline. Its own shader and its own pipeline
			// object: no colour attachment, so it cannot share the lit one.
			Ref<RHIShader>   ShadowShader;
			// The alpha-tested caster's depth shader and pipeline. Its vertex
			// layout carries a texture coordinate the opaque one does not, and
			// its fragment stage is the only one in a depth pass that is not
			// empty.
			Ref<RHIShader>   ShadowMaskedShader;
			Ref<RHIPipeline> ShadowMaskedPipeline;
			Ref<RHIPipeline> ShadowPipeline;
			Ref<RHIShader>   ShadowMeshletShader;
			Ref<RHIPipeline> ShadowMeshletPipeline;
			Ref<RHIShader>   MeshletLitShader;
			Ref<RHIPipeline> MeshletLitPipeline;
			Ref<RHIShader>   ShadowSkinnedShader;
			Ref<RHIPipeline> ShadowSkinnedPipeline;
			Format ShadowDepth = Format::D32_SFLOAT;
			Mat4 ShadowViewProjection{ 1.0f };
			bool ShadowActive = false;

			// Mip-filtered and clamped, unlike the material sampler: roughness
			// selects a level here, so a sampler pinned to level 0 would make
			// every surface a mirror.
			Ref<RHISampler> EnvironmentSampler;

			SceneUniforms Scene{};
			bool SceneActive = false;

			unsigned int DrawCalls = 0;
			// **Submitted, over every pass that counted a draw.** The two
			// numbers have to describe the same set of draws or the panel that
			// shows them side by side is lying with true numbers: this counted
			// the camera's classic draws alone for a while, so a frame that
			// went entirely through the GPU-driven path or spent itself on
			// shadow maps read "155 draws, 0 triangles". For an indirect draw
			// it is the slot's reserved length -- what was handed to the cull,
			// not what survived it, which is a number only the GPU has.
			unsigned int Triangles = 0;
			unsigned int Culled = 0;
			// How many of DrawCalls were indirect, so a reader can tell which
			// half of the frame the counts above are approximate for.
			unsigned int IndirectDraws = 0;

			bool Ready = false;
		};

		std::unique_ptr<Renderer3DData> s_Data;

		Renderer3DData::SceneSlot& AcquireSceneSlot()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& slots = s_Data->SceneSlots[frame];

			while (s_Data->SceneCursor >= slots.size())
			{
				Renderer3DData::SceneSlot slot;

				BufferDesc desc;
				desc.Size = sizeof(SceneUniforms);
				desc.Usage = BufferUsage::Uniform;
				desc.Memory = MemoryDomain::HostVisible;
				desc.DebugName = "Renderer3D.scene." + std::to_string(frame) + "." +
								 std::to_string(slots.size());
				slot.Buffer = s_Data->Device->CreateBuffer(desc);

				slots.push_back(std::move(slot));
			}

			Renderer3DData::SceneSlot& slot = slots[s_Data->SceneCursor++];

			// Lazily, because a set needs a pipeline and the pipeline is built
			// from target formats that Init does not know.
			if (!slot.Set)
				slot.Set = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);

			// The sets are kept, the claim on them is not.
			slot.MeshletCursor = 0;
			slot.WaterSetCursor = 0;

			return slot;
		}

		// Takes the next row of the frame's instance pool and points `draw` at
		// it. The reference is good until the next call, which is all any
		// caller needs: each fills its row completely before submitting.
		InstanceData& AllocateInstance(PendingDraw& draw)
		{
			draw.Instance = (uint32_t)s_Data->Instances.size();
			return s_Data->Instances.emplace_back();
		}

		// This material's row in the frame's material buffer, made on first
		// sight. Shared by both paths -- EndScene numbers the CPU draws'
		// materials through it, and SetSceneInstance the GPU rows' -- so a
		// material both paths use gets one record rather than two.
		uint32_t RegisterMaterial(const Ref<Material>& material)
		{
			const Material* key = material.get();
			auto it = s_Data->MaterialIndex.find(key);
			if (it != s_Data->MaterialIndex.end())
				return it->second;

			GpuMaterial record;
			if (material)
				material->WriteRecord(*s_Data->Heap, record);

			const uint32_t index = (uint32_t)s_Data->MaterialScratch.size();
			s_Data->MaterialIndex.emplace(key, index);
			s_Data->MaterialScratch.push_back(record);
			return index;
		}

		Renderer3DData::ShadowSlot& AcquireShadowSlot()
		{
			const uint32_t frame = s_Data->Device->GetFrameIndex();
			auto& slots = s_Data->ShadowSlots[frame];

			while (s_Data->ShadowCursor >= slots.size())
				slots.push_back(Renderer3DData::ShadowSlot{});

			Renderer3DData::ShadowSlot& slot = slots[s_Data->ShadowCursor++];

			if (!slot.Set)
				slot.Set = s_Data->Device->CreateResourceSet(s_Data->ShadowPipeline, 0);

			// Reused a frame later with whatever cursor it ended on; the sets
			// themselves are kept, the claim on them is not.
			slot.MeshletCursor = 0;

			return slot;
		}

		// Grows `buffer` to hold at least `count` elements of `stride`, in
		// powers of two so a scene that creeps upwards does not reallocate
		// every frame. Returns false if the device would not give one.
		bool EnsureInstanceBuffer(Ref<RHIBuffer>& buffer, uint32_t& capacity,
								  uint32_t count, uint32_t stride, const char* name)
		{
			if (buffer && capacity >= count)
				return true;

			uint32_t target = capacity > 0 ? capacity : 64;
			while (target < count)
				target *= 2;

			BufferDesc desc;
			desc.Size = (uint64_t)target * stride;
			desc.Usage = BufferUsage::Storage;
			desc.Memory = MemoryDomain::HostVisible;
			desc.DebugName = name;

			// A new buffer rather than a resize: the old one may still be
			// bound to a command buffer this frame, and the deletion queue is
			// what makes releasing it safe.
			Ref<RHIBuffer> grown = s_Data->Device->CreateBuffer(desc);
			if (!grown)
				return false;

			buffer = grown;
			capacity = target;
			return true;
		}
	}

	void Renderer3D::Init(RHIDevice& device)
	{
		s_Data = std::make_unique<Renderer3DData>();
		s_Data->Device = &device;

		ShaderCompiler::Init();

		// The one decision that forks the material path (ENGINE-NOTES 7al),
		// made once: the device has to have descriptor indexing, the flag has
		// to allow it, and the heap has to have been created -- and then the
		// lit shaders are compiled for that path and no other. On OpenGL the
		// caps say no and none of this runs, which is the whole of the OpenGL
		// implementation.
		const DeviceCaps& caps = device.GetCaps();
		if (EngineConfig::Get().Bindless && caps.SupportsDescriptorIndexing)
		{
			s_Data->Heap = TextureHeap::Create(device, caps.MaxBindlessTextures);
			s_Data->Bindless = s_Data->Heap != nullptr;
		}
		RV_CORE_INFO("Renderer3D: material textures {0} (change with --bindless=on|off)",
					 s_Data->Bindless
						 ? "through the bindless heap, " + std::to_string(caps.MaxBindlessTextures) + " slots"
						 : caps.SupportsDescriptorIndexing ? std::string("bound per material (heap disabled)")
														   : std::string("bound per material (no descriptor indexing)"));

		// The acceleration structures the lit pass may trace into. Unavailable
		// on a device without ray queries, and then everything below that asks
		// about it is answered "no" (ENGINE-NOTES 7am).
		RayShadows::Init(device);
		GpuCull::Init(device);

		if (!CompileLitShaders())
			return;

		// Slots are created on demand; most frames need one.
		s_Data->SceneSlots.resize(device.GetFramesInFlight());
		s_Data->ShadowSlots.resize(device.GetFramesInFlight());
		s_Data->CulledShadowSets.resize(device.GetFramesInFlight());
		s_Data->IrradianceFillSets.resize(device.GetFramesInFlight());

		s_Data->DefaultMaterial = Material::CreateDefault(device);

		// The cutout caster's depth shader. Not fatal if it is missing: masked
		// geometry then casts nothing rather than casting a solid sheet, which
		// is the behaviour this replaced and the quieter of the two wrongs.
		if (auto compiled =
				ShaderCompiler::CompileFromFile("assets/shaders/shadow_depth_masked.rvshader"))
		{
			s_Data->ShadowMaskedShader = device.CreateShader(*compiled);
		}
		else
		{
			RV_CORE_WARN("Renderer3D: assets/shaders/shadow_depth_masked.rvshader did not "
						 "compile; alpha-tested geometry will cast no shadow");
		}

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/shadow_depth.rvshader"))
			s_Data->ShadowShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/shadow_depth.rvshader");

		if (auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/shadow_depth_skinned.rvshader"))
			s_Data->ShadowSkinnedShader = device.CreateShader(*compiled);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/shadow_depth_skinned.rvshader");

		// The meshlet depth path (roadmap 8.3, second half). Asked for by
		// flag and answered by the device: no flag or no mesh shading means
		// no shader, and every branch downstream reads that as "classic
		// path" -- which draws the identical image, only per draw instead of
		// per meshlet.
		if (EngineConfig::Get().Meshlets)
		{
			if (!device.GetCaps().SupportsMeshShading)
			{
				RV_CORE_INFO("Meshlets requested but this device has no mesh "
							 "shading; the classic depth path runs instead");
			}
			else if (auto compiled = ShaderCompiler::CompileFromFile(
						 "assets/shaders/shadow_depth_meshlet.rvshader"))
			{
				s_Data->ShadowMeshletShader = device.CreateShader(*compiled);
				RV_CORE_INFO("Meshlet depth path on ({0}-vertex, {1}-triangle meshlets)",
							 MeshletData::kMaxVertices, MeshletData::kMaxTriangles);
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: failed to compile "
							  "assets/shaders/shadow_depth_meshlet.rvshader");
			}
		}

		SamplerDesc environment;
		environment.WrapU = WrapMode::ClampToEdge;
		environment.WrapV = WrapMode::ClampToEdge;
		environment.WrapW = WrapMode::ClampToEdge;
		environment.MaxLod = 16.0f;
		s_Data->EnvironmentSampler = device.CreateSampler(environment);

		// Point, clamped: the traced bounce reads a depth and a packed normal,
		// and the average of a near depth and a far one is a distance nothing
		// in the scene is at -- which is a ray starting in mid-air.
		SamplerDesc exact;
		exact.MinFilter = FilterMode::Nearest;
		exact.MagFilter = FilterMode::Nearest;
		exact.WrapU = WrapMode::ClampToEdge;
		exact.WrapV = WrapMode::ClampToEdge;
		exact.WrapW = WrapMode::ClampToEdge;
		s_Data->PointSampler = device.CreateSampler(exact);

		// Both shaders, not just the lit one. The shadow pass failing on its own
		// used to leave this announcing readiness while nothing cast anything.
		s_Data->Ready = s_Data->Shader != nullptr && s_Data->ShadowShader != nullptr &&
						s_Data->DefaultMaterial != nullptr;

		if (s_Data->Ready)
			RV_CORE_INFO("Renderer3D ready (Cook-Torrance PBR, shadow casting)");
		else
			RV_CORE_ERROR("Renderer3D incomplete; meshes or their shadows will not draw");
	}

	void Renderer3D::Shutdown()
	{
		Mesh::ClearCache();
		TextureLoader::ClearCache();
		RayShadows::Shutdown();
		GpuCull::Shutdown();
		// After s_Data's default material, which holds a reference to it.
		s_Data.reset();
		Material::ReleaseShared();
		LayeredMaterial::ReleaseShared();
	}

	// The two lit shaders, compiled for the paths this device and this frame
	// take: RV_BINDLESS is decided once at Init, RV_RAY_SHADOWS follows the
	// project setting. Called at Init and again whenever the shadow method
	// changes; the SPIR-V cache makes the second and later calls a file read.
	bool Renderer3D::CompileLitShaders()
	{
		// **The traced bounce's own pass, compiled before the lit shaders and
		// not after, because whether it exists decides how they are compiled.**
		//
		// It used to be built afterwards, and a failure there left the engine
		// in a state with no consistent answer: the lit shader carried
		// RV_RAY_GI, so it wrote zero to the indirect buffer and waited for a
		// pass that did not exist, while the frame graph suppressed the
		// screen-space chain because the *setting* still said traced. The
		// result was a frame with no indirect light at all and nothing said
		// so. Turning the flag off here is what makes the fallback whole:
		// every shader below is then compiled for the screen-space form, and
		// the graph resolves to it for the same reason.
		// **Compiled whenever rays are available, not only when the screen's
		// bounce is switched on.**
		//
		// This shader is two things at once: the traced bounce, and the set-0
		// layout the irradiance fill allocates its descriptors against -- the
		// fill includes the same header under the same defines precisely so the
		// two agree. Tying it to the bounce's switch therefore tied *baking* to
		// it as well: a project that turned the realtime bounce off to live on
		// a baked field instead got no field either, because there was no
		// layout to bind one through, and the picture fell back to no indirect
		// light at all while nothing said so.
		//
		// The pipeline costs one compile at startup and nothing per frame when
		// no pass draws with it.
		s_Data->GiShader = nullptr;
		if (s_Data->Bindless && s_Data->RayShadowsOn)
		{
			std::vector<std::string> traceDefines;
			if (s_Data->Bindless)
				traceDefines.push_back("RV_BINDLESS");
			traceDefines.push_back("RV_RAY_SHADOWS");
			if (s_Data->RayReflectionsOn)
				traceDefines.push_back("RV_RAY_REFLECTIONS");
			traceDefines.push_back("RV_RAY_GI");

			if (auto gi = ShaderCompiler::CompileFromFile("assets/shaders/rtgi_trace.rvshader",
														 traceDefines))
			{
				s_Data->GiShader = s_Data->Device->CreateShader(*gi);
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: assets/shaders/rtgi_trace.rvshader did not compile, "
							  "so the traced bounce cannot run; falling back to the "
							  "screen-space form for this session");
				s_Data->RayGlobalIlluminationOn = false;
			}
		}

		// **The pass that solves an irradiance field.** Same defines as the
		// bounce, because it borrows the same TraceSurface and has to see the
		// same engine.
		//
		// **Not the same conditions, though, and that distinction is the point
		// of a stored field.** It needs rays and the heap, as anything tracing
		// does; it does not need the screen-space bounce to be running, and
		// riding that switch meant a project that turned the realtime bounce
		// off lost the ability to bake a field as well -- which is precisely
		// the configuration someone turns it off *for*.
		s_Data->IrradianceFillShader = nullptr;
		if (s_Data->Bindless && s_Data->RayShadowsOn)
		{
			std::vector<std::string> fillDefines;
			fillDefines.push_back("RV_BINDLESS");
			fillDefines.push_back("RV_RAY_SHADOWS");
			if (s_Data->RayReflectionsOn)
				fillDefines.push_back("RV_RAY_REFLECTIONS");
			fillDefines.push_back("RV_RAY_GI");
			// **The solve is the one compile that reads the field at a hit** --
			// see the block this enables in ShadeTraced. The sampled field is
			// the previous sweep's completed answer (the solve writes the
			// volume's second texture and the two swap at each sweep boundary),
			// so each sweep carries the last one's transport a bounce further
			// and the sweeps converge on the full multi-bounce answer. The
			// read always tests the stored visibility: feedback amplifies
			// whatever it reads, and the visibility term is what makes the
			// loop safe to close (a sealed room went 0.15 levels to 6.3 over
			// eight sweeps without it).
			fillDefines.push_back("RV_IRRADIANCE_FILL");

			if (auto fill = ShaderCompiler::CompileFromFile(
					"assets/shaders/irradiance_fill.rvshader", fillDefines))
			{
				s_Data->IrradianceFillShader = s_Data->Device->CreateShader(*fill);
				RV_CORE_INFO("Renderer3D: irradiance fill shader compiled");
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: assets/shaders/irradiance_fill.rvshader did not "
							  "compile; irradiance fields keep whatever they were last "
							  "filled with");
			}
		}

		std::vector<std::string> defines;
		if (s_Data->Bindless)
			defines.push_back("RV_BINDLESS");
		if (s_Data->RayShadowsOn)
			defines.push_back("RV_RAY_SHADOWS");
		if (s_Data->RayReflectionsOn)
			defines.push_back("RV_RAY_REFLECTIONS");
		if (s_Data->RayGlobalIlluminationOn)
			defines.push_back("RV_RAY_GI");
		// The count rides the define: `#ifdef RV_RAY_SKY` still tests presence,
		// and the shader reads it as the loop bound. Changing quality
		// recompiles the lit shaders, which is what toggling any of the other
		// traced features already does.
		if (s_Data->RaySkyVisibilityRays > 0)
			defines.push_back("RV_RAY_SKY=" + std::to_string(s_Data->RaySkyVisibilityRays));

		// The meshlet lit stage compiles with the identical define set, so
		// its fragment half is bit-for-bit the classic one's: same bindless
		// choice, same traced features, same set 0. Flag and device gated
		// like the depth twin.
		if (EngineConfig::Get().Meshlets && s_Data->Device->GetCaps().SupportsMeshShading)
		{
			if (auto meshlet = ShaderCompiler::CompileFromFile(
					"assets/shaders/pbr_meshlet.rvshader", defines))
			{
				s_Data->MeshletLitShader = s_Data->Device->CreateShader(*meshlet);
				RV_CORE_INFO("Meshlet lit path on");
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: failed to compile "
							  "assets/shaders/pbr_meshlet.rvshader; the vertex "
							  "path draws the lit pass");
			}
		}

		auto compiled = ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader", defines);
		if (!compiled)
		{
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/pbr.rvshader");
			return false;
		}
		s_Data->Shader = s_Data->Device->CreateShader(*compiled);

		// **The depth prepass, from the same defines.** Its vertex stage is
		// pbr.rvshader's include for include, so the depth it writes is the
		// depth the lit pass would have written -- and the defines have to
		// match too, because scene_block.glsl's layout depends on them and a
		// prepass that positioned vertices from a differently laid out
		// instance buffer would write believable, wrong depth.
		//
		// A failure here is not fatal: no pipeline, no prepass, and the frame
		// draws exactly as it did before. That is deliberate -- the last time
		// a missing shader silently changed the picture it cost an afternoon.
		s_Data->PrepassShader = nullptr;
		if (auto prepass = ShaderCompiler::CompileFromFile(
				"assets/shaders/depth_prepass.rvshader", defines))
		{
			s_Data->PrepassShader = s_Data->Device->CreateShader(*prepass);
			RV_CORE_INFO("Renderer3D: depth prepass on");
		}
		else
		{
			RV_CORE_WARN("Renderer3D: assets/shaders/depth_prepass.rvshader did not "
						 "compile; drawing without a depth prepass");
		}

		// **The same shader again, with the cutout test.** One define, and the
		// same defines otherwise -- a masked variant that disagreed with the
		// opaque one about bindless or ray shadows would shade the same
		// surface two ways.
		//
		// A failure is not fatal, on the same reasoning as the prepass above:
		// without the variant the material still draws, opaque, with a hard
		// edge nowhere -- visibly wrong but not a black frame, and it says so.
		s_Data->MaskedShader = nullptr;
		{
			std::vector<std::string> masked = defines;
			masked.push_back("RV_ALPHA_CUTOUT");

			if (auto cutout = ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader",
															  masked))
			{
				s_Data->MaskedShader = s_Data->Device->CreateShader(*cutout);
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: the alpha-cutout variant of pbr.rvshader did "
							  "not compile; masked materials will draw as solid");
			}
		}

		// **The same shader, compiled to write two attachments instead of
		// four.** Not a second material model and not a second lighting path:
		// a blended fragment is shaded exactly as an opaque one and then put
		// somewhere else, which is the only version of this that cannot drift.
		{
			std::vector<std::string> blended = defines;
			blended.push_back("RV_TRANSPARENT");
			// On the *pair*, not on water alone. Only water reads it -- the
			// define is dead code in pbr.rvshader -- but it moves set 0's
			// layout (the ray-instance table comes with it), and the two
			// pipelines are served by one TransparentSet. Diverging layouts
			// here is the masked pipeline's black-cutout lesson again.
			if (s_Data->RayWaterRefractionOn)
				blended.push_back("RV_RAY_REFRACTION");

			if (auto transparent = ShaderCompiler::CompileFromFile("assets/shaders/pbr.rvshader",
																   blended))
			{
				s_Data->TransparentShader = s_Data->Device->CreateShader(*transparent);
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: the transparent variant of pbr.rvshader did not "
							  "compile; blended materials will not be drawn at all");
				s_Data->TransparentShader = nullptr;
			}

			// Water. Its own file rather than another define on pbr.rvshader,
			// because what differs is the *vertex* stage -- the waves -- and
			// pbr.rvshader's vertex stage is shared with the opaque and layered
			// pipelines, which must not grow a branch for it.
			if (auto water = ShaderCompiler::CompileFromFile("assets/shaders/water.rvshader",
															 blended))
			{
				s_Data->WaterShader = s_Data->Device->CreateShader(*water);
			}
			else
			{
				RV_CORE_ERROR("Renderer3D: water.rvshader did not compile; bodies of "
							  "water will not be drawn");
				s_Data->WaterShader = nullptr;
			}
		}

		if (auto skinned = ShaderCompiler::CompileFromFile("assets/shaders/pbr_skinned.rvshader", defines))
			s_Data->SkinnedShader = s_Data->Device->CreateShader(*skinned);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/pbr_skinned.rvshader");

		// The layered variant defines RV_LAYERED itself; everything else about
		// it -- the heap, the rays -- follows the same defines.
		if (auto layered = ShaderCompiler::CompileFromFile("assets/shaders/pbr_layered.rvshader", defines))
			s_Data->LayeredShader = s_Data->Device->CreateShader(*layered);
		else
			RV_CORE_ERROR("Renderer3D: failed to compile assets/shaders/pbr_layered.rvshader");

		return true;
	}

	bool Renderer3D::CanTraceGlobalIllumination()
	{
		return s_Data && s_Data->GiShader != nullptr;
	}


	void Renderer3D::TraceGlobalIllumination(RHICommandList& cmd,
											 const Ref<RHITexture>& depth,
											 const Ref<RHITexture>& surface,
											 const Ref<RHITexture>& budget,
											 Format targetColor,
											 const GiTraceView& view, int rays)
	{
		if (!s_Data || !s_Data->GiShader || !depth || !surface)
			return;
		if (!s_Data->ActiveScene)
			return;

		// The pipeline is built against the target's format, and the caller
		// chooses that -- so a change rebuilds here rather than at the next
		// EnsurePipeline, which has already run by the time a pass executes.
		if (s_Data->GiTargetColor != targetColor || !s_Data->GiPipeline)
		{
			s_Data->GiTargetColor = targetColor;

			GraphicsPipelineDesc gi;
			gi.Name = "Renderer3D.gi";
			gi.Shader = s_Data->GiShader;
			gi.Topology = PrimitiveTopology::TriangleList;
			gi.Rasterizer.Cull = CullMode::None;
			gi.Blend = BlendPreset::Opaque;
			gi.DepthStencil.DepthTestEnable = false;
			gi.DepthStencil.DepthWriteEnable = false;
			gi.ColorFormats = { targetColor };
			gi.DepthFormat = Format::Undefined;
			s_Data->GiPipeline = s_Data->Device->CreatePipeline(gi);

			// A set belongs to the layout it was allocated against, and that
			// layout has just been replaced.
			for (auto& frame : s_Data->SceneSlots)
				for (auto& slot : frame)
					slot.GiSet = nullptr;
		}

		if (!s_Data->GiPipeline)
			return;

		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;
		if (!slot.GiSet)
			return;   // the scene block has not been written for this pipeline yet

		if (!slot.GiInputs)
			slot.GiInputs = s_Data->Device->CreateResourceSet(s_Data->GiPipeline, 3);
		if (!slot.GiInputs)
			return;

		slot.GiInputs->SetTexture(0, depth, s_Data->PointSampler);
		slot.GiInputs->SetTexture(1, surface, s_Data->PointSampler);
		// Bound whether or not there is one: a set that leaves a declared
		// binding unwritten is a validation error, and the sign of Rays is
		// what says whether to believe it.
		slot.GiInputs->SetTexture(5, budget ? budget : depth,
								  s_Data->PointSampler);

		// The emitters. **At least one row even when the scene has none**: a
		// binding the layout declares and the set leaves unwritten is a
		// validation error rather than a harmless omission, which is the rule
		// the instance buffers above already follow.
		const uint32_t emitterRows = Math::Max((uint32_t)s_Data->Emitters.size(), 1u);
		if (EnsureInstanceBuffer(slot.GiEmitters, slot.GiEmitterCapacity, emitterRows,
								 sizeof(Renderer3DData::GpuEmitter), "Renderer3D.giemitters"))
		{
			if (s_Data->Emitters.empty())
			{
				const Renderer3DData::GpuEmitter none{};
				slot.GiEmitters->Upload(&none, sizeof(none));
			}
			else
			{
				slot.GiEmitters->Upload(s_Data->Emitters.data(),
										(uint64_t)s_Data->Emitters.size()
											* sizeof(Renderer3DData::GpuEmitter));
			}

			slot.GiInputs->SetStorageBuffer(2, slot.GiEmitters, 0,
											(uint64_t)emitterRows
												* sizeof(Renderer3DData::GpuEmitter));
		}

		// The aiming tables, all of this frame's end to end. At least one
		// float for the same reason the row above needs at least one row: a
		// declared binding left unwritten is a validation error.
		const uint32_t cdfCount = Math::Max((uint32_t)s_Data->EmitterCdf.size(), 1u);
		if (EnsureInstanceBuffer(slot.GiEmitterCdf, slot.GiEmitterCdfCapacity, cdfCount,
								 sizeof(float), "Renderer3D.giemittercdf"))
		{
			if (s_Data->EmitterCdf.empty())
			{
				const float none = 1.0f;
				slot.GiEmitterCdf->Upload(&none, sizeof(none));
			}
			else
			{
				slot.GiEmitterCdf->Upload(s_Data->EmitterCdf.data(),
										  (uint64_t)s_Data->EmitterCdf.size() * sizeof(float));
			}
			slot.GiInputs->SetStorageBuffer(3, slot.GiEmitterCdf, 0,
											(uint64_t)cdfCount * sizeof(float));
		}

		// The probe placements, for the same reason and by the same rules: at
		// least one row so a declared binding is never left unwritten.
		const uint32_t probeRows = Math::Max((uint32_t)s_Data->Probes.size(), 1u);
		if (EnsureInstanceBuffer(slot.GiProbes, slot.GiProbeCapacity, probeRows,
								 sizeof(Renderer3DData::GpuProbe), "Renderer3D.giprobes"))
		{
			if (s_Data->Probes.empty())
			{
				const Renderer3DData::GpuProbe none{};
				slot.GiProbes->Upload(&none, sizeof(none));
			}
			else
			{
				slot.GiProbes->Upload(s_Data->Probes.data(),
									  (uint64_t)s_Data->Probes.size()
										  * sizeof(Renderer3DData::GpuProbe));
			}

			slot.GiInputs->SetStorageBuffer(4, slot.GiProbes, 0,
											(uint64_t)probeRows
												* sizeof(Renderer3DData::GpuProbe));
		}

		slot.GiInputs->Commit();

		struct GiParams
		{
			float NearClip, FarClip, InvP0, InvP1;
			// Pad0 is the emitter count now. Kept in the padding rather than
			// appended, so the block stays the size every other trace pass's
			// does and nothing about the layout moves.
			// Pad1 is the probe count now, taken the same way Emitters took
			// Pad0: in the padding, so the block stays the size every other
			// trace pass's is.
			float FlipY, Rays, Emitters, Probes;
			Vec4  CameraRow0, CameraRow1, CameraRow2, CameraPosition;
		} params{};

		params.NearClip = view.NearClip;
		params.FarClip = view.FarClip;
		params.InvP0 = view.InvProjection0;
		params.InvP1 = view.InvProjection1;
		// Vulkan's first framebuffer row is the top of the image and OpenGL's
		// is the bottom, so a fullscreen pass reads its source the other way
		// up on one of them. The same rule every post pass carries.
		params.FlipY = s_Data->Device->GetBackend() == Backend::Vulkan ? 1.0f : 0.0f;
		// Negative means "a budget is bound; this is the ceiling".
		params.Rays = (float)Math::Clamp(rays, 1, 32) * (budget ? -1.0f : 1.0f);
		params.Emitters = (float)s_Data->Emitters.size();
		params.Probes = (float)s_Data->Probes.size();

		// The camera transform is the view's inverse; the rotation part of an
		// inverse is the transpose, so the camera's rows are the view's
		// columns, and the position falls out of the full inverse.
		const Mat4 camera = Math::Inverse(view.View);
		params.CameraRow0 = Vec4(camera[0][0], camera[1][0], camera[2][0], 0.0f);
		params.CameraRow1 = Vec4(camera[0][1], camera[1][1], camera[2][1], 0.0f);
		params.CameraRow2 = Vec4(camera[0][2], camera[1][2], camera[2][2], 0.0f);
		params.CameraPosition = Vec4(camera[3][0], camera[3][1], camera[3][2], 0.0f);

		cmd.BindPipeline(s_Data->GiPipeline);
		cmd.BindResourceSet(0, slot.GiSet);
		if (s_Data->Heap)
			cmd.BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd.BindResourceSet(3, slot.GiInputs);
		cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd.Draw(3);
	}

	void Renderer3D::SetRayTracedShadows(bool enabled)
	{
		if (!s_Data)
			return;
		if (enabled && !RayShadows::IsAvailable())
			enabled = false;
		if (s_Data->RayShadowsOn == enabled)
			return;

		s_Data->RayShadowsOn = enabled;
		// Reflections, the traced bounce, the water's refraction and the
		// sky's visibility all ride on the shadows' structure; off with them.
		if (!enabled)
		{
			s_Data->RayReflectionsOn = false;
			s_Data->RayGlobalIlluminationOn = false;
			s_Data->RayWaterRefractionOn = false;
			s_Data->RaySkyVisibilityRays = 0;
		}
		RV_CORE_INFO("Renderer3D: shadows {0}", enabled ? "traced" : "from maps");

		// New shaders, new pipelines, and every scene set with them: a set is
		// allocated against a layout, and the layouts differ by the structure
		// binding.
		if (CompileLitShaders())
			s_Data->PipelineDirty = true;
	}

	bool Renderer3D::IsRayTracedShadows()
	{
		return s_Data && s_Data->RayShadowsOn;
	}

	void Renderer3D::SetRayTracedReflections(bool enabled)
	{
		if (!s_Data)
			return;
		// Rays into a structure the shadows build, shaded through the heap:
		// neither absent is a mode this can run in.
		if (enabled && (!s_Data->RayShadowsOn || !s_Data->Bindless))
			enabled = false;
		if (s_Data->RayReflectionsOn == enabled)
			return;

		s_Data->RayReflectionsOn = enabled;
		RV_CORE_INFO("Renderer3D: reflections {0}", enabled ? "traced" : "screen-space or probe");
		if (CompileLitShaders())
			s_Data->PipelineDirty = true;
	}

	void Renderer3D::SetRayTracedSkyVisibility(int rays)
	{
		if (!s_Data)
			return;
		// Rides on the shadows' structure, like reflections and refraction:
		// with no acceleration structure there is nothing to ask.
		if (rays > 0 && !s_Data->RayShadowsOn)
			rays = 0;
		rays = Math::Clamp(rays, 0, 8);
		if (s_Data->RaySkyVisibilityRays == rays)
			return;

		s_Data->RaySkyVisibilityRays = rays;
		if (rays > 0)
			RV_CORE_INFO("Renderer3D: sky visibility traced, {0} rays", rays);
		else
			RV_CORE_INFO("Renderer3D: sky visibility from the baked volume");
		if (CompileLitShaders())
			s_Data->PipelineDirty = true;
	}

	int Renderer3D::RayTracedSkyVisibilityRays()
	{
		return s_Data ? s_Data->RaySkyVisibilityRays : 0;
	}

	bool Renderer3D::IsRayTracedReflections()
	{
		return s_Data && s_Data->RayReflectionsOn;
	}

	void Renderer3D::SetRayTracedGlobalIllumination(bool enabled)
	{
		if (!s_Data)
			return;
		// A bounce is a ray into the shadows' structure, shaded through the
		// heap: the same two prerequisites reflections have.
		if (enabled && (!s_Data->RayShadowsOn || !s_Data->Bindless))
			enabled = false;
		if (s_Data->RayGlobalIlluminationOn == enabled)
			return;

		s_Data->RayGlobalIlluminationOn = enabled;
		RV_CORE_INFO("Renderer3D: global illumination {0}",
					 enabled ? "traced" : "screen-space or none");
		if (CompileLitShaders())
			s_Data->PipelineDirty = true;
	}

	void Renderer3D::SetRayTracedWaterRefraction(bool enabled)
	{
		if (!s_Data)
			return;
		// A refraction ray is a ray into the shadows' structure, shaded
		// through the heap: the same two prerequisites reflections have.
		if (enabled && (!s_Data->RayShadowsOn || !s_Data->Bindless))
			enabled = false;
		if (s_Data->RayWaterRefractionOn == enabled)
			return;

		s_Data->RayWaterRefractionOn = enabled;
		RV_CORE_INFO("Renderer3D: water refraction {0}",
					 enabled ? "traced" : "from the backdrop copy");
		if (CompileLitShaders())
			s_Data->PipelineDirty = true;
	}

	bool Renderer3D::IsRayTracedWaterRefraction()
	{
		return s_Data && s_Data->RayWaterRefractionOn;
	}

	void Renderer3D::SetWaterBackdrop(const Ref<RHITexture>& color,
									  const Ref<RHITexture>& depth)
	{
		if (!s_Data)
			return;
		s_Data->WaterBackdropColor = color;
		s_Data->WaterBackdropDepth = depth;
	}

	void Renderer3D::SetBakedIrradianceOnly(bool enabled)
	{
		if (s_Data)
			s_Data->BakedIrradianceOnly = enabled;
	}

	bool Renderer3D::IsBakedIrradianceOnly()
	{
		return s_Data && s_Data->BakedIrradianceOnly;
	}

	bool Renderer3D::IsRayTracedGlobalIllumination()
	{
		return s_Data && s_Data->RayGlobalIlluminationOn;
	}

	Ref<Material> Renderer3D::GetDefaultMaterial()
	{
		return s_Data ? s_Data->DefaultMaterial : nullptr;
	}

	bool Renderer3D::IsBindless()
	{
		return s_Data && s_Data->Bindless;
	}

	unsigned int Renderer3D::GetHeapLiveCount()
	{
		return s_Data && s_Data->Heap ? s_Data->Heap->GetLiveCount() : 0u;
	}

	unsigned int Renderer3D::GetHeapFreeCount()
	{
		return s_Data && s_Data->Heap ? s_Data->Heap->GetFreeCount() : 0u;
	}

	void Renderer3D::SetTargetFormats(Format color, Format depth, uint32_t samples,
									   Format velocity, Format normal,
									   Format indirect)
	{
		if (!s_Data)
			return;
		if (s_Data->TargetColor == color && s_Data->TargetDepth == depth &&
			s_Data->TargetSamples == samples &&
			s_Data->TargetVelocity == velocity && s_Data->TargetNormal == normal &&
			s_Data->TargetIndirect == indirect && s_Data->Pipeline)
			return;

		s_Data->TargetColor = color;
		s_Data->TargetSamples = samples;
		s_Data->TargetVelocity = velocity;
		s_Data->TargetNormal = normal;
		s_Data->TargetIndirect = indirect;
		s_Data->TargetDepth = depth;
		s_Data->PipelineDirty = true;
	}

	void Renderer3D::SetIrradianceVolumes(const Ref<IrradianceVolume>& volume,
										  const std::vector<IrradianceVolume::Region>*
											  regionsOverride)
	{
		if (!s_Data)
			return;

		s_Data->Irradiance = volume;
		s_Data->IrradianceRegions.clear();
		if (!volume)
			return;

		// The readers must be told where the box is *now*, for the same reason
		// the solve is: a following grid's texture outlives the place it was
		// built for.
		const std::vector<IrradianceVolume::Region>& source =
			regionsOverride ? *regionsOverride : volume->Regions();

		for (const IrradianceVolume::Region& region : source)
		{
			if (s_Data->IrradianceRegions.size() >= kMaxIrradianceVolumes)
				break;

			IrradianceVolume::Region guarded = region;
			// Guarded against a degenerate box: the shader divides by these to
			// find a cell, and a zero extent would put every fragment at
			// infinity.
			guarded.Extents = Vec3(Math::Max(region.Extents.x, 1.0e-3f),
								   Math::Max(region.Extents.y, 1.0e-3f),
								   Math::Max(region.Extents.z, 1.0e-3f));
			s_Data->IrradianceRegions.push_back(guarded);
		}
	}

	bool Renderer3D::SolveIrradianceVolume(RHICommandList& cmd,
										   const Ref<IrradianceVolume>& volume,
										   const IrradianceVolume::Region& region,
										   int rays, float reach,
										   uint32_t rowBegin, uint32_t rowCount,
										   float blend, bool feedback)
	{
		if (!s_Data || !s_Data->IrradianceFillShader || !volume)
			return false;
		if (!s_Data->ActiveScene)
			return false;

		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;

		// **The traced bounce's set 0, not the lit pass's.** A descriptor set
		// belongs to the layout it was allocated against, and slot.Set was
		// allocated against the lit pipeline -- whose set 0 has the vertex
		// stage's bindings in it. This shader includes the same header under
		// the same RV_TRACE_ONLY and the same defines as rtgi_trace, so its
		// set 0 is that one exactly, and GiSet is the set already allocated
		// for it. Binding the lit one instead is a layout mismatch, which
		// validation reports and a driver may simply read as rubbish.
		if (!slot.GiSet)
			return false;

		// **The target exists to make fragments and for nothing else.** A
		// fragment shader only runs where a triangle covers a pixel, so the
		// volume is unrolled into one: x is the cell's x, and y carries its y
		// and z stacked. Nothing is written to it and nothing reads it -- the
		// cells leave through imageStore -- so its format is the cheapest one
		// that can be an attachment.
		// The region's own grid, not the atlas's: a fragment is generated per
		// cell of the volume being solved, and its neighbours in the texture
		// are somebody else's business.
		const uint32_t width = region.Width;
		const uint32_t height = region.Height * region.Depth;

		if (!s_Data->IrradianceFillTarget
			|| s_Data->IrradianceFillWidth < width
			|| s_Data->IrradianceFillHeight < height)
		{
			RenderTargetDesc target;
			target.Width = s_Data->IrradianceFillWidth = Math::Max(width, s_Data->IrradianceFillWidth);
			target.Height = s_Data->IrradianceFillHeight = Math::Max(height, s_Data->IrradianceFillHeight);
			target.ColorAttachments = { { Format::R8G8B8A8_UNORM } };
			target.HasDepth = false;
			target.DebugName = "irradiance.fill";

			s_Data->IrradianceFillTarget = s_Data->Device->CreateRenderTarget(target);
			if (!s_Data->IrradianceFillTarget)
				return false;
		}

		if (!s_Data->IrradianceFillPipeline)
		{
			GraphicsPipelineDesc fill;
			fill.Name = "Renderer3D.irradiancefill";
			fill.Shader = s_Data->IrradianceFillShader;
			fill.Topology = PrimitiveTopology::TriangleList;
			fill.Rasterizer.Cull = CullMode::None;
			fill.Blend = BlendPreset::Opaque;
			fill.DepthStencil.DepthTestEnable = false;
			fill.DepthStencil.DepthWriteEnable = false;
			fill.ColorFormats = { Format::R8G8B8A8_UNORM };
			fill.DepthFormat = Format::Undefined;

			s_Data->IrradianceFillPipeline = s_Data->Device->CreatePipeline(fill);
			// The sets were allocated against the pipeline that has just been
			// replaced, so they belong to a layout that no longer exists.
			for (Ref<RHIResourceSet>& set : s_Data->IrradianceFillSets)
				set = nullptr;
		}
		if (!s_Data->IrradianceFillPipeline)
			return false;

		if (s_Data->IrradianceFillSets.empty())
			return false;

		Ref<RHIResourceSet>& fillSet =
			s_Data->IrradianceFillSets[s_Data->Device->GetFrameIndex()
									   % s_Data->IrradianceFillSets.size()];
		if (!fillSet)
			fillSet = s_Data->Device->CreateResourceSet(s_Data->IrradianceFillPipeline, 3);
		if (!fillSet)
			return false;

		// The solve writes the volume's back texture while its front stays
		// bound for sampling -- the frame's preview, and the solve's own
		// feedback read of the previous sweep. FlipSolve exchanges the two at
		// each sweep boundary, in SolvePendingIrradiance.
		fillSet->SetStorageImage(0, volume->SolveTarget(), 0);
		fillSet->Commit();

		struct FillParams
		{
			Vec4 Centre;
			Vec4 Extents;
			Vec4 Grid;
			Vec4 Trace;
			// The box's own axes, as columns: a cell's place in the world is
			// the centre plus this applied to its place in the box.
			Vec4 Rotation[3];
		} params{};

		// **The two atlas numbers ride in the unused lanes**: where this
		// region's slices begin inside a tile, and how far apart one tile is
		// from the next. Everything else about the box is the region's own.
		params.Centre = Vec4(region.Centre, (float)region.ZOffset);
		params.Extents = Vec4(region.Extents, (float)volume->Depth());
		params.Grid = Vec4((float)region.Width, (float)region.Height,
						   (float)region.Depth, (float)Math::Clamp(rays, 1, 4096));
		// The counter the ray directions are hashed against. It moves per solve
		// rather than per frame, so re-solving a field that did not change is
		// not a way to make it flicker. `w` is the feedback switch the fill
		// hands to ShadeTraced: which flavour this pass is producing.
		params.Trace = Vec4(reach, (float)s_Data->IrradianceFrame++, blend,
							feedback ? 1.0f : 0.0f);
		for (int axis = 0; axis < 3; axis++)
			params.Rotation[axis] = Vec4(region.Rotation[axis], 0.0f);


		// **The writes, fenced in both directions**, and both directions matter.
		// The back texture was last left readable -- by the closing barrier of
		// the previous band, or by the frames that sampled it before the flip
		// handed it over -- and this pass overwrites it: without ShaderRead ->
		// FragmentWrite the overwrite can land while such a read is still
		// running. The front needs no fence here; the fill only samples it,
		// beside every other reader.
		//
		// Recorded outside the render pass below, which is where a barrier is
		// legal, and which is why this runs as a standalone pass of the frame
		// graph rather than inside one of its ordinary ones.
		cmd.TextureBarrier(volume->SolveTarget(), TextureSync::ShaderRead,
						   TextureSync::FragmentWrite);

		RenderPassBeginInfo begin;
		begin.Target = s_Data->IrradianceFillTarget.get();
		begin.ClearColor = true;
		begin.UseDepth = false;

		cmd.BeginRenderPass(begin);
		// Only the cells this field has, whatever the target grew to for a
		// larger one: a fragment outside the grid discards, but not generating
		// it at all is cheaper and says the intent.
		//
		// **And only the band of them this pass is solving.** The viewport
		// stays the whole grid so gl_FragCoord still names a cell the same way
		// -- the shader maps a row to a (y, z) pair and that mapping must not
		// depend on which band is running -- while the scissor is what limits
		// the work. Getting that the other way round would solve the right
		// number of cells and write them into the wrong ones.
		Viewport viewport;
		viewport.X = 0.0f;
		viewport.Y = 0.0f;
		viewport.Width = (float)width;
		viewport.Height = (float)height;
		cmd.SetViewport(viewport);

		const uint32_t bandBegin = Math::Min(rowBegin, height);
		const uint32_t bandEnd = Math::Min(rowBegin + rowCount, height);

		Rect2D scissor;
		scissor.X = 0;
		scissor.Y = (int32_t)bandBegin;
		scissor.Width = width;
		scissor.Height = bandEnd - bandBegin;
		if (scissor.Height == 0)
		{
			cmd.EndRenderPass();
			return false;
		}
		cmd.SetScissor(scissor);

		cmd.BindPipeline(s_Data->IrradianceFillPipeline);
		cmd.BindResourceSet(0, slot.GiSet);
		if (s_Data->Heap)
			cmd.BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
		cmd.BindResourceSet(3, fillSet);
		cmd.PushConstants(ShaderStage::Fragment, 0, sizeof(params), &params);
		cmd.Draw(3);
		cmd.EndRenderPass();

		// And the other half of the fence: what this pass stored is what the
		// first sampler of this texture -- next frame's set write, after the
		// sweep-boundary flip makes it the front -- has to see.
		cmd.TextureBarrier(volume->SolveTarget(), TextureSync::FragmentWrite,
						   TextureSync::ShaderRead);

		return true;
	}

	void Renderer3D::RequestIrradianceSolve(const Ref<IrradianceVolume>& volume,
											uint32_t passes, uint32_t raysPerCell,
											bool feedback)
	{
		if (!s_Data)
			return;

		// Clamped here rather than trusted: these come from a scene file, and a
		// nought would be a solve that never runs while claiming it did.
		s_Data->PendingIrradiancePasses = Math::Clamp(passes, 1u, 64u);
		s_Data->PendingIrradianceFeedback = feedback;
		s_Data->PendingIrradianceRays = Math::Clamp(raysPerCell, 64u, 4096u);
		s_Data->PendingIrradiance = volume;
		// A new request starts its sweeps over. Asking again mid-solve is what
		// a scene whose lighting moved does, and it wants the answer for the
		// lighting it has now rather than a blend of two of them.
		s_Data->PendingIrradianceRow = 0;
		s_Data->PendingIrradianceSweep = 0;
		s_Data->PendingIrradianceRegion = 0;
		// A bake request is the finishing kind. Said explicitly rather than
		// left at whatever a previous runtime request set.
		s_Data->PendingIrradianceContinuous = false;
	}

	void Renderer3D::RequestRuntimeIrradiance(const Ref<IrradianceVolume>& volume,
											  const IrradianceVolume::Region& region,
											  uint32_t raysPerCell, uint32_t rayBudget,
											  float hysteresis, bool feedback)
	{
		if (!s_Data)
			return;

		// **Asking again for the volume already being cached is a no-op.**
		// This is called every frame by the frame graph, and restarting the
		// sweep every frame would mean the sweep never advances past its first
		// band -- a cache that is always solving row zero and never anything
		// else. Only a *different* volume starts over.
		const bool sameVolume = s_Data->PendingIrradiance == volume
							 && s_Data->PendingIrradianceContinuous;

		s_Data->PendingIrradianceFeedback = feedback;
		s_Data->PendingIrradianceRays = Math::Clamp(raysPerCell, 64u, 4096u);
		s_Data->PendingIrradianceRayBudget = Math::Clamp(rayBudget, 4096u, 1u << 22);
		s_Data->PendingIrradianceHysteresis = Math::Clamp(hysteresis, 0.01f, 1.0f);
		s_Data->PendingIrradianceContinuous = true;
		// Taken every frame: this is what moves when the view does.
		s_Data->PendingIrradianceRegion2 = region;
		// Passes stops meaning anything in this mode -- there is no last sweep
		// -- but it is read below before the mode is known, so give it a value
		// that cannot end the solve early.
		s_Data->PendingIrradiancePasses = 64u;
		s_Data->PendingIrradiance = volume;

		if (!sameVolume)
		{
			s_Data->PendingIrradianceRow = 0;
			s_Data->PendingIrradianceSweep = 0;
			s_Data->PendingIrradianceRegion = 0;
		}
	}

	void Renderer3D::CancelRuntimeIrradiance()
	{
		// Only the continuous kind. A bake's request has an end of its own and
		// withdrawing it here would abandon a solve somebody is waiting on.
		if (!s_Data || !s_Data->PendingIrradianceContinuous)
			return;

		s_Data->PendingIrradiance = nullptr;
		s_Data->PendingIrradianceContinuous = false;
		s_Data->PendingIrradianceRow = 0;
		s_Data->PendingIrradianceSweep = 0;
		s_Data->PendingIrradianceRegion = 0;
	}

	bool Renderer3D::RuntimeIrradianceWarm()
	{
		// The sweep counter passes 1 the moment every region has been written
		// once, and the continuous path parks it there. So "has finished a
		// sweep" and "is at or past sweep 1" are the same question.
		return s_Data && s_Data->PendingIrradianceContinuous
			&& s_Data->PendingIrradianceSweep >= 1;
	}

	bool Renderer3D::HasRuntimeIrradiance()
	{
		return s_Data && s_Data->PendingIrradianceContinuous
			&& s_Data->PendingIrradiance != nullptr
			&& s_Data->IrradianceFillShader != nullptr;
	}

	bool Renderer3D::HasPendingIrradianceSolve()
	{
		// **And whether one could run**, not only whether one was asked for.
		// Without rays there is no fill shader and never will be on this
		// device, and a request that cannot be served would otherwise put a
		// pass that returns immediately into every frame for the rest of the
		// run. The request itself stands: a device that gains the shader --
		// the lit shaders recompile when the ray settings change -- solves it
		// then.
		return s_Data && s_Data->PendingIrradiance != nullptr
			&& s_Data->IrradianceFillShader != nullptr;
	}

	bool Renderer3D::SolvePendingIrradiance(RHICommandList& cmd)
	{
		if (!s_Data || !s_Data->PendingIrradiance)
			return false;

		const IrradianceVolume& field = *s_Data->PendingIrradiance;
		const uint32_t rows = field.Height() * field.Depth();

		// **How many rays a cell gets, and it is not a small number.**
		//
		// Sixty-four was the first answer and it is wrong for a reason no
		// average of the frame reveals: the variance left in a cell is
		// spatially *coherent* once trilinear interpolation spreads it over a
		// cell's width, so it reads as mottling -- patches of a wall slightly
		// lighter and darker than their neighbours, which the eye picks out
		// instantly and a mean absolute error scores at a third of a level.
		//
		// Variance falls as one over the ray count, and every one of these rays
		// is spent once. This is the whole argument for baking: the expensive,
		// converged answer is affordable precisely because no frame has to pay
		// for it twice.
		const int kRaysPerCell = (int)s_Data->PendingIrradianceRays;

		// **What one frame is allowed to solve**, held as a ray budget rather
		// than a cell count so that raising the rays above lengthens the solve
		// instead of deepening the hitch. Cells never drop below one row, or a
		// field wider than the budget would never finish.
		//
		// A megaray, not the old 131072: the solve only runs while a bake is
		// producing a file -- the editor's Bake button is a child process --
		// so the frame it stretches belongs to nobody, and bake time buys
		// accuracy at eight times the old rate.
		constexpr uint32_t kRayBudget = 1u << 20;
		// The runtime cache spends its own, much smaller, budget: this solve is
		// inside the frame being watched rather than one belonging to nobody.
		// Fixed per frame and independent of resolution, which is the property
		// the whole cache exists to get.
		const uint32_t rayBudget = s_Data->PendingIrradianceContinuous
								 ? s_Data->PendingIrradianceRayBudget
								 : kRayBudget;
		const uint32_t cellsPerFrame = Math::Max(rayBudget / (uint32_t)kRaysPerCell, 1u);
		const uint32_t rowsPerFrame = Math::Max(cellsPerFrame / Math::Max(field.Width(), 1u), 1u);

		// **Each pass is a bounce now, and this time the sentence is true.**
		//
		// Sweep 0 replaces: it reads a zeroed front and stores one bounce of
		// light. Every later sweep traces fresh rays whose hits read the
		// *previous sweep's completed field* -- the Jacobi discipline the
		// volume's swap pair enforces -- so its estimate carries one more
		// bounce of transport than the field it read, and the blend walks the
		// store toward the multi-bounce fixed point while averaging the ray
		// noise on the way. Half and half: the residual halves per sweep on
		// top of the transport's own decay (albedo is under one), so eight
		// sweeps sit within a fraction of a per cent of converged and sixteen
		// is bake-time cheap.
		//
		// This feedback existed once and was disabled for amplifying leaks --
		// a sealed room went 0.15 levels to 6.3 over eight sweeps -- BEFORE
		// the stored visibility existed. It is safe to close now because the
		// solve's field read always tests that visibility (see the fill's
		// defines), and the sealed-room fixtures are part of the acceptance
		// test rather than a surprise.
		const uint32_t kSweeps = s_Data->PendingIrradiancePasses;
		// **The cache blends by a fixed hysteresis instead.** A bake's schedule
		// -- replace on the first sweep, halve after -- converges fast and then
		// stops, which is right for a solve with an end. A cache has no end and
		// wants the opposite: a small, unchanging weight, so a cell revisited
		// forever is still rather than stepping each time the sweep comes
		// round. Sweep 0 still replaces, because a cell blending against an
		// unsolved field would take many revisits to climb out of black.
		const float blend = s_Data->PendingIrradianceContinuous
						  ? (s_Data->PendingIrradianceSweep == 0
								 ? 1.0f : s_Data->PendingIrradianceHysteresis)
						  : (s_Data->PendingIrradianceSweep == 0 ? 1.0f : 0.5f);

		const std::vector<IrradianceVolume::Region>& regions = field.Regions();
		if (regions.empty())
		{
			s_Data->PendingIrradiance = nullptr;
			return true;
		}

		const uint32_t index = Math::Min(s_Data->PendingIrradianceRegion,
										 (uint32_t)regions.size() - 1);
		// **The cache solves the box it is standing in now, not the one the
		// texture was built with.** They differ whenever the view has moved,
		// and following the stored one would solve the wrong places.
		const IrradianceVolume::Region& region =
			s_Data->PendingIrradianceContinuous ? s_Data->PendingIrradianceRegion2
											    : regions[index];

		// Far enough that a cell in the middle of a room can see its far wall,
		// derived from the box rather than guessed: a volume over a corridor
		// and one over a stadium want different answers and neither wants a
		// constant. Per region, since that is now what a box is.
		const float reach = Math::Max(Math::Length(region.Extents) * 4.0f, 10.0f);

		// **Cleared only on success.** A frame that could not solve -- no
		// traced set yet, no rays on this device -- leaves the request
		// standing, so the field is either solved or still asking, and never
		// quietly holding the placeholder while believing itself done.
		if (!SolveIrradianceVolume(cmd, s_Data->PendingIrradiance, region,
								   kRaysPerCell, reach,
								   s_Data->PendingIrradianceRow, rowsPerFrame, blend,
								   s_Data->PendingIrradianceFeedback))
		{
			return false;
		}

		s_Data->PendingIrradianceRow += rowsPerFrame;
		if (s_Data->PendingIrradianceRow < region.Height * region.Depth)
			return true;        // more of this region's sweep to go

		// **The next region, at the same sweep.** The sweep is the outer loop
		// and the region the inner one, which is not a matter of taste: the
		// flip below exchanges the *whole* texture, so a region carried to its
		// last sweep while its neighbours sat at their first would leave theirs
		// stale in whichever buffer came to the front.
		s_Data->PendingIrradianceRow = 0;
		if (++s_Data->PendingIrradianceRegion < (uint32_t)regions.size())
			return true;

		// Every region has had this sweep: what they wrote becomes the front
		// everything samples -- the frame's preview, and the next sweep's
		// feedback and history -- and the old front becomes the next target.
		// The bound descriptors catch up on the next frame's set write, which
		// is also the first read of the new front.
		s_Data->PendingIrradiance->FlipSolve();

		s_Data->PendingIrradianceRegion = 0;
		++s_Data->PendingIrradianceSweep;

		// **The cache never finishes.** It rolls straight into the next sweep,
		// and the counter parks at 1 rather than climbing: nothing below reads
		// it except the sweep-0 replace test, and letting it run away would
		// overflow eventually for no gain. Every sweep from here traces fresh
		// rays whose hits read the last completed field, so the stored light
		// keeps gaining bounces and keeps following the lights.
		if (s_Data->PendingIrradianceContinuous)
		{
			s_Data->PendingIrradianceSweep = 1;
			return true;
		}

		if (s_Data->PendingIrradianceSweep < kSweeps)
			return true;        // another sweep, one bounce deeper

		// Once per field, not once per solve: a scene whose lights move asks
		// for one every frame, and this is a fact about a field existing
		// rather than a running commentary.
		if (s_Data->AnnouncedIrradiance != s_Data->PendingIrradiance.get())
		{
			RV_CORE_INFO("Renderer3D: irradiance solved, {0} volume(s) in a "
						 "{1}x{2}x{3} atlas over {4} sweeps ({5})",
						 regions.size(), field.Width(), field.Height(), field.Depth(),
						 kSweeps,
						 s_Data->PendingIrradianceFeedback
							 ? "traced flavour, sweeps are bounces"
							 : "screen flavour, single bounce");
			s_Data->AnnouncedIrradiance = s_Data->PendingIrradiance.get();
		}

		s_Data->PendingIrradiance = nullptr;
		s_Data->PendingIrradianceSweep = 0;
		s_Data->PendingIrradianceRegion = 0;
		return true;
	}

	void Renderer3D::SetProbeVolumes(const std::vector<ProbeVolume>& probes)
	{
		if (!s_Data)
			return;

		s_Data->Probes.clear();
		s_Data->Probes.reserve(probes.size());

		for (const ProbeVolume& probe : probes)
		{
			// A probe with no reach selects nothing, and uploading it would
			// only make the shader's scan longer for a row that can never win.
			if (probe.Influence <= 0.0f)
				continue;

			Renderer3DData::GpuProbe row;
			row.Placement = Vec4(probe.Position, probe.Influence);
			row.Slot = Vec4((float)probe.Slot, 0.0f, 0.0f, 0.0f);
			s_Data->Probes.push_back(row);
		}

	}

	void Renderer3D::SetAreaEmitters(const std::vector<AreaEmitter>& emitters)
	{
		if (!s_Data)
			return;

		s_Data->Emitters.clear();
		s_Data->EmitterOwners.clear();
		s_Data->EmitterCdf.clear();

		for (const AreaEmitter& source : emitters)
		{
			if (s_Data->Emitters.size() >= kMaxAreaEmitters)
				break;

			// The rectangle spans centre +/- U +/- V, so its area is that of a
			// parallelogram with sides 2U and 2V.
			const float area = 4.0f * Math::Length(Math::Cross(source.TangentU, source.TangentV));

			// A degenerate rectangle has no solid angle to sample and would
			// divide by its own area. Dropped rather than clamped: an emitter
			// with no extent is a modelling accident, and giving it a
			// pretend size puts light where there is no surface.
			if (area <= 1e-6f)
				continue;

			Renderer3DData::GpuEmitter row;
			row.CentreArea = Vec4(source.Centre, area);
			// The plane normal rides in the three w lanes, which were uploaded
			// as zero and read nowhere. The shader used to rebuild it per
			// shadow ray; it depends on nothing that varies per ray.
			//
			// Degenerate rectangles never reach here -- the scene walk drops
			// them before an emitter is built -- but a zero cross would put a
			// NaN in every sample that touched this row, so it is guarded
			// rather than assumed.
			const Vec3 cross = Math::Cross(source.TangentU, source.TangentV);
			const float span = Math::Length(cross);
			const Vec3 normal = span > 1.0e-12f ? cross / span : Vec3(0.0f, 1.0f, 0.0f);

			row.TangentU = Vec4(source.TangentU, normal.x);
			row.TangentV = Vec4(source.TangentV, normal.y);
			row.Radiance = Vec4(source.Radiance, normal.z);

			// **The aiming table, when the surface has one and the heap can
			// hold its map.** Radiance carries the *unfolded* scalar in this
			// mode: the sampler multiplies by the texel it lands on, so
			// folding the mean in as well would count the map twice.
			if (source.Emission && source.Emission->Grid > 0 && source.EmissiveMap
				&& source.EmissiveSampler && s_Data->Bindless && s_Data->Heap)
			{
				const uint32_t grid = source.Emission->Grid;

				// w carries log2(grid), which the sampler uses to reach a
				// cell's row and column with a shift and a mask instead of a
				// runtime integer divide and modulo. The slot was already
				// uploaded and read nowhere; the grid is a power of two by
				// construction, which is asserted where it is declared.
				uint32_t shift = 0;
				while ((1u << shift) < grid)
					shift++;

				row.Aim = Vec4((float)s_Data->EmitterCdf.size(), (float)grid,
							   (float)s_Data->Heap->Slot(source.EmissiveMap,
														 source.EmissiveSampler),
							   (float)shift);
				row.UvToSurface0 = Vec4(source.UvToSurface[0], source.UvToSurface[1],
										source.UvToSurface[2], 0.0f);
				row.UvToSurface1 = Vec4(source.UvToSurface[3], source.UvToSurface[4],
										source.UvToSurface[5], 0.0f);
				s_Data->EmitterCdf.insert(s_Data->EmitterCdf.end(),
										  source.Emission->Cdf.begin(),
										  source.Emission->Cdf.end());
			}

			s_Data->Emitters.push_back(row);
			s_Data->EmitterOwners.push_back(source.Owner);
		}
	}

	void Renderer3D::SetWireframe(bool enabled)
	{
		if (!s_Data || s_Data->Wireframe == enabled)
			return;
		s_Data->Wireframe = enabled;
		s_Data->PipelineDirty = true;
	}

	void Renderer3D::EnsurePipeline()
	{
		if (!s_Data->PipelineDirty || !s_Data->Shader)
			return;

		GraphicsPipelineDesc desc;
		desc.Name = "Renderer3D.pbr";
		desc.Shader = s_Data->Shader;
		desc.Topology = PrimitiveTopology::TriangleList;
		// Primitives are generated counter-clockwise when viewed from outside.
		desc.Rasterizer.Cull = s_Data->Wireframe ? CullMode::None : CullMode::Back;
		desc.Rasterizer.Front = FrontFace::CounterClockwise;
		desc.Rasterizer.Polygon = s_Data->Wireframe ? PolygonMode::Line : PolygonMode::Fill;
		desc.Blend = BlendPreset::Opaque;
		desc.DepthStencil.DepthTestEnable = true;
		desc.DepthStencil.DepthWriteEnable = true;
		desc.ColorFormats = { s_Data->TargetColor };
		desc.Samples = s_Data->TargetSamples;
		if (s_Data->TargetVelocity != Format::Undefined)
			desc.ColorFormats.push_back(s_Data->TargetVelocity);
		if (s_Data->TargetNormal != Format::Undefined)
			desc.ColorFormats.push_back(s_Data->TargetNormal);
		if (s_Data->TargetIndirect != Format::Undefined)
			desc.ColorFormats.push_back(s_Data->TargetIndirect);
		desc.DepthFormat = s_Data->TargetDepth;

		s_Data->Pipeline = s_Data->Device->CreatePipeline(desc);

		// **The masked twin: the opaque pipeline with the cutout shader.**
		//
		// Every state identical, including depth write and back-face culling:
		// a cutout is opaque wherever it survives the test, so it occludes and
		// sorts exactly as opaque geometry does. Only the fragment stage
		// differs.
		//
		// Culling stays on. A single-sided railing quad will vanish when seen
		// from behind, which is the honest consequence of this engine having
		// no per-material two-sidedness yet -- glTF carries `doubleSided` and
		// nothing reads it. That is a separate gap, not a cutout one, and
		// making masked materials two-sided here would hide it while charging
		// every cutout twice the fill.
		if (s_Data->MaskedShader)
		{
			GraphicsPipelineDesc cutout = desc;
			cutout.Name = "Renderer3D.pbr.masked";
			cutout.Shader = s_Data->MaskedShader;
			s_Data->MaskedPipeline = s_Data->Device->CreatePipeline(cutout);
		}
		else
		{
			s_Data->MaskedPipeline = nullptr;
		}

		// **The prepass twin: same everything, minus the colour.**
		//
		// Same target formats and sample count, because it draws *into the
		// scene pass* rather than into a pass of its own -- so the pipeline
		// has to describe the attachments that pass binds even though it
		// writes none of them. Same depth state, because writing depth is the
		// entire job. Taken from `desc` before the skinned and layered
		// variants edit it, so it cannot inherit their shader by accident.
		if (s_Data->PrepassShader)
		{
			GraphicsPipelineDesc prepass = desc;
			prepass.Name = "Renderer3D.depthprepass";
			prepass.Shader = s_Data->PrepassShader;
			prepass.ColorWrite = false;
			s_Data->PrepassPipeline = s_Data->Device->CreatePipeline(prepass);

			// A set belongs to the layout it was allocated against.
			for (auto& frame : s_Data->SceneSlots)
				for (auto& slot : frame)
					slot.PrepassSet = nullptr;
		}
		else
		{
			s_Data->PrepassPipeline = nullptr;
		}

		// The meshlet twin: the same targets, samples, raster and depth state
		// -- a different front end and nothing else, the depth path's
		// contract again.
		if (s_Data->MeshletLitShader)
		{
			GraphicsPipelineDesc meshlet = desc;
			meshlet.Name = "Renderer3D.pbr.meshlet";
			meshlet.Shader = s_Data->MeshletLitShader;
			meshlet.VertexInput = {};
			s_Data->MeshletLitPipeline = s_Data->Device->CreatePipeline(meshlet);
			if (!s_Data->MeshletLitPipeline)
				RV_CORE_ERROR("Renderer3D: meshlet lit pipeline failed; the "
							  "vertex path draws the lit pass");
		}

		// The same description with the other shader. Its vertex layout is
		// reflected from that shader, so the wider vertex needs nothing stated
		// here -- and both pipelines share every raster and depth setting,
		// which is what stops a skinned mesh being subtly differently lit.
		if (s_Data->SkinnedShader)
		{
			desc.Name = "Renderer3D.pbr.skinned";
			desc.Shader = s_Data->SkinnedShader;
			s_Data->SkinnedPipeline = s_Data->Device->CreatePipeline(desc);
		}

		// And the layered one, again from the same description: the static
		// vertex layout, every raster and depth setting shared, so a terrain
		// chunk is rasterised exactly as the crate resting on it is.
		if (s_Data->LayeredShader)
		{
			desc.Name = "Renderer3D.pbr.layered";
			desc.Shader = s_Data->LayeredShader;
			s_Data->LayeredPipeline = s_Data->Device->CreatePipeline(desc);
		}

		// The transparent variant, drawn in a later pass into the accumulate
		// and revealage attachments the frame graph clears for it.
		//
		// **Depth-tested and not depth-written**, which is the whole of
		// order-independent transparency's contract: glass is hidden by the
		// wall in front of it and does not hide the glass behind it. And
		// two-sided, because the far side of a windscreen is as real as the
		// near one and back-face culling would delete half of it.
		//
		// The formats are stated rather than taken from SetTargetFormats: they
		// are the frame graph's own choice for those two attachments and
		// ParticleRenderer names the same two literals for the same pass.
		if (s_Data->TransparentShader)
		{
			GraphicsPipelineDesc blended;
			blended.Name = "Renderer3D.pbr.transparent";
			blended.Shader = s_Data->TransparentShader;
			blended.Topology = PrimitiveTopology::TriangleList;
			blended.Rasterizer.Cull = CullMode::None;
			blended.Rasterizer.Front = FrontFace::CounterClockwise;
			blended.Rasterizer.Polygon = s_Data->Wireframe ? PolygonMode::Line : PolygonMode::Fill;
			blended.DepthStencil.DepthTestEnable = true;
			blended.DepthStencil.DepthWriteEnable = false;
			blended.ColorFormats = { Format::R16G16B16A16_SFLOAT, Format::R8_UNORM };
			blended.Samples = s_Data->TargetSamples;
			blended.BlendPerAttachment = { BlendPreset::WeightedAccumulate,
										   BlendPreset::WeightedRevealage };
			blended.DepthFormat = s_Data->TargetDepth;
			s_Data->TransparentPipeline = s_Data->Device->CreatePipeline(blended);

			// Water's, identical but for the shader. **Cull::None matters here
			// rather than being inherited carelessly**: a wave tall enough to
			// be seen from below is a wave whose back faces are the surface,
			// and culling them punches holes in the sea exactly where it is
			// most obviously moving.
			if (s_Data->WaterShader)
			{
				blended.Name = "Renderer3D.water";
				blended.Shader = s_Data->WaterShader;
				s_Data->WaterPipeline = s_Data->Device->CreatePipeline(blended);
			}
			else
			{
				s_Data->WaterPipeline = nullptr;
			}
		}
		else
		{
			s_Data->TransparentPipeline = nullptr;
			s_Data->WaterPipeline = nullptr;
		}

		// The traced bounce: a fullscreen triangle with no depth of its own and
		// one colour out. Its target is the indirect buffer, whose format the
		// caller states -- not this pipeline's business what size it is, which
		// is the whole point of moving the work out of an attachment.
		// Its target is the indirect buffer unless a caller says otherwise, so
		// the pipeline -- and with it the whole set 0 the shader reflects --
		// is built and validated here rather than the first time a frame
		// happens to trace.
		if (s_Data->GiTargetColor == Format::Undefined)
			s_Data->GiTargetColor = s_Data->TargetIndirect;

		if (s_Data->GiShader && s_Data->GiTargetColor != Format::Undefined)
		{
			GraphicsPipelineDesc gi;
			gi.Name = "Renderer3D.gi";
			gi.Shader = s_Data->GiShader;
			gi.Topology = PrimitiveTopology::TriangleList;
			gi.Rasterizer.Cull = CullMode::None;
			gi.Blend = BlendPreset::Opaque;
			gi.DepthStencil.DepthTestEnable = false;
			gi.DepthStencil.DepthWriteEnable = false;
			gi.ColorFormats = { s_Data->GiTargetColor };
			gi.DepthFormat = Format::Undefined;
			s_Data->GiPipeline = s_Data->Device->CreatePipeline(gi);
		}
		else
		{
			s_Data->GiPipeline = nullptr;
		}

		s_Data->PipelineDirty = false;

		// Resource sets are tied to a pipeline layout, so they go with it and
		// are recreated on demand.
		//
		// **Every set in the slot, and forgetting one is a crash rather than a
		// wrong picture.** Resizing the editor rebuilds these pipelines,
		// because the target formats are part of them; a set left pointing at
		// the layout that has just been destroyed faults the next time it is
		// bound. GpuSet was added without being added here, and that is what
		// resizing found.
		for (auto& frame : s_Data->SceneSlots)
		{
			for (auto& slot : frame)
			{
				slot.Set.reset();
				slot.SkinnedSet.reset();
				slot.LayeredSet.reset();
				slot.MaskedSet.reset();
				slot.GpuSet.reset();
				slot.MaskedGpuSet.reset();
				// Every set in the slot. Forgetting one is a crash on resize
				// rather than a wrong picture -- which is the note above, and
				// is exactly how GpuSet was found.
				slot.TransparentSet.reset();
				slot.TransparentGpuSet.reset();
				slot.WaterSets.clear();
				slot.WaterSetCursor = 0;
			}
		}
	}

	void Renderer3D::BeginFrame()
	{
		if (!s_Data)
			return;

		// The GPU has finished with this frame's slots, so they are reusable.
		s_Data->SceneCursor = 0;
		s_Data->ShadowCursor = 0;
		s_Data->CulledShadowCursor = 0;
		// And with the heap slots retired when this frame index last came
		// round, which is what lets the heap recycle them now.
		if (s_Data->Heap)
			s_Data->Heap->BeginFrame(s_Data->Device->GetFrameIndex());
		// Last frame's acceleration structure is last frame's.
		RayShadows::BeginFrame();
		// Rewinds the ring of per-view cull results, and forgets last frame's
		// object table -- which must happen before any scene refreshes its
		// draw list, and does, because this runs once at the top of the frame.
		GpuCull::BeginFrame();
		VoxelGI::BeginFrame();
		// Accumulated across every scene drawn this frame rather than reset per
		// scene, or the statistics panel would only ever show the last viewport.
		s_Data->DrawCalls = 0;
		s_Data->Triangles = 0;
		s_Data->Culled = 0;
		s_Data->IndirectDraws = 0;
	}

	void Renderer3D::BeginScene(const Camera& camera, const Mat4& cameraTransform,
								const LightList& lights, const SceneEnvironment& environment,
								const RenderSettings& render,
								const Ref<RHITexture>& environmentMap,
								const Ref<RHITexture>& irradianceMap,
								const Vec2& jitter)
	{
		if (!s_Data)
			return;

		s_Data->Scene = {};

		const Mat4 viewProjection = camera.GetProjection() * Math::Inverse(cameraTransform);

		// What *this frame chain* drew with last time, which is the only camera
		// a velocity here can mean anything against: the history about to be
		// reprojected is that camera's image.
		//
		// **This used to be one matrix for the whole process**, written by
		// every BeginScene and read by the next, justified by "the scene pass
		// is the last caller in a frame". That is true of the runtime and false
		// of the editor, which draws the viewport and the game view in one
		// frame from two cameras: the game view then differenced itself against
		// the *editor* camera and TAA fetched its history from wherever that
		// gap pointed. The result was a faint second copy of the whole scene
		// over the game view that slid about as the editor camera moved -- an
		// image answering to an input that has nothing to do with it.
		// ENGINE-NOTES 7u.
		//
		// Null for anything with no history to reproject: a probe capture's six
		// faces, a shadow cascade, a chain with temporal filtering off. Those
		// difference against themselves, which is zero velocity -- the same
		// value the velocity attachment is cleared to.
		CameraMotion* motion = Renderer::GetCameraMotion();

		s_Data->Scene.PreviousViewProjection = motion ? motion->ViewProjection : viewProjection;
		const Vec2 previousJitter = motion ? motion->Jitter : jitter;
		s_Data->Scene.Jitter = Vec4(jitter.x, jitter.y, previousJitter.x, previousJitter.y);

		s_Data->Scene.ViewProjection = viewProjection;

		// The lights' glow draws with the same jittered camera the geometry
		// does, so its discs move with the scene rather than against it.
		LightGlow::BeginScene(viewProjection, cameraTransform, camera.GetProjection());

		// Recorded after reading, and only for a real chain. What this frame
		// draws with is what the next frame of *this* chain reprojects from.
		if (motion)
		{
			motion->ViewProjection = viewProjection;
			motion->Jitter = jitter;
		}

		// Last frame's reflection trace, if this chain has one and the
		// feature is on. Null for everything else -- probe faces, shadow
		// casters, chains with SSR off, the first frame of a chain -- and
		// null binds an empty texture at intensity zero, which the shader
		// treats as "the probe answers". The row sign is the same fact
		// taa_resolve's FlipY states: an NDC y-offset runs *down* the rows of
		// a target whose row 0 is the top. ENGINE-NOTES 7af.
		const Renderer::ScreenReflections* reflections = Renderer::GetScreenReflections();
		const bool haveReflections = reflections && reflections->Texture
								  && reflections->Intensity > 0.0f;
		const float rowSign = s_Data->Device->GetBackend() == Backend::Vulkan ? -1.0f : 1.0f;
		// The traced bounce's dial, and the counter its directions are hashed
		// from: incremented per BeginScene rather than per frame, so a probe
		// face and the main view do not draw the same four rays either.
		{
			static uint32_t giFrame = 0;
			// w is the bounce's reach in metres; zero means the shader's own
			// unbounded default, which is what a caller that never set it gets.
			s_Data->Scene.GlobalIllumination = Vec4(Renderer::GetGlobalIllumination(),
													(float)(giFrame++ & 0xFFFFu),
													(float)Renderer::GetGiBounces(),
													Renderer::GetGiReach());
		}

		// zw: the gloss window a *traced* mirror ray is weighed in over. The
		// two forms never run together -- the screen-space passes are not
		// added when the traced one is compiled in -- so the pair is free, and
		// putting it here costs no new binding.
		const Vec2 gloss = Renderer::GetReflectionGloss();
		s_Data->Scene.ScreenReflections = Vec4(haveReflections ? reflections->Intensity : 0.0f,
											   rowSign, gloss.x, gloss.y);

		// The same two numbers for the indirect buffer, read the same way.
		const Renderer::ScreenIndirect* indirect = Renderer::GetScreenIndirect();
		const bool haveIndirect = indirect && indirect->Texture && indirect->Intensity > 0.0f;
		// z carries the traced reflection's firefly floor, which had been a
		// constant in the shader. Here because this vec4's zw were the only
		// pair in the block already declared in *both* mirrors and written as
		// zero -- adding a field would have meant editing pbr_fragment.glsl
		// and scene_vertex.glsl in step, and 7at is the note about what
		// happens on OpenGL when that is done to one of them only.
		s_Data->Scene.Indirect = Vec4(haveIndirect ? indirect->Intensity : 0.0f,
									  rowSign, Renderer::GetReflectionFloor(), 0.0f);
		s_Data->Scene.CameraPosition = Vec4(Vec3(cameraTransform[3]), 1.0f);
		s_Data->Scene.Ambient = Vec4(environment.AmbientColor, environment.AmbientIntensity);

		// Every light, with no cap. The shader reads them from a storage buffer
		// whose length is decided here rather than declared in a block.
		//
		// Reordered so the directional lights come first. They have no position
		// and reach every cell, so binning them would put a copy of each in all
		// 3456 cells; instead the shader reads the first N unconditionally and
		// takes the rest from its own cell.
		const int lightCount = (int)lights.size();

		s_Data->LightOrder.clear();
		s_Data->LightOrder.reserve(lights.size());
		for (uint32_t i = 0; i < (uint32_t)lights.size(); i++)
		{
			if (lights[i].Type == Light::LightType::Directional)
				s_Data->LightOrder.push_back(i);
		}

		const uint32_t directionalCount = (uint32_t)s_Data->LightOrder.size();

		for (uint32_t i = 0; i < (uint32_t)lights.size(); i++)
		{
			if (lights[i].Type != Light::LightType::Directional)
				s_Data->LightOrder.push_back(i);
		}

		s_Data->Ordered.clear();
		s_Data->Ordered.reserve(lights.size());
		for (uint32_t index : s_Data->LightOrder)
			s_Data->Ordered.push_back(lights[index]);

		// **WR-17: the light cutoff.** Clamped on the frame's own copy, before
		// the cluster grid bins by range and before the range reaches the
		// shader, so a lamp past the cutoff leaves every cell and every pixel's
		// loop at once -- the scene's lights keep the ranges they were authored
		// with, and so does the lighting hash. Left alone while baking: the
		// solve must see the lights the hash describes.
		{
			const EngineConfig& config = EngineConfig::Get();
			const float cutoff = config.HasLightCutoffOverride ? config.LightCutoffOverride
															   : render.LightCutoffDistance;
			if (cutoff > 0.0f && !config.BakeLighting && !config.ForceLightingBake)
			{
				for (LightRenderData& light : s_Data->Ordered)
					if (light.Type != Light::LightType::Directional)
						light.Range = Math::Min(light.Range, cutoff);
			}
		}

		s_Data->LightScratch.clear();
		s_Data->LightScratch.reserve(lights.size());

		for (const LightRenderData& light : s_Data->Ordered)
		{
			const bool directional = light.Type == Light::LightType::Directional;

			GpuLight entry{};
			// w == 0 tells the shader distance attenuation does not apply.
			entry.Position = Vec4(light.Position, directional ? 0.0f : 1.0f);
			// Copied through, still unit length -- see the normalise in
			// Scene.cpp that produces it. The shaders do not re-normalise.
			// w is the emitter's radius in metres (Light::SourceRadius): the
			// lane was spare, and the sphere-light specular reads it there.
			entry.Direction = Vec4(light.Direction, light.SourceRadius);
			entry.Color = Vec4(light.Color, light.Intensity);

			// Cones are compared as cosines in the shader, so convert once here
			// rather than per fragment. Equal angles disable the cone test.
			const float inner = light.Type == Light::LightType::Spot
							  ? Math::Cos(Math::Radians(light.InnerCone)) : 1.0f;
			const float outer = light.Type == Light::LightType::Spot
							  ? Math::Cos(Math::Radians(light.OuterCone)) : 1.0f;

			// w carries Light::IsBaked, read only by the irradiance fill: the
			// solve must shade its hits without the realtime lights, or their
			// bounce would be stored into files their toggles cannot rename.
			entry.Params = { Math::Max(light.Range, 0.0001f), inner, outer,
							 light.IsBaked ? 1.0f : 0.0f };
			entry.Shadow = Vec4(0.0f);

			s_Data->LightScratch.push_back(entry);
		}
		s_Data->Scene.LightCount = lightCount;

		// Zero intensity is how "nothing to reflect" is expressed: the sampler
		// still has to be bound, because the shader declares it either way, but
		// the term it feeds multiplies out.
		const float mips = environmentMap ? (float)environmentMap->GetDesc().MipLevels : 1.0f;
		s_Data->Scene.Environment = {
			environmentMap ? environment.SkyIntensity : 0.0f,
			Math::Max(mips - 1.0f, 0.0f),
			Math::Cos(environment.SkyRotation),
			Math::Sin(environment.SkyRotation),
		};

		// Sent, not derived. A prefiltered cube stops at its roughness levels,
		// so exp2(highest mip) is a sixteenth of its real face size -- which
		// quietly turned the reflection's anti-aliasing term off.
		s_Data->Scene.EnvironmentSize = {
			environmentMap ? (float)environmentMap->GetWidth() : 1.0f, 0.0f, 0.0f, 0.0f,
		};

		// Cascades come from ShadowMap rather than being passed in: this runs
		// once per viewport, and the cascades belong to the frame.
		s_Data->Scene.CameraForward =
			Vec4(Math::Normalize(Vec3(cameraTransform * Vec4(0, 0, -1, 0))), 0.0f);
		s_Data->Scene.ShadowParams = Vec4(0.0f, 0.0f, 0.0f, -1.0f);

		// WR-17: the render settings' shadow-ray falloff, or the run's
		// --shadow-rays override, into the block's last row. Sent under maps
		// too; the shader reads it only on the traced path.
		{
			const EngineConfig& config = EngineConfig::Get();
			s_Data->Scene.ShadowRayFade = config.HasShadowRayOverride
				? Vec4((float)config.ShadowRayShapeOverride, config.ShadowRayStartOverride,
					   config.ShadowRayEndOverride, config.ShadowRayShareOverride)
				: Vec4((float)(uint32_t)render.ShadowRayFade, render.ShadowRayFadeStart,
					   render.ShadowRayFadeEnd,
					   render.ShadowRayFade == ShadowRayFalloff::Share ? render.ShadowRayShare
																	   : render.ShadowRayFloor);
		}

		const uint32_t cascadeCount = ShadowMap::HasCascades() ? ShadowMap::GetCascadeCount() : 0;
		if (s_Data->RayShadowsOn)
		{
			// Traced: ShadowParams.x is a flag rather than a count -- one when
			// a structure was built this frame, zero when not (a probe
			// capture, or before the first RenderShadows), and the shader's
			// "count <= 0 means lit" reads it the same way either path.
			s_Data->Scene.ShadowParams = {
				RayShadows::IsActive() ? 1.0f : 0.0f,
				render.ShadowNormalOffset,
				0.0f,
				0.0f,
			};
		}
		else if (cascadeCount > 0)
		{
			const ShadowCascade* cascades = ShadowMap::GetCascades();
			const uint32_t resolution = Math::Max(ShadowMap::GetResolution(), 1u);

			for (uint32_t i = 0; i < cascadeCount; i++)
			{
				s_Data->Scene.CascadeLookup[i] = cascades[i].LookupMatrix;
				s_Data->Scene.CascadeSplits[i] = cascades[i].SplitDepth;
				s_Data->Scene.CascadeTexel[i] = cascades[i].TexelWorldSize;
			}

			// The last cascade's split repeated, so a fragment past the end
			// selects the coarsest map rather than reading an uninitialised
			// split and picking arbitrarily.
			for (uint32_t i = cascadeCount; i < ShadowMap::kMaxCascades; i++)
			{
				s_Data->Scene.CascadeLookup[i] = cascades[cascadeCount - 1].LookupMatrix;
				s_Data->Scene.CascadeSplits[i] = cascades[cascadeCount - 1].SplitDepth;
				s_Data->Scene.CascadeTexel[i] = cascades[cascadeCount - 1].TexelWorldSize;
			}

			s_Data->Scene.ShadowParams = {
				(float)cascadeCount,
				render.ShadowNormalOffset,
				1.0f / (float)resolution,
				0.0f,
			};
		}
		else
		{
			s_Data->Scene.ShadowParams.y = render.ShadowNormalOffset;
		}

		// Which map each light got, decided when the shadows were rendered --
		// or, under rays (7an), which kind of ray, and no map.
		//
		// Under maps only the first few can have one: there are four spot maps
		// and four point cubes however many lights a scene has. Past that a
		// light lights and does not cast, which is the budget rather than the
		// cap that used to sit here.
		// Indexed by the light's *original* position, not its position after the
		// reorder above -- ShadowMap assigned slots while walking the scene, and
		// asking it about the wrong light gives a light somebody else's map.
		for (uint32_t slot = 0; slot < (uint32_t)s_Data->LightOrder.size(); slot++)
		{
			const uint32_t original = s_Data->LightOrder[slot];
			const LocalShadow& assigned = ShadowMap::GetAssignment(original);

			s_Data->LightScratch[slot].Shadow = {
				(float)(uint32_t)assigned.Type,
				(float)Math::Max(assigned.Slot, 0),
				assigned.FarClip,
				assigned.TexelScale,
			};

			if (assigned.Type == LocalShadow::Kind::Spot && assigned.Slot >= 0)
				s_Data->Scene.SpotLookup[assigned.Slot] = assigned.LookupMatrix;
		}

		EnsurePipeline();
		if (!s_Data->Pipeline)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		Renderer3DData::SceneSlot& slot = AcquireSceneSlot();
		s_Data->ActiveScene = &slot;

		// The light buffer. Always at least one element: a zero-length storage
		// buffer is not a binding, and a scene with no lights at all still has
		// to bind something the layout is happy with.
		const uint32_t lightSlots = Math::Max<uint32_t>((uint32_t)s_Data->LightScratch.size(), 1u);
		if (!EnsureInstanceBuffer(slot.Lights, slot.LightCapacity, lightSlots,
								  sizeof(GpuLight), "Renderer3D.lights"))
		{
			return;
		}

		if (!s_Data->LightScratch.empty())
		{
			slot.Lights->Upload(s_Data->LightScratch.data(),
								s_Data->LightScratch.size() * sizeof(GpuLight));
		}

		// The cluster grid. Built here rather than in the scene, because it is
		// keyed to the camera this pass is drawing with -- the editor's two
		// viewports need one each, for the same reason their shadow cascades do.
		s_Data->Grid.Build(camera, cameraTransform, s_Data->Ordered, directionalCount);

		const auto& cells = s_Data->Grid.Cells();
		const auto& cellIndices = s_Data->Grid.Indices();
		const uint32_t indexSlots = Math::Max<uint32_t>((uint32_t)cellIndices.size(), 1u);

		if (!EnsureInstanceBuffer(slot.Cells, slot.CellCapacity, (uint32_t)cells.size(),
								  sizeof(LightGrid::Cell), "Renderer3D.cells") ||
			!EnsureInstanceBuffer(slot.CellIndices, slot.CellIndexCapacity, indexSlots,
								  sizeof(uint32_t), "Renderer3D.cellIndices"))
		{
			return;
		}

		slot.Cells->Upload(cells.data(), cells.size() * sizeof(LightGrid::Cell));
		if (!cellIndices.empty())
			slot.CellIndices->Upload(cellIndices.data(), cellIndices.size() * sizeof(uint32_t));

		float nearPlane = 0.1f, farPlane = 1000.0f;
		LightGrid::DepthRangeOf(camera.GetProjection(), nearPlane, farPlane);

		s_Data->Scene.ClusterGrid = {
			(float)LightGrid::kTilesX, (float)LightGrid::kTilesY,
			(float)LightGrid::kSlices, (float)directionalCount,
		};
		s_Data->Scene.ClusterDepth = {
			nearPlane, farPlane,
			LightGrid::SliceScale(nearPlane, farPlane),
			LightGrid::SliceBias(nearPlane, farPlane),
		};

		// **The probes, written here rather than where they are set.**
		// BeginScene clears the whole block (`s_Data->Scene = {}` above), so
		// anything a caller wrote into it beforehand is gone by now -- which
		// is exactly what happened to the first draft of this: SetProbeVolumes
		// filled the rows, BeginScene zeroed them a moment later, and every
		// fragment blended between fifteen empty probes and took all of its
		// ambient from the sky. The room went 35% darker and the cause was
		// nowhere near the shader.
		//
		// s_Data->Probes outlives the clear, so the copy belongs on this side
		// of it. Capped at the block's fifteen rows, which is ProbeArray's own
		// cap too, so a scene cannot hold more than fit.
		const uint32_t probeRows = Math::Min((uint32_t)s_Data->Probes.size(), 15u);
		s_Data->Scene.ProbeCount = Vec4((float)probeRows, 0.0f, 0.0f, 0.0f);
		for (uint32_t i = 0; i < probeRows; i++)
		{
			s_Data->Scene.ProbePlacement[i] = s_Data->Probes[i].Placement;
			s_Data->Scene.ProbeSlot[i] = s_Data->Probes[i].Slot;
		}

		// **Every volume's box, five rows apiece.** See IrradianceBox for the
		// layout. The rows that take a world vector into a box's own axes are
		// its rotation's columns read across -- sent rather than inverted in
		// the shader, because an inverse per fragment for a matrix that
		// changes once a frame is work in the wrong place.
		{
			const uint32_t volumes = (uint32_t)s_Data->IrradianceRegions.size();
			for (uint32_t i = 0; i < volumes; i++)
			{
				const IrradianceVolume::Region& region = s_Data->IrradianceRegions[i];
				Vec4* row = &s_Data->Scene.IrradianceBox[i * 5];
				row[0] = Vec4(region.Centre, (float)region.ZOffset);
				row[1] = Vec4(region.Extents, region.Spacing);
				row[2] = Vec4(region.Rotation[0], (float)region.Width);
				row[3] = Vec4(region.Rotation[1], (float)region.Height);
				row[4] = Vec4(region.Rotation[2], (float)region.Depth);
			}

			// The count, where a lone field's "is there one at all" flag used
			// to sit -- zero still means every reader falls back to the flat
			// ambient, which is what kept this compatible.
			s_Data->Scene.IrradianceExtents = Vec4(1.0f, 1.0f, 1.0f, (float)volumes);
			// The atlas's own depth: the stride from one tile to the next, and
			// the number every reader divides by to address a slice.
			s_Data->Scene.IrradianceCentre =
				Vec4(0.0f, 0.0f, 0.0f,
					 s_Data->Irradiance ? (float)s_Data->Irradiance->Depth() : 0.0f);
		}

		// Written after the grid, because the grid decides the two vectors
		// above and the block is uploaded once.
		slot.Buffer->Upload(&s_Data->Scene, sizeof(SceneUniforms));

		// Every set gets every write. Three sets rather than one because each
		// is allocated against its own pipeline's layout, and one loop rather
		// than three copies because the day they drift is the day a skinned
		// mesh -- or a terrain chunk -- is lit from a different environment
		// than the mesh beside it.
		if (s_Data->SkinnedPipeline && !slot.SkinnedSet)
			slot.SkinnedSet = s_Data->Device->CreateResourceSet(s_Data->SkinnedPipeline, 0);
		if (s_Data->LayeredPipeline && !slot.LayeredSet)
			slot.LayeredSet = s_Data->Device->CreateResourceSet(s_Data->LayeredPipeline, 0);
		if (s_Data->MaskedPipeline && !slot.MaskedSet)
			slot.MaskedSet = s_Data->Device->CreateResourceSet(s_Data->MaskedPipeline, 0);
		if (s_Data->TransparentPipeline && !slot.TransparentSet)
			slot.TransparentSet = s_Data->Device->CreateResourceSet(s_Data->TransparentPipeline, 0);
		if (s_Data->TransparentPipeline && !slot.TransparentGpuSet)
			slot.TransparentGpuSet = s_Data->Device->CreateResourceSet(s_Data->TransparentPipeline, 0);

		// The GPU-driven path's set: the same set 0 in every binding but the
		// visible indices, so it is populated alongside the others here and
		// differs only where EndScene says.
		if (s_Data->Pipeline && !slot.GpuSet)
			slot.GpuSet = s_Data->Device->CreateResourceSet(s_Data->Pipeline, 0);
		if (s_Data->MaskedPipeline && !slot.MaskedGpuSet)
			slot.MaskedGpuSet = s_Data->Device->CreateResourceSet(s_Data->MaskedPipeline, 0);

		// The prepass's, on the same terms. It is filled where the GPU path's
		// visible indices are written rather than in the loop below, because
		// the loop writes bindings this layout does not declare -- and a
		// binding a layout does not declare is a validation error, not a
		// harmless extra.
		if (s_Data->PrepassPipeline && !slot.PrepassSet)
			slot.PrepassSet = s_Data->Device->CreateResourceSet(s_Data->PrepassPipeline, 0);

		Ref<RHIResourceSet> targets[] = { slot.Set, slot.SkinnedSet, slot.LayeredSet,
										  slot.MaskedSet, slot.GpuSet, slot.MaskedGpuSet,
										  slot.TransparentSet, slot.TransparentGpuSet };

		for (const Ref<RHIResourceSet>& sceneSet : targets)
		{
		if (!sceneSet)
			continue;

		sceneSet->SetUniformBuffer(0, slot.Buffer, 0, sizeof(SceneUniforms));
		sceneSet->SetStorageBuffer(8, slot.Lights, 0, (uint64_t)lightSlots * sizeof(GpuLight));
		sceneSet->SetStorageBuffer(9, slot.Cells, 0, cells.size() * sizeof(LightGrid::Cell));
		sceneSet->SetStorageBuffer(10, slot.CellIndices, 0,
								   (uint64_t)indexSlots * sizeof(uint32_t));
		// Never left unwritten. A binding the layout declares and the set does
		// not fill is a validation error rather than a harmless omission, which
		// is the same lesson the tonemap pass learned about its bloom input.
		//
		// Both are cube *arrays* now -- the probe arrays, whose slot 0 is the
		// sky -- so the stand-in has to be one too. A plain cube here is a
		// different descriptor type, which is a validation error rather than a
		// dark reflection.
		// **The irradiance field: one volume, three tiles.** Binding 18 -- the
		// first free one in set 0, whose layout every lit pipeline family
		// reflects, which is why it is declared in the shared fragment include
		// rather than in one shader.
		//
		// It was three bindings, one per colour channel, until the count
		// mattered: OpenGL gives a fragment shader thirty-two samplers and the
		// layered terrain variant was at thirty without this.
		sceneSet->SetTexture(18,
							 s_Data->Irradiance ? s_Data->Irradiance->Texture()
												: TextureLoader::BlackVolume(*s_Data->Device),
							 s_Data->Irradiance ? s_Data->Irradiance->Sampler()
												: s_Data->PointSampler);

		sceneSet->SetTexture(1, environmentMap ? environmentMap
											   : TextureLoader::BlackCubeArray(*s_Data->Device),
							 s_Data->EnvironmentSampler);
		sceneSet->SetTexture(5, irradianceMap ? irradianceMap
											  : TextureLoader::BlackCubeArray(*s_Data->Device),
							 s_Data->EnvironmentSampler);

		// The BRDF table. Never null in practice, but the binding has to be
		// filled either way, and a white 1x1 reads as "reflect everything"
		// rather than as nothing.
		if (const Ref<RHITexture> brdf = EnvironmentIBL::GetBRDF())
			sceneSet->SetTexture(6, brdf, EnvironmentIBL::GetBRDFSampler());
		else
			sceneSet->SetTexture(6, TextureLoader::White(*s_Data->Device),
								 s_Data->EnvironmentSampler);

		// Last frame's reflection trace, or a 1x1 zero whose alpha -- the
		// confidence -- is zero, so the shader mixes nothing in even if the
		// intensity branch were somehow taken. Filtered, clamped: the same
		// sampler the environment uses.
		sceneSet->SetTexture(12, haveReflections ? reflections->Texture
												 : TextureLoader::TransparentBlack(*s_Data->Device),
							 s_Data->EnvironmentSampler);

		// Last frame's indirect diffuse, or a 1x1 transparent black whose
		// alpha -- the confidence -- is zero (7av). Binding 16: 7 is the
		// instance buffer and 11 the bone buffer, both in the vertex stage of
		// a shader that includes this one, and one binding with two descriptor
		// types is a pipeline that does not build.
		sceneSet->SetTexture(16, haveIndirect ? indirect->Texture
											  : TextureLoader::TransparentBlack(*s_Data->Device),
							 s_Data->EnvironmentSampler);

		// The structure the shadow ray traces into, only where the layout
		// declares it. This frame's when one was built, the empty one when
		// not; never left unwritten.
		if (s_Data->RayShadowsOn)
			sceneSet->SetAccelerationStructure(RayShadows::kBinding, RayShadows::GetStructure());

		// All four, always. A comparison sampler the layout declares and the
		// set does not fill is a validation error; a 1x1 depth of 1.0 is the
		// harmless answer, because under LessOrEqual every comparison against
		// the far plane passes and the surface reads as lit.
		{
			const Ref<RHISampler> shadowSampler = ShadowMap::GetSampler();
			const Ref<RHITexture> empty = ShadowMap::GetEmptyTexture();

			for (uint32_t i = 0; i < ShadowMap::kMaxCascades; i++)
			{
				Ref<RHITexture> cascade = i < cascadeCount ? ShadowMap::GetCascadeTexture(i)
														  : nullptr;
				sceneSet->SetTexture(2, cascade ? cascade : empty, shadowSampler, i);
			}

			const Ref<RHITexture> emptyCube = ShadowMap::GetEmptyCube();
			for (uint32_t i = 0; i < ShadowMap::kMaxLocal; i++)
			{
				const Ref<RHITexture> spot = ShadowMap::GetSpotTexture(i);
				sceneSet->SetTexture(3, spot ? spot : empty, shadowSampler, i);

				const Ref<RHITexture> point = ShadowMap::GetPointTexture(i);
				sceneSet->SetTexture(4, point ? point : emptyCube, shadowSampler, i);
			}
		}
		}   // both sets

		// The traced bounce's set, written on its own because its layout is a
		// subset: no shadow maps (it traces its shadows), no cluster grid (a
		// hit is not on screen and has no cluster), no BRDF table, no
		// reflection or indirect history. Writing a binding a layout does not
		// declare is a validation error, not a harmless extra, so this cannot
		// simply join the loop above.
		if (s_Data->GiPipeline)
		{
			if (!slot.GiSet)
				slot.GiSet = s_Data->Device->CreateResourceSet(s_Data->GiPipeline, 0);

			slot.GiSet->SetUniformBuffer(0, slot.Buffer, 0, sizeof(SceneUniforms));
			slot.GiSet->SetStorageBuffer(8, slot.Lights, 0,
										 (uint64_t)lightSlots * sizeof(GpuLight));
			// The GI set reflects the same include, so it declares the same
			// binding and must fill it too.
			slot.GiSet->SetTexture(18,
								   s_Data->Irradiance
									   ? s_Data->Irradiance->Texture()
									   : TextureLoader::BlackVolume(*s_Data->Device),
								   s_Data->Irradiance ? s_Data->Irradiance->Sampler()
													  : s_Data->PointSampler);

			slot.GiSet->SetTexture(1, environmentMap ? environmentMap
													 : TextureLoader::BlackCubeArray(*s_Data->Device),
								   s_Data->EnvironmentSampler);
			slot.GiSet->SetTexture(5, irradianceMap ? irradianceMap
													: TextureLoader::BlackCubeArray(*s_Data->Device),
								   s_Data->EnvironmentSampler);
			slot.GiSet->SetAccelerationStructure(RayShadows::kBinding, RayShadows::GetStructure());
		}

		// Not committed and not bound yet.
		//
		// The instance buffer is part of this same set, and its contents are
		// not known until every mesh has been submitted -- so the commit waits
		// for EndScene. Committing here and again there would be rewriting a
		// descriptor set that is already bound to a command buffer, which is
		// the hazard recorded in HANDOFF §5.
		s_Data->Pending.clear();
		s_Data->Instances.clear();
		s_Data->ReservedInstances = 0;
		s_Data->IndirectView = {};
		s_Data->IndirectSlots.clear();
		s_Data->BoneScratch.clear();
		// Cleared here rather than in EndScene, because SetSceneInstance
		// registers materials before EndScene runs and clearing there would
		// throw away the records the GPU rows already point at.
		s_Data->MaterialScratch.clear();
		s_Data->MaterialIndex.clear();
		s_Data->SceneActive = true;
	}

	void Renderer3D::EndScene()
	{
		if (!s_Data || !s_Data->SceneActive)
			return;

		s_Data->SceneActive = false;

		RHICommandList* cmd = Renderer::GetCommandList();

		// **An empty pending list is not an empty frame any more** (roadmap
		// 8.3). A scene of nothing but static meshes submits no pending draws
		// at all -- the cull pass decided what to draw and the CPU never made
		// a record of any of it -- so returning here on `Pending.empty()` drew
		// the sky and nothing else. It only looked right on scenes that also
		// had something skinned in them.
		const bool haveIndirect = s_Data->IndirectView.IsValid() && !s_Data->IndirectSlots.empty();
		// And the blended table is a frame on its own for the same reason: a
		// scene of nothing but glass fills its instance rows and records its
		// indirect view without a single pending draw, and returning here
		// would skip the uploads FlushTransparent draws from -- the exact
		// lesson the opaque table taught above, learned by its twin.
		const bool haveBlended = s_Data->TransparentView.IsValid()
							  && !s_Data->TransparentSlots.empty();
		if (!cmd || !s_Data->Pipeline
			|| (s_Data->Pending.empty() && !haveIndirect && !haveBlended))
			return;

		if (!s_Data->ActiveScene)
			return;

		Renderer3DData::SceneSlot& slot = *s_Data->ActiveScene;

		// Orders draws front to back without breaking a single batch.
		//
		// Early-z only skips shading a pixel once something nearer has written
		// depth, so the order draws arrive in decides how much fragment work is
		// wasted. Measured on 200 full-screen slabs: **0.32 ms nearest-first
		// against 33.9 ms furthest-first**, a factor of 105. That is the ceiling,
		// and it is what makes this worth doing.
		//
		// **Two levels, and the inner one is where the win actually is.**
		//
		// Instances *within* a run share a mesh and a material by definition, so
		// reordering them changes nothing about the draw -- same batch, same
		// instance count, same state -- and buys the whole of the early-z effect.
		// This is the level that matters: the slab scene is 200 objects sharing
		// one cube and one material, so it is a *single* batch, and reordering
		// batches alone left it at 33.5 ms against 33.9. Sorting inside the batch
		// is what takes it to 0.32.
		//
		// Runs are then ordered by their nearest member, which helps when a scene
		// has many distinct meshes rather than many copies of one.
		//
		// What is deliberately *not* done is a global sort by depth. It orders
		// perfectly and dissolves the grouping instancing depends on, and on a
		// realistic scene that trade loses: 1500 spread-out meshes measured
		// 0.567 ms globally depth-sorted against 0.548 ms in the grouped order,
		// because the draw calls lost cost more than the overdraw saved.
		//
		// ---
		//
		// **Nothing below moves a PendingDraw until the very end** (roadmap
		// 8.16). Both orderings are decided over an array of indices, and the
		// records are permuted once, afterwards, in a single pass. At sixty
		// thousand objects the two sorts were 6.48 ms of a 13.94 ms render
		// graph -- the largest single item in it -- because `std::sort` moves
		// its elements about `n log n` times and each element was 72 bytes.
		// Four-byte indices move instead, and 72-byte records move once.
		auto& pending = s_Data->Pending;
		const size_t pendingCount = pending.size();

		// **One sort, one key, nothing chased.**
		//
		// Deciding the order over an array of indices was the obvious fix and
		// it bought almost nothing: 6.48 ms to 6.15 at sixty thousand objects.
		// The records had already been shrunk to 72 bytes, so moving them was
		// no longer what cost -- every *comparison* was, because it dereferenced
		// two indices into a four-megabyte array and missed cache twice.
		//
		// So the key carries everything the comparison needs. Pipeline, mesh,
		// material and depth pack into one 64-bit word, sorted as plain
		// unsigned integers with no indirection at all, and the sort that used
		// to be two passes is one:
		//
		//   bits 62-63  pipeline kind    static, skinned, layered
		//   bits 47-61  mesh id          compact, assigned below
		//   bits 32-46  material id      compact, assigned below
		//   bits  0-31  view depth       IEEE bits, which order like the float
		//
		// Grouping falls out of the high bits and front-to-back ordering out of
		// the low ones, in a single ascending sort -- within one run the high
		// bits are equal, so the depth decides. A view depth is a distance and
		// therefore never negative, which is what makes its bit pattern compare
		// in the same order the float does.
		s_Data->SortEntries.clear();
		s_Data->SortEntries.reserve(pendingCount);

		// Compact ids, by linear scan with a last-hit cache. A scene has tens
		// of thousands of objects and a handful of meshes and materials, so the
		// answer is almost always the one before it and the fallback walks a
		// vector that fits in a cache line or two. A hash per draw would be a
		// hash per draw.
		auto& meshIds = s_Data->MeshIds;
		auto& materialIds = s_Data->MaterialIds;
		meshIds.clear();
		materialIds.clear();

		const Mesh* lastMesh = nullptr;
		uint32_t lastMeshId = 0;
		uint64_t lastMaterial = 0;
		uint32_t lastMaterialId = 0;
		bool lastMaterialValid = false;

		// 15 bits each. A scene past this is not one this packing can order, so
		// it takes the comparator below instead of being ordered wrongly.
		// **The mesh id lost a bit to transparency**, and it is the one that
		// could afford it: sixteen thousand distinct meshes in one frame is
		// already far past where the unpacked comparator takes over, and the
		// alternative was a bit off the depth, which is a float and has no
		// spare.
		//
		//   62-63  bucket: opaque, then masked, then blended
		//   60-61  kind
		//   47-59  mesh
		//   32-46  material
		//   0-31   depth, or the submission index with depth sorting off
		//
		// The bucket took a second bit when cutouts arrived and the mesh field
		// gave it up, 14 bits to 13. That is 8191 distinct meshes in one frame
		// before `packable` falls back to the unpacked path, which is the
		// graceful answer already written for exactly this.
		constexpr uint32_t kMaxPackedMeshId = (1u << 13) - 1;
		constexpr uint32_t kMaxPackedId = (1u << 15) - 1;
		bool packable = true;

		for (uint32_t i = 0; i < (uint32_t)pendingCount && packable; i++)
		{
			const PendingDraw& draw = pending[i];

			uint32_t meshId;
			if (draw.MeshKey == lastMesh)
			{
				meshId = lastMeshId;
			}
			else
			{
				meshId = (uint32_t)meshIds.size();
				for (uint32_t j = 0; j < (uint32_t)meshIds.size(); j++)
				{
					if (meshIds[j] == draw.MeshKey)
					{
						meshId = j;
						break;
					}
				}
				if (meshId == (uint32_t)meshIds.size())
					meshIds.push_back(draw.MeshKey);
				lastMesh = draw.MeshKey;
				lastMeshId = meshId;
			}

			uint32_t materialId;
			if (lastMaterialValid && draw.MaterialKey == lastMaterial)
			{
				materialId = lastMaterialId;
			}
			else
			{
				materialId = (uint32_t)materialIds.size();
				for (uint32_t j = 0; j < (uint32_t)materialIds.size(); j++)
				{
					if (materialIds[j] == draw.MaterialKey)
					{
						materialId = j;
						break;
					}
				}
				if (materialId == (uint32_t)materialIds.size())
					materialIds.push_back(draw.MaterialKey);
				lastMaterial = draw.MaterialKey;
				lastMaterialId = materialId;
				lastMaterialValid = true;
			}

			if (meshId > kMaxPackedMeshId || materialId > kMaxPackedId)
			{
				packable = false;
				break;
			}

			// Depth only where it is wanted. With front-to-back ordering off
			// the low half is the submission index instead, which keeps runs
			// in the order the scene produced them.
			uint32_t low = i;
			if (EngineConfig::Get().DepthSortOpaque)
			{
				const float depth = draw.ViewDepth;
				memcpy(&low, &depth, sizeof(low));
			}

			const uint64_t key = ((uint64_t)draw.Bucket << 62)
							   | ((uint64_t)draw.Kind << 60)
							   | ((uint64_t)meshId << 47)
							   | ((uint64_t)materialId << 32)
							   | (uint64_t)low;
			s_Data->SortEntries.push_back({ key, i });
		}


		std::vector<uint32_t>& order = s_Data->SortOrder;
		order.resize(pendingCount);

		if (packable)
		{
			auto& entries = s_Data->SortEntries;
			std::sort(entries.begin(), entries.end(),
					  [](const SortEntry& a, const SortEntry& b)
					  {
						  if (a.Key != b.Key)
							  return a.Key < b.Key;
						  // Equal in every packed field: keep submission order,
						  // so the arrangement is the same on every run of the
						  // same frame.
						  return a.Index < b.Index;
					  });

			for (size_t i = 0; i < pendingCount; i++)
				order[i] = entries[i].Index;
		}
		else
		{
			// More distinct meshes or materials than the key can hold. Said
			// once: the frame is still correct, and still grouped and ordered,
			// by the comparator this packing exists to avoid.
			if (!s_Data->PackWarned)
			{
				RV_CORE_WARN("Renderer3D: more than {0} distinct meshes or materials in one "
							 "scene, so the draw order is decided by comparison rather than "
							 "by a packed key. Correct, and slower at scale.", kMaxPackedId);
				s_Data->PackWarned = true;
			}

			for (uint32_t i = 0; i < (uint32_t)pendingCount; i++)
				order[i] = i;

			std::sort(order.begin(), order.end(),
					  [&pending](uint32_t lhs, uint32_t rhs)
					  {
						  const PendingDraw& a = pending[lhs];
						  const PendingDraw& b = pending[rhs];
						  // The same order the packed key produces, field for
						  // field. Two orderings of one list that disagree is a
						  // picture that changes with the mesh count.
						  if (a.Bucket != b.Bucket)
							  return a.Bucket < b.Bucket;
						  if (a.Kind != b.Kind)
							  return a.Kind < b.Kind;
						  if (a.MeshKey != b.MeshKey)
							  return a.MeshKey < b.MeshKey;
						  if (a.MaterialKey != b.MaterialKey)
							  return a.MaterialKey < b.MaterialKey;
						  if (EngineConfig::Get().DepthSortOpaque && a.ViewDepth != b.ViewDepth)
							  return a.ViewDepth < b.ViewDepth;
						  return lhs < rhs;
					  });
		}


		// Runs are ordered by their nearest member, which helps when a scene
		// has many distinct meshes rather than many copies of one. The order
		// *within* each run is already settled by the key's low half.
		if (EngineConfig::Get().DepthSortOpaque && pendingCount >= 2)
		{
			s_Data->Runs.clear();

			for (size_t begin = 0; begin < pendingCount;)
			{
				const PendingDraw& first = pending[order[begin]];

				size_t end = begin + 1;
				while (end < pendingCount)
				{
					const PendingDraw& next = pending[order[end]];
					// IndexCount is not in the key -- it differs only for a
					// terrain chunk drawn without its skirts (7ap) -- so it is
					// checked here, where a run is actually cut. Transparent
					// IS in the key, at the top, so the blended draws are a
					// contiguous block at the end of `order` -- but under
					// bindless every material's batch key is zero, so the
					// last opaque run and the first blended one can agree on
					// every other field (the same mesh, once painted and once
					// glazed) and would merge across the boundary. A run is
					// one pipeline, so it is cut there too.
					if (next.Bucket != first.Bucket ||
						next.Kind != first.Kind || next.MeshKey != first.MeshKey ||
						next.MaterialKey != first.MaterialKey ||
						next.IndexCount != first.IndexCount)
					{
						break;
					}
					end++;
				}

				s_Data->Runs.push_back({ begin, end, pending[order[begin]].ViewDepth });
				begin = end;
			}

			if (s_Data->Runs.size() >= 2)
			{
				// **Opaque before blended, then the kinds, then the depth.**
				// The packed key already put every blended draw after every
				// opaque one; this reorder has to keep that, because the
				// opaque issue below ends where the first blended draw sits
				// (TransparentBegin) and hands everything after it to the
				// transparent pass. Ordering by nearest member alone broke
				// exactly that: the car's glass, nearer than the walls, was
				// sorted ahead of them, and every opaque run behind it --
				// walls, piers, mirror, the bay -- was drawn through the OIT
				// pipeline: no depth write (so depth of field blurred them as
				// "far"), no g-buffer (so nothing screen-space saw them), and
				// a weighted blend where an opaque surface was authored. The
				// GPU-driven path was immune and that is what made the two
				// disagree by 1.6M pixels on the showroom (HANDOFF, gpu-lit).
				//
				// The kinds in their order whatever their depth: the pipelines
				// must stay separated, which is the property the grouping gives.
				std::stable_sort(s_Data->Runs.begin(), s_Data->Runs.end(),
								 [&](const DrawRun& a, const DrawRun& b)
								 {
									 const PendingDraw& firstA = pending[order[a.Begin]];
									 const PendingDraw& firstB = pending[order[b.Begin]];
									 if (firstA.Bucket != firstB.Bucket)
										 return firstA.Bucket < firstB.Bucket;
									 if (firstA.Kind != firstB.Kind)
										 return firstA.Kind < firstB.Kind;
									 return a.Nearest < b.Nearest;
								 });

				s_Data->OrderScratch.clear();
				s_Data->OrderScratch.reserve(pendingCount);
				for (const DrawRun& run : s_Data->Runs)
				{
					for (size_t i = run.Begin; i < run.End; i++)
						s_Data->OrderScratch.push_back(order[i]);
				}
				order.swap(s_Data->OrderScratch);
			}
		}


		// **The one place a PendingDraw moves.** Everything above decided an
		// order; this applies it, once.
		s_Data->SortScratch.clear();
		s_Data->SortScratch.reserve(pendingCount);
		for (uint32_t index : order)
			s_Data->SortScratch.push_back(std::move(pending[index]));
		pending.swap(s_Data->SortScratch);

		const uint32_t count = (uint32_t)s_Data->Pending.size();

		// The material records (ENGINE-NOTES 7al). Every distinct material this
		// scene drew gets one, numbered in first-seen order after the sort, and
		// the number goes into the instance where the fragment stage reads it
		// back. Before the instance upload below, because it writes into the
		// instances. Bound path: nothing -- the material set carries it all.
		if (s_Data->Bindless)
		{
			// Not cleared here: BeginScene does that, because the GPU path's
			// rows registered their materials before this ran.
			for (PendingDraw& draw : s_Data->Pending)
				s_Data->Instances[draw.Instance].Indices.z =
					(float)RegisterMaterial(draw.MaterialRef);

			// The ray-instance table (7ao): one row per structure instance, in
			// build order, each naming its buffers by address and its material
			// by the same record index the draws use -- so a material seen
			// only in a reflection still gets a record this frame.
			//
			// **Filling it and having it are two different conditions**, and
			// conflating them was a null dereference for anyone who turned ray
			// tracing on without the two effects that shade a hit.
			//
			// Only reflections and the traced bounce ever *read* a row, so
			// only they are worth the walk over the casters. But the buffer
			// has to exist for every descriptor set that declares the binding,
			// and `rtgi_trace.rvshader` always compiles with `RV_RAY_GI` --
			// the fill pass needs it whether or not the realtime bounce is
			// running -- so the GI set declares the binding whenever ray
			// shadows are on at all, and a declared binding must be written.
			// Writing it from a table nobody built passed a null buffer
			// straight to the backend.
			//
			// So: build the table when something reads it, and keep one empty
			// row when nothing does. The same "smallest honest filler" the
			// bone buffer below uses, and for the same reason.
			const bool tracesHits = s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn
								 || s_Data->RayWaterRefractionOn;
			if (tracesHits)
			{
				const std::vector<RayCaster>& casters = RayShadows::GetCasters();
				s_Data->RayInstanceScratch.clear();
				s_Data->RayInstanceScratch.reserve(casters.size());
				for (const RayCaster& caster : casters)
				{
					const Material* key = caster.MaterialRef.get();
					auto it = s_Data->MaterialIndex.find(key);
					if (it == s_Data->MaterialIndex.end())
					{
						GpuMaterial record;
						if (caster.MaterialRef)
							caster.MaterialRef->WriteRecord(*s_Data->Heap, record);
						it = s_Data->MaterialIndex.emplace(key, (uint32_t)s_Data->MaterialScratch.size()).first;
						s_Data->MaterialScratch.push_back(record);
					}

					GpuRayInstance row{};
					const Ref<RHIBuffer>& vertices = caster.MeshRef ? caster.MeshRef->GetVertexBuffer() : nullptr;
					const Ref<RHIBuffer>& indices = caster.MeshRef ? caster.MeshRef->GetIndexBuffer() : nullptr;
					row.AttributeAddress = vertices ? vertices->GetDeviceAddress() : 0;
					row.IndexAddress = indices ? indices->GetDeviceAddress() : 0;
					row.AttributeStrideWords = caster.MeshRef && caster.MeshRef->IsSkinned()
											 ? (uint32_t)(sizeof(SkinnedVertex) / 4) : (uint32_t)(sizeof(MeshVertex) / 4);
					if (caster.Posed)
					{
						row.PositionAddress = caster.Posed->GetDeviceAddress();
						row.PositionStrideWords = (uint32_t)(sizeof(Vec4) / 4);
						row.Flags = kRayInstancePosed;
					}
					else
					{
						row.PositionAddress = row.AttributeAddress;
						row.PositionStrideWords = row.AttributeStrideWords;
					}

					// **Is this surface one the emitter list answers for?**
					// Asked here because this is the first point where both
					// halves are final: SetAreaEmitters has run for the frame
					// (the scene hands the list over before EndScene) and the
					// casters are this frame's. A linear scan over at most
					// sixteen owners, per caster.
					//
					// Owner zero is "no id", which every caller that does not
					// set one leaves behind -- it must never match, or the
					// first unowned emitter would silently claim every
					// unowned instance in the scene.
					if (caster.Owner != 0)
					{
						for (uint64_t owner : s_Data->EmitterOwners)
						{
							if (owner == caster.Owner)
							{
								row.Flags |= kRayInstanceEmitter;
								break;
							}
						}
					}

					if (caster.MaterialRef &&
						caster.MaterialRef->GetBlendMode() == BlendMode::Masked)
					{
						row.Flags |= kRayInstanceMasked;
						row.AlphaCutoff = caster.Params.AlphaCutoff;
					}

					row.MaterialIndex = it->second;
					row.BaseColor = caster.Params.BaseColor;
					row.EmissiveColor = caster.Params.EmissiveColor;
					row.Surface = { caster.Params.Metallic, caster.Params.Roughness,
									caster.Params.Occlusion, caster.Params.NormalScale };
					s_Data->RayInstanceScratch.push_back(row);
				}

			}
			else
			{
				s_Data->RayInstanceScratch.clear();
			}

			// `GiPipeline` is the exact condition `GiSet` is created under, so
			// this covers every set that will be asked for the binding.
			if (tracesHits || s_Data->GiPipeline)
			{
				const uint32_t rows = Math::Max((uint32_t)s_Data->RayInstanceScratch.size(), 1u);
				if (!EnsureInstanceBuffer(slot.RayInstances, slot.RayInstanceCapacity, rows,
										  sizeof(GpuRayInstance), "Renderer3D.rayinstances"))
				{
					return;
				}
				if (s_Data->RayInstanceScratch.empty())
				{
					const GpuRayInstance none{};
					slot.RayInstances->Upload(&none, sizeof(none));
				}
				else
				{
					slot.RayInstances->Upload(s_Data->RayInstanceScratch.data(),
											  (uint64_t)s_Data->RayInstanceScratch.size() * sizeof(GpuRayInstance));
				}
			}

			const uint32_t records = (uint32_t)s_Data->MaterialScratch.size();
			if (!EnsureInstanceBuffer(slot.Materials, slot.MaterialCapacity, records,
									  sizeof(GpuMaterial), "Renderer3D.materials"))
			{
				return;
			}
			slot.Materials->Upload(s_Data->MaterialScratch.data(),
								   (uint64_t)records * sizeof(GpuMaterial));

			// The heap's staged writes, once, before the command buffer that
			// reads them is submitted.
			s_Data->Heap->Commit();
		}

		// **The instance data never moves.** It goes up exactly as it was
		// written, in submission order, as one memcpy -- and the sorts are
		// expressed by the four-byte index buffer below instead. This used to
		// end with a gather: fifteen megabytes read in scattered 256-byte runs
		// at sixty thousand objects, to write a contiguous copy.
		//
		// Sized on the pool rather than on the draw count. They agree today --
		// every submitted draw takes exactly one row -- but a row allocated by
		// a draw that then bailed out would make the pool longer, and
		// uploading `count` rows would truncate the tail that a later draw's
		// index still points at.
		const uint32_t instanceRows = (uint32_t)s_Data->Instances.size();
		if (!EnsureInstanceBuffer(slot.Instances, slot.InstanceCapacity, instanceRows,
								  sizeof(InstanceData), "Renderer3D.instances"))
		{
			return;
		}

		// At least one element even when the CPU path submitted nothing: a
		// binding the layout declares and the set leaves unwritten is a
		// validation error, not a harmless omission.
		if (!EnsureInstanceBuffer(slot.Visible, slot.VisibleCapacity, Math::Max(count, 1u),
								  sizeof(uint32_t), "Renderer3D.visible"))
		{
			return;
		}

		slot.Instances->Upload(s_Data->Instances.data(),
							   (uint64_t)instanceRows * sizeof(InstanceData));

		s_Data->VisibleScratch.clear();
		s_Data->VisibleScratch.reserve(count);
		for (const PendingDraw& draw : s_Data->Pending)
			s_Data->VisibleScratch.push_back(draw.Instance);

		slot.Visible->Upload(s_Data->VisibleScratch.data(),
							 (uint64_t)count * sizeof(uint32_t));

		slot.Set->SetStorageBuffer(7, slot.Instances, 0,
								   (uint64_t)instanceRows * sizeof(InstanceData));
		slot.Set->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
								   (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));

		// The same instance table, read through the indices the cull pass
		// wrote instead of the ones the sort produced. One binding apart, and
		// that is the whole difference between the two paths at draw time.
		if (slot.GpuSet && s_Data->IndirectView.IsValid())
		{
			slot.GpuSet->SetStorageBuffer(7, slot.Instances, 0,
										  (uint64_t)instanceRows * sizeof(InstanceData));
			slot.GpuSet->SetStorageBuffer(kVisibleBinding, s_Data->IndirectView.Instances);

			// **The prepass reads the cull pass's survivors, not the sort's.**
			// It has to draw exactly what the lit pass will draw, through the
			// same indirection, or the depth it writes describes a different
			// set of objects than the one tested against it.
			if (slot.PrepassSet)
			{
				slot.PrepassSet->SetUniformBuffer(0, slot.Buffer, 0, sizeof(SceneUniforms));
				slot.PrepassSet->SetStorageBuffer(7, slot.Instances, 0,
												  (uint64_t)instanceRows * sizeof(InstanceData));
				slot.PrepassSet->SetStorageBuffer(kVisibleBinding,
												  s_Data->IndirectView.Instances);
				slot.PrepassSet->Commit();
			}
			if (s_Data->Bindless)
			{
				slot.GpuSet->SetStorageBuffer(13, slot.Materials, 0,
											  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.GpuSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.GpuSet->Commit();

			// The masked indirect set carries identical contents; only the
			// pipeline it was allocated against differs.
			if (slot.MaskedGpuSet)
			{
				slot.MaskedGpuSet->SetStorageBuffer(7, slot.Instances, 0,
													(uint64_t)instanceRows * sizeof(InstanceData));
				slot.MaskedGpuSet->SetStorageBuffer(kVisibleBinding,
													s_Data->IndirectView.Instances);
				if (s_Data->Bindless)
				{
					slot.MaskedGpuSet->SetStorageBuffer(13, slot.Materials, 0,
														(uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
					if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
						slot.MaskedGpuSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
				}
				slot.MaskedGpuSet->Commit();
			}
		}
		// Only where the layout declares it: on the bound path the shader has
		// no binding 13, and writing an undeclared binding is the validation
		// error HANDOFF section 5 records.
		if (s_Data->Bindless)
		{
			slot.Set->SetStorageBuffer(13, slot.Materials, 0,
									   (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
			if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
				slot.Set->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);

			// The same two the traced bounce shades a hit through.
			if (slot.GiSet)
			{
				slot.GiSet->SetStorageBuffer(13, slot.Materials, 0,
											 (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				slot.GiSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
		}

		// Always bound, even with nothing skinned in the scene: the layout
		// declares the binding whether or not this frame uses it, and a
		// declared binding left unwritten is a validation error rather than an
		// unread one. One identity is the smallest honest filler.
		const uint32_t boneCount = Math::Max((uint32_t)s_Data->BoneScratch.size(), 1u);
		if (!EnsureInstanceBuffer(slot.Bones, slot.BoneCapacity, boneCount,
								  sizeof(Mat4), "Renderer3D.bones"))
		{
			return;
		}

		if (s_Data->BoneScratch.empty())
		{
			const Mat4 identity(1.0f);
			slot.Bones->Upload(&identity, sizeof(identity));
		}
		else
		{
			slot.Bones->Upload(s_Data->BoneScratch.data(),
							   s_Data->BoneScratch.size() * sizeof(Mat4));
		}

		// The instance buffer to all three, the bones only to the set whose
		// layout declares them.
		if (slot.SkinnedSet)
		{
			slot.SkinnedSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
											  (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.SkinnedSet->SetStorageBuffer(7, slot.Instances, 0,
											  (uint64_t)instanceRows * sizeof(InstanceData));
			slot.SkinnedSet->SetStorageBuffer(11, slot.Bones, 0,
											  (uint64_t)boneCount * sizeof(Mat4));
			if (s_Data->Bindless)
			{
				slot.SkinnedSet->SetStorageBuffer(13, slot.Materials, 0,
												  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.SkinnedSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.SkinnedSet->Commit();
		}

		if (slot.TransparentSet)
		{
			slot.TransparentSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
												  (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.TransparentSet->SetStorageBuffer(7, slot.Instances, 0,
												  (uint64_t)instanceRows * sizeof(InstanceData));
			if (s_Data->Bindless)
			{
				slot.TransparentSet->SetStorageBuffer(13, slot.Materials, 0,
													  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				// Refraction shades its hits through the same table, and the
				// transparent pair's layout declares the binding whenever it
				// was compiled with any of the three.
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn
					|| s_Data->RayWaterRefractionOn)
					slot.TransparentSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.TransparentSet->Commit();
		}

		// The same table, through the blended cull's indices.
		if (slot.TransparentGpuSet && s_Data->TransparentView.IsValid())
		{
			slot.TransparentGpuSet->SetStorageBuffer(7, slot.Instances, 0,
													 (uint64_t)instanceRows * sizeof(InstanceData));
			slot.TransparentGpuSet->SetStorageBuffer(kVisibleBinding,
													 s_Data->TransparentView.Instances);
			if (s_Data->Bindless)
			{
				slot.TransparentGpuSet->SetStorageBuffer(13, slot.Materials, 0,
														 (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn
					|| s_Data->RayWaterRefractionOn)
					slot.TransparentGpuSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.TransparentGpuSet->Commit();
		}

		if (slot.LayeredSet)
		{
			slot.LayeredSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
											  (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			slot.LayeredSet->SetStorageBuffer(7, slot.Instances, 0,
											  (uint64_t)instanceRows * sizeof(InstanceData));
			if (s_Data->Bindless)
			{
				slot.LayeredSet->SetStorageBuffer(13, slot.Materials, 0,
												  (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.LayeredSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.LayeredSet->Commit();
		}

		// **The masked set carries exactly what the static one does.** Its
		// layout is identical -- the cutout variant adds a test, not a binding
		// -- so every binding the static set fills, this one must fill too.
		// Written here, in one place and next to the commit, because the
		// static set is filled in two separate blocks above and a mirror
		// beside each of them is a mirror that drifts.
		if (slot.MaskedSet)
		{
			slot.MaskedSet->SetStorageBuffer(7, slot.Instances, 0,
											 (uint64_t)instanceRows * sizeof(InstanceData));
			slot.MaskedSet->SetStorageBuffer(kVisibleBinding, slot.Visible, 0,
											 (uint64_t)Math::Max(count, 1u) * sizeof(uint32_t));
			if (s_Data->Bindless)
			{
				slot.MaskedSet->SetStorageBuffer(13, slot.Materials, 0,
												 (uint64_t)s_Data->MaterialScratch.size() * sizeof(GpuMaterial));
				if (s_Data->RayReflectionsOn || s_Data->RayGlobalIlluminationOn)
					slot.MaskedSet->SetStorageBuffer(kRayInstanceBinding, slot.RayInstances);
			}
			slot.MaskedSet->Commit();
		}

		slot.Set->Commit();

		// The traced bounce's set with them. Written in two places -- the
		// scene block and the environment above, the two storage buffers with
		// the material scratch -- and committed once here, where the sets it
		// shares a frame with are.
		if (slot.GiSet)
			slot.GiSet->Commit();

		// Bound per run rather than once, because the run decides which of the
		// three pipelines draws it.
		DrawKind boundKind = DrawKind::Static;
		// The bucket is part of what a bound pipeline is, not just the kind:
		// masked and opaque share a kind and differ in pipeline, so tracking
		// only the kind would draw a cutout run through whichever of the two
		// happened to be bound.
		DrawBucket boundBucket = DrawBucket::Opaque;
		bool anyPipelineBound = false;

		// **The GPU-driven half, first.** Its instance counts live in device
		// memory and its draws depend on nothing the loop below decides, so
		// the order between the two is free -- and doing it first means the
		// static geometry has written depth before the skinned and layered
		// draws are shaded.
		// **The depth prepass, over the same indirect draws.**
		//
		// Inside the scene pass rather than before it, which is what the
		// colour mask buys: the instance buffer is uploaded and the sets are
		// committed by the time this runs, and none of that has to be split
		// out of EndScene and handed to a second pass.
		//
		// What it buys is early-Z on geometry the sort cannot help. Instances
		// are already ordered front to back, so a crate behind a wall is
		// rejected without this; what is not ordered is the inside of a single
		// mesh, and a 490k-triangle car has bodywork, interior and engine bay
		// overlapping in index order. Every one of those hidden fragments runs
		// the full shader -- twenty lights, a shadow ray each under traced
		// shadows, and a mirror ray if it is glossy.
		//
		// It costs a second rasterisation of the same geometry, at four
		// samples, writing nothing but depth. Whether that trade pays is a
		// measurement and not an argument.
		if (haveIndirect && slot.PrepassSet && s_Data->PrepassPipeline)
		{
			cmd->BindPipeline(s_Data->PrepassPipeline);
			cmd->BindResourceSet(0, slot.PrepassSet);

			const std::vector<GpuCull::Slot>& indirect = s_Data->IndirectSlots;
			const uint32_t slotCount =
				Math::Min((uint32_t)indirect.size(), s_Data->IndirectView.SlotCount);

			for (uint32_t i = 0; i < slotCount; i++)
			{
				const GpuCull::Slot& entry = indirect[i];
				if (!entry.MeshRef)
					continue;

				// **Never a cutout.** The prepass writes depth with no fragment
				// stage, so it would lay down depth for texels the lit pass is
				// about to discard -- and a hole whose depth was already
				// written occludes whatever is behind it. Masked slots are
				// simply absent from this pass; they write their own depth in
				// the lit pass, which is what the depth test then reads.
				if (entry.MaskedMaterial)
					continue;

				ObjectPushConstants object;
				object.BaseInstance = (int32_t)entry.InstanceBase;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

				cmd->BindVertexBuffer(0, entry.MeshRef->GetVertexBuffer());
				cmd->BindIndexBuffer(entry.MeshRef->GetIndexBuffer(), IndexType::UInt32);
				cmd->DrawIndexedIndirect(s_Data->IndirectView.Commands,
										 (uint64_t)i * sizeof(GpuCull::SlotCommand),
										 1, sizeof(GpuCull::SlotCommand));
			}

			// **Deliberately not counted.** The draw and triangle totals
			// describe what the scene is made of, and this draws the same
			// geometry a second time -- adding it would report the showroom as
			// twice the scene it is. The cost is not hidden: it lands in the
			// scene pass's GPU time, which is where the profiler looks.
		}

		if (haveIndirect && slot.GpuSet)
		{
			const std::vector<GpuCull::Slot>& indirect = s_Data->IndirectSlots;
			const uint32_t slotCount =
				Math::Min((uint32_t)indirect.size(), s_Data->IndirectView.SlotCount);

			// **Two sweeps, opaque then masked**, rather than one that rebinds
			// whenever the kind changes. The table is in creation order, so
			// cutouts are scattered through it; sweeping twice costs one extra
			// pipeline bind and sorting the table would cost a sort.
			//
			// Opaque first, so every cutout is tested against a depth buffer
			// the solid geometry has already filled -- the same order the CPU
			// path's buckets produce, and for the same reason.
			for (int sweep = 0; sweep < 2; sweep++)
			{
			const bool maskedSweep = sweep == 1;
			if (maskedSweep && (!s_Data->MaskedPipeline || !slot.MaskedGpuSet))
				continue;

			bool sweepBound = false;

			for (uint32_t i = 0; i < slotCount; i++)
			{
				const GpuCull::Slot& entry = indirect[i];
				if (!entry.MeshRef)
					continue;
				if ((entry.MaskedMaterial != nullptr) != maskedSweep)
					continue;

				if (!sweepBound)
				{
					cmd->BindPipeline(maskedSweep ? s_Data->MaskedPipeline : s_Data->Pipeline);
					cmd->BindResourceSet(0, maskedSweep ? slot.MaskedGpuSet : slot.GpuSet);
					if (s_Data->Bindless)
						cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
					sweepBound = true;
				}

				// Where this slot's survivors begin in the index buffer. Not
				// the draw's firstInstance, for the reason scene_vertex.glsl
				// gives.
				ObjectPushConstants object;
				object.BaseInstance = (int32_t)entry.InstanceBase;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

				cmd->BindVertexBuffer(0, entry.MeshRef->GetVertexBuffer());
				cmd->BindIndexBuffer(entry.MeshRef->GetIndexBuffer(), IndexType::UInt32);
				cmd->DrawIndexedIndirect(s_Data->IndirectView.Commands,
										 (uint64_t)i * sizeof(GpuCull::SlotCommand),
										 1, sizeof(GpuCull::SlotCommand));

				// The draw is counted, and its triangles at the count that was
				// *submitted* -- the slot's reserved length. How many of them
				// survive is in device memory the CPU never reads back, which
				// is the point; reporting nothing at all instead made the
				// panel read as broken on every frame this path drew.
				s_Data->DrawCalls++;
				s_Data->IndirectDraws++;
				s_Data->Triangles +=
					(entry.MeshRef->GetIndexCount() / 3) * entry.InstanceCount;
			}
			}

			// The loop below has to bind its own pipeline and set again.
			anyPipelineBound = false;
		}

		// **Where the opaque list ends.** The sort put every blended draw in one
		// block at the end, so this is a scan for the first of them rather than
		// a partition -- and when there are none it is `count`, which is the
		// loop the renderer has always run.
		//
		// Masked draws are *before* this point, not after: they write depth and
		// belong to the opaque pass. They form their own runs within it,
		// because the merge below compares the whole bucket.
		s_Data->TransparentBegin = count;
		for (uint32_t i = 0; i < count; i++)
		{
			if (s_Data->Pending[i].Bucket == DrawBucket::Blended)
			{
				s_Data->TransparentBegin = i;
				break;
			}
		}

		const uint32_t opaqueCount = s_Data->TransparentBegin;


		// One draw per run of identical mesh and bound material state.
		uint32_t start = 0;
		while (start < opaqueCount)
		{
			uint32_t end = start + 1;
			// Kind as well: a run is one pipeline, and under bindless the
			// material key is zero for every material, so without it only
			// the mesh pointer separates a static run from a skinned one at
			// a kind boundary -- the same shape as the transparent bit
			// above: a field the sort separates and the merge forgot.
			while (end < opaqueCount &&
				   s_Data->Pending[end].Bucket == s_Data->Pending[start].Bucket &&
				   s_Data->Pending[end].Kind == s_Data->Pending[start].Kind &&
				   s_Data->Pending[end].MeshKey == s_Data->Pending[start].MeshKey &&
				   s_Data->Pending[end].MaterialKey == s_Data->Pending[start].MaterialKey &&
				   s_Data->Pending[end].IndexCount == s_Data->Pending[start].IndexCount)
			{
				end++;
			}

			const PendingDraw& first = s_Data->Pending[start];

			// Static opaque runs go as meshlets when the pipeline exists and
			// the mesh cut cleanly; every other kind -- skinned, layered,
			// blended, a mesh the cut refused -- takes its classic pipeline
			// in the same pass. The scene set is the same object on both
			// paths: the descriptor layouts are identically defined (the
			// coarse stage flags in VulkanPipeline are what make that true),
			// so one fill serves both front ends.
			if (first.Kind == DrawKind::Static && s_Data->MeshletLitPipeline &&
				first.MeshRef && slot.Set)
			{
				const Mesh::MeshletBuffers& meshlets =
					first.MeshRef->GetMeshletBuffers(*s_Data->Device);
				if (meshlets.Count > 0)
				{
					while (slot.MeshletCursor >= slot.MeshletSets.size())
						slot.MeshletSets.push_back(nullptr);
					Ref<RHIResourceSet>& meshletSet =
						slot.MeshletSets[slot.MeshletCursor];
					if (!meshletSet)
					{
						meshletSet = s_Data->Device->CreateResourceSet(
							s_Data->MeshletLitPipeline, 3);
					}

					if (meshletSet)
					{
						slot.MeshletCursor++;

						meshletSet->SetStorageBuffer(0, meshlets.Meshlets);
						meshletSet->SetStorageBuffer(1, meshlets.Vertices);
						meshletSet->SetStorageBuffer(2, meshlets.Triangles);
						meshletSet->SetStorageBuffer(3, first.MeshRef->GetVertexBuffer());
						meshletSet->Commit();

						// A different push-constant interface disturbs every
						// bound set on the switch, so everything is rebound
						// here and the classic loop rebinds its own after.
						cmd->BindPipeline(s_Data->MeshletLitPipeline);
						cmd->BindResourceSet(0, slot.Set);
						if (s_Data->Bindless)
							cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
						else if (first.MaterialRef)
							first.MaterialRef->Bind(*cmd, s_Data->MeshletLitPipeline, 1);
						cmd->BindResourceSet(3, meshletSet);
						anyPipelineBound = false;

						ObjectPushConstants object;
						object.BaseInstance = (int32_t)start;
						cmd->PushConstants(ShaderStage::Mesh, 0, sizeof(object), &object);

						cmd->DrawMeshTasks(meshlets.Count, end - start, 1);
						s_Data->DrawCalls++;
						s_Data->Triangles += (first.IndexCount / 3) * (end - start);

						start = end;
						continue;
					}
				}
			}

			// Each kind needs its own pipeline. Without one the run is skipped
			// rather than drawn by another: a skinned mesh through the static
			// pipeline reads joint indices as texture coordinates and scatters
			// across the world, and a layered chunk through it binds a set the
			// layout does not describe.
			// **Masked replaces the static pipeline only.** A skinned or a
			// layered cutout would each need their own variant and neither
			// exists yet, so one of those falls back to drawing solid -- the
			// same choice the skinned transparent path documents, and for the
			// same reason: a visible material with a hard edge missing beats
			// a material that does not draw.
			const bool masked = first.Bucket == DrawBucket::Masked;
			const Ref<RHIPipeline>& pipeline =
				first.Kind == DrawKind::Skinned ? s_Data->SkinnedPipeline
				: first.Kind == DrawKind::Layered ? s_Data->LayeredPipeline
				: (masked && s_Data->MaskedPipeline) ? s_Data->MaskedPipeline
				: s_Data->Pipeline;
			if (!pipeline)
			{
				start = end;
				continue;
			}

			const Ref<RHIResourceSet>& sceneSet =
				first.Kind == DrawKind::Skinned ? slot.SkinnedSet
				: first.Kind == DrawKind::Layered ? slot.LayeredSet
				: (masked && s_Data->MaskedPipeline && slot.MaskedSet) ? slot.MaskedSet
				: slot.Set;
			if (!sceneSet)
			{
				start = end;
				continue;
			}

			if (!anyPipelineBound || boundKind != first.Kind || boundBucket != first.Bucket)
			{
				cmd->BindPipeline(pipeline);
				cmd->BindResourceSet(0, sceneSet);
				// The heap, at the set every bindless shader declares it at.
				// Once per pipeline, not per run: nothing about it changes
				// between draws, which is the point of it.
				if (s_Data->Bindless)
					cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
				boundKind = first.Kind;
				boundBucket = first.Bucket;
				anyPipelineBound = true;
			}

			// Any material in the run would do: the key is exactly the state
			// this binds, so they are interchangeable by construction. On the
			// bindless path there is nothing to bind -- the maps are heap
			// slots in the record the instance names -- and set 1 is empty.
			// A layered run binds its own set 1 on *both* paths: the block is
			// the layers' scalars and, bindless, their heap slots (7aq).
			if (first.Kind == DrawKind::Layered)
			{
				if (first.LayeredRef)
					first.LayeredRef->Bind(*cmd, pipeline, 1);
			}
			else if (first.MaterialRef && !s_Data->Bindless)
			{
				first.MaterialRef->Bind(*cmd, pipeline, 1);
			}

			ObjectPushConstants object;
			object.BaseInstance = (int32_t)start;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

			cmd->BindVertexBuffer(0, first.MeshRef->GetVertexBuffer());
			cmd->BindIndexBuffer(first.MeshRef->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(first.IndexCount, end - start);

			s_Data->DrawCalls++;
			s_Data->Triangles += (first.IndexCount / 3) * (end - start);

			start = end;
		}

		// **The lights' glow, last of the opaque pass** (WR-5, first half).
		// After every opaque draw so the depth it tests against is complete,
		// and before the transparent pass so the water composites over it
		// where it should. The buffer is the one the lit draws just read.
		// Draws nothing outside the scene pass proper -- a probe face or a
		// cascade never receives a viewport -- and nothing in a scene whose
		// lights have no size.
		LightGlow::Draw(*cmd, slot.Lights, (uint32_t)Math::Max(s_Data->Scene.LightCount, 0));

		// **Kept, not cleared, when something blended is still waiting.** The
		// transparent pass runs later in the same frame and reads the same
		// records, the same instance table and the same bound sets -- all of
		// which are alive until the next BeginScene, which clears them.
		if (s_Data->TransparentBegin >= count)
		{
			s_Data->Pending.clear();
			s_Data->Instances.clear();
			s_Data->TransparentBegin = 0;
		}
	}

	bool Renderer3D::HasTransparent()
	{
		if (!s_Data || !s_Data->TransparentPipeline)
			return false;

		return s_Data->TransparentBegin < (uint32_t)s_Data->Pending.size()
			|| (s_Data->TransparentView.IsValid() && !s_Data->TransparentSlots.empty());
	}

	// The blended block, drawn into the transparent pass's two attachments.
	//
	// The same loop the opaque issue runs, over the tail of the same array,
	// with one pipeline instead of three. **One pipeline** because a skinned or
	// layered blended mesh has nowhere to go yet: the transparent variant is
	// compiled from pbr.rvshader only, so a skinned material marked Blend would
	// need a second variant and a second pipeline for a case nothing in this
	// project has. It is skipped and said rather than drawn through the static
	// pipeline, which would read joint indices as texture coordinates.
	void Renderer3D::FlushTransparent()
	{
		if (!s_Data || !HasTransparent())
			return;

		// Armed but undrawable: drop the recorded view rather than keep it.
		// The clear at the bottom only runs when the pass runs, so an early
		// return here used to hold the view across frames -- and a held
		// view replays a dead camera cull result out of ring buffers the
		// next frame is rewriting. The opaque twin never had the hazard:
		// its view is reset in BeginScene and consumed in EndScene.
		RHICommandList* cmd = Renderer::GetCommandList();
		Renderer3DData::SceneSlot* active = s_Data->ActiveScene;
		if (!cmd || !active || !active->TransparentSet)
		{
			s_Data->TransparentView = {};
			s_Data->TransparentSlots.clear();
			return;
		}

		Renderer3DData::SceneSlot& slot = *active;

		// **The GPU-driven half first**, so the CPU list's draws are laid over
		// it rather than under. Both write the same two attachments and the
		// resolve is order-independent, so this is a preference rather than a
		// requirement -- but the table holds the surfaces and the CPU list
		// holds what could not go in one, and surfaces first is the same order
		// the frame graph's hook puts meshes before particles for.
		if (s_Data->TransparentView.IsValid() && !s_Data->TransparentSlots.empty()
			&& slot.TransparentGpuSet)
		{
			cmd->BindPipeline(s_Data->TransparentPipeline);
			cmd->BindResourceSet(0, slot.TransparentGpuSet);
			if (s_Data->Bindless)
				cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());

			for (uint32_t i = 0; i < (uint32_t)s_Data->TransparentSlots.size(); i++)
			{
				const GpuCull::Slot& entry = s_Data->TransparentSlots[i];
				if (!entry.MeshRef)
					continue;

				ObjectPushConstants object;
				object.BaseInstance = (int32_t)entry.InstanceBase;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

				cmd->BindVertexBuffer(0, entry.MeshRef->GetVertexBuffer());
				cmd->BindIndexBuffer(entry.MeshRef->GetIndexBuffer(), IndexType::UInt32);
				// Twenty-four bytes apart, not twenty: the blended table's
				// commands are the same SlotCommand rows the opaque table's
				// are -- a draw and its InstanceBase -- because one cull
				// shader writes both. Stepping by the bare draw command read
				// slot 1 from the middle of slot 0 and every later slot from
				// the wrong place, so only the first blended mesh ever drew:
				// the windscreen, and no headlamp glass, grille or side
				// windows behind it.
				cmd->DrawIndexedIndirect(s_Data->TransparentView.Commands,
										 (uint64_t)i * sizeof(GpuCull::SlotCommand),
										 1, sizeof(GpuCull::SlotCommand));

				s_Data->DrawCalls++;
				s_Data->IndirectDraws++;
				s_Data->Triangles +=
					(entry.MeshRef->GetIndexCount() / 3) * entry.InstanceCount;
			}
		}

		const uint32_t count = (uint32_t)s_Data->Pending.size();
		Ref<RHIPipeline> boundPipeline;

		uint32_t start = s_Data->TransparentBegin;
		while (start < count)
		{
			uint32_t end = start + 1;
			while (end < count &&
				   s_Data->Pending[end].Kind == s_Data->Pending[start].Kind &&
				   s_Data->Pending[end].MeshKey == s_Data->Pending[start].MeshKey &&
				   s_Data->Pending[end].MaterialKey == s_Data->Pending[start].MaterialKey &&
				   s_Data->Pending[end].IndexCount == s_Data->Pending[start].IndexCount)
			{
				end++;
			}

			const PendingDraw& first = s_Data->Pending[start];

			const bool isWater = first.Kind == DrawKind::Water;

			if (first.Kind != DrawKind::Static && !isWater)
			{
				// The skip the contract above promises, now actually said:
				// this run was classified blended but only the static
				// pipeline has a transparent variant.
				if (!s_Data->SkinnedBlendWarned)
				{
					RV_CORE_WARN("Renderer3D: a non-static mesh wears a blended "
								 "material; the transparent pass has no pipeline "
								 "for it, so it is not drawn. Give it an opaque "
								 "material or extend the transparent variant to "
								 "its pipeline kind.");
					s_Data->SkinnedBlendWarned = true;
				}
				start = end;
				continue;
			}

			const Ref<RHIPipeline>& pipeline =
				isWater ? s_Data->WaterPipeline : s_Data->TransparentPipeline;
			if (!pipeline)
			{
				start = end;
				continue;
			}

			// **Which pipeline was bound, not whether one was.** `bound` used to
			// be a bool, which was exact while the transparent pass had one
			// pipeline and silently wrong the moment it had two: the first
			// water run would bind, and a static run after it would inherit the
			// water pipeline and draw a crate as a wave.
			//
			// The sort keys on Kind, so in practice this switches once -- but
			// the correctness must not rest on that.
			if (boundPipeline != pipeline)
			{
				cmd->BindPipeline(pipeline);
				cmd->BindResourceSet(0, slot.TransparentSet);
				if (s_Data->Bindless)
					cmd->BindResourceSet(TextureHeap::kSet, s_Data->Heap->GetSet());
				boundPipeline = pipeline;
			}

			if (first.MaterialRef && !s_Data->Bindless)
				first.MaterialRef->Bind(*cmd, pipeline, 1);

			if (isWater)
			{
				// The water pipeline's own set (set 3): the backdrop pair the
				// frame graph handed over, this body's foam buffer, and the
				// two generated tiles. Every binding filled every time -- a
				// declared binding left empty is a validation error -- with
				// the shared stand-ins where a texture is not there: black
				// foam is a calm sea, a flat normal is no ripple, and a black
				// backdrop never shows because the flags lane says not to
				// read it.
				if (!s_Data->WaterClampSampler)
				{
					SamplerDesc clamp;
					clamp.WrapU = WrapMode::ClampToEdge;
					clamp.WrapV = WrapMode::ClampToEdge;
					clamp.WrapW = WrapMode::ClampToEdge;
					clamp.MaxLod = 0.0f;
					s_Data->WaterClampSampler = s_Data->Device->CreateSampler(clamp);

					// The tiles carry full mip chains now; pinned to level 0
					// they aliased into a static-like shimmer past a few
					// hundred metres. Defaults are already trilinear; the
					// anisotropy is for the view a sea always is -- the most
					// grazing surface the engine draws.
					SamplerDesc wrap;
					wrap.MaxAnisotropy = 8.0f;
					s_Data->WaterWrapSampler = s_Data->Device->CreateSampler(wrap);
				}

				if (slot.WaterSetCursor >= (uint32_t)slot.WaterSets.size())
					slot.WaterSets.push_back(
						s_Data->Device->CreateResourceSet(s_Data->WaterPipeline, 3));
				const Ref<RHIResourceSet>& waterSet =
					slot.WaterSets[slot.WaterSetCursor++];

				const bool backdrop = s_Data->WaterBackdropColor
								   && s_Data->WaterBackdropDepth;
				const Ref<RHITexture> black = TextureLoader::TransparentBlack(*s_Data->Device);
				waterSet->SetTexture(0, backdrop ? s_Data->WaterBackdropColor : black,
									 s_Data->WaterClampSampler);
				waterSet->SetTexture(1, backdrop ? s_Data->WaterBackdropDepth : black,
									 s_Data->WaterClampSampler);
				waterSet->SetTexture(2, first.Water.Foam ? first.Water.Foam : black,
									 s_Data->WaterClampSampler);
				waterSet->SetTexture(3, Water::GetDetailNormal(),
									 s_Data->WaterWrapSampler);
				waterSet->SetTexture(4, Water::GetFoamPattern(),
									 s_Data->WaterWrapSampler);
				waterSet->Commit();
				cmd->BindResourceSet(3, waterSet);

				WaterPushConstants object;
				object.BaseInstance = (int32_t)start;
				object.Shallow = first.Water.Shallow;
				object.Deep = first.Water.Deep;
				object.Wave = first.Water.Wave;
				object.Extra = first.Water.Extra;
				object.Size = first.Water.Size;

				// The flags lane is the renderer's to fill: whether the
				// backdrop is there to read, and -- in the sign -- which way
				// texture rows run on this backend, the same fact
				// ScreenReflections.y states for the SSR trace.
				const float rowSign =
					s_Data->Device->GetBackend() == Backend::Vulkan ? -1.0f : 1.0f;
				object.Size.z = backdrop ? rowSign : 0.0f;

				// Both stages: the vertex reads the dials, and the fragment
				// declares the same block so the ranges agree.
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			}
			else
			{
				ObjectPushConstants object;
				object.BaseInstance = (int32_t)start;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			}

			cmd->BindVertexBuffer(0, first.MeshRef->GetVertexBuffer());
			cmd->BindIndexBuffer(first.MeshRef->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(first.IndexCount, end - start);

			s_Data->DrawCalls++;
			s_Data->Triangles += (first.IndexCount / 3) * (end - start);

			start = end;
		}

		s_Data->Pending.clear();
		s_Data->Instances.clear();
		s_Data->TransparentBegin = 0;
		s_Data->TransparentView = {};
		s_Data->TransparentSlots.clear();
	}

	void Renderer3D::BeginShadow(const Mat4& viewProjection)
	{
		if (!s_Data || !s_Data->ShadowShader)
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		if (!s_Data->ShadowPipeline)
		{
			GraphicsPipelineDesc desc;
			desc.Name = "Renderer3D.shadow";
			desc.Shader = s_Data->ShadowShader;
			desc.Topology = PrimitiveTopology::TriangleList;

			// Stated rather than reflected. The shader reads only the position,
			// so reflection would produce a tightly packed 12-byte stride and
			// walk through the mesh buffer reading normals as positions.
			VertexBinding binding;
			binding.Binding = 0;
			binding.Stride = sizeof(MeshVertex);
			desc.VertexInput.Bindings = { binding };
			desc.VertexInput.Attributes = {
				{ 0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, Position) },
			};

			// Nothing culled.
			//
			// Culling front faces is the classic way to avoid acne -- the depth
			// written is then the *back* of each caster, a bias that scales with
			// the geometry instead of with a guess. It also moves every shadow
			// away from its caster by that same thickness, which on a sphere is
			// a whole diameter: the small spheres in the sample scene had their
			// shadows sitting most of a radius away from them, which reads as
			// the shadow belonging to something else.
			//
			// Culling nothing records the surface nearest the light, keeps
			// contact where it belongs, and lets single-sided geometry cast at
			// all. Acne is then entirely the biases' problem, which is what the
			// normal offset was written for.
			desc.Rasterizer.Cull = CullMode::None;
			desc.Rasterizer.Front = FrontFace::CounterClockwise;

			// Slope-scaled on top, for surfaces nearly edge-on to the light,
			// where one texel of shadow map covers a lot of depth.
			//
			// Small, because these were tuned alongside front-face culling and
			// its free thickness. Every unit of bias here is a unit the shadow
			// moves away from whatever cast it, and on an object the size of a
			// few texels the shadow ends up looking like it belongs to
			// something else standing nearby.
			//
			// **Negative, because depth is reversed.** A bias pushes the
			// fragment away from the light, and away is now *down* -- the far
			// plane is 0. The magnitudes are the ones tuned above; only the
			// direction changed. Left positive they would pull each caster
			// towards the light instead, which is acne turned up rather than
			// off, and it reads as a shadow bug rather than a sign error.
			desc.Rasterizer.DepthBiasEnable = true;
			desc.Rasterizer.DepthBiasConstant = -0.6f;
			desc.Rasterizer.DepthBiasSlope = -1.4f;

			desc.DepthStencil.DepthTestEnable = true;
			desc.DepthStencil.DepthWriteEnable = true;
			desc.ColorFormats.clear();
			desc.DepthFormat = s_Data->ShadowDepth;

			s_Data->ShadowPipeline = s_Data->Device->CreatePipeline(desc);

			// **The cutout twin: the same depth state, one more attribute.**
			//
			// Every rasterizer setting is shared, the biases included -- a
			// cutout wants exactly the acne treatment its opaque neighbour
			// gets. What differs is the vertex layout, which now carries the
			// texture coordinate, and a fragment stage that is not empty.
			if (s_Data->ShadowMaskedShader)
			{
				GraphicsPipelineDesc masked = desc;
				masked.Name = "Renderer3D.shadowMasked";
				masked.Shader = s_Data->ShadowMaskedShader;
				masked.VertexInput.Attributes = {
					{ 0, 0, Format::R32G32B32_SFLOAT, offsetof(MeshVertex, Position) },
					{ 1, 0, Format::R32G32_SFLOAT,    offsetof(MeshVertex, TexCoord) },
				};
				s_Data->ShadowMaskedPipeline = s_Data->Device->CreatePipeline(masked);
			}
			else
			{
				s_Data->ShadowMaskedPipeline = nullptr;
			}

			// The meshlet twin: the same rasterizer, the same biases, the same
			// depth format -- a different front end and nothing else, which is
			// what "the two paths draw the same image" rests on. No vertex
			// input: a mesh stage has none, and the pipeline knows it from the
			// shader.
			if (s_Data->ShadowMeshletShader)
			{
				GraphicsPipelineDesc meshlet = desc;
				meshlet.Name = "Renderer3D.shadowMeshlet";
				meshlet.Shader = s_Data->ShadowMeshletShader;
				meshlet.VertexInput = {};
				s_Data->ShadowMeshletPipeline = s_Data->Device->CreatePipeline(meshlet);
				if (!s_Data->ShadowMeshletPipeline)
					RV_CORE_ERROR("Renderer3D: meshlet depth pipeline failed; "
								  "the classic path runs instead");
			}

			// The skinned depth pipeline. Its vertex layout is reflected from
			// its own shader -- which declares the normal and texture
			// coordinate it does not read, precisely so the stride matches the
			// buffer the lit skinned pass reads. Stating a tighter one here
			// would walk through the mesh reading joints as positions.
			if (s_Data->ShadowSkinnedShader)
			{
				GraphicsPipelineDesc skinned = desc;
				skinned.Name = "Renderer3D.shadow.skinned";
				skinned.Shader = s_Data->ShadowSkinnedShader;
				skinned.VertexInput = {};   // reflected, unlike the static one
				s_Data->ShadowSkinnedPipeline = s_Data->Device->CreatePipeline(skinned);
			}
		}

		if (!s_Data->ShadowPipeline)
			return;

		s_Data->ShadowViewProjection = viewProjection;
		s_Data->ShadowPending.clear();
		s_Data->ShadowBoneScratch.clear();
		cmd->BindPipeline(s_Data->ShadowPipeline);
		s_Data->ShadowActive = true;
	}

	void Renderer3D::DrawMeshShadow(const Ref<Mesh>& mesh, const Mat4& transform,
									const Ref<Material>& masked)
	{
		if (!s_Data || !s_Data->ShadowActive || !mesh)
			return;

		PendingShadowDraw draw;
		draw.MeshKey = mesh.get();
		draw.MeshRef = mesh;
		draw.LightMVP = s_Data->ShadowViewProjection * transform;

		// **Only an alpha-tested material is carried.** Everything else casts
		// through the position-only pipeline, which is the whole scene in
		// almost every frame -- so the caller passes the material and this
		// decides, rather than every caller having to know the rule.
		if (masked && masked->GetBlendMode() == BlendMode::Masked &&
			s_Data->ShadowMaskedPipeline)
		{
			draw.MaterialKey = masked.get();
			draw.MaterialRef = masked;
		}

		s_Data->ShadowPending.push_back(std::move(draw));
	}

	void Renderer3D::ReserveSceneInstances(uint32_t count)
	{
		if (!s_Data || !s_Data->SceneActive)
			return;

		// Grown, never shrunk within a scene: the CPU path's draws append
		// after these and their indices are absolute.
		if (s_Data->Instances.size() < count)
			s_Data->Instances.resize(count);
		s_Data->ReservedInstances = count;
	}

	void Renderer3D::SetSceneInstance(uint32_t index, const Mat4& transform,
									  const Mat4& previousTransform,
									  const Ref<Material>& material,
									  const MaterialParams& params, uint32_t probe)
	{
		if (!s_Data || !s_Data->SceneActive || index >= s_Data->ReservedInstances)
			return;

		InstanceData& instance = s_Data->Instances[index];
		instance.Model = transform;
		instance.PreviousModel = previousTransform;
		instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		instance.BaseColor = params.BaseColor;
		instance.EmissiveColor = params.EmissiveColor;
		instance.Surface = { params.Metallic, params.Roughness,
							 params.Occlusion, params.NormalScale };

		// The material's record, resolved here rather than in EndScene: these
		// rows have no pending draw for EndScene's pass to find them by. Same
		// table, same numbering, so a material used by both paths gets one
		// record.
		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		const float record = s_Data->Bindless ? (float)RegisterMaterial(effective) : 0.0f;

		instance.Indices = { 0.0f, (float)probe, record, 0.0f };
	}

	void Renderer3D::DrawSceneIndirect(const GpuCull::View& view,
									   const std::vector<GpuCull::Slot>& slots)
	{
		if (!s_Data || !s_Data->SceneActive || !view.IsValid() || slots.empty())
			return;

		// Recorded, not issued. EndScene has to fill and bind the instance
		// table before any of this can draw, and it cannot do that until every
		// submission is in.
		s_Data->IndirectView = view;
		s_Data->IndirectSlots = slots;
	}

	// The blended table's indirect draws, held for the transparent pass.
	//
	// **Bindless only, exactly as the opaque indirect path is.** One indirect
	// call covers a whole mesh slot, so every instance in it is shaded by
	// whatever set is bound -- correct only when the material is per instance,
	// which it is only under bindless. Without it the scene keeps its blended
	// meshes on the CPU list and this is never called.
	void Renderer3D::DrawTransparentIndirect(const GpuCull::View& view,
											 const std::vector<GpuCull::Slot>& slots)
	{
		if (!s_Data || !s_Data->SceneActive || !view.IsValid() || slots.empty())
			return;
		if (!s_Data->Bindless || !s_Data->TransparentPipeline)
			return;

		s_Data->TransparentView = view;
		s_Data->TransparentSlots = slots;
	}

	void Renderer3D::DrawShadowIndirect(const GpuCull::View& view,
										const std::vector<GpuCull::Slot>& slots)
	{
		if (!s_Data || !s_Data->ShadowActive || !view.IsValid() || slots.empty())
			return;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd || !s_Data->ShadowPipeline)
			return;

		const uint32_t frame = s_Data->Device->GetFrameIndex();
		auto& sets = s_Data->CulledShadowSets[frame];

		// **Both rows claimed and the vector grown before either is touched.**
		//
		// This pass needs two sets now -- one per pipeline -- and the ring they
		// come from grows on demand. Taking a reference to the first row and
		// then pushing the second is a dangling reference the moment the push
		// reallocates, which is an access violation inside the draw loop a long
		// way from the cause. So: claim the indices, grow once, and hold the
		// sets by value.
		const uint32_t opaqueRow = s_Data->CulledShadowCursor++;
		const bool wantMasked = s_Data->ShadowMaskedPipeline != nullptr;
		const uint32_t maskedRow = wantMasked ? s_Data->CulledShadowCursor++ : opaqueRow;

		while (sets.size() <= (size_t)Math::Max(opaqueRow, maskedRow))
			sets.push_back(nullptr);

		if (!sets[opaqueRow])
			sets[opaqueRow] = s_Data->Device->CreateResourceSet(s_Data->ShadowPipeline, 0);

		const Ref<RHIResourceSet> set = sets[opaqueRow];
		if (!set)
			return;

		// The cutout casters' own set against their own pipeline, reading the
		// same instance buffer.
		Ref<RHIResourceSet> maskedSet;
		if (wantMasked)
		{
			if (!sets[maskedRow])
			{
				sets[maskedRow] =
					s_Data->Device->CreateResourceSet(s_Data->ShadowMaskedPipeline, 0);
			}
			maskedSet = sets[maskedRow];
			if (maskedSet)
			{
				maskedSet->SetStorageBuffer(0, view.Instances);
				maskedSet->Commit();
			}
		}

		// The buffer the cull pass wrote, in place of the one the CPU used to
		// fill. shadow_depth.rvshader does not know the difference: it was
		// already reading one matrix per instance out of set 0 binding 0, and
		// it still is.
		set->SetStorageBuffer(0, view.Instances);
		set->Commit();

		// The cull filled as many commands as the table had slots; a shorter
		// `slots` than that would mean the two came from different walks.
		const uint32_t count = Math::Min((uint32_t)slots.size(), view.SlotCount);

		// **A cutout casts the shadow its alpha describes**, which needs the
		// pipeline that tests the alpha and the material to test it against.
		// Bound per slot rather than per sweep, because a masked slot is keyed
		// by material precisely so that this bind is unambiguous.
		bool boundMasked = false;
		bool anyBound = false;

		for (uint32_t i = 0; i < count; i++)
		{
			const GpuCull::Slot& entry = slots[i];
			if (!entry.MeshRef)
				continue;

			const bool masked = entry.MaskedMaterial != nullptr;
			if (masked && !maskedSet)
				continue;   // no cutout pipeline: cast nothing rather than a solid sheet

			if (!anyBound || boundMasked != masked)
			{
				cmd->BindPipeline(masked ? s_Data->ShadowMaskedPipeline : s_Data->ShadowPipeline);
				cmd->BindResourceSet(0, masked ? maskedSet : set);
				boundMasked = masked;
				anyBound = true;
			}

			if (masked)
				entry.MaskedMaterial->Bind(*cmd, s_Data->ShadowMaskedPipeline, 1);

			// Where this slot's instances begin, from the CPU's copy of the
			// same table the GPU has. Not the draw's firstInstance, for the
			// reason shadow_depth.rvshader gives: the two backends disagree
			// about whether it reaches the shader.
			ObjectPushConstants object;
			object.BaseInstance = (int32_t)entry.InstanceBase;
			cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);

			cmd->BindVertexBuffer(0, entry.MeshRef->GetVertexBuffer());
			cmd->BindIndexBuffer(entry.MeshRef->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexedIndirect(view.Commands,
									 (uint64_t)i * sizeof(GpuCull::SlotCommand),
									 1, sizeof(GpuCull::SlotCommand));

			// Counted the same way the camera's indirect draws are: the draw
			// exactly, the triangles at what was submitted to the cull.
			s_Data->DrawCalls++;
			s_Data->IndirectDraws++;
			s_Data->Triangles +=
				(entry.MeshRef->GetIndexCount() / 3) * entry.InstanceCount;
		}
	}

	void Renderer3D::DrawSkinnedMeshShadow(const Ref<Mesh>& mesh, const Mat4& transform,
										   const std::vector<Mat4>& bones)
	{
		if (!s_Data || !s_Data->ShadowActive || !mesh)
			return;

		const int32_t base = (int32_t)s_Data->ShadowBoneScratch.size();

		if (bones.empty())
			s_Data->ShadowBoneScratch.emplace_back(1.0f);
		else
		{
			s_Data->ShadowBoneScratch.insert(s_Data->ShadowBoneScratch.end(),
											 bones.begin(), bones.end());
		}

		PendingShadowDraw draw;
		draw.MeshKey = mesh.get();
		draw.MeshRef = mesh;
		draw.LightMVP = s_Data->ShadowViewProjection * transform;
		draw.Skinned = true;
		draw.BoneBase = base;

		s_Data->ShadowPending.push_back(std::move(draw));
	}

	bool Renderer3D::LitMeshletsActive()
	{
		return s_Data && (s_Data->MeshletLitPipeline || s_Data->MeshletLitShader);
	}

	bool Renderer3D::ShadowMeshletsActive()
	{
		// The pipeline is made lazily in BeginShadow, so before the first
		// shadow pass the shader is the honest answer to "will it be".
		return s_Data && (s_Data->ShadowMeshletPipeline || s_Data->ShadowMeshletShader);
	}

	void Renderer3D::EndShadow()
	{
		if (!s_Data || !s_Data->ShadowActive)
			return;

		s_Data->ShadowActive = false;

		RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd || s_Data->ShadowPending.empty() || !s_Data->ShadowPipeline)
		{
			s_Data->ShadowPending.clear();
			return;
		}

		// The depth pass is where batching pays most: a frame opens one of
		// these per cascade, per casting spot light and per face of every point
		// light's cube, and every one of them walked the whole scene issuing a
		// draw per caster.
		std::sort(s_Data->ShadowPending.begin(), s_Data->ShadowPending.end(),
				  [](const PendingShadowDraw& a, const PendingShadowDraw& b)
				  {
					  // Static first, then skinned, for the same reason the lit
					  // pass does it: two pipelines, two vertex layouts.
					  if (a.Skinned != b.Skinned)
						  return !a.Skinned;
					  // Masked casters after opaque ones: a third pipeline, so
					  // a third block, exactly as the skinned bit above.
					  if ((a.MaterialKey != nullptr) != (b.MaterialKey != nullptr))
						  return a.MaterialKey == nullptr;
					  if (a.MeshKey != b.MeshKey)
						  return a.MeshKey < b.MeshKey;
					  // And by material within that: a masked run binds one
					  // set 1, so two cutouts with different alpha maps cannot
					  // share a draw. Opaque casters all have null here and the
					  // comparison is a no-op for them.
					  if (a.MaterialKey != b.MaterialKey)
						  return a.MaterialKey < b.MaterialKey;
					  // A skinned run is one caster at a time: each has its own
					  // pose, and the bone base is pushed rather than carried
					  // per instance in this pass.
					  return a.BoneBase < b.BoneBase;
				  });

		Renderer3DData::ShadowSlot& slot = AcquireShadowSlot();
		const uint32_t count = (uint32_t)s_Data->ShadowPending.size();

		if (!EnsureInstanceBuffer(slot.Instances, slot.InstanceCapacity, count,
								  sizeof(Mat4), "Renderer3D.shadowInstances"))
		{
			s_Data->ShadowPending.clear();
			return;
		}

		s_Data->ShadowScratch.clear();
		s_Data->ShadowScratch.reserve(count);
		for (const PendingShadowDraw& draw : s_Data->ShadowPending)
			s_Data->ShadowScratch.push_back(draw.LightMVP);

		slot.Instances->Upload(s_Data->ShadowScratch.data(),
							   (uint64_t)count * sizeof(Mat4));

		slot.Set->SetStorageBuffer(0, slot.Instances, 0, (uint64_t)count * sizeof(Mat4));
		slot.Set->Commit();

		// The skinned depth pass has a second set, because its layout declares
		// a bone buffer the static one does not. Built only when something in
		// this pass is actually skinned.
		const bool anySkinned = s_Data->ShadowPending.back().Skinned;
		if (anySkinned && s_Data->ShadowSkinnedPipeline)
		{
			const uint32_t boneCount =
				Math::Max((uint32_t)s_Data->ShadowBoneScratch.size(), 1u);

			if (EnsureInstanceBuffer(slot.Bones, slot.BoneCapacity, boneCount,
									 sizeof(Mat4), "Renderer3D.shadowBones"))
			{
				if (s_Data->ShadowBoneScratch.empty())
				{
					const Mat4 identity(1.0f);
					slot.Bones->Upload(&identity, sizeof(identity));
				}
				else
				{
					slot.Bones->Upload(s_Data->ShadowBoneScratch.data(),
									   s_Data->ShadowBoneScratch.size() * sizeof(Mat4));
				}

				if (!slot.SkinnedSet)
				{
					slot.SkinnedSet =
						s_Data->Device->CreateResourceSet(s_Data->ShadowSkinnedPipeline, 0);
				}

				slot.SkinnedSet->SetStorageBuffer(0, slot.Instances, 0,
												  (uint64_t)count * sizeof(Mat4));
				slot.SkinnedSet->SetStorageBuffer(1, slot.Bones, 0,
												  (uint64_t)boneCount * sizeof(Mat4));
				slot.SkinnedSet->Commit();
			}
		}

		bool boundSkinned = false;
		bool boundMasked = false;
		bool anyBound = false;

		uint32_t start = 0;
		while (start < count)
		{
			uint32_t end = start + 1;
			while (end < count &&
				   s_Data->ShadowPending[end].Skinned == s_Data->ShadowPending[start].Skinned &&
				   // One set 1 per run, so a run is one material.
				   s_Data->ShadowPending[end].MaterialKey ==
					   s_Data->ShadowPending[start].MaterialKey &&
				   s_Data->ShadowPending[end].MeshKey == s_Data->ShadowPending[start].MeshKey &&
				   // A skinned run is one caster: the bone base is a push
				   // constant here, so two characters cannot share a draw.
				   !s_Data->ShadowPending[start].Skinned)
			{
				end++;
			}

			const PendingShadowDraw& first = s_Data->ShadowPending[start];
			const Ref<Mesh>& mesh = first.MeshRef;

			// Static casters go as meshlets when the pipeline exists and the
			// mesh cut cleanly; everything else -- skinned casters, a mesh
			// the cut refused -- takes the classic path in the same pass.
			// **Never for a masked caster.** There is no cutout meshlet
			// variant, and drawing one through this would put back exactly the
			// solid-sheet shadow the masked pipeline exists to remove.
			if (!first.Skinned && !first.MaterialKey && s_Data->ShadowMeshletPipeline && mesh)
			{
				const Mesh::MeshletBuffers& meshlets =
					mesh->GetMeshletBuffers(*s_Data->Device);
				if (meshlets.Count > 0)
				{
					while (slot.MeshletCursor >= slot.MeshletSets.size())
						slot.MeshletSets.push_back(nullptr);
					Ref<RHIResourceSet>& meshletSet =
						slot.MeshletSets[slot.MeshletCursor];
					if (!meshletSet)
					{
						meshletSet = s_Data->Device->CreateResourceSet(
							s_Data->ShadowMeshletPipeline, 0);
					}

					if (meshletSet)
					{
						slot.MeshletCursor++;

						meshletSet->SetStorageBuffer(0, slot.Instances, 0,
													 (uint64_t)count * sizeof(Mat4));
						meshletSet->SetStorageBuffer(1, meshlets.Meshlets);
						meshletSet->SetStorageBuffer(2, meshlets.Vertices);
						meshletSet->SetStorageBuffer(3, meshlets.Triangles);
						meshletSet->SetStorageBuffer(4, meshlets.Positions);
						meshletSet->Commit();

						cmd->BindPipeline(s_Data->ShadowMeshletPipeline);
						cmd->BindResourceSet(0, meshletSet);
						// The next classic batch must rebind its own pipeline.
						anyBound = false;

						ObjectPushConstants object;
						object.BaseInstance = (int32_t)start;
						cmd->PushConstants(ShaderStage::Mesh, 0, sizeof(object), &object);

						// x spans the meshlets, y the instances of this run.
						cmd->DrawMeshTasks(meshlets.Count, end - start, 1);
						s_Data->DrawCalls++;
						s_Data->Triangles +=
							(mesh->GetIndexCount() / 3) * (end - start);

						start = end;
						continue;
					}
				}
			}

			// A masked caster reads its alpha through set 1, so it wants the
			// pipeline whose fragment stage tests it. Its set 0 is the same
			// instance data as everything else in the pass -- only allocated
			// against this pipeline, because a set always is.
			const bool masked = first.MaterialKey != nullptr;
			if (masked && s_Data->ShadowMaskedPipeline && !slot.MaskedSet)
			{
				slot.MaskedSet =
					s_Data->Device->CreateResourceSet(s_Data->ShadowMaskedPipeline, 0);
				if (slot.MaskedSet)
				{
					slot.MaskedSet->SetStorageBuffer(0, slot.Instances, 0,
													 (uint64_t)count * sizeof(Mat4));
					slot.MaskedSet->Commit();
				}
			}

			const Ref<RHIPipeline>& pipeline =
				first.Skinned ? s_Data->ShadowSkinnedPipeline
				: masked      ? s_Data->ShadowMaskedPipeline
							  : s_Data->ShadowPipeline;
			const Ref<RHIResourceSet>& set =
				first.Skinned ? slot.SkinnedSet
				: masked      ? slot.MaskedSet
							  : slot.Set;

			// A skinned caster with no skinned pipeline is skipped rather than
			// drawn by the static one: it would cast the shadow of a mesh
			// scattered across the world, which is worse than casting none.
			//
			// A masked caster whose pipeline is missing is skipped for the
			// matching reason: through the opaque pipeline it would cast the
			// solid shadow of its whole sheet.
			if (!pipeline || !set)
			{
				start = end;
				continue;
			}

			if (!anyBound || boundSkinned != first.Skinned || boundMasked != masked)
			{
				cmd->BindPipeline(pipeline);
				cmd->BindResourceSet(0, set);
				boundSkinned = first.Skinned;
				boundMasked = masked;
				anyBound = true;
			}

			// Set 1, per run, exactly as the lit bound path binds it -- so the
			// alpha this pass tests is read from the same block and the same
			// texture the lit pass will test.
			if (masked && first.MaterialRef)
				first.MaterialRef->Bind(*cmd, pipeline, 1);

			if (first.Skinned)
			{
				SkinnedShadowPushConstants object;
				object.BaseInstance = (int32_t)start;
				object.BoneBase = first.BoneBase < 0 ? 0 : first.BoneBase;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			}
			else
			{
				ObjectPushConstants object;
				object.BaseInstance = (int32_t)start;
				cmd->PushConstants(ShaderStage::Vertex, 0, sizeof(object), &object);
			}

			cmd->BindVertexBuffer(0, mesh->GetVertexBuffer());
			cmd->BindIndexBuffer(mesh->GetIndexBuffer(), IndexType::UInt32);
			cmd->DrawIndexed(mesh->GetIndexCount(), end - start);

			s_Data->DrawCalls++;
			s_Data->Triangles += (mesh->GetIndexCount() / 3) * (end - start);

			start = end;
		}

		s_Data->ShadowPending.clear();
	}

	void Renderer3D::DrawMesh(const Ref<Mesh>& mesh, const Mat4& transform,
							  const Ref<Material>& material, const MaterialParams& params,
							  uint32_t probe,
							   const Mat4* previousTransform)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (!effective)
			return;

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = effective->GetBatchKey(s_Data->Bindless);
		draw.MeshRef = mesh;
		draw.MaterialRef = effective;
		draw.IndexCount = mesh->GetIndexCount();
		draw.Bucket = IsBlended(effective->GetBlendMode())   ? DrawBucket::Blended
					: effective->GetBlendMode() == BlendMode::Masked ? DrawBucket::Masked
																	 : DrawBucket::Opaque;

		InstanceData& instance = AllocateInstance(draw);
		instance.Model = transform;
		instance.PreviousModel = previousTransform ? *previousTransform : transform;
		// Once per object rather than once per vertex, which is where the
		// shader was doing it -- an inverse and a transpose of a 3x3 for every
		// vertex of every mesh, all producing the same matrix.
		instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		instance.BaseColor = params.BaseColor;
		instance.EmissiveColor = params.EmissiveColor;
		instance.Surface = { params.Metallic, params.Roughness,
							 params.Occlusion, params.NormalScale };
		// No bones, and the probe the scene picked for this object.
		instance.Indices = { 0.0f, (float)probe, 0.0f, 0.0f };

		{
			const Vec3 eye = Vec3(s_Data->Scene.CameraPosition);
			const Vec3 centre = Vec3(transform[3]);
			draw.ViewDepth = Math::Length(centre - eye);
		}

		// Recorded, not drawn. EndScene sorts these and issues one draw per run
		// of identical state; drawing here is what made the count equal the
		// object count.
		s_Data->Pending.push_back(std::move(draw));
	}

	void Renderer3D::DrawWaterMesh(const Ref<Mesh>& mesh, const Mat4& transform,
								   const Ref<Material>& material,
								   const MaterialParams& params, uint32_t probe,
								   const WaterDraw& water,
								   const Mat4* previousTransform)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (!effective)
			return;

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = effective->GetBatchKey(s_Data->Bindless);
		draw.MeshRef = mesh;
		draw.MaterialRef = effective;
		draw.IndexCount = mesh->GetIndexCount();
		draw.Kind = DrawKind::Water;
		draw.Water = water;

		// **Blended whatever the material says.** The water pipeline writes the
		// accumulate/revealage pair and nothing else; a body routed to the
		// opaque pass would be drawn by a pipeline whose outputs do not match
		// the attachments, which is a validation error rather than a dark sea.
		draw.Bucket = DrawBucket::Blended;

		InstanceData& instance = AllocateInstance(draw);
		instance.Model = transform;
		instance.PreviousModel = previousTransform ? *previousTransform : transform;
		instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		instance.BaseColor = params.BaseColor;
		instance.EmissiveColor = params.EmissiveColor;
		instance.Surface = { params.Metallic, params.Roughness,
							 params.Occlusion, params.NormalScale };
		instance.Indices = { 0.0f, (float)probe, 0.0f, 0.0f };

		{
			const Vec3 eye = Vec3(s_Data->Scene.CameraPosition);
			const Vec3 centre = Vec3(transform[3]);
			draw.ViewDepth = Math::Length(centre - eye);
		}

		s_Data->Pending.push_back(std::move(draw));
	}

	void Renderer3D::DrawSkinnedMesh(const Ref<Mesh>& mesh, const Mat4& transform,
									 const Ref<Material>& material, const MaterialParams& params,
									 const std::vector<Mat4>& bones, uint32_t probe,
							   const Mat4* previousTransform,
							   const std::vector<Mat4>* previousBones)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh)
			return;

		// A skinned mesh with no pose is drawn by the skinned pipeline anyway:
		// its vertex layout is the wider one, and the static pipeline would
		// read joints as texture coordinates. An empty bone run leaves every
		// weighted matrix zero, so one identity is supplied instead and the
		// mesh appears in its bind pose.
		const uint32_t base = (uint32_t)s_Data->BoneScratch.size();

		if (bones.empty())
			s_Data->BoneScratch.emplace_back(1.0f);
		else
			s_Data->BoneScratch.insert(s_Data->BoneScratch.end(), bones.begin(), bones.end());

		// **And last frame's pose, immediately after this one.** In the same
		// buffer rather than a second binding: set 0 is the scene's, four
		// other shaders reflect its layout, and a run appended here costs a
		// base index the instance row already has a free lane for.
		//
		// A run of the same length either way, so `prevBase + joint` addresses
		// the matching bone. Without a previous pose the current one stands in
		// and the deformation contributes no velocity -- which is what this
		// did before, and is still right for a mesh that has no animator.
		const uint32_t prevBase = (uint32_t)s_Data->BoneScratch.size();
		const std::vector<Mat4>& previous =
			previousBones && previousBones->size() == bones.size() ? *previousBones : bones;

		if (previous.empty())
			s_Data->BoneScratch.emplace_back(1.0f);
		else
			s_Data->BoneScratch.insert(s_Data->BoneScratch.end(),
									   previous.begin(), previous.end());

		const Ref<Material>& effective = material ? material : s_Data->DefaultMaterial;
		if (!effective)
			return;

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = effective->GetBatchKey(s_Data->Bindless);
		draw.Kind = DrawKind::Skinned;
		draw.MeshRef = mesh;
		draw.MaterialRef = effective;
		draw.IndexCount = mesh->GetIndexCount();
		// Classified exactly as the static path classifies, or the scene and
		// the renderer disagree about which pass owns the mesh: the scene
		// allocates the OIT attachments for a blended skinned mesh while an
		// unset flag here would draw it opaque -- an opaque-looking
		// character that casts no shadow, because the shadow walk excludes
		// what it believes is glass. The transparent pass then skips it and
		// says so (no skinned transparent variant exists), which is the
		// documented contract.
		draw.Bucket = IsBlended(effective->GetBlendMode())   ? DrawBucket::Blended
					: effective->GetBlendMode() == BlendMode::Masked ? DrawBucket::Masked
																	 : DrawBucket::Opaque;

		InstanceData& instance = AllocateInstance(draw);
		instance.Model = transform;
		instance.PreviousModel = previousTransform ? *previousTransform : transform;
		instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		instance.BaseColor = params.BaseColor;
		instance.EmissiveColor = params.EmissiveColor;
		instance.Surface = { params.Metallic, params.Roughness,
							 params.Occlusion, params.NormalScale };
		instance.Indices = { (float)base, (float)probe, 0.0f, (float)prevBase };

		// The same depth the other two submission paths carry, so a skinned
		// run is ordered front to back like the runs beside it rather than
		// riding at depth zero ahead of everything.
		{
			const Vec3 eye = Vec3(s_Data->Scene.CameraPosition);
			const Vec3 centre = Vec3(transform[3]);
			draw.ViewDepth = Math::Length(centre - eye);
		}

		s_Data->Pending.push_back(std::move(draw));
	}

	void Renderer3D::DrawLayeredMesh(const Ref<Mesh>& mesh, const Mat4& transform,
									 const Ref<LayeredMaterial>& layered, uint32_t probe,
									 uint32_t indexCount, const Mat4* previousTransform)
	{
		if (!s_Data || !s_Data->SceneActive || !mesh || !layered)
			return;
		indexCount = Math::Min(indexCount, mesh->GetIndexCount());
		if (indexCount == 0)
			return;

		// Layer 0 stands in for "the material" everywhere the rest of the
		// frame wants one: the instance's scalars, the record the bindless
		// instance names (so g_Material is a real record), and the material a
		// traced reflection of this chunk shades with (7aq's stated limit).
		Ref<Material> base = layered->GetLayer(0);
		if (!base)
			base = s_Data->DefaultMaterial;
		if (!base)
			return;
		const MaterialParams& params = base->GetParams();

		PendingDraw draw;
		draw.MeshKey = mesh.get();
		draw.MaterialKey = layered->GetBatchKey();
		draw.Kind = DrawKind::Layered;
		draw.MeshRef = mesh;
		draw.MaterialRef = base;
		draw.LayeredRef = layered;
		draw.IndexCount = indexCount;

		InstanceData& instance = AllocateInstance(draw);
		instance.Model = transform;
		instance.PreviousModel = previousTransform ? *previousTransform : transform;
		instance.NormalMatrix = Mat4(Math::Transpose(Math::Inverse(Mat3(transform))));
		instance.BaseColor = params.BaseColor;
		instance.EmissiveColor = params.EmissiveColor;
		instance.Surface = { params.Metallic, params.Roughness,
							 params.Occlusion, params.NormalScale };
		instance.Indices = { 0.0f, (float)probe, 0.0f, 0.0f };

		{
			const Vec3 eye = Vec3(s_Data->Scene.CameraPosition);
			const Vec3 centre = Vec3(transform[3]);
			draw.ViewDepth = Math::Length(centre - eye);
		}

		s_Data->Pending.push_back(std::move(draw));
	}

	TextureHeap* Renderer3D::GetTextureHeap()
	{
		return s_Data && s_Data->Bindless ? s_Data->Heap.get() : nullptr;
	}

	unsigned int Renderer3D::GetCulledCount() { return s_Data ? s_Data->Culled : 0; }
	void Renderer3D::CountCulled() { if (s_Data) s_Data->Culled++; }

	bool Renderer3D::IsReady()
	{
		return s_Data && s_Data->Ready;
	}

	unsigned int Renderer3D::GetMaxCellLoad() { return s_Data ? s_Data->Grid.MaxCellLoad() : 0; }
	unsigned int Renderer3D::GetLightCount() { return s_Data ? (unsigned int)s_Data->LightScratch.size() : 0; }

	unsigned int Renderer3D::GetAreaEmitterCount()
	{
		return s_Data ? (unsigned int)s_Data->Emitters.size() : 0;
	}

	unsigned int Renderer3D::GetAimedEmitterCount()
	{
		if (!s_Data)
			return 0;

		// Aim.y is the grid side, and zero is the documented "radiate evenly"
		// case -- the same test the shader makes.
		unsigned int aimed = 0;
		for (const Renderer3DData::GpuEmitter& row : s_Data->Emitters)
			if (row.Aim.y > 0.0f)
				aimed++;
		return aimed;
	}

	unsigned int Renderer3D::GetDrawCallCount() { return s_Data ? s_Data->DrawCalls : 0; }
	unsigned int Renderer3D::GetTriangleCount() { return s_Data ? s_Data->Triangles : 0; }
	unsigned int Renderer3D::GetIndirectDrawCount() { return s_Data ? s_Data->IndirectDraws : 0; }
}
