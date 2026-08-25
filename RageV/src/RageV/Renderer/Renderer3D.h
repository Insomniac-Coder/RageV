#pragma once
#include "RageV/Math/Math.h"
#include "Camera.h"
#include "IrradianceVolume.h"
#include "Light.h"
#include "Environment.h"
#include "RenderSettings.h"
#include "Mesh.h"
#include "Material.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "GpuCull.h"

namespace RageV
{
	// Lit mesh rendering. Unlike Renderer2D, geometry is not merged: each mesh
	// keeps its own buffers and per-object data goes through push constants, so
	// a draw costs a push-constant write rather than a descriptor update.
	class Renderer3D
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when a shader did not compile. Meshes draw nothing in that
		// state, which is easier to diagnose from here than from the picture.
		static bool IsReady();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth,
									 uint32_t samples = 1,
									 RHI::Format velocity = RHI::Format::Undefined,
									 RHI::Format normal = RHI::Format::Undefined,
									 RHI::Format indirect = RHI::Format::Undefined);
		// At most this many emissive rectangles reach the shader. Past the cap
		// the bounce falls back to finding *those* emitters by hemisphere
		// sampling -- noisier, and still correct, which is the right way
		// round for a cap to fail. See AreaEmitter in Light.h.
		//
		// **That fallback only became true when the subtraction went per
		// instance.** The shader removes a hit's emissive because a shadow
		// ray already answered for it, and it used to do that whenever the
		// list was non-empty at all -- so the seventeenth emitter, and every
		// surface under the strength threshold, was subtracted by the
		// hemisphere term and sampled by nothing. Their light did not become
		// noisier; it disappeared. AreaEmitter::Owner is what carries the
		// answer now, matched onto the ray instance in EndScene.
		static constexpr uint32_t kMaxAreaEmitters = 16;

		// Handed over by the scene each frame, before EndScene. Copied rather
		// than referenced: the upload happens later, and a span into the
		// scene's own vector is a span into something a probe capture may have
		// refreshed in between (7bw).
		static void SetAreaEmitters(const std::vector<AreaEmitter>& emitters);

		// **Where the reflection probes stand**, so the traced bounce can pick
		// one per hit instead of standing every hit on the sky.
		//
		// The lit pass resolves a probe per *object* on the CPU and ships the
		// slot in its instance row; a fullscreen trace has no instance to ask,
		// so it was passing slot 0 -- the sky -- as the ambient behind every
		// bounce. Indoors that is the one answer a probe exists to replace.
		// Pass an empty list to force the sky, which is what a probe capture
		// itself must do: a capture reflects the sky, never another probe.
		static void SetProbeVolumes(const std::vector<ProbeVolume>& probes);

		// **The scene's irradiance field**, and the box it covers in world
		// space. Null unbinds it and every reader falls back to what it did
		// before, which is the flat ambient constant.
		static void SetIrradianceVolume(const RHI::Ref<IrradianceVolume>& volume,
										const Vec3& centre, const Vec3& extents);

		static void SetWireframe(bool enabled);

		// Whether this frame recorded any blended draws, and the pass that
		// issues them. Called from the frame graph's transparent hook, beside
		// ParticleRenderer::FlushWeighted -- the two write the same two
		// attachments and resolve together.
		// The blended table's indirect draws, recorded for the transparent
		// pass. Bindless only, exactly as DrawSceneIndirect is.
		static void DrawTransparentIndirect(const GpuCull::View& view,
											const std::vector<GpuCull::Slot>& slots);

		static bool HasTransparent();
		static void FlushTransparent();

		// Resets the per-frame scene-slot pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// `environmentMap` is what surfaces reflect -- the scene's environment
		// map, a probe's capture, or the sky's gradient baked into a small
		// cube. Null means nothing is reflected, not that the term is
		// undefined: the binding is filled with a neutral cube and the
		// intensity set to zero.
		//
		// `jitter` is the sub-pixel offset already folded into `camera`, in
		// NDC. It is passed as well as applied because the motion vectors have
		// to be free of it: what the velocity attachment means is where the
		// *surface* moved, and half a pixel of camera offset is not that. The
		// default is the honest one -- a caller that did not jitter its camera
		// must not have the correction applied to it either.
		//
		// Two settings blocks rather than one: `environment` is what the scene
		// owns (ambient and sky) and `render` is what the project owns (the
		// shadow normal offset, here). ENGINE-NOTES 7s.
		static void BeginScene(const Camera& camera, const Mat4& cameraTransform,
							   const LightList& lights = {},
							   const SceneEnvironment& environment = {},
							   const RenderSettings& render = {},
							   const RHI::Ref<RHI::RHITexture>& environmentMap = nullptr,
							   const RHI::Ref<RHI::RHITexture>& irradianceMap = nullptr,
							   const Vec2& jitter = Vec2(0.0f, 0.0f));
		static void EndScene();

		// Depth only, from a light. Wraps the same mesh binding the lit path
		// uses, because a shadow pass draws the same geometry and differs only
		// in what it writes.
		//
		// The light's view-projection is folded into each draw's model matrix,
		// so a caster costs one push constant and nothing else -- no descriptor
		// set is bound in this pass at all.
		static void BeginShadow(const Mat4& viewProjection);
		static void DrawMeshShadow(const RHI::Ref<Mesh>& mesh, const Mat4& transform);

		// Whether static casters draw as meshlets this run (--meshlets, a
		// device with mesh shading, and the pipeline built). The scene asks
		// before GPU-culling a shadow view: the meshlet path culls per
		// meshlet in the mesh stage, and a view routed through the indirect
		// path would bypass it entirely.
		static bool ShadowMeshletsActive();
		// The lit-pass twin of the above, gating the camera's compute cull
		// for the same reason.
		static bool LitMeshletsActive();

		// Every static caster this view kept, in as many draws as the scene
		// has distinct meshes -- each one's instance count read out of the
		// buffer GpuCull::Cull filled rather than counted here (roadmap 8.3).
		//
		// Between BeginShadow and EndShadow, like the calls above, and it does
		// not disturb them: the pending list the CPU path accumulates is
		// untouched, so a view can submit its static casters this way and its
		// skinned ones and its terrain the other. `slots` is the table the
		// same RefreshDrawList built, and must be the one the cull was given.
		static void DrawShadowIndirect(const GpuCull::View& view,
									   const std::vector<GpuCull::Slot>& slots);

		// --- the GPU-driven lit path (roadmap 8.3) --------------------------
		//
		// The camera's cull writes *indices* into an instance table rather than
		// instance data, so the table has to exist and be indexed the way the
		// cull table is. These three calls build it.
		//
		// Reserves rows 0..count-1 of the frame's instance pool for the cull
		// table's objects, so SetSceneInstance can fill them in any order. The
		// CPU path's own draws append after them, as they always did.
		static void ReserveSceneInstances(uint32_t count);

		// Fills one reserved row. `index` is the object's place in the cull
		// table -- Scene::DrawItem::CullIndex -- not its place in any draw
		// order, because the GPU decides that.
		//
		// Does what DrawMesh does to build an instance, and nothing else: no
		// pending draw is recorded, because there is no draw to record until
		// the cull says which rows survived.
		static void SetSceneInstance(uint32_t index, const Mat4& transform,
									 const Mat4& previousTransform,
									 const RHI::Ref<Material>& material,
									 const MaterialParams& params, uint32_t probe);

		// Every static mesh the camera kept, in as many draws as the scene has
		// distinct meshes, each one's instance count read out of the buffer
		// GpuCull::CullLit filled.
		//
		// Between BeginScene and EndScene like the other draw calls, and it
		// leaves the pending list alone -- so a scene can submit its static
		// meshes this way and its skinned ones, its layered ones and its
		// terrain the other.
		static void DrawSceneIndirect(const GpuCull::View& view,
									  const std::vector<GpuCull::Slot>& slots);

		// The same pose the lit pass was given. Without this a skinned figure
		// walks and its shadow stands still in the bind pose.
		static void DrawSkinnedMeshShadow(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
										  const std::vector<Mat4>& bones);
		static void EndShadow();

		// `probe` is which cube of the reflection-probe arrays this object
		// reflects, chosen by the scene against the object's own position. Slot
		// 0 is the sky, so it is the answer for an object no probe reaches --
		// not a sentinel, and not something the shader has to branch on.
		//
		// Passed per object rather than set as state before a run of draws,
		// because it lands in the instance stream: two objects that chose
		// differently are still one instanced draw, and making it state would
		// invite exactly the per-batch shape this replaced.
		// `params` is the material's scalars with the entity's overrides already
		// folded in. Passed rather than read from `material`, because the
		// material is *shared* -- writing an override into it would change
		// every other object using it, and reading from it would make overrides
		// impossible. The scalars ride in the instance stream, so this costs a
		// struct copy and no draw calls.
		static void DrawMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
							 const RHI::Ref<Material>& material,
							 const MaterialParams& params, uint32_t probe,
							 const Mat4* previousTransform = nullptr);
		// Where this instance was last frame, or null for "it did not move".
		//
		// A pointer with a null default rather than a required argument: every
		// caller that has no idea is telling the truth by saying nothing, and
		// the answer it gets -- zero velocity -- is the correct one for static
		// geometry, which is most of a scene. ENGINE-NOTES 7r.


		// A mesh a skeleton moves. `bones` is one matrix per bone, already
		// composed with the inverse binds -- ComposeSkinning produces exactly
		// this, and an empty list draws the bind pose.
		//
		// Separate from DrawMesh rather than an overload with a default,
		// because a skinned mesh must never reach the static pipeline: the
		// vertex layouts differ and the static one would read joint indices as
		// texture coordinates.
		static void DrawSkinnedMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
									const RHI::Ref<Material>& material,
									const MaterialParams& params,
									const std::vector<Mat4>& bones, uint32_t probe,
									const Mat4* previousTransform = nullptr,
									// Last frame's pose, for motion vectors. Null
									// stands the current pose in, which reports the
									// object's movement and none of the limb's --
									// what this did before there was a second pose
									// to give it.
									const std::vector<Mat4>* previousBones = nullptr);

		// A mesh whose surface is four materials in painted proportions -- a
		// terrain chunk (ENGINE-NOTES 7aq). Drawn by the third lit pipeline,
		// whose set 1 the layered material binds itself; the instance's scalars
		// are layer 0's, and its material record on the bindless path is
		// layer 0's too, which is what a traced reflection of it shades with.
		// `layered` must have had Refresh called this frame. `indexCount` is
		// how many of the mesh's indices to draw, from the first -- the whole
		// mesh, or the part before its skirts while the camera is under the
		// ground (7ap); clamped to the mesh's count.
		static void DrawLayeredMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
									const RHI::Ref<LayeredMaterial>& layered, uint32_t probe,
									uint32_t indexCount,
									const Mat4* previousTransform = nullptr);

		// Shared by every mesh that has no material of its own.
		static RHI::Ref<Material> GetDefaultMaterial();

		// How many meshes were skipped by frustum culling this frame, across
		// every pass. Reported so the saving is visible rather than assumed.
		static unsigned int GetCulledCount();
		static void CountCulled();

		// The most lights any one cluster ended up holding, and the number that
		// actually decides a fragment's cost. If it equals the scene's light
		// count, clustering did nothing and is pure overhead.
		static unsigned int GetMaxCellLoad();
		static unsigned int GetLightCount();

		static unsigned int GetDrawCallCount();
		static unsigned int GetTriangleCount();

		// The area emitters the traced bounce aims shadow rays at, and how
		// many of those carry a per-texel aiming table.
		//
		// Reported so a measurement can say whether the feature under test
		// engaged at all. The showroom that shipped produces two emitters and
		// *zero* aiming ones -- its lit fitting tiles its material, which the
		// affine-uv gate rejects -- so a benchmark run against it exercises
		// none of the texel path, and nothing in the report said so.
		static unsigned int GetAreaEmitterCount();
		static unsigned int GetAimedEmitterCount();

		// Whether the lit shaders trace the directional light's shadow instead
		// of sampling cascades (ENGINE-NOTES 7am). Switching recompiles the two
		// lit shaders with RV_RAY_SHADOWS and rebuilds their pipelines, which
		// the SPIR-V cache makes cheap after the first time; called by
		// Scene::RenderShadows once it has resolved the mode. A no-op on a
		// device that cannot trace, whatever it is asked.
		static void SetRayTracedShadows(bool enabled);
		static bool IsRayTracedShadows();

		// Ray-traced reflections (ENGINE-NOTES 7ao): the lit shaders compiled
		// with RV_RAY_REFLECTIONS trace the mirror ray from every glossy
		// pixel and shade the hit through the ray-instance table this
		// renderer writes at set 0 binding 15. Only ever on with the shadows
		// on and the heap available -- Scene::RenderShadows resolves that.
		static void SetRayTracedReflections(bool enabled);
		static bool IsRayTracedReflections();

		// The traced bounce (ENGINE-NOTES 7at): needs traced shadows for the
		// structure and bindless for the heap, exactly as reflections do, and
		// silently stays off without either. Recompiles the lit shaders.
		static void SetRayTracedGlobalIllumination(bool enabled);
		static bool IsRayTracedGlobalIllumination();

		// What the traced bounce needs to put a screen pixel back in the world:
		// the clips that turn a depth value into metres, the projection's two
		// scale terms, and the view matrix it inverts to reach world space.
		// The same description the ambient-occlusion pass carries, because it
		// is answering the same question.
		struct GiTraceView
		{
			float NearClip = 0.05f;
			float FarClip = 1000.0f;
			float InvProjection0 = 1.0f;
			float InvProjection1 = 1.0f;
			Mat4  View{ 1.0f };
		};

		// The traced bounce, over a whole target (ENGINE-NOTES 7bs).
		//
		// `depth` and `surface` are the scene pass's own attachments; `rays` is
		// how many cosine directions each pixel casts. The target's size is
		// whatever the caller bound, which is the entire reason this is a pass:
		// as part of the lit fragment it could only ever run at the frame's
		// resolution.
		//
		// Writes raw irradiance, albedo-free, exactly as the in-shader form
		// did -- so the denoise after it and the read next frame are unchanged.
		// Does nothing where the traced form is not compiled in.
		static void TraceGlobalIllumination(RHI::RHICommandList& cmd,
											const RHI::Ref<RHI::RHITexture>& depth,
											const RHI::Ref<RHI::RHITexture>& surface,
											RHI::Format targetColor,
											const GiTraceView& view, int rays);
		static bool CanTraceGlobalIllumination();

		// Whether the lit pass reads material textures through the bindless
		// heap this session (ENGINE-NOTES 7al): the device can, and
		// --bindless did not say no. Reported rather than assumed, because a
		// device that lacks the feature takes the bound path silently and a
		// pixel comparison between the two needs to know which it got.
		static bool IsBindless();
		// The heap itself, or null on the bound path: what a layered material
		// registers its maps in (7aq).
		static TextureHeap* GetTextureHeap();
		// How many heap slots are live and how many remain, for the stats
		// panel; both zero on the bound path.
		static unsigned int GetHeapLiveCount();
		static unsigned int GetHeapFreeCount();

	private:
		static void EnsurePipeline();
		static bool CompileLitShaders();
	};
}
