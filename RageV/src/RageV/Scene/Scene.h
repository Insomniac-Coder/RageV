#pragma once
#include <filesystem>
#include "ECS.h"
#include "RageV/Core/Timestep.h"
#include "RageV/Core/UUID.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Renderer/Environment.h"
#include "RageV/Renderer/GpuCull.h"
#include "RageV/Renderer/IrradianceVolume.h"
#include "RageV/Renderer/RuntimeIrradianceField.h"
#include "RageV/Renderer/Light.h"
#include "RageV/Renderer/PostSettings.h"
#include "RageV/Renderer/ViewportGrid.h"
#include "RageV/Renderer/RHI/RHIResources.h"
#include "RageV/Renderer/Terrain.h"
#include "RageV/Math/Math.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace RageV
{
	class Entity;
	class Camera;
	class EditorCamera;
	struct TransformComponent;
	struct MeshComponent;
	struct TerrainComponent;
	// Only ever held as a pointer here; the editor is what fills one in.
	struct EditorIconSettings;

	class Scene
	{
	public:
		Scene();
		~Scene();

		void OnViewportResize(float width, float height);

		Entity CreateEntity(const std::string& name = std::string());
		// Preserves an existing identity. The deserializer needs this: creating
		// entities with fresh IDs would break every reference in the file.
		Entity CreateEntityWithUUID(UUID id, const std::string& name = std::string());

		// By value: an Entity is a handle pair, and taking it by reference meant
		// callers could not pass a temporary. Children are destroyed with it.
		void DeleteEntity(Entity entity);

		Entity GetEntityByUUID(UUID id);

		// First match, or an invalid entity. Names are not unique -- they are a
		// label, not an identity -- so FindEntitiesByName exists for the cases
		// where that matters.
		// **Where this scene was loaded from**, which is what its baked
		// lighting is filed under. Set by the serializer; empty for a scene
		// built in memory, which is exactly the scene that has nothing to bake
		// to and no name to bake under.
		void SetSourcePath(const std::string& path) { m_SourcePath = path; }
		const std::string& GetSourcePath() const { return m_SourcePath; }

		Entity FindEntityByName(const std::string& name);
		std::vector<Entity> FindEntitiesByName(const std::string& name);

		// Queued rather than immediate. A script destroying an entity while the
		// script pass is walking them would invalidate that iteration, and a
		// script destroying itself would delete the object it is executing in.
		void DestroyDeferred(Entity entity);
		void FlushDestroyQueue();
		bool HasEntity(UUID id) const { return m_EntityMap.find(id) != m_EntityMap.end(); }

		// --- hierarchy ------------------------------------------------------
		// Passing an invalid parent unparents. The child's world transform is
		// preserved across the move, which is what makes reparenting in the
		// hierarchy panel feel non-destructive.
		//
		// Returns false and changes nothing if the move would form a cycle.
		// This is the only place a cycle can be created, so rejecting it here
		// is what lets the transform pass recurse without a depth guard.
		bool SetParent(Entity child, Entity parent);
		bool IsDescendantOf(Entity entity, Entity possibleAncestor);

		Entity GetParent(Entity entity);
		const std::vector<UUID>& GetChildren(Entity entity);

		// Identity when the entity has no parent.
		Mat4 GetParentWorldTransform(Entity entity);
		Mat4 GetWorldTransform(Entity entity);

		// One top-down pass writing TransformComponent::World. Called by the
		// render entry points; call it directly after mutating transforms if a
		// world value is needed before the next frame.
		// Whether anything in the draw list wears a blended material.
		//
		// **Asked of the scene rather than of the renderer**, and for the
		// reason the particles' own version already gives: the frame graph is
		// described before anything draws, so a renderer would be answering
		// for last frame. Answered from the draw-list refresh, which resolves
		// every material anyway to decide what may go in the GPU cull table.
		bool HasBlendedMeshes() const { return m_HasBlended; }

		void UpdateWorldTransforms();

		// --- the draw list ----------------------------------------------------
		//
		// **Everything drawable, resolved once a frame and tested by every
		// view.** A frame draws the scene five times over -- once for the
		// camera and once per shadow cascade -- and each of those used to be a
		// full walk: a view, a hash lookup for the mesh, a second for
		// the material, a bounds transform and a frustum test, per object per
		// view. Measured at twenty thousand objects that is 100,004 tests and
		// 14.5 ms a frame against 0.78 ms of GPU work, and the same scene with
		// the camera pointed at the sky -- nothing drawn at all -- still cost
		// 8.26 ms.
		//
		// The lookups and the bounds do not depend on which view is asking, so
		// they are done once. What is left per view is a frustum test over a
		// flat array, which is the cheapest thing in the frame.
		//
		// **Split in two, and that is the point rather than a tidy-up.** The
		// cull touches `DrawBounds` alone -- 24 bytes an object, sequential, no
		// pointer chased -- and only what survives reads the item beside it.
		// One array of everything would stream two hundred bytes an object
		// through the test to look at twenty-four of them.
		struct DrawBounds
		{
			Vec3 Centre{ 0.0f };
			Vec3 Extents{ 0.0f };
		};

		// Parallel to DrawBounds by index.
		//
		// The component pointers are borrowed for the frame, not owned. That
		// is safe for exactly the reason the code it replaces was already
		// safe: rendering adds and removes no components, and the previous
		// walk already handed `&transform.PreviousWorld` to the renderer. It
		// is not safe across a frame, so nothing here outlives RefreshDrawList.
		//
		// `ResolveParams` is deliberately *not* precomputed. It is cheap, it is
		// 80 bytes an object to store, and it is only needed for what survives
		// -- so it stays where the survivor is handled and the array stays 64
		// bytes wide.
		struct DrawItem
		{
			ECS::Entity Entity = ECS::Null;
			TransformComponent* Transform = nullptr;
			MeshComponent* Source = nullptr;
			RHI::Ref<Mesh> Resolved;
			bool Skinned = false;
			// Which mesh slot this object went into in the GPU cull table, or
			// kNoCullSlot when it went into none -- a skinned mesh, or any
			// object at all when the cull passes are unavailable.
			//
			// A depth view reads this to know what it must *not* draw: the
			// static casters were already submitted by an indirect draw, and
			// drawing them again would double every shadow's depth writes for
			// no visible change and twice the cost.
			uint32_t CullSlot = kNoCullSlot;
			// And this object's row in the cull table, which is the index the
			// camera's cull pass hands back as a survivor. Meaningless when
			// CullSlot is kNoCullSlot.
			uint32_t CullIndex = 0;

			// The same pair against the *blended* table, for a static mesh
			// wearing a material that is not opaque. An entry is in one table
			// or the other and never both, so these two and the two above are
			// exclusive -- which is what lets the draw loop tell which pass
			// already accounted for it.
			uint32_t BlendSlot = kNoCullSlot;
			uint32_t BlendIndex = 0;

			// Whether its material is blended at all, which the two above only
			// answer when there is a cull pass to have put it in a table.
			// **Shadow casting reads this**: glass does not cast one.
			bool Blended = false;
			// Alpha-tested. Kept apart from Blended because the two want
			// opposite answers nearly everywhere: a cutout is opaque where it
			// survives, so it writes depth and sorts with the opaque geometry,
			// where glass does neither.
			bool Masked = false;
		};

		static constexpr uint32_t kNoCullSlot = ~0u;

		// Rebuilt from the registry. Call after UpdateWorldTransforms and
		// before anything reads the list; both render entry points do.
		void RefreshDrawList();

		const std::vector<DrawBounds>& GetDrawBounds() const { return m_DrawBounds; }
		const std::vector<DrawItem>& GetDrawItems() const { return m_DrawItems; }
		// The shadow maps half of RenderShadows: cascades and local maps, or
		// the ray structures under traced shadows.
		void RenderShadowMaps(const Camera& camera, const Mat4& cameraTransform);
		// Every light in the scene as the renderer reads it -- what
		// BeginScene and the voxel injection are both handed (7bc).
		LightList CollectLights();

		// Advances every animator and rebuilds its pose.
		//
		// `editing` says the scene is being edited rather than played, and in
		// that state only animators with `RunInEditor` advance; the rest have
		// their pose cleared, which draws them at their bind pose. Animation
		// is something a running game does, so the editor's default is still.
		//
		// Public because both update paths call it and neither is the other's
		// business. On the frame rather than the fixed step: an animation is
		// presentation, like the audio positions and the transform blend.
		void UpdateAnimators(Timestep ts, bool editing = false);

		// --- frame ----------------------------------------------------------
		// Creates the physics world and every body in it. Called on Play.
		void OnRuntimeStart();
		// Tears it down. Called on Stop, before the scene is restored -- every
		// body refers to entities that are about to be replaced.
		void OnRuntimeStop();

		// Freezes a running scene without leaving play mode.
		//
		// Stopping the fixed step is not enough, and the editor's pause used to
		// do only that: the physics blend runs on the *frame*, and the
		// interpolation alpha keeps moving while the two states it blends are
		// frozen -- so a body paused mid-fall was re-blended to a different
		// point along its last step every frame, which reads as jittering in
		// place. Paused therefore also holds everything the frame advances:
		// the physics blend, the animators, the frame scripts and the
		// particles. World transforms still derive and audio positions still
		// follow, so a paused scene can be inspected and edited and what is on
		// screen stays truthful.
		void SetPaused(bool paused) { m_Paused = paused; }
		bool IsPaused() const { return m_Paused; }

		// Null outside play mode.
		// The managed half of the fixed-step script pass, and its teardown.
		void StepManagedScripts(Timestep dt);
		void ReleaseManagedScripts();

		// Calls a method by name on whichever scripts an entity carries.
		//
		// **Both languages, both delivered** -- the same rule DeliverContact
		// follows, and for the same reason: an entity may hold a C++ script and
		// a C# one, and a call addressed to the entity belongs to both.
		//
		// The name comes from data (a button's OnClick binding), so failing to
		// resolve it is an ordinary outcome rather than a bug here. It answers
		// false and says nothing; the caller knows what the binding was and is
		// the only one able to write a message worth reading.
		bool InvokeScriptMethod(Entity entity, const std::string& method);

		// Whether that call *would* find something, without making it.
		//
		// Answered from the script **names** the entity carries rather than
		// from live instances, because the callers run with the scene stopped:
		// a load-time check, and the packager. InvokeScriptMethod cannot work
		// that way -- it has to have an object to call on -- so the two
		// resolve the same question through different doors, and the door this
		// one uses is `ScriptName` where the other uses `ActiveScript`. They
		// agree from the first simulation step onwards, which is the earliest
		// anything could be invoked.
		bool CanInvokeScriptMethod(Entity entity, const std::string& method);

		// The bound half of a UI click. Runs on the fixed step, after the
		// script pass -- instances are created there, and a handler on an
		// entity spawned this same step has to exist before it can be called.
		void DispatchUIClicks();

		// The per-frame script pass, both languages. Returns whether it stepped
		// anything, so a scene with no scripts in it does not pay for the
		// second hierarchy walk a script that moved something needs.
		//
		// It never creates or reconciles an instance -- that happens in exactly
		// one place, the fixed pass -- so a script whose OnCreate has not run
		// yet simply is not here.
		bool StepFrameScripts(Timestep ts);

		Physics::World* GetPhysics() { return m_Physics.get(); }

		// Per rendered frame while playing. Presentational work only, plus
		// pulling simulated transforms across at the frame's blend factor.
		void OnUpdateRuntime(Timestep ts);

		// Per fixed simulation step while playing. Scripts and, later, physics.
		// Nothing here may depend on the frame rate.
		void OnFixedUpdateRuntime(Timestep dt);

		// Per frame while editing. Deliberately empty of simulation: scripts
		// used to run in the editor, which meant a script could modify a scene
		// nobody had pressed Play on -- and those edits were then saved.
		void OnUpdateEditor(Timestep ts);

		// Draws through the primary camera entity. Draws nothing without one.
		//
		// The aspect ratio is per pass, not per scene. A camera carries one
		// projection but may be drawn into two panels of different shapes -- the
		// game view and a preview in the scene view -- and a single stored
		// aspect can only be right for one of them. It is also why a camera's
		// aspect is not serialized: it belongs to the surface being drawn into,
		// not to the scene.
		void OnRenderRuntime(float aspectRatio = 0.0f);
		// Draws through the viewport's own camera, which needs no entity.
		//
		// `grid` draws the editor's ground plane after the sky. Passed in rather
		// than read from a setting, and only here: the game view and the runtime
		// both go through OnRenderRuntime, which has nowhere to put one, so a
		// grid cannot reach a picture that is meant to be what a player sees.
		//
		// `icons` is the same argument again, for the gizmo marks on entities
		// with no geometry (7.4) -- and the same structural guarantee, which
		// is why it is a second pointer rather than a flag on the first.
		void OnRenderEditor(const EditorCamera& camera,
							const ViewportGridSettings* grid = nullptr,
							const EditorIconSettings* icons = nullptr);

		// Convolves the scene's environment map into roughness levels, once.
		//
		// Like the shadow and probe passes, it opens render passes of its own
		// and so belongs before the frame graph. Unlike them it does nothing
		// after the first frame with a given environment.
		// Copy every transform's world matrix into its previous one. Call
		// exactly once per frame, before anything recomputes them: this is
		// what makes a motion vector the difference between two frames rather
		// than between two calls. ENGINE-NOTES 7r.
		void AdvanceMotionHistory();

		void PrepareEnvironment();

		// Renders the shadow cascades for the first directional light that casts.
		//
		// Takes the camera it is about to be rendered with, because cascades are
		// fitted to a frustum: shadows for a camera other than the one looking
		// at them would be the wrong size in the wrong place. Like the probe
		// capture, it opens render passes and so must run before the graph.
		void RenderShadows(const Camera& camera, const Mat4& cameraTransform);

		// Re-renders whatever the scene's reflection probes can see.
		//
		// Called by the application between BeginFrame and the frame graph,
		// because a capture opens render passes of its own and nothing may do
		// that inside another one. It is not folded into OnRender for the same
		// reason: OnRender runs *inside* the scene pass.
		void CaptureReflectionProbes();

		Entity GetPrimaryCameraEntity();

		// Lowest ListenerRank wins. Falls back to the primary camera when the
		// scene has no AudioListenerComponent at all, which is the case almost
		// every time and is what makes audio work without being set up.
		Entity GetPrimaryListenerEntity();

		SceneEnvironment& GetEnvironment() { return m_Environment; }
		const SceneEnvironment& GetEnvironment() const { return m_Environment; }

		// How the frame is graded: the profile attached to this scene's
		// primary camera, or the neutral grade when it has none.
		//
		// One function, three callers -- the editor's viewport, the editor's
		// game view and the runtime -- because a grade that differs between
		// the panel you author in and the panel you check in is a grade you
		// cannot author. The editor's *viewport* uses the primary camera's
		// profile rather than a setting of its own for the same reason.
		//
		// Never fails: an unset, unknown or unreadable handle all answer the
		// defaults, which is what every scene rendered with before profiles
		// existed. ENGINE-NOTES 7s.
		PostSettings GetPostSettings();

		// The world bounds of everything drawable under `root`, the root
		// included. False when there is nothing drawable there at all, which is
		// the honest answer for an empty entity and is not an error -- a focus
		// target that names one is simply a target with no size, and the caller
		// leaves the manual values alone rather than solving from zero.
		//
		// Walked from the components rather than read out of `m_DrawBounds`,
		// deliberately: the draw list is rebuilt inside the render entry points
		// and this is called by the *layer* that fills the frame description,
		// which happens first. Reading it here would use last frame's list, and
		// on the first frame there is no list at all.
		bool GetSubtreeBounds(Entity root, Vec3& centre, Vec3& extents);

		// Target-mode depth of field: the focus distance and the aperture,
		// solved from the subject and written into the caller's copy of the
		// profile. A no-op unless depth of field is on and the mode is Target.
		//
		// **Public because it is arithmetic, and arithmetic is the part worth
		// testing.** GetPostSettings calls it and nothing else has to; it is
		// here so a check can hand it a camera, a subject and a known distance
		// and assert the f-number, rather than looking at a render and
		// agreeing that it seems sharper.
		void ResolveFocus(PostSettings& settings, Entity camera);

		// The primary camera's near and far planes, as (near, far).
		//
		// Resolved the same way the post settings are, and for the same
		// caller: the frame graph needs real distances to turn a depth buffer
		// value back into metres, and the buffer holds a non-linear 0..1.
		// Falls back to a sane pair when the scene names no camera, because a
		// scene without one still renders through the editor's. ENGINE-NOTES 7z.
		Vec2 GetCameraClipPlanes();

		// Inverses of the primary camera's projection diagonal, for the frame
		// chain's position reconstruction. (1, 1) when there is no camera.
		Vec2 GetCameraProjectionInverse();

		// The primary camera's view matrix -- the inverse of its world transform.
		// Identity when there is no camera. SSR needs its rotation.
		Mat4 GetCameraView();

		// For tools and tests that need to iterate arbitrary component sets.
		// The editor panels reach it through friendship instead; this exists so
		// that code outside the engine does not have to.
		ECS::Registry& GetRegistry() { return m_Registry; }

		// Which cube of the probe arrays an object at `position` reflects: the
		// nearest probe whose influence reaches it, and slot 0 -- the sky --
		// when none does.
		//
		// Public because it is the part of 7.7 worth checking without looking
		// at a picture. Every way of getting this wrong produces a plausible
		// reflection: the nearest probe and the second nearest usually contain
		// much the same room, so a scene can select entirely the wrong probe
		// for every object in it and still look approximately correct.
		uint32_t ProbeSlotFor(const Vec3& position) const;

		// Every terrain that has something to draw (ENGINE-NOTES 7ap): the
		// entity, its world transform, its component and the terrain built from
		// its asset -- resolved on first use; an entity whose asset is missing
		// is skipped.
		void ForEachTerrain(const std::function<void(Entity, TransformComponent&,
												 TerrainComponent&, Terrain&)>& fn);

		// Every terrain chunk at the level SelectLod last chose: the same, and
		// the chunk. The one walk the scene pass, the shadow casters, the
		// ray-instance list and the picker share, so none of them can disagree
		// about which level a chunk is at.
		void ForEachTerrainChunk(const std::function<void(Entity, TransformComponent&,
													  TerrainComponent&, Terrain&,
													  Terrain::Chunk&)>& fn);

		// Once per frame, before anything reads a chunk: chooses every terrain
		// chunk's level for a camera at `cameraPosition`, and refreshes each
		// terrain's layered material from its component's four handles (7aq).
		// RenderShadows calls it first thing, so the level the shadows are
		// rendered from is the level the frame draws, and the layers are the
		// frame's before the draw asks for them.
		void PrepareTerrains(const Vec3& cameraPosition);

		// True when at least one terrain has something to draw.
		bool HasTerrain();

		// The voxel grid for this frame (8.1, ENGINE-NOTES 7bc): every static
		// mesh and terrain chunk inside the outermost cascade, submitted to
		// VoxelGI, which voxelises, lights and mips them. Called by
		// RenderShadows after the maps, because the grid is lit from them;
		// runs only where the project resolves to the voxel form and rays do
		// not win, and tells VoxelGI nothing was lit otherwise.
		void UpdateVoxelGI(const Mat4& cameraTransform);

		// The height of the ground under a world point (ENGINE-NOTES 7au).
		//
		// **False means there is no terrain there**, and that is the reason
		// this returns a bool at all: `Terrain::HeightAt` clamps to its own
		// extent, so forwarding it unchanged would answer a point a kilometre
		// past the edge with the rim's height and no hedging. `height` is left
		// at zero on a miss, so a caller that ignores the bool gets an obvious
		// wrong answer rather than a plausible one.
		//
		// Takes a point, not an (x, z) pair, so it is defined under every
		// transform: the point is inverse-transformed into terrain space, its
		// x and z are asked of the heightfield, and the answer comes back
		// through the same matrix. Under the transform a terrain actually has
		// -- translated, yawed, uniformly scaled -- the input's y falls out of
		// the arithmetic and every point on a vertical line gives the same
		// answer. Under one tilted about x or z it does not, and what comes
		// back is the surface along the terrain's own up axis.
		//
		// Where several terrains cover the point, **the highest surface
		// wins**: the answer a body dropped from above would get, and the same
		// answer whatever order the registry walks.
		bool TerrainHeightAt(const Vec3& worldPosition, float& height);
		// The same, on one named terrain -- for a scene where the rule above
		// picks the wrong one, and for a caller that would rather not walk.
		// False for an entity with no TerrainComponent, and for a point off
		// that terrain's extent even when another terrain covers it.
		bool TerrainHeightAt(Entity terrainEntity, const Vec3& worldPosition, float& height);

	private:
		// `viewCamera` is the camera as the viewer set it. What the scene is
		// actually drawn through is that projection plus this frame's temporal
		// jitter, which is applied inside -- see the note there.
		void OnRender(const Camera& viewCamera, const Mat4& cameraTransform,
					  const ViewportGridSettings* grid = nullptr,
					  const EditorIconSettings* icons = nullptr);

		// The sky cube for this frame, whether it came from an asset or from
		// the gradient. Resolved in one place because three callers want it and
		// two of them used to derive it themselves.
		RHI::Ref<RHI::RHITexture> ResolveSky() const;

		// The face size the probe arrays should hold: the largest of the sky's
		// resolution and every probe's.
		uint32_t ProbeFaceSize() const;

		// How many reflection probes the scene has, for sizing the arrays.
		uint32_t ProbeCount();

		// Convolves the sky and every complete probe into their array slices,
		// and records which slice each probe went to.
		// Sizes, fills and hands over the scene's irradiance field. See
		// IrradianceVolumeComponent.
		void UpdateIrradianceVolumes(const LightList& lights,
									 const Vec3& cameraPosition);
		// Places and sizes the camera-following field, creating it when the mode
		// calls for one and releasing it when it does not. Placement only -- the
		// solve is asked for by the caller and run by the frame graph.
		void UpdateRuntimeIrradianceField(const Vec3& cameraPosition);

		std::string m_SourcePath;

		// **The scene's one irradiance field**, composed from every volume
		// component in it -- see UpdateIrradianceVolumes for why composition
		// happens here rather than per fragment in the shader. The state beside
		// it is what the field was built for, and what a change in any of it
		// invalidates.
		RHI::Ref<IrradianceVolume> m_Field;
		Vec3 m_FieldCentre{ 0.0f };
		Vec3 m_FieldExtents{ 1.0f };
		Mat3 m_FieldRotation{ 1.0f };
		float m_FieldSpacing = 0.0f;
		// The solve quality the volumes asked for, kept beside the shape for
		// the same reason: a change to either makes the stored answer wrong.
		int m_FieldPasses = 0;
		int m_FieldRays = 0;
		uint64_t m_FieldLighting = 0;
		uint32_t m_FieldVolumes = 0;
		// **The arrangement of every volume, hashed.** The atlas's dimensions
		// cannot stand in for it -- one texture of a given size holds any
		// number of layouts -- so this is what notices a volume moving, or two
		// of them swapping order. Stamped into the bake beside the lighting.
		uint64_t m_FieldLayout = 0;
		// Said once per scene, not once a frame.
		bool m_WarnedVolumeCap = false;
		bool m_FieldBaked = false;

		// The box the last walk derived for an AutoFit volume, held so the
		// editor's overlay can draw the box the bake actually uses rather
		// than re-deriving one that might disagree. Valid only while some
		// volume in the scene has AutoFit on.
		Vec3 m_AutoFitCentre{ 0.0f };
		Vec3 m_AutoFitExtents{ 1.0f };
		bool m_AutoFitValid = false;

		// **A bake is a pair of files, one per flavour of GI it stands in
		// for.** The traced flavour is solved with feedback -- passes become
		// bounces -- so it matches (and slightly surpasses) realtime RTGI; the
		// screen flavour is solved without, a converged single bounce, so it
		// matches what the gather and the voxel form estimate. Handing the
		// screen path the traced flavour would brighten a scene the moment its
		// author switched to Baked, which is exactly the disparity the owner
		// ruled out. The reader picks the file by which GI form is active.
		//
		// `m_FieldFlavourRt` is the flavour the resident field was loaded (or
		// last solved) for; `m_FieldBakedAlt` says the *other* flavour's file
		// is on disk and matches. A bake run is not done until both are.
		bool m_FieldFlavourRt = false;
		bool m_FieldBakedAlt = false;

		// Which flavour the solve in flight belongs to: 0 none, 1 the other
		// flavour (solved first, stored, then the field is handed back), 2 the
		// active one (solved last so it is resident when the probes capture
		// and when the bake ends). The store block reads this to know which
		// file the completed solve is.
		int m_FieldSolvePhase = 0;

		// **That the field wants solving is state, not an event.**
		//
		// The change is *noticed* once -- the frame the lighting hash moves --
		// but the frame that notices is very often one that may not act on it.
		// A reflection probe capture re-enters the whole scene walk, once per
		// cube face, before the main pass of the same frame runs; and a script
		// that changes the lighting almost always dirties the probe in the same
		// breath, because a room that changed is a room whose reflections
		// changed. The showroom's mode switch does exactly that.
		//
		// So the notice landed inside the capture, where solving is refused,
		// and the main pass a moment later saw a field whose recorded lighting
		// already matched and asked for nothing. The field stayed at the zeros
		// it had just been cleared to, for the life of the mode -- and under
		// `--bake=on` those zeros are what got written to disk, which is worse
		// than no file at all because a later run loads them and trusts them.
		//
		// Held as a want, it survives the frames that cannot serve it.
		bool m_FieldSolveWanted = false;

		// **Whether "Baked" can actually be honoured**: a field exists and it
		// came off disk. Asked by the frame graph before it decides whether to
		// skip the realtime chain, because a Baked setting with nothing baked
		// must fall back rather than render a scene with no indirect light.
		bool m_FieldUsable = false;
		// **Whether the runtime cache has written over what the field holds.**
		//
		// The cache and a bake share one texture, and the disk load that fills
		// it only runs when the volume's *shape* changes -- the box, the grid,
		// the quality, the flavour. Switching the GI source from Realtime back
		// to Baked changes none of those, so without this the texture would
		// still hold whatever the cache last solved while the mode says Baked:
		// a stored answer that is not the stored answer. Set when the cache
		// runs, and forces one reload when a Baked source next asks.

		// **The camera-following field, and it is deliberately not m_Field.**
		// The baked one is a set of boxes somebody placed holding an answer
		// solved offline; this is one box that slides with the view holding an
		// answer being solved now. Separate objects, separate lifetimes -- only
		// one is bound at a time, and switching mode releases the other. The
		// placement policy lives in the class rather than here.
		RuntimeIrradianceField m_RuntimeField;
		// Said once, not once a frame. Cleared when a scene loads.
		bool m_WarnedMissingBake = false;
		// Whether the field walk has run at all this scene. Until it has, there
		// is nothing true to say about whether a bake exists.
		bool m_FieldEvaluated = false;
		// How long the scene has wanted a bake it does not have. See the
		// warning in RenderShadows.
		uint32_t m_UnbakedFrames = 0;
		// The lighting a bake may be written for, and how long it has held.
		// A hash that changed this frame is not a lighting anybody chose.
		uint64_t m_SettledLighting = 0;
		uint32_t m_SettledFrames = 0;

		// **A bake somebody pressed a button for**, as opposed to `--bake=on`.
		//
		// The two are not the same request and must not share a flag. The
		// command line says "produce whatever this scene is missing", so it
		// loads a file that already matches and writes nothing. A press says
		// "make this one again" -- the author has moved a wall or changed a
		// material, neither of which the lighting hash covers, so the stored
		// answer is stale in a way nothing can detect. So a requested bake
		// skips the load and overwrites.
		bool m_BakeRequested = false;
		uint32_t m_BakeFrames = 0;
		// Frames in a row in which nothing moved. A solve that is running is
		// not a bake that is stuck, however long it runs.
		uint32_t m_BakeStalledFrames = 0;
		// `--bake=force` asks once per scene, not once a frame.
		bool m_ForcedBakeAsked = false;

		// Whether the GI source in force says Baked -- asked by the field
		// binding and by RenderShadows' flag hand-off, which must agree.
		bool WantsBakedGi();

		// Either kind of bake. Every write and the solve behind them ask this
		// rather than the config, so there is one answer to "are we baking".
		bool BakingLighting() const;

		// Whether a bake in progress should ignore what is already on disk.
		bool ForcingBake() const;

		// Ends a requested bake once its field and its probes are on disk, or
		// gives up with a reason. Called once per field update.
		void UpdateLightingBake();

		// Where this scene's composed field is stored under a given lighting
		// and flavour -- one pair of files per lighting the scene can be in;
		// see the definition.
		std::filesystem::path FieldBakePath(uint64_t lighting, bool rtFlavour) const;

		// Where one probe's cube is stored under the scene's current lighting.
		// One file per probe *per lighting*, for the reason the definition
		// gives: a scene can be in more than one, and a cube captured in one is
		// the wrong photograph in the other.
		std::filesystem::path ProbeBakePath(UUID probe) const;

	public:
		// **Bake this scene's stored lighting now.**
		//
		// The editor's half of `--bake=on`: solves the irradiance field under
		// the lighting the scene is in *at this moment* and writes it beside
		// the scene, along with a fresh cube for every non-realtime reflection
		// probe. Takes a few dozen frames -- the lighting hash has to hold
		// still before a file can be named for it -- so it is a request rather
		// than a call that returns a result.
		//
		// **The moment matters, and it is why this exists.** A scene can be in
		// more than one lighting, a bake is one file per lighting, and the
		// only way to reach a lighting a script applies is to be standing in
		// it. Pressed while the game is playing, this bakes what is on screen.
		void RequestLightingBake();

		// Whether a requested bake is still running. False under `--bake=on`,
		// which is not a request and never finishes.
		bool LightingBakePending() const { return m_BakeRequested; }

		// **Whether everything a bake can store for the current lighting is
		// stored.** True between lightings too -- a script sweeping a scene's
		// modes drops it back to false the moment it switches -- so a caller
		// waiting for a whole run to finish waits for this to *hold*, not
		// merely to happen. See RuntimeLayer's exit-when-settled.
		bool BakedLightingSettled();

		// **Pick up bakes that landed on disk after this scene loaded.**
		//
		// The editor's Bake button runs the baker as a child process -- a
		// whole runtime, baking the scene as saved -- so the files appear
		// under a scene that has already decided nothing on disk matches.
		// Nothing re-asks by itself: the field load runs only when the
		// field's shape or lighting changes, and a probe looks for its cube
		// exactly once per scene. This clears both memories, so the next
		// frame walks the load paths again and finds what the child wrote.
		void ReloadBakedLighting();

		// Everything true about the field and what is stored for it, in one
		// read. For the inspector, which has to say something accurate about a
		// feature whose files are named by a hash nobody can read.
		struct FieldStatus
		{
			uint32_t Volumes = 0;          // components composing the field
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Depth = 0;
			float    Spacing = 0.0f;       // metres, the finest any asked for
			uint64_t Lighting = 0;         // names this lighting's file
			bool     Loaded = false;       // the field came off disk
			bool     Solving = false;      // a solve is in flight or waiting
			std::filesystem::path File;      // this lighting's bake
			std::filesystem::path Directory; // where every lighting's bake lives
		};
		FieldStatus GetFieldStatus() const;

		// **Why a `Baked` GI source is not being honoured, when it is not.**
		//
		// The renderer already made this decision -- it has to, before it can
		// choose which chain to build -- and until now it said so only in the
		// log, once per scene. A setting that reads `Baked` while the frame is
		// rendering Realtime is a control that lies about what it is doing, and
		// the log is not where somebody looking at that control is looking.
		//
		// Recorded where the decision is made rather than derived again in the
		// editor, so the panel and the frame cannot disagree.
		enum class BakedGi
		{
			NotAsked,   // the setting says Realtime; nothing to report
			Honoured,   // a bake is loaded and the frame is reading it
			Settling,   // asked for, not true yet, and too early to call it
			NoVolume,   // asked for, and this scene has no Irradiance Volume
			NoBake,     // asked for, there is a volume, no file fits the lighting
		};
		BakedGi GetBakedGiState() const { return m_BakedGi; }

		// Which flavour of bake the active GI form reads: true when the
		// traced bounce is the form in force, false when the screen forms
		// are. The same resolve RenderShadows makes, shared so the loader,
		// the solver, the warnings and the stats overlay's interop keys all
		// name the same file.
		bool ActiveGiIsTraced() const;

		// The box an AutoFit volume derived on the last walk, so the editor's
		// overlay draws the box the bake actually uses. False while nothing
		// in the scene is auto-fitting.
		bool GetAutoFitBox(Vec3& centre, Vec3& extents) const
		{
			centre = m_AutoFitCentre;
			extents = m_AutoFitExtents;
			return m_AutoFitValid;
		}

	private:
		// Written once a frame beside the decision it describes. Declared here
		// rather than with the other field state because a member cannot name
		// a type its own class has not declared yet.
		BakedGi m_BakedGi = BakedGi::NotAsked;

	public:

		// Whether this scene has indirect light stored and loaded -- what a
		// `GiSource::Baked` setting needs to be true. False means the setting
		// cannot be honoured and the caller should say so and use Realtime.
		bool HasBakedIrradiance() const { return m_FieldUsable; }

	private:

		void PackProbes(RHI::RHICommandList& cmd);
		// One subtree, top down. Returns whether this node's world matrix
		// actually changed, which is what lets its children skip: a child's
		// world is its parent's world times its own local, so if neither
		// moved, neither did the child's.
		bool PropagateTransform(ECS::Entity handle, const Mat4& parentWorld,
								bool parentChanged);
		void UnlinkFromParent(Entity entity);

		// Wired to EnTT's on_destroy signals rather than called from
		// DeleteEntity. Destroying an entity, removing a component and clearing
		// the registry all have to run these; a signal cannot be forgotten at
		// one of those call sites.
		void OnNativeScriptDestroyed(ECS::Registry& registry, ECS::Entity handle);
		void OnAudioSourceDestroyed(ECS::Registry& registry, ECS::Entity handle);
		void OnIDDestroyed(ECS::Registry& registry, ECS::Entity handle);

		// Hands what the simulation reported to the scripts on both entities.
		void DispatchContactEvents();

		// Starts every PlayOnAwake source, stops everything the scene started,
		// and pushes the listener and each source's world position to the mixer.
		void StartAudioSources();
		void StopAudioSources();
		void UpdateAudio();
		// One side of one event. `flip` is true for the entity the stored
		// normal points away from.
		void DeliverContact(const ContactEvent& event, UUID to, UUID other, bool flip);

	private:
		ECS::Registry m_Registry;
		std::unordered_map<UUID, ECS::Entity> m_EntityMap;
		// See SetPaused. Runtime state, deliberately not serialized: a saved
		// scene is not "a scene somebody had paused".
		bool m_Paused = false;
		// True while probe faces are being rendered. A probe capture draws the
		// scene, and the scene reflects a probe -- without this the second
		// probe in a scene would capture the first one's reflection of it, and
		// a probe would capture itself.
		bool m_CapturingProbes = false;

		// This frame's delta, remembered by whichever update ran, for the
		// probe rate clock -- the capture happens outside the update calls
		// and has no Timestep of its own. The loop's frame time, so it stays
		// a function of frame number under --frame-time (ENGINE-NOTES 7y).
		float m_FrameDelta = 0.0f;

		// Said once per scene, not once per frame. A light that asks to cast
		// and silently does not is the kind of thing someone spends an evening
		// on.
		bool m_ShadowBudgetWarned = false;
		// The same, for a scene with more probes than the arrays have slots.
		bool m_WarnedProbeCount = false;

		// Where each complete probe went in the arrays, rebuilt every frame by
		// PackProbes and read by ProbeSlotFor. One table for both, because a
		// probe's slot and an object's choice of slot have to be the same
		// answer -- deriving them from two walks of the registry would agree
		// until one of the walks grew a condition the other did not.
		// The renderer's type, not a second copy of it: the traced bounce
		// selects a probe per hit from this same table, and two structs that
		// had to agree would eventually not.
		using ProbeSlot = ProbeVolume;
		// Set by UpdateWorldTransforms, cleared by RefreshDrawList.
		//
		// **The flag is what makes calling Refresh from every render entry
		// point free.** A frame reaches the list from two places -- the shadow
		// pass and the lit pass -- and a reflection probe reaches it six more
		// times inside one of them. Rebuilding per caller would be six extra
		// walks to avoid five, which is the wrong direction.
		//
		// It is safe because every render path begins with
		// UpdateWorldTransforms, so anything that could have changed the list
		// has already raised this by the time a view asks for it.
		bool m_DrawListDirty = true;

		// Cleared and refilled by RefreshDrawList; the capacity is kept, which
		// is most of why rebuilding it every frame costs what it does.
		std::vector<DrawBounds> m_DrawBounds;
		std::vector<DrawItem> m_DrawItems;

		// The same objects again, in the form the cull pass reads (roadmap
		// 8.3), built in the same walk and uploaded at the end of it. Static
		// meshes only: a skinned caster needs bones, which are a per-frame CPU
		// product, and there are a handful of those against tens of thousands
		// of the other kind.
		//
		// Three arrays because the GPU and the CPU need different halves of
		// the same answer. `m_CullObjects` is what the dispatch reads;
		// `m_CullSlots` is the indirect commands' template, one per distinct
		// mesh, which the reset pass copies; `m_CullMeshes` is what the draw
		// needs and the GPU does not -- which mesh to bind before each slot's
		// indirect draw.
		std::vector<GpuCull::Object> m_CullObjects;
		std::vector<GpuCull::SlotCommand> m_CullSlots;
		std::vector<GpuCull::Slot> m_CullMeshes;
		// How many objects named each slot, which becomes the length of the
		// slot's range in the instance buffer. Kept between frames for the
		// allocation, like everything else here.
		std::vector<uint32_t> m_CullCounts;
		// How many objects the cull table holds.
		//
		// **Not `m_CullObjects.size()`**: that vector is filled only on the
		// frame's *first* refresh, because the table it feeds is uploaded once
		// a frame, and it is cleared on every refresh like everything else
		// here. The count has to survive the refreshes that do not rebuild it,
		// because the instance table is indexed by it.
		uint32_t m_CullObjectCount = 0;

		// Which entries of the draw list the GPU table does *not* hold: the
		// skinned casters, and anything with too few indices to draw. Filled
		// only when there is a cull pass, because without one the answer is
		// every entry and walking the whole array is what already happens.
		//
		// A list rather than a test inside the loop, so the walk that survives
		// stays over the small array. Checking each item's slot would mean
		// touching `DrawItem` for every object to find the handful that are
		// not static, which is exactly the streaming the two arrays exist to
		// avoid.
		std::vector<uint32_t> m_CpuDraws;

		// **The blended table, which is the whole of the GPU transparent
		// path.** Its own meshes, slots, counts and objects, because a table is
		// culled once and issued once and these are issued in a different pass
		// with a different pipeline. Everything about them is the opaque
		// table's shape; what differs is which pass reads the result.
		std::vector<GpuCull::Slot>        m_BlendMeshes;
		std::vector<GpuCull::SlotCommand> m_BlendSlots;
		std::vector<uint32_t>             m_BlendCounts;
		std::vector<GpuCull::Object>      m_BlendObjects;
		uint32_t                          m_BlendObjectCount = 0;
		GpuCull::View                     m_CulledBlend;
		bool m_HasBlended = false;
		// Emissive rectangles the traced bounce aims at. Built with the draw
		// list, since that is where the transform, the bounds and the material
		// are all resolved together.
		std::vector<AreaEmitter> m_Emitters;

		// What the camera's cull pass decided, for the lit pass to draw
		// through (roadmap 8.3). Produced in the frame's prepare phase --
		// RenderShadows, outside any render pass, because it is a dispatch --
		// and consumed inside the graph's scene pass. Invalid when there is no
		// cull pass or no table, and then the lit pass walks as it always did.
		GpuCull::View m_CulledLit;
		// Which camera it was culled *for*.
		//
		// **A scene is rendered from more than one place in a frame.** A
		// realtime reflection probe captures six faces through this same path,
		// and the editor draws a viewport and a game view. Each calls
		// RenderShadows with its own camera, so the view above is only good
		// for the render that produced it -- and a probe face drawing the main
		// camera's survivors is a scene that alternates between two pictures
		// every frame, which is what it did.
		//
		// Compared rather than trusted: the lit pass takes the GPU path only
		// when the camera it is drawing through is the one this was culled
		// for, and walks its own draws otherwise.
		Mat4 m_CulledLitFor{ 0.0f };

		std::vector<ProbeSlot> m_ProbeSlots;

		SceneEnvironment m_Environment;
		std::vector<UUID> m_PendingDestroy;
		std::unique_ptr<Physics::World> m_Physics;

		// Kept between steps rather than allocated each one: TakeContactEvents
		// swaps, so the two buffers trade places and neither reallocates once
		// they have grown to the scene's busiest step.
		std::vector<ContactEvent> m_ContactEvents;

		friend class Entity;
		friend class SceneHierarchyPanel;
		friend class SceneSerializer;
	};
}
