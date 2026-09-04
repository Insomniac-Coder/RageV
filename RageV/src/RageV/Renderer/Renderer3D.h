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

		// **How many irradiance volumes one scene may hold**, each with its
		// own grid and its own place in the shared atlas.
		//
		// Eight because the table lives in the scene block and costs five
		// rows a volume there, and because eight local boxes is already a
		// different order of coverage from the one composed box this
		// replaced -- a corridor scene wants four or five. Volumes past the
		// cap are dropped with a warning rather than silently merged, since
		// merging is exactly the behaviour this exists to end.
		static constexpr uint32_t kMaxIrradianceVolumes = 8;

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

		// **The scene's irradiance volumes**, however many it has. Null unbinds
		// them and every reader falls back to what it did before, which is the
		// flat ambient constant.
		//
		// The boxes come off the volume's own region list rather than being
		// passed: they were one composed box until 2026-08-27, and a caller
		// that still describes "the" box is a caller working from the model
		// this replaced. Each region keeps its own rotation, so a volume that
		// was turned is still a box rather than the axis-aligned one its
		// bounds would suggest.
		// `regionsOverride`, when given, replaces the boxes the volume itself
		// carries. The runtime cache needs it: its box slides with the view
		// every few frames, and the volume's own region list is fixed at
		// creation -- so the field that moves keeps its texture, and only the
		// box travelling with it changes. Null means use the volume's own.
		static void SetIrradianceVolumes(const RHI::Ref<IrradianceVolume>& volume,
										 const std::vector<IrradianceVolume::Region>*
											 regionsOverride = nullptr);

		// **Solves a field**: one traced gather per cell, written straight into
		// the volume's textures. Runs when a field is dirty and not otherwise,
		// which is a scene loading and then only what invalidates one.
		//
		// Returns false when it could not run -- no rays on this device, the
		// shader did not compile -- and a caller that gets false leaves the
		// field holding whatever it had, which is the flat ambient a fresh one
		// is created with.
		// `rowBegin`/`rowCount` are the band of the unrolled grid this call
		// solves -- the whole of it, or a slice, which is what keeps a large
		// field from arriving as one frame's hitch. `blend` is how much of
		// this pass to keep against what the field already holds: one to
		// replace, less to converge. `feedback` is whether this pass's rays
		// read the previous sweep's field at their hits -- the traced flavour's
		// bounces -- or shade against the probe alone, the screen flavour's
		// single bounce.
		static bool SolveIrradianceVolume(RHI::RHICommandList& cmd,
										  const RHI::Ref<IrradianceVolume>& volume,
										  const IrradianceVolume::Region& region,
										  int rays, float reach,
										  uint32_t rowBegin, uint32_t rowCount,
										  float blend, bool feedback);

		// **Asks for a field to be solved**, rather than solving it here. The
		// caller is the scene walk, which runs before BeginScene because the
		// block carrying the box's bounds is uploaded there -- and the solve
		// traces, so nothing it needs exists that early. The frame graph's fill
		// pass runs the request once the scene pass has built what it traces
		// against.
		//
		// Asking twice for the same field is asking once: there is one request,
		// and it stands until a pass solves it.
		//
		// `passes` and `raysPerCell` are the solve's quality, and both are paid
		// once in the baker. With `feedback` true -- the traced flavour --
		// every pass past the first is a bounce: its rays read the previous
		// sweep's completed field at their hits, and the sweeps converge on
		// the multi-bounce answer. With it false -- the screen flavour -- the
		// passes average a single bounce, matching what the gather and the
		// voxel form estimate. Either way the passes also buy the mottling
		// out. They come from the volume components rather than from constants
		// here, because the right answer is a property of the room.
		// The boxes ride on the volume itself now -- one request covers every
		// region it holds, because the sweeps have to advance across all of
		// them together (see SolvePendingIrradiance).
		static void RequestIrradianceSolve(const RHI::Ref<IrradianceVolume>& volume,
										   uint32_t passes, uint32_t raysPerCell,
										   bool feedback);

		// **The same solve, asked to run forever: a runtime radiance cache.**
		//
		// Where RequestIrradianceSolve converges a field and stops, this keeps
		// sweeping at a fixed ray cost a frame and blends each revisit into
		// what is stored, so the stored light tracks a scene whose lights move.
		//
		// **What this does not do yet, and the reason it was built.** The point
		// of a cache is that the frame's indirect diffuse becomes a *lookup*
		// rather than a per-pixel trace, so its ray count stops scaling with
		// screen resolution. That step is NOT taken: the RT GI pass still
		// traces per pixel exactly as before, and this runs alongside it. So
		// today the cache buys multi-bounce -- a traced hit reads stored light
		// instead of falling back to a probe -- at a small net *cost*, not a
		// saving. Making the GI pass sample the field instead of tracing, in
		// whole or in part, is what turns that round.
		//
		// `rayBudget` is rays a frame across the whole atlas. `hysteresis` is
		// how much of each fresh estimate a revisited cell takes: low is still,
		// high is responsive. Asking again with the same volume keeps the sweep
		// where it is rather than restarting it, so this may be called every
		// frame.
		// `region` is the box as it stands this frame, which for a following
		// cache is not the one the volume was built with. The solve reads it
		// instead of the volume's own list, so the grid can travel without
		// being rebuilt -- and without the volume needing to know it moved.
		static void RequestRuntimeIrradiance(const RHI::Ref<IrradianceVolume>& volume,
											 const IrradianceVolume::Region& region,
											 uint32_t raysPerCell, uint32_t rayBudget,
											 float hysteresis, bool feedback);

		// Whether the standing request is the continuous kind. The frame graph
		// asks so it can declare the fill pass every frame rather than once.
		static bool HasRuntimeIrradiance();

		// **Withdraws a standing continuous request.**
		//
		// Releasing the field is not enough on its own: the request holds a
		// reference of its own, so the volume stays alive and the solve keeps
		// sweeping it forever -- a fill pass in every frame of a mode that
		// cannot read what it writes. Called when the mode stops wanting a
		// cache. Leaves a *bake* request alone; only the continuous kind is
		// withdrawn.
		static void CancelRuntimeIrradiance();

		// **Whether the cache has visited every cell at least once.**
		//
		// A freshly created runtime field is black, and a room that fades up
		// from black is worse than anything this buys. The scene asks so it can
		// keep the baked field bound over the handover and release it only once
		// this says yes. False when no continuous request is standing.
		static bool RuntimeIrradianceWarm();

		// Whether a request is standing, asked by the frame graph before it
		// declares a fill pass -- so a frame with no field to solve, which is
		// almost every frame, has no pass at all.
		static bool HasPendingIrradianceSolve();

		// Runs the standing request if it can run yet, and clears it if it did.
		//
		// **Must be recorded outside a render pass**: it opens one of its own,
		// and it records the barriers fencing the volume's writes against the
		// draws that read them. That is what RGPassKind::Standalone is for.
		static bool SolvePendingIrradiance(RHI::RHICommandList& cmd);

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
		// WR-16 S4b: the water runs of the same list, drawn earlier into the
		// two attachments that describe the sea's surface, so the pass that
		// chooses and shades its lamps has something to read. Leaves the list
		// standing for FlushTransparent.
		static void FlushWaterSurface();

		// Resets the per-frame scene-slot pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// **The per-pixel debug counts** (`--debug-view=rays|lights`, WR-16
		// S0). When the lit shaders were compiled with RV_DEBUG_VIEW they add
		// each pixel's ray count and light count into this buffer: a 16-byte
		// header (width, height, pixels, 0) and two planes of one word per
		// pixel, rays then lights. The frame graph sizes it to the scene
		// target before the scene pass, zeroes the planes with a fill, and
		// reads it back in the debug composite after everything else. Null
		// when the view is off; sized 1x1 until the first frame.
		static void EnsureDebugCounts(uint32_t width, uint32_t height);
		static const RHI::Ref<RHI::RHIBuffer>& DebugCountsBuffer();
		static bool DebugCountsSize(uint32_t& width, uint32_t& height);
		// Bytes past the header: what a fill zeroes.
		static constexpr uint64_t kDebugCountsHeaderBytes = 16;

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
		// `masked` is the caster's material, and only an alpha-tested one is
		// used: it routes the draw to the pipeline that tests the alpha, so a
		// cutout casts a cutout's shadow rather than its sheet's. Anything
		// else -- opaque, blended, absent -- casts through the position-only
		// pipeline as before, which is almost every caster in a frame.
		static void DrawMeshShadow(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
								   const RHI::Ref<Material>& masked = nullptr);

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
									 const MaterialParams& params, uint32_t probe,
									 bool isStatic);

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
		// `isStatic` is MeshComponent::Static as the scene resolved it (7cx).
		// It rides in the instance stream too (InstanceData.Indices.w), for
		// the same reason the probe does: two objects that differ in it are
		// still one instanced draw, and the fragment reads it per pixel to
		// decide whether the fully baked lights are in the field for it.
		static void DrawMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
							 const RHI::Ref<Material>& material,
							 const MaterialParams& params, uint32_t probe, bool isStatic,
							 const Mat4* previousTransform = nullptr);

		// A body of water: the same draw, through the pipeline whose vertex
		// stage displaces the grid into waves.
		//
		// **Packed four-wide, in the order the shader reads them**, rather than
		// named one dial per field. This struct is copied straight into the
		// push constant and has to agree with ObjectData in scene_block.glsl
		// lane for lane; the closer the two look, the harder it is for them to
		// drift apart unnoticed -- and a drift here shows up as one dial moving
		// the wrong thing, which reads as a shader bug rather than a layout one.
		struct WaterDraw
		{
			// rgb = the colour where the bottom is close, a = metres of depth
			// it takes to reach the deep colour.
			Vec4 Shallow{ 0.06f, 0.19f, 0.22f, 12.0f };
			// rgb = the open-water colour, a = seconds since the scene started.
			Vec4 Deep{ 0.012f, 0.031f, 0.055f, 0.0f };
			// crest-to-trough metres, metres between crests, choppiness,
			// metres a second.
			Vec4 Wave{ 0.6f, 24.0f, 0.55f, 1.6f };
			// direction in radians, foam, the previous frame's time, spare.
			Vec4 Extra{ 0.785f, 0.45f, 0.0f, 0.0f };
			// xy = the body's rectangle in metres -- what turns a local
			// position into the foam buffer's coordinate. z and w are the
			// renderer's to fill, not the scene's: z carries the backdrop
			// flag and the backend's NDC sign (see water_params.glsl).
			Vec4 Size{ 200.0f, 200.0f, 0.0f, 0.0f };

			// This body's foam accumulation buffer -- the readable half of
			// the ping-pong pair after Water::UpdateFoam ran. Null draws with
			// the shared black stand-in, which is a calm sea rather than a
			// broken one.
			RHI::Ref<RHI::RHITexture> Foam;
		};

		static void DrawWaterMesh(const RHI::Ref<Mesh>& mesh, const Mat4& transform,
								  const RHI::Ref<Material>& material,
								  const MaterialParams& params, uint32_t probe,
								  const WaterDraw& water,
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
									bool isStatic, uint32_t indexCount,
									const Mat4* previousTransform = nullptr);

		// Shared by every mesh that has no material of its own.
		static RHI::Ref<Material> GetDefaultMaterial();

		// How many meshes were skipped by frustum culling this frame, across
		// every pass. Reported so the saving is visible rather than assumed.
		//
		// **On the CPU only.** Everything in a GPU-driven table is rejected by
		// a compute pass whose survivor count stays in device memory, so this
		// counts what the CPU walk rejected and nothing else. A frame drawn
		// entirely through the indirect path honestly reports zero, which is
		// why the panel says which kind of culling it is reporting -- read as
		// a bare "Culled: 0" it looks like culling stopped working.
		static unsigned int GetCulledCount();
		static void CountCulled();

		// The most lights any one cluster ended up holding, and the number that
		// actually decides a fragment's cost. If it equals the scene's light
		// count, clustering did nothing and is pure overhead.
		static unsigned int GetMaxCellLoad();
		static unsigned int GetLightCount();

		static unsigned int GetDrawCallCount();

		// Triangles *submitted* this frame, over every pass that counted a
		// draw. Exact for a classic draw; for an indirect one it is the slot's
		// reserved length, because what survived the cull is a number only the
		// GPU has. GetIndirectDrawCount says how much of the frame that
		// applies to.
		static unsigned int GetTriangleCount();
		static unsigned int GetIndirectDrawCount();

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

		// The sky term's visibility, traced per pixel instead of read from a
		// baked volume. Rides on the shadows' structure.
		// `rays` is the dial as a count: 0 off, 2/4/8 for Quarter/Half/Full.
		static void SetRayTracedSkyVisibility(int rays);
		static int RayTracedSkyVisibilityRays();

		// The traced bounce (ENGINE-NOTES 7at): needs traced shadows for the
		// structure and bindless for the heap, exactly as reflections do, and
		// silently stays off without either. Recompiles the lit shaders.
		static void SetRayTracedGlobalIllumination(bool enabled);

		// Ray-traced water refraction: the water shader compiled with
		// RV_RAY_REFRACTION bends the view ray at the surface and traces it
		// into the scene instead of sampling the backdrop copy. The same two
		// prerequisites reflections have -- the shadows' structure and the
		// bindless heap -- and silently stays off without either. Recompiles
		// the transparent pair (the plain transparent shader takes the define
		// too, dead code there, so the two set-0 layouts cannot diverge and
		// the one resource set keeps serving both).
		static void SetRayTracedWaterRefraction(bool enabled);
		static bool IsRayTracedWaterRefraction();

		// What the water samples for the glassy see-through this frame: the
		// opaque scene's colour and its view depth in metres, produced by the
		// backdrop pass between the opaque passes and the transparent one.
		// Set by the frame graph around the transparent pass and cleared
		// after it; null means the pass did not run and the water falls back
		// to plain blending.
		static void SetWaterBackdrop(const RHI::Ref<RHI::RHITexture>& color,
									 const RHI::Ref<RHI::RHITexture>& depth);

		// **Whether indirect light is read rather than computed.**
		//
		// True stops the frame graph adding the gather or the traced bounce at
		// all: the field answers alone, which is the whole saving -- measured
		// at 0.83 ms against 1.62 on the GI corner, and 4.07 against 7.45 on
		// the showroom. Only set where a bake actually exists to read; the
		// scene checks that and says so when it does not.
		static void SetBakedIrradianceOnly(bool enabled);
		static bool IsBakedIrradianceOnly();

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
											// The ray budget's map, or null for a fixed count.
											const RHI::Ref<RHI::RHITexture>& budget,
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
		// The blended list, drawn either as the transparent pass or as the
		// water surface pass before it (WR-16 S4b).
		static void FlushBlended(bool surfaceOnly);
	};
}
