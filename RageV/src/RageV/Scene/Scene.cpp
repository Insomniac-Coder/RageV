#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
#include "ScriptableEntity.h"
#include "Components.h"
#include "RageV/Managed/Interop.h"
#include "ScriptRegistry.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Core/Application.h"
#include "RageV/Core/FrameClock.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/BakedLighting.h"
#include "RageV/Renderer/Cubemap.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Renderer/EditorIcons.h"
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/Skybox.h"
#include "RageV/Renderer/ShadowMap.h"
#include "RageV/Renderer/RayShadows.h"
#include "RageV/Renderer/VoxelGI.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Renderer/EnvironmentIBL.h"
#include "RageV/Renderer/ProbeArray.h"
#include "RageV/Renderer/UIRenderer.h"
#include "RageV/Renderer/Frustum.h"
#include "RageV/Renderer/FrameGraphBuilder.h"
#include "RageV/Renderer/EditorCamera.h"
#include "RageV/Renderer/ParticleRenderer.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Particles/GpuParticles.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Project/Project.h"
#include "RageV/UI/Interaction.h"
#include "RageV/UI/Canvas.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	namespace
	{
		const std::vector<UUID> s_NoChildren;
	}

	Scene::Scene()
	{
		m_Registry.OnDestroy<NativeScriptComponent>(&Scene::OnNativeScriptDestroyed, this);
		m_Registry.OnDestroy<AudioSourceComponent>(&Scene::OnAudioSourceDestroyed, this);
		m_Registry.OnDestroy<IDComponent>(&Scene::OnIDDestroyed, this);
	}

	Scene::~Scene()
	{
		// Explicit, in the destructor body, rather than left to the registry's
		// own destructor: this way the on_destroy handlers run while every
		// member is still alive. Without it script instances leaked -- nothing
		// in the engine ever called the destroy hooks at all.
		m_Registry.Clear();
	}

	void Scene::OnNativeScriptDestroyed(ECS::Registry& registry, ECS::Entity handle)
	{
		auto& component = registry.Get<NativeScriptComponent>(handle);
		if (!component.Instance)
			return;

		component.Instance->OnDestroy();
		// Virtual destructor, so this destroys the derived type.
		delete component.Instance;
		component.Instance = nullptr;
	}

	// A signal rather than a line in DeleteEntity, for the same reason the
	// script hook is one: destroying an entity, removing the component and
	// clearing the registry all have to stop the sound, and a signal cannot be
	// forgotten at one of three call sites. A looping source on a destroyed
	// entity would otherwise play until the process ended.
	void Scene::OnAudioSourceDestroyed(ECS::Registry& registry, ECS::Entity handle)
	{
		Audio::Engine::Stop(registry.Get<AudioSourceComponent>(handle).Voice);
	}

	void Scene::OnIDDestroyed(ECS::Registry& registry, ECS::Entity handle)
	{
		m_EntityMap.erase(registry.Get<IDComponent>(handle).ID);
	}

	// -------------------------------------------------------------------------
	// Entities
	// -------------------------------------------------------------------------
	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID id, const std::string& name)
	{
		Entity entity = { m_Registry.Create(), this };
		entity.AddComponent<IDComponent>(id);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		TagComponent& tag = entity.AddComponent<TagComponent>();

		tag.Name = name.empty() ? "Entity" : name;
		m_EntityMap[id] = entity;
		return entity;
	}

	Entity Scene::GetEntityByUUID(UUID id)
	{
		auto it = m_EntityMap.find(id);
		if (it == m_EntityMap.end())
			return {};
		return { it->second, this };
	}

	Entity Scene::FindEntityByName(const std::string& name)
	{
		auto view = m_Registry.GetView<TagComponent>();
		for (auto handle : view)
		{
			if (view.Get<TagComponent>(handle).Name == name)
				return { handle, this };
		}
		return {};
	}

	std::vector<Entity> Scene::FindEntitiesByName(const std::string& name)
	{
		std::vector<Entity> found;
		auto view = m_Registry.GetView<TagComponent>();
		for (auto handle : view)
		{
			if (view.Get<TagComponent>(handle).Name == name)
				found.push_back({ handle, this });
		}
		return found;
	}

	void Scene::DestroyDeferred(Entity entity)
	{
		if (!entity)
			return;

		// Out of the simulation before it leaves the scene, or the body would
		// keep colliding with things on behalf of an entity that is gone.
		if (m_Physics)
			m_Physics->RemoveBody(entity.GetUUID());

		// By id, not by handle: whatever runs the queue may be several steps
		// later, and handles are recycled.
		m_PendingDestroy.push_back(entity.GetUUID());
	}

	void Scene::FlushDestroyQueue()
	{
		if (m_PendingDestroy.empty())
			return;

		// Moved out first: DeleteEntity recurses into children, and a script's
		// OnDestroy running during that could queue more.
		std::vector<UUID> pending;
		pending.swap(m_PendingDestroy);

		for (UUID id : pending)
			DeleteEntity(GetEntityByUUID(id));
	}

	void Scene::DeleteEntity(Entity entity)
	{
		if (!entity)
			return;

		// Copied, not referenced: destroying a child mutates the parent's list
		// through UnlinkFromParent, and iterating a container being modified
		// underneath is how this becomes an intermittent crash.
		if (entity.HasComponent<RelationshipComponent>())
		{
			const std::vector<UUID> children = entity.GetComponent<RelationshipComponent>().Children;
			for (UUID childID : children)
				DeleteEntity(GetEntityByUUID(childID));
		}

		UnlinkFromParent(entity);
		m_Registry.Destroy(entity);
	}

	// -------------------------------------------------------------------------
	// Hierarchy
	// -------------------------------------------------------------------------
	void Scene::UnlinkFromParent(Entity entity)
	{
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return;

		auto& relationship = entity.GetComponent<RelationshipComponent>();
		if (!relationship.Parent.IsValid())
			return;

		if (Entity parent = GetEntityByUUID(relationship.Parent))
		{
			auto& siblings = parent.GetComponent<RelationshipComponent>().Children;
			siblings.erase(std::remove(siblings.begin(), siblings.end(), entity.GetUUID()), siblings.end());
		}

		relationship.Parent = UUID::Invalid();
	}

	bool Scene::IsDescendantOf(Entity entity, Entity possibleAncestor)
	{
		if (!entity || !possibleAncestor)
			return false;

		Entity current = GetParent(entity);
		while (current)
		{
			if (current == possibleAncestor)
				return true;
			current = GetParent(current);
		}
		return false;
	}

	bool Scene::SetParent(Entity child, Entity parent)
	{
		if (!child || !child.HasComponent<RelationshipComponent>())
			return false;

		// Parenting an entity to itself or to one of its own descendants would
		// detach the subtree from every root and make PropagateTransform
		// recurse forever. This is the only place a cycle can form.
		if (parent && (child == parent || IsDescendantOf(parent, child)))
			return false;

		// Captured before the move so the entity does not visibly jump when its
		// new parent has a different transform.
		const Mat4 world = GetWorldTransform(child);

		UnlinkFromParent(child);

		if (parent && parent.HasComponent<RelationshipComponent>())
		{
			child.GetComponent<RelationshipComponent>().Parent = parent.GetUUID();
			parent.GetComponent<RelationshipComponent>().Children.push_back(child.GetUUID());
		}

		// Re-express the same world transform relative to the new parent.
		const Mat4 local = Math::Inverse(GetParentWorldTransform(child)) * world;

		Vec3 position, scale;
		Quat rotation;
		if (Math::Decompose(local, position, rotation, scale))
		{
			auto& transform = child.GetComponent<TransformComponent>();
			transform.Position = position;
			transform.Rotation = Math::ToEuler(rotation);
			transform.Scale = scale;
		}

		UpdateWorldTransforms();
		return true;
	}

	Entity Scene::GetParent(Entity entity)
	{
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return {};
		return GetEntityByUUID(entity.GetComponent<RelationshipComponent>().Parent);
	}

	const std::vector<UUID>& Scene::GetChildren(Entity entity)
	{
		if (!entity || !entity.HasComponent<RelationshipComponent>())
			return s_NoChildren;
		return entity.GetComponent<RelationshipComponent>().Children;
	}

	Mat4 Scene::GetParentWorldTransform(Entity entity)
	{
		Entity parent = GetParent(entity);
		if (!parent)
			return Mat4(1.0f);
		return GetWorldTransform(parent);
	}

	Mat4 Scene::GetWorldTransform(Entity entity)
	{
		if (!entity || !entity.HasComponent<TransformComponent>())
			return Mat4(1.0f);

		// Walks the chain rather than reading the cached World, so callers that
		// have just mutated a local transform get the current answer without
		// having to remember to run the pass first.
		const auto& transform = entity.GetComponent<TransformComponent>();
		return GetParentWorldTransform(entity) * transform.GetLocalTransform();
	}

	bool Scene::PropagateTransform(ECS::Entity handle, const Mat4& parentWorld,
								   bool parentChanged)
	{
		auto& transform = m_Registry.Get<TransformComponent>(handle);

		// **Compared rather than reported.** Nothing in the engine has to
		// remember to say it moved something -- the inspector, the gizmo, the
		// serializer, physics, both scripting languages and everything added
		// later all just write the vectors, as they always have. This looks at
		// what they wrote. A dirty flag would be faster still and would be
		// wrong the first time one of those sites forgot to raise it, which is
		// a silently frozen object rather than a visible failure.
		//
		// Exact comparison, deliberately. A write of the same value is not a
		// move, and a tolerance here would be a threshold below which slow
		// motion stops.
		const bool localChanged = !transform.CacheValid
							   || transform.Position != transform.CachedPosition
							   || transform.Rotation != transform.CachedRotation
							   || transform.Scale != transform.CachedScale;

		const bool changed = parentChanged || localChanged;

		if (changed)
		{
			transform.World = parentWorld * transform.GetLocalTransform();
			transform.CachedPosition = transform.Position;
			transform.CachedRotation = transform.Rotation;
			transform.CachedScale = transform.Scale;
			transform.CacheValid = true;
		}

		// Copied out before recursing: the reference above stays valid only
		// while the registry is not restructured, and the copy costs one matrix.
		const Mat4 world = transform.World;

		// Descended into regardless of `changed`, because a child may have
		// moved under a parent that did not. What the flag saves is the
		// arithmetic at each node, not the visit.
		for (UUID childID : m_Registry.Get<RelationshipComponent>(handle).Children)
		{
			if (Entity child = GetEntityByUUID(childID))
				PropagateTransform(child, world, changed);
		}

		return changed;
	}

	void Scene::UpdateWorldTransforms()
	{

		// The draw list is built out of these matrices, so it is stale the
		// moment they are recomputed. Raised unconditionally, and *not* only
		// when something moved: the list also holds a resolved mesh and a
		// borrowed pointer per entity, so adding or removing either would
		// leave it stale in a way no transform comparison can see.
		m_DrawListDirty = true;

		auto view = m_Registry.GetView<TransformComponent, RelationshipComponent>();
		for (auto handle : view)
		{
			// Roots only; children are reached by recursion, and starting from
			// every entity would compute deep nodes once per ancestor.
			//
			// A root has no parent, so nothing above it can have moved -- which
			// is what the `false` says.
			if (!view.Get<RelationshipComponent>(handle).Parent.IsValid())
				PropagateTransform(handle, Mat4(1.0f), false);
		}
	}

	// -------------------------------------------------------------------------
	// Frame
	// -------------------------------------------------------------------------
	// Lowest ViewRank wins. Ties break on entity id rather than on registry
	// order, so two cameras at the same rank resolve the same way on every run
	// instead of depending on what was created when.
	Entity Scene::GetPrimaryCameraEntity()
	{
		Entity best;
		int bestRank = std::numeric_limits<int>::max();
		uint64_t bestID = std::numeric_limits<uint64_t>::max();

		auto view = m_Registry.GetView<CameraComponent, IDComponent>();
		for (auto item : view)
		{
			const int rank = view.Get<CameraComponent>(item).ViewRank;
			const uint64_t id = view.Get<IDComponent>(item).ID;

			if (rank < bestRank || (rank == bestRank && id < bestID))
			{
				best = { item, this };
				bestRank = rank;
				bestID = id;
			}
		}

		return best;
	}

	PostSettings Scene::GetPostSettings()
	{
		Entity camera = GetPrimaryCameraEntity();
		if (!camera || !camera.HasComponent<CameraComponent>())
			return PostSettings{};

		PostSettings settings = Assets::Manager::GetPostSettings(
			camera.GetComponent<CameraComponent>().PostProfile);

		// **On the copy, and that is the whole reason this is here.** The
		// caller gets a value; the profile on disk and the one in the asset
		// cache keep the numbers a person typed. A solve that wrote back would
		// mean opening a scene and looking at it edited the asset -- and the
		// inspector would show a focus distance sliding about under the cursor
		// with nobody dragging it.
		ResolveFocus(settings, camera);
		return settings;
	}

	// The circle of confusion a photograph is judged sharp by, in metres on the
	// sensor. The 35 mm convention: roughly the film diagonal over 1500, and
	// the number every depth-of-field table ever printed is computed against.
	//
	// **In millimetres of sensor rather than pixels of output, deliberately.**
	// An f-number is a property of a lens and does not change when somebody
	// resizes the window; defining "sharp" per pixel would stop a scene down
	// at 4K and open it up at 720p, so the same shot would be a different
	// photograph on two machines.
	static constexpr float kCircleOfConfusion = 0.00003f;

	void Scene::ResolveFocus(PostSettings& settings, Entity camera)
	{
		if (!settings.DepthOfField || settings.Focus != FocusMode::Target)
			return;

		// **Nothing to focus on falls back to Manual, and says so.**
		//
		// Three ways to get here: no target named, a target that is not in this
		// scene, and a target with nothing drawable under it. The middle one is
		// the case worth designing for -- a `.rvpostprofile` is an asset and can
		// be shared, so a profile authored against one scene's car names a UUID
		// the *other* scene has never heard of.
		//
		// The mode on the copy is changed rather than merely left unsolved. The
		// numbers would be the same either way: FocusDistance and Aperture keep
		// what the author typed. What differs is that everything downstream --
		// the inspector's greying, a script reading the mode back, anyone
		// debugging a frame -- is told the truth about what this frame is
		// actually doing, instead of reading "Target" from a profile that is
		// behaving as Manual.
		auto fallBackToManual = [&]
		{
			settings.Focus = FocusMode::Manual;
			settings.TargetResolved = false;
		};

		Entity target = GetEntityByUUID(settings.FocusTarget.Value);
		if (!target)
		{
			fallBackToManual();
			return;
		}

		Vec3 centre{ 0.0f }, extents{ 0.0f };
		if (!GetSubtreeBounds(target, centre, extents))
		{
			fallBackToManual();
			return;
		}

		settings.TargetResolved = true;

		const Mat4 view = GetWorldTransform(camera);
		const Vec3 eye = Vec3(view[3]);

		// The camera looks down its own -Z, the same axis every light and every
		// camera in this engine aims along.
		const Vec3 forward = Math::Normalize(-Vec3(view[2]));

		// **Depth along the forward axis, not distance to the subject.** The
		// prepass compares its linearised depth buffer against FocusDistance,
		// and a depth buffer holds the first and not the second. For a subject
		// in the middle of frame the two agree; for one at the corner of a wide
		// lens they differ by the cosine of the angle off-axis, and focusing a
		// long way past your subject is exactly the failure this feature is
		// for.
		const float depth = Math::Dot(centre - eye, forward);

		// Behind the camera, or so close that the thin-lens denominator has
		// nothing left in it. Neither is a state to solve in.
		const float focal = settings.FocalLength * 0.001f;
		if (depth <= focal * 1.05f)
		{
			// A resolved target the solve cannot use, which is not the same as
			// no target: the subject is real and the camera has been walked
			// behind it. Manual numbers, and the mode stays Target so that
			// turning back round picks the subject up again.
			settings.Focus = FocusMode::Manual;
			return;
		}

		settings.FocusDistance = depth;

		// How deep the subject is along that same axis. An axis-aligned box's
		// extent along an arbitrary direction is the dot of its half-extents
		// with the direction's absolute components -- the support function,
		// which is exact for a box and is what the frustum test already uses in
		// the other direction.
		const float half = Math::Dot(extents, Math::Abs(forward))
						 * Math::Clamp(settings.SubjectCoverage, 0.0f, 1.0f);

		// Nothing to contain: a flat subject seen face on, or coverage at zero.
		// The plane is on it and the aperture stays where the author left it,
		// which is the useful answer rather than an infinity.
		if (half <= 1.0e-4f)
			return;   // the plane is on it; the aperture stays where it was

		// The near edge is what decides the aperture, and this is not
		// symmetry-blind laziness: the circle of confusion grows faster in
		// front of the plane than behind it -- the `1/z` in the prepass -- so
		// an aperture that keeps the near edge sharp keeps the far edge sharp
		// with room to spare, and one solved from the far edge does not.
		//
		//     N = r / (d - r) * f^2 / ((d - f) * c)
		//
		// which is the prepass's own expression, set equal to `c` at
		// `z = d - r` and rearranged for the f-number.
		const float aperture = (half / (depth - half))
							 * (focal * focal)
							 / ((depth - focal) * kCircleOfConfusion);

		// Clamped to the range the slider offers, which is the range a lens
		// has. A subject too deep to contain gets the smallest opening there
		// is and stays slightly soft at the ends, which is what a photographer
		// standing where this camera is would also get.
		settings.Aperture = Math::Clamp(aperture, 0.7f, 32.0f);
	}

	bool Scene::GetSubtreeBounds(Entity root, Vec3& centre, Vec3& extents)
	{
		if (!root)
			return false;

		Vec3 low(std::numeric_limits<float>::max());
		Vec3 high(std::numeric_limits<float>::lowest());
		bool any = false;

		// Iterative rather than recursive. A model's node hierarchy is six or
		// seven deep and would recurse perfectly well; a scene graph is
		// author-controlled and has no bound at all, and a stack overflow in a
		// focus solve is a poor way to find that out.
		std::vector<UUID> pending;
		pending.push_back(root.GetUUID());

		while (!pending.empty())
		{
			Entity entity = GetEntityByUUID(pending.back());
			pending.pop_back();
			if (!entity)
				continue;

			for (UUID child : GetChildren(entity))
				pending.push_back(child);

			if (!entity.HasComponent<MeshComponent>() || !entity.HasComponent<TransformComponent>())
				continue;

			RHI::Ref<Mesh> resolved =
				Assets::Manager::GetMesh(entity.GetComponent<MeshComponent>().Mesh);
			if (!resolved)
				continue;

			Vec3 itemCentre, itemExtents;
			Frustum::TransformBounds(resolved->GetBounds(),
									 entity.GetComponent<TransformComponent>().World,
									 itemCentre, itemExtents);

			low = Math::Min(low, itemCentre - itemExtents);
			high = Math::Max(high, itemCentre + itemExtents);
			any = true;
		}

		if (!any)
			return false;

		centre = (low + high) * 0.5f;
		extents = (high - low) * 0.5f;
		return true;
	}

	Vec2 Scene::GetCameraClipPlanes()
	{
		Entity camera = GetPrimaryCameraEntity();
		if (!camera || !camera.HasComponent<CameraComponent>())
			return Vec2(0.05f, 1000.0f);

		const SceneCamera& cam = camera.GetComponent<CameraComponent>().Camera;

		// Orthographic near and far are signed distances either side of the
		// camera rather than distances in front of it, so the perspective pair
		// is the only one a depth linearisation means anything for.
		if (cam.Projection != SceneCamera::ProjectionType::Perspective)
			return Vec2(0.05f, 1000.0f);

		return Vec2(cam.PerspectiveNear, cam.PerspectiveFar);
	}

	Vec2 Scene::GetCameraProjectionInverse()
	{
		Entity camera = GetPrimaryCameraEntity();
		if (!camera || !camera.HasComponent<CameraComponent>())
			return Vec2(1.0f, 1.0f);

		// The inverses of the projection's two diagonal scales -- what turns
		// an NDC coordinate back into a view-space one, which is how SSAO
		// reconstructs positions from depth. ENGINE-NOTES 7ac.
		const Mat4& projection =
			camera.GetComponent<CameraComponent>().Camera.GetProjection();

		return Vec2(1.0f / Math::Max(Math::Abs(projection[0][0]), 1.0e-4f),
					1.0f / Math::Max(Math::Abs(projection[1][1]), 1.0e-4f));
	}

	Mat4 Scene::GetCameraView()
	{
		Entity camera = GetPrimaryCameraEntity();
		if (!camera || !camera.HasComponent<TransformComponent>())
			return Mat4(1.0f);
		return Math::Inverse(camera.GetComponent<TransformComponent>().World);
	}

	void Scene::OnViewportResize(float width, float height)
	{
		auto view = m_Registry.GetView<CameraComponent>();

		for (auto& item : view)
		{
			CameraComponent& cam = view.Get<CameraComponent>(item);

			if (!cam.fixedAspectRatio)
			{
				cam.Camera.SetViewport(width, height);
			}
		}
	}

	// Lowest ListenerRank wins, on the same rule as cameras. With no listener
	// component anywhere, the camera is where the scene is heard from -- which
	// is right often enough that requiring one would mostly produce silent
	// scenes and a confused user.
	Entity Scene::GetPrimaryListenerEntity()
	{
		Entity best;
		int bestRank = std::numeric_limits<int>::max();
		uint64_t bestID = std::numeric_limits<uint64_t>::max();

		auto view = m_Registry.GetView<AudioListenerComponent, IDComponent>();
		for (auto item : view)
		{
			const int rank = view.Get<AudioListenerComponent>(item).ListenerRank;
			const uint64_t id = view.Get<IDComponent>(item).ID;

			if (rank < bestRank || (rank == bestRank && id < bestID))
			{
				best = { item, this };
				bestRank = rank;
				bestID = id;
			}
		}

		return best ? best : GetPrimaryCameraEntity();
	}

	void Scene::StartAudioSources()
	{
		UpdateWorldTransforms();

		auto view = m_Registry.GetView<AudioSourceComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [source, transform] = view.Get<AudioSourceComponent, TransformComponent>(handle);

			// Whatever was left in the component is not this run's. A scene
			// saved while playing would otherwise start believing it already
			// owns a voice that no longer exists.
			source.Voice = 0;

			if (!source.PlayOnAwake || !source.Clip.IsValid())
				continue;

			AudioPlayback playback;
			playback.Clip = source.Clip;
			playback.Bus = source.Bus;
			playback.Volume = source.Volume;
			playback.Pitch = source.Pitch;
			playback.Loop = source.Loop;
			playback.Stream = source.Stream;
			playback.Spatial = source.Spatial;
			playback.Position = Vec3(transform.World[3]);
			playback.MinDistance = source.MinDistance;
			playback.MaxDistance = source.MaxDistance;

			source.Voice = Audio::Engine::Play(playback);
		}
	}

	void Scene::StopAudioSources()
	{
		auto view = m_Registry.GetView<AudioSourceComponent>();
		for (auto handle : view)
		{
			AudioSourceComponent& source = view.Get<AudioSourceComponent>(handle);
			Audio::Engine::Stop(source.Voice);
			source.Voice = 0;
		}

		// Everything else the run started -- one-shots a script fired, and
		// sounds belonging to entities that have since been destroyed. Stop
		// means silence, not "silence except for whatever is still in flight".
		Audio::Engine::StopAll();
	}

	void Scene::UpdateAudio()
	{
		if (Entity listener = GetPrimaryListenerEntity())
		{
			const Mat4& world = listener.GetComponent<TransformComponent>().World;

			// -Z forward, matching the camera and light convention.
			Audio::Engine::SetListener(Vec3(world[3]),
									 Vec3(world * Vec4(0.0f, 0.0f, -1.0f, 0.0f)),
									 Vec3(world * Vec4(0.0f, 1.0f, 0.0f, 0.0f)));
		}

		// Positions follow the entity every frame, so a sound attached to
		// something moving is heard where it is rather than where it started.
		auto view = m_Registry.GetView<AudioSourceComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [source, transform] = view.Get<AudioSourceComponent, TransformComponent>(handle);

			if (source.Voice != 0 && source.Spatial)
				Audio::Engine::SetVoicePosition(source.Voice, Vec3(transform.World[3]));
		}
	}

	void Scene::OnRuntimeStart()
	{
		// A fresh run is never paused, whatever state the last one ended in.
		m_Paused = false;

		m_Physics = std::make_unique<Physics::World>();
		m_Physics->Build(*this);

		StartAudioSources();
	}

	void Scene::OnRuntimeStop()
	{
		StopAudioSources();

		// Before the physics goes: a managed OnDestroy may still ask about the
		// entity it was attached to, and answering "no scene" is worse than
		// answering truthfully.
		ReleaseManagedScripts();

		m_Physics.reset();
	}

	void Scene::AdvanceMotionHistory()
	{
		auto view = m_Registry.GetView<TransformComponent>();
		for (auto handle : view)
		{
			TransformComponent& transform = view.Get<TransformComponent>(handle);
			transform.PreviousWorld = transform.World;
		}
	}

	void Scene::OnUpdateEditor(Timestep ts)
	{
		m_FrameDelta = ts.GetSeconds();
		AdvanceMotionHistory();

		// Animated only where an animator asked to be. Scripts and physics
		// deliberately do not run here either; the difference is that an
		// animation is presentation, so previewing one changes nothing anyone
		// can save -- which is what makes an opt-in reasonable where running a
		// script would not be.
		UpdateAnimators(ts, /*editing*/ true);

		// Nothing. Editing a scene must not run it.
		(void)ts;
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		m_FrameDelta = ts.GetSeconds();
		AdvanceMotionHistory();

		// A paused frame derives and places, but advances nothing.
		//
		// Skipping the physics blend is the part that matters most: the
		// interpolation alpha keeps moving while the two states it blends are
		// frozen, so re-blending every frame walks a falling body up and down
		// its last step -- jitter, in place, on a scene that claims to be
		// paused. The animators, frame scripts and particles hold for the same
		// reason the fixed step does: paused means the scene stays as it was
		// mid-run. The world transforms still derive so an inspector edit is
		// visible, and audio positions still follow the entities they sit on.
		if (m_Paused)
		{
			UpdateWorldTransforms();
			UpdateAudio();
			return;
		}

		// On the frame rather than the fixed step, for the same reason the
		// audio positions are: an animation is presentation, and pinning it to
		// the simulation rate would make it stutter at any other frame rate.
		UpdateAnimators(ts);

		// Per frame, not per step: this is where the blend between the last two
		// simulation states is applied, and it is the frame that needs it.
		if (m_Physics)
			m_Physics->SyncTransforms(*this, Application::GetInterpolationAlpha());

		UpdateWorldTransforms();

		// After the sync and the derive, which is the entire point: OnFrame
		// reads the transforms that are about to be drawn, blended for this
		// frame, rather than the ones the last simulation step left behind.
		// Moved any earlier it would see stale positions and there would be no
		// reason to have it.
		if (StepFrameScripts(ts))
		{
			// A frame script may destroy, like a fixed one may -- deferred to
			// here so it can destroy anything, including itself, without
			// deleting the object it is executing in.
			FlushDestroyQueue();

			// Again, because a frame script may have moved something and
			// everything after this reads the world matrix: audio placement
			// first, then the particle systems, then rendering.
			//
			// The flag only ever means "the scene has live scripts in it" --
			// C++ cannot tell whether one overrides OnFrame -- so a scene of
			// tick-only scripts pays one extra hierarchy walk. That is the
			// cheap way to be wrong; the other way is a sound placed a frame
			// behind the thing making it.
			UpdateWorldTransforms();
		}

		// After the sync, so a sound on a simulated body is placed where it is
		// being drawn rather than one step behind it. Audio is presentation,
		// like rendering, and belongs on the frame for the same reason.
		UpdateAudio();

		// Presentation too: nothing collides with a particle and nothing
		// scores one, so they move at whatever rate the display shows them.
		Particles::System::Update(*this, ts.GetSeconds());

		// The GPU half, for emitters that asked for it.
		//
		// Here rather than in OnRender for two reasons, both of which are
		// bugs in the version that looks more natural. This function runs
		// once per frame; OnRender runs once per *view*, and the editor has
		// two -- a simulation stepped there would advance at double speed
		// with the game view open. And a dispatch is illegal inside a render
		// pass, which OnRender is called from the middle of.
		if (RHI::RHICommandList* cmd = Renderer::GetCommandList())
			Particles::Gpu::Simulate(*this, *cmd, ts.GetSeconds());
	}

	// The managed half of the script pass.
	//
	// After the native scripts rather than interleaved, so that ordering between
	// the two languages is defined and stated rather than being whatever the
	// component pools happen to do this build.
	void Scene::StepManagedScripts(Timestep dt)
	{
		if (!Managed::Interop::IsReady())
			return;

		const Managed::ManagedApi& managed = Managed::Interop::Managed();
		if (!managed.Create || !managed.InvokeTick)
			return;

		// The scene the interop functions act on. Set every step rather than
		// once, because play mode swaps the scene underneath and a stale binding
		// would have scripts editing the scene that is no longer running.
		Managed::Interop::SetScene(this);

		std::vector<ECS::Entity> scripted;
		m_Registry.GetView<ManagedScriptComponent>().Each(
			[&](auto handle, ManagedScriptComponent&) { scripted.push_back(handle); });

		for (ECS::Entity handle : scripted)
		{
			auto* script = m_Registry.TryGet<ManagedScriptComponent>(handle);
			if (!script)
				continue;

			// Reconciled rather than created once, so changing the script in the
			// inspector mid-run takes effect. ActiveScript is assigned even when
			// creation fails, which is what stops an unknown name being retried
			// and warned about every single step.
			if (script->ActiveScript != script->ScriptName)
			{
				if (script->Handle != 0)
				{
					managed.InvokeDestroy(script->Handle);
					managed.Destroy(script->Handle);
					script->Handle = 0;
				}

				script->ActiveScript = script->ScriptName;

				if (!script->ScriptName.empty())
				{
					Entity entity{ handle, this };
					script->Handle = managed.Create(script->ScriptName.c_str(),
													(uint64_t)entity.GetUUID());

					if (script->Handle != 0)
					{
						// Fields before OnCreate: a script that reads its own
						// configuration in OnCreate has to see what the
						// inspector set, not the constructor's default.
						if (managed.SetFieldValue)
						{
							for (const auto& entry : script->Fields.Values)
								managed.SetFieldValue(script->Handle, entry.Name.c_str(), entry.Value.c_str());
						}

						managed.InvokeCreate(script->Handle);

						// OnCreate may have destroyed something, including this
						// entity.
						script = m_Registry.TryGet<ManagedScriptComponent>(handle);
						if (!script)
							continue;
					}
				}
			}

			if (script->Handle != 0)
				managed.InvokeTick(script->Handle, dt.GetSeconds());
		}
	}

	// The per-frame script pass.
	//
	// Deliberately much smaller than the fixed one: no body creation, no
	// reconciliation, no OnCreate. Instances are made in exactly one place, and
	// this is not it -- which is what guarantees OnFrame can never arrive before
	// OnCreate, and means choosing a different script in the inspector takes
	// effect at a step boundary rather than halfway through a frame.
	bool Scene::StepFrameScripts(Timestep ts)
	{
		bool stepped = false;

		// Collected before stepping, for the same reason the fixed pass does it:
		// a script may spawn or destroy, and either restructures the pool a view
		// is walking.
		std::vector<ECS::Entity> scripted;
		m_Registry.GetView<NativeScriptComponent>().Each(
			[&](auto handle, NativeScriptComponent&) { scripted.push_back(handle); });

		for (ECS::Entity handle : scripted)
		{
			// May have been destroyed by an earlier script this frame.
			auto* script = m_Registry.TryGet<NativeScriptComponent>(handle);
			if (!script || !script->Instance)
				continue;

			script->Instance->OnFrame(ts);
			stepped = true;
		}

		if (Managed::Interop::IsReady())
		{
			const Managed::ManagedApi& managed = Managed::Interop::Managed();
			if (managed.InvokeFrame)
			{
				// Set every pass rather than once: play mode swaps the scene
				// underneath, and a stale binding would have scripts editing the
				// one that is no longer running.
				Managed::Interop::SetScene(this);

				std::vector<ECS::Entity> handles;
				m_Registry.GetView<ManagedScriptComponent>().Each(
					[&](auto handle, ManagedScriptComponent&) { handles.push_back(handle); });

				for (ECS::Entity handle : handles)
				{
					auto* script = m_Registry.TryGet<ManagedScriptComponent>(handle);
					if (!script || script->Handle == 0)
						continue;

					managed.InvokeFrame(script->Handle, ts.GetSeconds());
					stepped = true;
				}
			}
		}

		return stepped;
	}

	// Releases every managed instance this scene owns.
	//
	// Called when play mode stops. A handle that is never released is a script
	// that never stops running -- and because the managed side holds the
	// instance, it is also an object the collector can never take.
	void Scene::ReleaseManagedScripts()
	{
		if (!Managed::Interop::IsReady())
			return;

		const Managed::ManagedApi& managed = Managed::Interop::Managed();
		if (!managed.Destroy)
			return;

		m_Registry.GetView<ManagedScriptComponent>().Each(
			[&](auto, ManagedScriptComponent& script)
			{
				if (script.Handle != 0)
				{
					managed.InvokeDestroy(script.Handle);
					managed.Destroy(script.Handle);
					script.Handle = 0;
				}
				script.ActiveScript.clear();
			});

		Managed::Interop::SetScene(nullptr);
	}

	void Scene::OnFixedUpdateRuntime(Timestep dt)
	{
		// A step is a step however it was reached. The engine's loop has
		// already opened one, and this borrows that number rather than
		// starting a second; a tool or a test stepping a scene directly gets
		// the clock advanced anyway, which is what stops an input edge from
		// living forever outside the loop. Core/FrameClock.h says why it is
		// both places.
		//
		// Before the pause guard, so a paused step still ages the edges by
		// exactly as much as an unpaused one -- otherwise a press made during
		// a pause would be waiting for the first step after it.
		FrameClock::StepScope step;

		// Guarded here as well as at the callers, so pausing works for anyone
		// holding a Scene -- a game's own pause menu reaches it through
		// SetPaused without every layer having to know.
		if (m_Paused)
			return;

		// Bodies for anything that gained a rigid body after Build -- a spawned
		// prefab, an entity a script assembled. Before the script pass, so a
		// spawned thing's OnCreate can already push its body around; its
		// transform was written last step, which is what the body is created
		// from.
		//
		// An entity waiting in the destroy queue is skipped: DestroyDeferred
		// removes the body eagerly so it stops simulating at once, and the
		// entity lingers until the flush -- exactly the shape this pass would
		// otherwise mistake for a fresh spawn and resurrect.
		if (m_Physics)
		{
			auto simulated = m_Registry.GetView<RigidBodyComponent, ColliderComponent>();
			for (auto handle : simulated)
			{
				Entity entity{ handle, this };
				const UUID id = entity.GetUUID();

				const bool condemned =
					std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), id)
					!= m_PendingDestroy.end();

				if (!condemned && !m_Physics->HasBody(id))
					m_Physics->AddBody(*this, entity);
			}
		}

		// The handles are collected before stepping any of them. A script may
		// spawn an entity, attach a script to it, or remove one -- any of which
		// restructures the pool a view is iterating.
		std::vector<ECS::Entity> scripted;
		m_Registry.GetView<NativeScriptComponent>().Each(
			[&](auto handle, NativeScriptComponent&) { scripted.push_back(handle); });

		for (ECS::Entity handle : scripted)
		{
			// May have been destroyed by an earlier script in this same step.
			auto* script = m_Registry.TryGet<NativeScriptComponent>(handle);
			if (!script)
				continue;

			// Reconciled rather than created-once, so choosing a different
			// script in the inspector mid-run actually takes effect. Assigning
			// ActiveScript even when creation fails is what stops an unknown
			// name from being retried, and warned about, every single step.
			if (script->ActiveScript != script->ScriptName)
			{
				if (script->Instance)
				{
					script->Instance->OnDestroy();
					delete script->Instance;
					script->Instance = nullptr;
				}

				script->ActiveScript = script->ScriptName;

				if (!script->ScriptName.empty())
				{
					script->Instance = ScriptRegistry::Create(script->ScriptName);
					if (script->Instance)
					{
						script->Instance->m_Entity = Entity{ handle, this };

						// Before OnCreate: a script that reads its own
						// configuration there has to see what the inspector set,
						// not what the constructor left.
						for (const ScriptField& field : ScriptRegistry::FieldsOf(script->ScriptName))
						{
							if (const std::string* value = script->Fields.Find(field.Name))
								field.Set(script->Instance, *value);
						}

						script->Instance->OnCreate();

						// OnCreate may have destroyed something, including this
						// entity.
						script = m_Registry.TryGet<NativeScriptComponent>(handle);
						if (!script)
							continue;
					}
				}
			}

			if (!script->Instance)
				continue;

			// On the fixed step, not the frame: a script that moves something
			// has to agree with the physics that will push it. The frame half
			// is OnFrame, in StepFrameScripts.
			script->Instance->OnTick(dt);
		}

		StepManagedScripts(dt);

		// After both languages' OnTick, so a handler runs against a world the
		// step has already advanced -- and after creation, so a button bound to
		// something spawned this same step finds it.
		DispatchUIClicks();

		// Applied once the pass is over, so a script can destroy anything --
		// including itself -- without deleting the object it is executing in.
		FlushDestroyQueue();
		UpdateWorldTransforms();

		// After the scripts, so a script's forces and velocity changes are part
		// of the step they were issued for rather than the next one.
		if (m_Physics)
		{
			m_Physics->Step(dt.GetSeconds());

			// After the step, not during it: a contact callback that pushes
			// something back should push it in the world the solver has already
			// finished with, and Jolt forbids touching bodies from inside the
			// callback at all.
			DispatchContactEvents();

			// A collision handler is as entitled to destroy something as any
			// other script code is.
			FlushDestroyQueue();
		}
	}

	// Every button clicked this step calls what it was bound to.
	//
	// **A binding that resolves to nothing complains, every time it is
	// clicked.** The first draft of this warned once per button, which reads
	// well and is wrong in use: clicks two through ten then look exactly like
	// the click never registered, which is the harder bug to report. A click is
	// a deliberate act, so a line per deliberate act is proportionate -- and it
	// is the only thing standing between a renamed method and a button that
	// silently does nothing, because a method name in a scene file has no
	// compiler behind it.
	void Scene::DispatchUIClicks()
	{
		// Collected before any of them runs. A handler may spawn an entity,
		// destroy one, or add a component -- each of which restructures the
		// pool a view is iterating, which is the same reason the script pass
		// collects its handles first.
		std::vector<ECS::Entity> clicked;
		m_Registry.GetView<UIButtonComponent>().Each(
			[&](auto handle, UIButtonComponent& button)
			{
				if (button.Clicked.IsNow() && !button.OnClickMethod.empty())
					clicked.push_back(handle);
			});

		for (ECS::Entity handle : clicked)
		{
			// May have been destroyed by an earlier handler in this same pass.
			auto* button = m_Registry.TryGet<UIButtonComponent>(handle);
			if (!button)
				continue;

			// Copies, not references: invoking may move the component's
			// storage, and both of these are read after that call.
			const std::string method = button->OnClickMethod;
			const EntityRef targetRef = button->OnClickTarget;

			Entity self{ handle, this };
			const std::string name = self.GetName();

			// An empty target means this entity -- the script-on-the-button
			// case, which is most of them.
			Entity target = targetRef.IsValid() ? GetEntityByUUID(targetRef.Value) : self;

			if (!target)
			{
				RV_CORE_WARN("UI button '{0}': OnClick target entity {1} is not in the "
							 "scene, so '{2}' was not called. It was probably deleted.",
							 name, (uint64_t)targetRef, method);
				continue;
			}

			if (!InvokeScriptMethod(target, method))
			{
				RV_CORE_WARN("UI button '{0}': nothing on '{1}' answers to '{2}'. A C++ "
							 "script must register the method with .Method<>(); a C# one "
							 "needs it public, with no arguments, returning void.",
							 name, target.GetName(), method);
			}
		}
	}

	bool Scene::InvokeScriptMethod(Entity entity, const std::string& method)
	{
		if (!entity || method.empty())
			return false;

		bool invoked = false;

		// Native first, then managed -- the defined order the step already runs
		// them in, so a handler present in both languages behaves the same way
		// a contact delivered to both does.
		if (auto* script = m_Registry.TryGet<NativeScriptComponent>(entity);
			script && script->Instance)
		{
			for (const ScriptMethod& candidate : ScriptRegistry::MethodsOf(script->ActiveScript))
			{
				if (candidate.Name == method)
				{
					candidate.Invoke(script->Instance);
					invoked = true;
					break;
				}
			}
		}

		// Re-fetched rather than held: the native handler above is entitled to
		// destroy things, and one of the things it may destroy is this entity.
		// The managed component's storage would have moved underneath a
		// pointer taken before the call.
		if (!m_Registry.Valid(entity))
			return invoked;

		if (auto* managed = m_Registry.TryGet<ManagedScriptComponent>(entity);
			managed && managed->Handle != 0 && Managed::Interop::IsReady())
		{
			if (Managed::Interop::Managed().InvokeMethod(managed->Handle, method.c_str()) != 0)
				invoked = true;
		}

		return invoked;
	}

	bool Scene::CanInvokeScriptMethod(Entity entity, const std::string& method)
	{
		if (!entity || method.empty())
			return false;

		if (auto* script = m_Registry.TryGet<NativeScriptComponent>(entity))
		{
			for (const ScriptMethod& candidate : ScriptRegistry::MethodsOf(script->ScriptName))
			{
				if (candidate.Name == method)
					return true;
			}
		}

		if (auto* managed = m_Registry.TryGet<ManagedScriptComponent>(entity);
			managed && Managed::Interop::IsReady() && Managed::Interop::Managed().ListMethods)
		{
			// Length first, then the copy -- the GetEntityName contract. A
			// script with many methods must not come back clipped, or a
			// perfectly good binding would be reported dead.
			const int32_t needed = Managed::Interop::Managed().ListMethods(
				managed->ScriptName.c_str(), nullptr, 0);

			if (needed > 0)
			{
				std::string listing((size_t)needed + 1, '\0');
				Managed::Interop::Managed().ListMethods(managed->ScriptName.c_str(),
														listing.data(), (int32_t)listing.size());

				std::stringstream stream(listing.c_str());
				std::string line;
				while (std::getline(stream, line))
				{
					if (line == method)
						return true;
				}
			}
		}

		return false;
	}

	void Scene::DispatchContactEvents()
	{
		m_Physics->TakeContactEvents(m_ContactEvents);

		for (const ContactEvent& event : m_ContactEvents)
		{
			// The stored normal runs from A towards B, so B is the one it
			// already points at and A is the one that needs it reversed.
			DeliverContact(event, event.A, event.B, true);
			DeliverContact(event, event.B, event.A, false);
		}

		// Not cleared: the buffer is swapped back next step, and holding the
		// events until then costs nothing.
	}

	void Scene::DeliverContact(const ContactEvent& event, UUID to, UUID other, bool flip)
	{
		Entity entity = GetEntityByUUID(to);
		if (!entity)
			return;   // destroyed since the step, which is not an error

		Collision collision;
		collision.Other = GetEntityByUUID(other);
		collision.Trigger = event.Trigger;
		collision.Point = event.Point;
		collision.Normal = flip ? -event.Normal : event.Normal;
		collision.ImpactSpeed = event.ImpactSpeed;

		// Native first, then managed -- the same defined order the step runs
		// them in. An entity may carry both, and each is delivered regardless
		// of the other: a destroy queued by the native handler is deferred, so
		// the managed one still runs against a consistent scene.
		if (auto* script = m_Registry.TryGet<NativeScriptComponent>(entity);
			script && script->Instance)
		{
			ScriptableEntity* instance = script->Instance;

			if (event.Trigger)
			{
				switch (event.Phase)
				{
					case ContactPhase::Enter: instance->OnTriggerEnter(collision); break;
					case ContactPhase::Stay:  instance->OnTriggerStay(collision);  break;
					case ContactPhase::Exit:  instance->OnTriggerExit(collision);  break;
				}
			}
			else
			{
				switch (event.Phase)
				{
					case ContactPhase::Enter: instance->OnCollisionEnter(collision); break;
					case ContactPhase::Stay:  instance->OnCollisionStay(collision);  break;
					case ContactPhase::Exit:  instance->OnCollisionExit(collision);  break;
				}
			}
		}

		auto* managed = m_Registry.TryGet<ManagedScriptComponent>(entity);
		if (!managed || managed->Handle == 0 || !Managed::Interop::IsReady())
			return;

		Managed::CollisionData data;
		data.Other = collision.Other ? (uint64_t)collision.Other.GetUUID() : 0;
		data.Trigger = collision.Trigger ? 1 : 0;
		data.Point = collision.Point;
		data.Normal = collision.Normal;
		data.ImpactSpeed = collision.ImpactSpeed;

		const int32_t base = event.Trigger
			? (int32_t)Managed::ContactKind::TriggerEnter
			: (int32_t)Managed::ContactKind::CollisionEnter;
		const int32_t kind = base + (int32_t)event.Phase;

		Managed::Interop::Managed().InvokeContact(managed->Handle, kind, &data);
	}

	void Scene::OnRenderRuntime(float aspectRatio)
	{
		UpdateWorldTransforms();

		Entity camera = GetPrimaryCameraEntity();
		if (!camera || !camera.HasComponent<TransformComponent>())
			return;   // nothing to render from

		auto& component = camera.GetComponent<CameraComponent>();

		// Matched to this pass. Without it a scene that was just loaded or
		// restored draws through a camera at its default aspect -- the stored
		// one is not serialized, because it describes the surface rather than
		// the scene -- and the image is stretched until something happens to
		// resize the panel.
		if (!component.fixedAspectRatio && aspectRatio > 0.0f)
			component.Camera.SetAspectRatio(aspectRatio);

		// World, so a camera parented to a rig follows it.
		OnRender(component.Camera, camera.GetComponent<TransformComponent>().World);
	}

	void Scene::OnRenderEditor(const EditorCamera& camera, const ViewportGridSettings* grid,
							   const EditorIconSettings* icons)
	{
		UpdateWorldTransforms();
		OnRender(camera, camera.GetTransform(), grid, icons);
	}

	void Scene::PrepareEnvironment()
	{
		if (m_CapturingProbes || !Renderer::HasDevice() || !EnvironmentIBL::IsReady())
			return;

		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		// Slot 0 of the probe arrays, and the only slot a scene with no probes
		// at all has. Filling it here rather than beside the probes keeps the
		// two independent: a scene whose probes are all still capturing must
		// still have something to reflect, and it is this.
		//
		// Cached on the source, so this is a pointer comparison on every frame
		// after the one that built it.
		ProbeArray::Begin(ProbeFaceSize(), 1 + ProbeCount());
		ProbeArray::SetSky(*cmd, ResolveSky());
	}

	RHI::Ref<RHI::RHITexture> Scene::ResolveSky() const
	{
		RHI::Ref<RHI::RHITexture> sky;
		if (m_Environment.Sky == SkyType::Cubemap)
			sky = Assets::Manager::GetCubemap(m_Environment.SkyTexture);

		return Skybox::ResolveEnvironment(m_Environment, sky);
	}

	uint32_t Scene::ProbeCount()
	{
		auto view = m_Registry.GetView<const ReflectionProbeComponent>();
		return (uint32_t)view.Size();
	}

	uint32_t Scene::ProbeFaceSize() const
	{
		// The largest source in the scene decides, because there is one array
		// and a slice cannot be a different size from its neighbours. A smaller
		// probe is resampled up into its slice, which costs it nothing it had
		// -- its capture was always going to be blurred by the convolution --
		// and a scene where every probe agrees, which is every scene anybody
		// authors, gets exactly the size it asked for.
		//
		// The sky is a source too, and forgetting it was a real regression: a
		// 512 HDR sky reflected through a 128 slice keeps a quarter of its
		// sharpness, and every reflective surface in the scene goes soft with
		// it. The memory this costs is what the dynamic slot count in Begin
		// exists to pay for.
		uint32_t largest = 0;

		if (const RHI::Ref<RHI::RHITexture> sky = ResolveSky())
			largest = Math::Max(largest, sky->GetWidth());

		auto view = m_Registry.GetView<const ReflectionProbeComponent>();
		for (auto& item : view)
		{
			const auto& probe = view.Get<const ReflectionProbeComponent>(item);
			largest = Math::Max(largest, (uint32_t)Math::Clamp(probe.Resolution, 16, 1024));
		}

		// Zero probes still need slot 0 for the sky, and this is the only place
		// its size is decided. Not a floor under the loop above: a scene of
		// 64-pixel probes should get 64-pixel slices, and a floor would quietly
		// give it four times the memory it asked for -- and would make "the
		// largest probe decides" a sentence that is only true above 128.
		return largest > 0 ? largest : 128;
	}

	// --- terrain (ENGINE-NOTES 7ap) ------------------------------------------

	void Scene::ForEachTerrain(const std::function<void(Entity, TransformComponent&,
													TerrainComponent&, Terrain&)>& fn)
	{
		auto view = m_Registry.GetView<TransformComponent, TerrainComponent>();
		for (auto& item : view)
		{
			auto [transform, component] = view.Get<TransformComponent, TerrainComponent>(item);
			const RHI::Ref<Terrain>& terrain = Terrain::Resolve(component);
			if (!terrain)
				continue;
			fn(Entity{ item, this }, transform, component, *terrain);
		}
	}

	void Scene::ForEachTerrainChunk(const std::function<void(Entity, TransformComponent&,
														 TerrainComponent&, Terrain&,
														 Terrain::Chunk&)>& fn)
	{
		ForEachTerrain([&](Entity entity, TransformComponent& transform,
						   TerrainComponent& component, Terrain& terrain)
		{
			for (Terrain::Chunk& chunk : terrain.GetChunks())
				fn(entity, transform, component, terrain, chunk);
		});
	}

	void Scene::PrepareTerrains(const Vec3& cameraPosition)
	{
		ForEachTerrain([&](Entity, TransformComponent& transform,
						   TerrainComponent& component, Terrain& terrain)
		{
			terrain.SelectLod(cameraPosition, transform.World);
			terrain.RefreshLayers(component);
		});
	}

	namespace
	{
		// Where a world point lands in a terrain's own metres, and whether that
		// terrain's extent actually contains it.
		//
		// The extent test is here rather than inside Terrain::HeightAt on
		// purpose (7au): HeightAt clamps, which is right for the brush and for
		// the skirt rule, and wrong for a caller asking whether there is ground
		// at all. Changing it would touch three call sites for the benefit of
		// one that none of them share.
		bool LocalPointOn(const Terrain& terrain, const Mat4& world,
						  const Vec3& worldPosition, Vec3& local)
		{
			local = Vec3(Math::Inverse(world) * Vec4(worldPosition, 1.0f));
			const float half = terrain.GetDimensions().Size * 0.5f;
			return Math::Abs(local.x) <= half && Math::Abs(local.z) <= half;
		}

		// The local surface point put back into world space. The y of that is
		// the height; under any transform that keeps the terrain level, its x
		// and z are the input's again.
		float WorldHeightOf(const Terrain& terrain, const Mat4& world, const Vec3& local)
		{
			const float height = terrain.HeightAt(local.x, local.z);
			return Vec3(world * Vec4(local.x, height, local.z, 1.0f)).y;
		}
	}

	bool Scene::TerrainHeightAt(const Vec3& worldPosition, float& height)
	{
		bool found = false;
		float highest = 0.0f;

		ForEachTerrain([&](Entity entity, TransformComponent&, TerrainComponent&,
						   Terrain& terrain)
		{
			// GetWorldTransform rather than TransformComponent::World: the
			// cached one is written by the transform pass, and a script may ask
			// this before the first frame of a scene has been drawn -- which is
			// exactly the situation a headless check is in.
			const Mat4 world = GetWorldTransform(entity);

			Vec3 local;
			if (!LocalPointOn(terrain, world, worldPosition, local))
				return;

			const float y = WorldHeightOf(terrain, world, local);
			if (!found || y > highest)
			{
				highest = y;
				found = true;
			}
		});

		height = found ? highest : 0.0f;
		return found;
	}

	bool Scene::TerrainHeightAt(Entity terrainEntity, const Vec3& worldPosition, float& height)
	{
		height = 0.0f;
		if (!terrainEntity || !terrainEntity.HasComponent<TerrainComponent>())
			return false;

		const RHI::Ref<Terrain>& built =
			Terrain::Resolve(terrainEntity.GetComponent<TerrainComponent>());
		if (!built)
			return false;

		const Mat4 world = GetWorldTransform(terrainEntity);
		Vec3 local;
		if (!LocalPointOn(*built, world, worldPosition, local))
			return false;

		height = WorldHeightOf(*built, world, local);
		return true;
	}

	bool Scene::HasTerrain()
	{
		auto view = m_Registry.GetView<TransformComponent, TerrainComponent>();
		for (auto& item : view)
		{
			if (Terrain::Resolve(view.Get<TerrainComponent>(item)))
				return true;
		}
		return false;
	}

	void Scene::RenderShadows(const Camera& camera, const Mat4& cameraTransform)
	{
		// A probe capture draws the scene, and the scene samples shadows. Doing
		// this during one would fit cascades to a cube face's 90-degree frustum
		// and then leave them there for the real camera.
		if (m_CapturingProbes || !Renderer::HasDevice())
			return;

		// The terrain's levels of detail for this camera, and its layers for
		// this frame, before anything -- the shadow casters, the ray-instance
		// list, the draw -- reads them (7ap, 7aq). First, so a shadows-off
		// return below still leaves them chosen for the frame's draw.
		PrepareTerrains(Vec3(cameraTransform[3]));

		// Maps or rays (ENGINE-NOTES 7am, 7an), resolved once here and told to
		// the lit pass, which recompiles its shaders when the answer changes.
		// Under rays every casting light of every kind traces and no map is
		// rendered; under maps the cascades and the local maps are what they
		// always were. Told *before* the shadows-off return below, so that
		// switching shadows off while tracing takes the rays away with them:
		// the resolve says no once shadows are off, and a lit pass left
		// believing otherwise would trace into the empty structure.
		const bool traced = ResolveRayTracing(Project::Render());
		Renderer3D::SetRayTracedShadows(traced);
		// And whether the same structure answers reflections (7ao); resolved
		// after the shadows because it rides on them.
		Renderer3D::SetRayTracedReflections(
			ResolveRayTracedReflections(Project::Render()) != RayDetail::Off);
		// And the traced bounce (7at), on the same beat and by the same
		// resolve. Its dial is the post profile's, handed to the renderer
		// here so the lit shader has it: zero while the traced form is not
		// running, which is what makes the shader's block cost nothing.
		const RayDetail giDetail = ResolveRayTracedGlobalIllumination(Project::Render());

		// **Baked means baked, and says so when it cannot be.**
		//
		// A source of Baked asks the frame to stop computing indirect light and
		// read what was stored instead. That is only an instruction the engine
		// can follow if something *was* stored: it needs an irradiance volume
		// in the scene and a bake to have been run over it. Where either is
		// missing the setting is not quietly ignored and not quietly obeyed --
		// obeying it would render a scene with no indirect light at all, which
		// reads as an art problem rather than a setting.
		//
		// Said once per scene rather than per frame: it is a fact about how the
		// scene is set up, and a line a frame is a line nobody reads.
		const bool wantsBaked = giDetail != RayDetail::Off
			? Project::Render().RayTracedGiSource == GiSource::Baked
			: GetPostSettings().GiSource == GiSource::Baked;
		const bool canBake = HasBakedIrradiance();

		// **Not before the field has had a chance to exist.**
		//
		// This runs before the frame graph is built, and the field is composed
		// and loaded further in, inside the scene pass -- so on the first frame
		// of a scene there is nothing to report on yet. Asking then would warn
		// about a bake that is about to load and fall back for the life of the
		// scene. One frame of Realtime while the answer arrives costs nothing;
		// a wrong warning costs somebody an afternoon.
		// **Once the state has settled, not the moment it first looks wrong.**
		//
		// A scene's lighting hash is not final on frame one: lights arrive as
		// the registry is walked, and the field is composed and looked up
		// inside the frame after this runs. A warning fired in that window
		// names a bake that is about to load, and then sticks for the life of
		// the scene because it only fires once. Half a second of settling costs
		// nothing and makes the warning mean what it says.
		if (m_FieldEvaluated && !canBake)
			m_UnbakedFrames++;
		else
			m_UnbakedFrames = 0;

		// **The same answer the warning gives, for anything that has a screen.**
		// Written every frame rather than once, because a bake can arrive --
		// somebody presses Bake -- and a state that only ever goes one way
		// would leave the editor showing a fallback that has stopped happening.
		// It waits out the same settling window the warning does, so a scene
		// does not open with a complaint about a bake that is still loading.
		m_BakedGi = !wantsBaked      ? BakedGi::NotAsked
				  : canBake          ? BakedGi::Honoured
				  : m_UnbakedFrames <= 30 ? BakedGi::Settling
				  : m_FieldVolumes == 0   ? BakedGi::NoVolume
										  : BakedGi::NoBake;

		if (wantsBaked && !canBake && m_UnbakedFrames > 30 && !m_WarnedMissingBake
			&& !Renderer3D::HasPendingIrradianceSolve())
		{
			m_WarnedMissingBake = true;
			// **Named, so it can be acted on.** "No baked field" is true and
			// useless: the file is named after the lighting, so the thing an
			// author needs to know is which lighting this is and where it was
			// looked for. A bake made under different lighting -- the editor
			// showing a scene whose mode script has not run, say -- is a
			// present file with the wrong name, and that reads identically to
			// no file at all unless the message says otherwise.
			if (m_FieldVolumes == 0)
			{
				RV_CORE_WARN("Global illumination is set to Baked, but this scene has no "
							 "Irradiance Volume in it. Falling back to Realtime.");
			}
			else
			{
				RV_CORE_WARN("Global illumination is set to Baked, but no bake matches "
							 "this scene's current lighting. Falling back to Realtime."
							 " wanted {0}, in {1}",
							 FieldBakePath(m_FieldLighting, ActiveGiIsTraced())
								 .filename().string(),
							 BakedLighting::DirectoryFor(m_SourcePath).string());

				std::error_code code;
				for (const auto& entry : std::filesystem::directory_iterator(
						 BakedLighting::DirectoryFor(m_SourcePath), code))
				{
					if (entry.path().extension() == ".rvfield")
						RV_CORE_WARN("    found:  {0}", entry.path().filename().string());
				}
			}
		}

		// **Baked means baked, under both forms.** A source of Baked stops the
		// frame computing indirect light -- the screen-space gather on the
		// rasterised path, the traced bounce under the traced one -- and reads
		// the stored field instead. The saving is the entire point of a bake:
		// one-time compute, recreated per frame for the cost of a fetch.
		//
		// An earlier version kept the traced chain running under a Baked
		// source, with the field terminating its rays. It rendered the best
		// picture anything here has rendered and it cost MORE frame time than
		// realtime -- which is exactly backwards for a bake, and the owner
		// said so. What that chain added at runtime is now baked into the
		// field itself: the solve's sweeps read the previous sweep's completed
		// answer, so the stored field carries the multi-bounce transport
		// (Renderer3D::SolvePendingIrradiance) and no frame ray is needed to
		// recover it.
		//
		// Realtime is what happens unless Baked can actually be honoured --
		// no volume, no matching bake, still solving -- and the scene says so
		// rather than quietly obeying or ignoring: m_BakedGi above is what the
		// editor's notice and both dropdowns read.
		const bool fieldReady = canBake && m_FieldEvaluated;
		const bool bakedHonoured = wantsBaked && fieldReady;
		Renderer3D::SetBakedIrradianceOnly(bakedHonoured);
		Renderer3D::SetRayTracedGlobalIllumination(giDetail != RayDetail::Off
												   && !bakedHonoured);

		RenderShadowMaps(camera, cameraTransform);

		// **What the camera can see, decided here and not in the lit pass**
		// (roadmap 8.3). It is a dispatch, and a dispatch may not be recorded
		// inside a render pass; this is the last point in the frame that is
		// outside one and knows where the camera is.
		//
		// The draw list is refreshed first because the table the cull reads is
		// built with it -- free when RenderShadowMaps has already done it, and
		// necessary when it returned early because shadows are off.
		m_CulledLit = {};
		m_CulledBlend = {};
		m_CulledLitFor = Mat4(0.0f);
		RefreshDrawList();
		if (RHI::RHICommandList* cull = Renderer::GetCommandList())
		{
			const Mat4 viewProjection = camera.GetProjection() * Math::Inverse(cameraTransform);

			// Not when the lit pass eats meshlets: the indirect path would
			// draw static meshes through the vertex pipeline and the meshlet
			// pipeline would never see them. Per-meshlet culling in the mesh
			// stage stands in for the compute cull, exactly as on the shadow
			// views.
			if (!Renderer3D::LitMeshletsActive())
				m_CulledLit = GpuCull::CullLit(*cull, viewProjection);
			if (m_CulledLit.IsValid())
				m_CulledLitFor = cameraTransform;

			// **The blended table's rows sit after the opaque ones** in the
			// instance buffer the two share, so its cull is told where they
			// begin. The base is the opaque object count whether or not the
			// opaque cull succeeded: the instance rows are reserved from the
			// same count either way, and a base that moved when a cull failed
			// would put the glass on top of the bodywork's matrices.
			m_CulledBlend = GpuCull::CullLit(*cull, viewProjection,
											 GpuCull::Pass::Blended, m_CullObjectCount);
		}

		// The voxel grid, after the maps it is lit from (ENGINE-NOTES 7bc).
		UpdateVoxelGI(cameraTransform);
	}

	// The five walks a frame used to do, collapsed into one.
	//
	// Everything here is view-independent: which mesh a handle names, which
	// material, where the object's bounds land in the world. None of it changes
	// between the camera and a cascade, and all of it was being redone for each.
	//
	// **The order is the registry's**, and it has to stay that way. Instancing
	// downstream groups runs of the same mesh, and the depth sort is applied to
	// what survives -- neither is disturbed by this, because this changes when
	// the resolution happens rather than what order things arrive in.
	void Scene::RefreshDrawList()
	{
		if (!m_DrawListDirty)
			return;
		m_DrawListDirty = false;

		m_DrawBounds.clear();
		m_DrawItems.clear();

		// Built alongside, when there is a cull pass to read it (8.3). The
		// flag is taken once rather than per object: whether the pass exists
		// is a property of the device and the shaders, decided at startup.
		const bool gpuCull = GpuCull::IsAvailable();

		// **The table is rebuilt whenever this list is, and that is not an
		// optimisation left on the table -- it is a correctness rule.**
		//
		// Building it only on the frame's first refresh saved a 96-byte-per-
		// object write and upload, and it was wrong: the slot offsets, the
		// per-object row indices and the list of what the CPU still draws are
		// derived here on *every* refresh, and they have to describe the table
		// that was actually uploaded. While a scene's meshes are still
		// streaming in, two refreshes of one frame see different sets of
		// resolvable objects, and the second one's offsets point into the
		// first one's table. It showed as a scene appearing piece by piece for
		// its first few seconds -- which reads as slow loading rather than as
		// a mismatched buffer.
		//
		// Anything cheaper than this has to prove the object set is unchanged,
		// not assume it.
		const bool buildTable = gpuCull;
		m_CullObjects.clear();
		m_CullSlots.clear();
		m_CullMeshes.clear();
		m_CullCounts.clear();
		m_CpuDraws.clear();
		m_HasBlended = false;
		m_Emitters.clear();
		m_CullObjectCount = 0;

		m_BlendMeshes.clear();
		m_BlendSlots.clear();
		m_BlendCounts.clear();
		if (buildTable)
			m_BlendObjects.clear();
		m_BlendObjectCount = 0;

		auto meshView = m_Registry.GetView<TransformComponent, MeshComponent>();

		// Reserving on the view's size rather than growing: at sixty thousand
		// objects the reallocations alone are a measurable share of the build,
		// and the capacity survives the clear above so this is a no-op after
		// the first frame.
		const size_t count = meshView.SizeHint();
		m_DrawBounds.reserve(count);
		m_DrawItems.reserve(count);
		if (buildTable)
			m_CullObjects.reserve(count);

		// **The slot lookup, and what its justification actually assumes.** The
		// argument for a scan is that a scene has a handful of distinct meshes
		// -- "the scale scenes have four" -- so the one-entry cache almost
		// always hits and the fallback walks a vector that fits in a cache
		// line or two. That holds for the scale scenes and not for the ones
		// this engine ships: the showroom has **155 distinct meshes across
		// 185 objects**, so the cache misses nearly every time and the
		// fallback averages half of 155 pointer compares -- about 11k a
		// refresh, two to four refreshes a frame.
		//
		// Left as a scan deliberately, and this is the reasoning rather than
		// an oversight. It is ~20-40 us a frame on that scene, and the frame
		// is GPU bound (measured: 4.03 ms of GPU work in a 4.05 ms frame,
		// with the CPU idle in `wait`), so the time is worth nothing today. A
		// map would need two new Scene members, and a change to this type's
		// layout is the documented stale-build trap that costs a Release-only
		// crash. Revisit when a scene is CPU bound or the object count grows:
		// at 60k objects this is milliseconds, not microseconds.
		const Mesh* lastMesh = nullptr;
		uint32_t lastSlot = kNoCullSlot;

		// Which row of the cull table the next static object takes. Counted
		// rather than read off m_CullObjects.size(), because the table is only
		// *built* on the frame's first refresh -- later ones still have to
		// number the objects the same way to point at it.
		uint32_t cullRow = 0;

		// The blended table's own cursor and slot cache. A second pair rather
		// than reusing the first: the two tables number their rows
		// independently, and a mesh may legitimately appear in both -- the same
		// pane of glass wearing two materials, one of them opaque.
		const Mesh* lastBlendMesh = nullptr;
		uint32_t lastBlendSlot = kNoCullSlot;
		uint32_t blendRow = 0;

		auto blendSlotFor = [&](const Mesh* key) -> uint32_t
		{
			if (key == lastBlendMesh)
				return lastBlendSlot;

			for (uint32_t i = 0; i < (uint32_t)m_BlendMeshes.size(); i++)
			{
				if (m_BlendMeshes[i].MeshRef.get() == key)
				{
					lastBlendMesh = key;
					lastBlendSlot = i;
					return i;
				}
			}
			return kNoCullSlot;
		};

		auto slotFor = [&](const Mesh* key) -> uint32_t
		{
			if (key == lastMesh)
				return lastSlot;

			for (uint32_t i = 0; i < (uint32_t)m_CullMeshes.size(); i++)
			{
				if (m_CullMeshes[i].MeshRef.get() == key)
				{
					lastMesh = key;
					lastSlot = i;
					return i;
				}
			}

			return kNoCullSlot;
		};

		for (auto& item : meshView)
		{
			auto [transform, mesh] = meshView.Get<TransformComponent, MeshComponent>(item);

			// Null when the handle points at nothing loadable -- a deleted
			// model, or a scene opened without its assets. Skipped rather than
			// substituted, so the gap is visible. Skipped *here* means every
			// view skips it, which is what the five walks each did separately.
			RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
			if (!resolved)
				continue;

			DrawBounds bounds;
			Frustum::TransformBounds(resolved->GetBounds(), transform.World,
									 bounds.Centre, bounds.Extents);

			DrawItem entry;
			entry.Entity = item;
			entry.Transform = &transform;
			entry.Source = &mesh;
			entry.Skinned = resolved->IsSkinned();

			// **And never a blended one.** The GPU path draws its table with the
			// opaque pipeline in the opaque pass; a windscreen in it is a
			// windscreen you cannot see through, and the wheel-blur discs that
			// are meant to vanish become solid plates over the rims. Blended
			// materials take the CPU path, which is where the transparent list
			// is built.
			const RHI::Ref<Material> resolvedMaterial =
				Assets::Manager::GetMaterial(mesh.Material);
			const bool blended = resolvedMaterial
							  && IsBlended(resolvedMaterial->GetBlendMode());
			m_HasBlended = m_HasBlended || blended;
			entry.Blended = blended;

			// **And not a masked one either, for now.**
			//
			// A cull slot is keyed by *mesh*, not by material, so one mesh
			// drawn opaque here and cut out there would share a slot and one
			// of the two would draw through the wrong pipeline. Blended solved
			// that with a second table; masked would want a third, and that is
			// a table, a cull call, a slot base and a draw loop of its own.
			//
			// Until it has one, masked geometry takes the CPU path -- which
			// already sorts it into its own bucket and draws it with its own
			// pipeline, so the picture is right. What it gives up is GPU
			// culling for cutouts specifically, which matters for a scene made
			// of railings and will want the third table then. It is a cost,
			// not a defect, and it is the reason a cutout does not appear in
			// the indirect draw count.
			const bool masked = resolvedMaterial
							 && resolvedMaterial->GetBlendMode() == BlendMode::Masked;
			entry.Masked = masked;

			// **Emissive geometry becomes something the traced bounce can aim
			// at** (7cb). Collected here because this is the one place that
			// already has the world transform, the mesh's bounds and the
			// resolved material at once -- and because it is the draw-list
			// refresh, so it costs nothing on a frame where nothing moved.
			//
			// The emitter is the *flattest rectangle* of the mesh's own
			// bounds: the shortest axis of the box is taken as the normal and
			// the other two as the extent. Exact for a plane, which is what
			// every light fitting in a scene is, and a bounded approximation
			// for anything else.
			if (resolvedMaterial)
			{
				// **The entity's emissive, not its material's.** The override
				// is how a light is dimmed or switched -- the showroom's mode
				// switch dims the luminaire through it and its light switch
				// raises the car's lamps the same way -- and reading the
				// material here made the bounce light the room from a value
				// nothing on screen was emitting. It also put the two halves
				// of the estimator into disagreement, which is worse than
				// either being wrong: the ray-instance walk below resolves the
				// override, so the hemisphere term subtracted the emissive the
				// surface really had while this added the one it used to.
				const Vec3 emissive =
					Vec3(mesh.ResolveParams(resolvedMaterial->GetParams()).EmissiveColor);
				const float strength = Math::Max(emissive.x, Math::Max(emissive.y, emissive.z));

				// **What the surface emits on average, not what its brightest
				// texel emits.** The rectangle below stands in for the whole
				// mesh and radiates uniformly, so the scalar alone is right
				// only for a surface that glows evenly -- and wrong by the
				// ratio of lit to unlit area for one whose emissive map is
				// mostly dark. The showroom's studio ceiling lit four cells of
				// a hundred and forty-four through its texture and lit the
				// room as though all of them were on.
				//
				// Folding the map's mean in makes the emitted *power* right
				// for any map. Where that power comes from on the surface is
				// still wrong -- the whole rectangle radiates a little instead
				// of four cells radiating a lot -- which is the next stage's
				// job (docs/TEXEL-EMITTERS.md).
				//
				// Membership stays on the scalar above, deliberately: a map
				// is LDR, so folding can only ever reduce, and testing the
				// folded value would drop exactly the bright-and-sparse
				// emitter this is for -- the case that most needs a shadow
				// ray aimed at it.
				const Vec3 radiance = emissive * resolvedMaterial->GetEmissiveMean();

				// Above one, not above zero. A surface emitting a fifth of what
				// it reflects is a glow rather than a light source, and putting
				// it in the emitter list spends a shadow ray per pixel on
				// something that changes nothing.
				if (strength > 1.0f)
				{
					const AABB local = resolved->GetBounds();
					const Vec3 halfExtent = (local.Max - local.Min) * 0.5f;
					const Vec3 localCentre = (local.Max + local.Min) * 0.5f;

					// Which axis is the thin one.
					int normalAxis = 0;
					if (halfExtent.y < halfExtent[normalAxis]) normalAxis = 1;
					if (halfExtent.z < halfExtent[normalAxis]) normalAxis = 2;

					const int axisU = (normalAxis + 1) % 3;
					const int axisV = (normalAxis + 2) % 3;

					Vec3 u(0.0f), v(0.0f);
					u[axisU] = halfExtent[axisU];
					v[axisV] = halfExtent[axisV];

					AreaEmitter emitter;
					emitter.Centre = Vec3(transform.World * Vec4(localCentre, 1.0f));
					emitter.TangentU = Vec3(transform.World * Vec4(u, 0.0f));
					emitter.TangentV = Vec3(transform.World * Vec4(v, 0.0f));
					emitter.Radiance = radiance;
					// The handle, so the ray instance built for this same
					// entity below can be told the list answers for it. The
					// registry handle rather than the UUID: it is what both
					// walks already hold, and a handle is only recycled when
					// an entity is destroyed -- which dirties the draw list
					// this walk lives in, so the pairing is rebuilt with it.
					emitter.Owner = (uint64_t)item + 1;

					// **And where on it the light actually is**, when the
					// surface is one whose texture coordinates this can state
					// exactly. The emitter is a rectangle in (su, sv) over
					// [-1,1]; the map is sampled in uv over [0,1]; aiming at a
					// texel needs the affine map between them, and getting it
					// wrong would put light on the wrong part of a surface
					// with nothing in the picture to say so.
					//
					// Only the two flat primitives, and only untransformed
					// coordinates. A modelled fitting keeps its uv in the
					// vertex buffer, which the mesh deliberately does not
					// retain (Mesh.h), and a tiled or offset material samples
					// a window of its map that the whole map's table does not
					// describe. Both fall back to radiating evenly, which is
					// what this did before and is never wrong -- only
					// average.
					const MaterialParams& params = resolvedMaterial->GetParams();
					const bool plainUv = params.UvTransform.x == 1.0f
									  && params.UvTransform.y == 1.0f
									  && params.UvTransform.z == 0.0f
									  && params.UvTransform.w == 0.0f;

					if (plainUv && resolvedMaterial->GetEmissiveStats())
					{
						// Both primitives are one quad whose uv runs 0..1
						// along its own two axes; what differs is which world
						// axis the bounds walk above called U and which V.
						//
						// Plane lies in XZ with u = x + 1/2, v = 1/2 - z, and
						// the walk takes Y as its normal, so U runs along Z
						// and V along X. Quad stands in XY with u = x + 1/2,
						// v = y + 1/2, normal Z, so U runs along X and V
						// along Y. Inverted, that is:
						if (mesh.Mesh == PrimitiveHandle(PrimitiveType::Plane))
						{
							emitter.UvToSurface[0] =  0.0f;   // su from u
							emitter.UvToSurface[1] = -2.0f;   // su from v
							emitter.UvToSurface[2] =  1.0f;
							emitter.UvToSurface[3] =  2.0f;   // sv from u
							emitter.UvToSurface[4] =  0.0f;
							emitter.UvToSurface[5] = -1.0f;
						}
						else if (mesh.Mesh == PrimitiveHandle(PrimitiveType::Quad))
						{
							emitter.UvToSurface[0] =  2.0f;
							emitter.UvToSurface[1] =  0.0f;
							emitter.UvToSurface[2] = -1.0f;
							emitter.UvToSurface[3] =  0.0f;
							emitter.UvToSurface[4] =  2.0f;
							emitter.UvToSurface[5] = -1.0f;
						}

						if (emitter.UvToSurface[0] != 0.0f || emitter.UvToSurface[1] != 0.0f)
						{
							emitter.EmissiveMap = resolvedMaterial->GetEmissiveMap();
							emitter.EmissiveSampler = resolvedMaterial->GetSampler();
							emitter.Emission = resolvedMaterial->GetEmissiveStats();
							// The sampler reads the real texel, so the
							// radiance it multiplies must be the *unfolded*
							// scalar -- folding the mean in as well would
							// count the map twice.
							emitter.Radiance = emissive;
						}
					}

					m_Emitters.push_back(emitter);
				}
			}

			// Static meshes only, and only when there is a pass to read them.
			// A skinned caster is posed from bones the CPU composed this
			// frame, and an indexless mesh has nothing for an indexed draw to
			// draw.
			if (gpuCull && !entry.Skinned && !blended && !masked && resolved->GetIndexCount() >= 3)
			{
				uint32_t slot = slotFor(resolved.get());
				if (slot == kNoCullSlot)
				{
					slot = (uint32_t)m_CullMeshes.size();

					GpuCull::Slot record;
					record.MeshRef = resolved;
					m_CullMeshes.push_back(std::move(record));
					m_CullCounts.push_back(0);

					GpuCull::SlotCommand command;
					command.Draw.IndexCount = resolved->GetIndexCount();
					m_CullSlots.push_back(command);

					lastMesh = resolved.get();
					lastSlot = slot;
				}

				if (buildTable)
				{
					GpuCull::Object object;
					object.Model = transform.World;
					object.Centre = bounds.Centre;
					object.Extents = bounds.Extents;
					object.Slot = slot;
					m_CullObjects.push_back(object);
				}

				m_CullCounts[slot]++;
				entry.CullSlot = slot;
				entry.CullIndex = cullRow++;
			}
			else if (gpuCull && blended && !entry.Skinned && resolved->GetIndexCount() >= 3)
			{
				// The same construction against the blended table. Drawn later,
				// in the transparent pass, with the pipeline that writes
				// accumulate and revealage instead of colour.
				uint32_t slot = blendSlotFor(resolved.get());
				if (slot == kNoCullSlot)
				{
					slot = (uint32_t)m_BlendMeshes.size();

					GpuCull::Slot record;
					record.MeshRef = resolved;
					m_BlendMeshes.push_back(std::move(record));
					m_BlendCounts.push_back(0);

					GpuCull::SlotCommand command;
					command.Draw.IndexCount = resolved->GetIndexCount();
					m_BlendSlots.push_back(command);

					lastBlendMesh = resolved.get();
					lastBlendSlot = slot;
				}

				if (buildTable)
				{
					GpuCull::Object object;
					object.Model = transform.World;
					object.Centre = bounds.Centre;
					object.Extents = bounds.Extents;
					object.Slot = slot;
					m_BlendObjects.push_back(object);
				}

				m_BlendCounts[slot]++;
				entry.BlendSlot = slot;
				entry.BlendIndex = blendRow++;
			}
			else if (gpuCull)
			{
				m_CpuDraws.push_back((uint32_t)m_DrawItems.size());
			}

			entry.Resolved = std::move(resolved);

			m_DrawBounds.push_back(bounds);
			m_DrawItems.push_back(std::move(entry));
		}

		// Where each slot's range begins: the running total of the ones before
		// it, so the ranges together are exactly the object count. Every slot's
		// range is as long as the number of objects that named it, which is why
		// an atomic handing out places inside it cannot overrun however the
		// culling goes -- and why nothing needs sorting.
		m_CullObjectCount = cullRow;

		if (gpuCull && !m_CullSlots.empty())
		{
			uint32_t base = 0;
			for (uint32_t i = 0; i < (uint32_t)m_CullSlots.size(); i++)
			{
				m_CullSlots[i].InstanceBase = base;
				m_CullMeshes[i].InstanceBase = base;
				m_CullMeshes[i].InstanceCount = m_CullCounts[i];
				base += m_CullCounts[i];
			}

			if (!GpuCull::SetObjects(this, m_CullObjects, m_CullSlots))
			{
				// The table did not reach the GPU, so nothing may claim its
				// objects were drawn from it. Every view then takes the walk,
				// which is what the empty list below says.
				for (DrawItem& entry : m_DrawItems)
					entry.CullSlot = kNoCullSlot;
				m_CpuDraws.clear();
				m_CullObjectCount = 0;
			}
		}

		// And the blended table, laid out the same way.
		m_BlendObjectCount = blendRow;

		if (gpuCull && !m_BlendSlots.empty())
		{
			uint32_t base = 0;
			for (uint32_t i = 0; i < (uint32_t)m_BlendSlots.size(); i++)
			{
				m_BlendSlots[i].InstanceBase = base;
				m_BlendMeshes[i].InstanceBase = base;
				m_BlendMeshes[i].InstanceCount = m_BlendCounts[i];
				base += m_BlendCounts[i];
			}

			if (!GpuCull::SetObjects(this, m_BlendObjects, m_BlendSlots,
									 GpuCull::Pass::Blended))
			{
				// Back to the CPU list for the blended ones only. The opaque
				// table is untouched: two tables fail independently, and one
				// falling back must not drag the other with it.
				for (DrawItem& entry : m_DrawItems)
				{
					if (entry.BlendSlot != kNoCullSlot)
					{
						entry.BlendSlot = kNoCullSlot;
						m_CpuDraws.push_back((uint32_t)(&entry - m_DrawItems.data()));
					}
				}
				m_BlendObjectCount = 0;
			}
		}
		else if (gpuCull)
		{
			m_BlendObjectCount = 0;
		}
	}

	void Scene::RenderShadowMaps(const Camera& camera, const Mat4& cameraTransform)
	{
		// The same answer RenderShadows told the lit pass a moment ago.
		const bool traced = ResolveRayTracing(Project::Render());

		if (!Project::Render().ShadowsEnabled)
			return;

		if (!ShadowMap::IsReady())
			return;

		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		UpdateWorldTransforms();
		RefreshDrawList();

		// Nothing to shadow, and a shadow pass over an empty scene is a render
		// pass that clears and stops.
		auto meshView = m_Registry.GetView<TransformComponent, MeshComponent>();
		if (meshView.begin() == meshView.end() && !HasTerrain())
			return;

		const uint32_t localResolution =
			(uint32_t)Math::Clamp(Project::Render().ShadowResolution / 2, 256, 4096);
		const uint32_t pointResolution =
			(uint32_t)Math::Clamp(Project::Render().ShadowResolution / 4, 128, 2048);

		// **What this view kept, when the GPU decided it** (roadmap 8.3),
		// handed from the half of the pass that runs before the render pass
		// opens to the half that runs inside it. Invalid when there is no cull
		// pass, when the table did not reach the device, or on a frame that
		// never built one -- and then every caster is walked here as before.
		GpuCull::View culled;

		ShadowMap::CasterPass casters;

		// A dispatch, so it cannot be inside the render pass. This is the only
		// reason ShadowMap's caster callback has two halves.
		casters.Prepare = [cmd, &culled](const Mat4& viewProjection)
		{
			// Not when meshlets draw the static casters: the indirect path
			// would issue them as vertex-pipeline draws and the meshlet
			// pipeline would never see them. Per-meshlet culling in the mesh
			// stage is what stands in for the compute cull on these views.
			if (Renderer3D::ShadowMeshletsActive())
				return;
			culled = GpuCull::Cull(*cmd, viewProjection);
		};

		casters.Draw = [this, &culled](const Mat4& viewProjection)
		{
			// Against this pass's own frustum, not the camera's. A cascade sees
			// a different volume from the viewer, and culling it against the
			// camera would drop exactly the casters standing outside the view
			// whose shadows fall inside it.
			const Frustum frustum(viewProjection);

			Renderer3D::BeginShadow(viewProjection);

			// **The frustum test alone, over the flat array.** Everything this
			// loop used to do first -- the view's component fetch, the mesh
			// lookup, the bounds transform -- does not depend on which cascade
			// is asking, and RefreshDrawList did it once for all of them.
			auto submit = [this, &frustum](size_t index)
			{
				const DrawBounds& bounds = m_DrawBounds[index];
				if (!frustum.Intersects(bounds.Centre, bounds.Extents))
				{
					Renderer3D::CountCulled();
					return;
				}

				// Only what survives reads the item, which is the whole reason
				// the two arrays are separate.
				const DrawItem& entry = m_DrawItems[index];

				// **Glass casts no shadow**, and this is not a shortcut. A
				// windscreen that shadows like a wall puts the cabin in the
				// dark, and a lamp lens that does it makes the lamp a black
				// hole -- which is exactly what it did. A blended surface
				// transmits most of what reaches it, and the honest choice
				// between "all of the light" and "none of it" is all.
				if (entry.Blended)
					return;

				const Mat4& world = entry.Transform->World;

				// The same pose the lit pass will be given. Without this a
				// skinned figure walks and its shadow stands still in the bind
				// pose -- which reads as a shadow bug rather than a skinning
				// one, and is why the depth pass has a skinned shader at all.
				if (entry.Skinned)
				{
					const auto* animator = m_Registry.TryGet<AnimatorComponent>(entry.Entity);

					if (animator && !animator->Skinning.empty())
					{
						Renderer3D::DrawSkinnedMeshShadow(entry.Resolved, world,
														  animator->Skinning);
					}
					else if (const Skeleton* skeleton =
								 Assets::Manager::GetSkeleton(entry.Source->Mesh))
					{
						const std::vector<Mat4> bind(skeleton->Size(), Mat4(1.0f));
						Renderer3D::DrawSkinnedMeshShadow(entry.Resolved, world, bind);
					}

					return;
				}

				// **A cutout casts the shadow its alpha describes.** The
				// material goes with the caster only when it is masked --
				// Renderer3D decides that, so this does not have to -- and it
				// is what lets the depth pass test the same alpha the lit pass
				// will. Without it a railing threw the shadow of a solid
				// sheet, which is a louder wrong than throwing none.
				Renderer3D::DrawMeshShadow(entry.Resolved, world,
										   entry.Masked
											   ? Assets::Manager::GetMaterial(
													 entry.Source->Material)
											   : nullptr);
			};

			if (culled.IsValid())
			{
				// One draw per distinct mesh, each one's instance count read
				// out of device memory. The CPU never learns how many
				// survived, which is the point: learning it is a read back
				// across the bus and a stall to make it meaningful.
				Renderer3D::DrawShadowIndirect(culled, m_CullMeshes);

				// Everything the table does not hold -- the skinned casters,
				// and anything with too few indices to draw indexed. A short
				// list, walked in full.
				for (uint32_t index : m_CpuDraws)
					submit(index);
			}
			else
			{
				const size_t drawCount = m_DrawBounds.size();
				for (size_t index = 0; index < drawCount; index++)
					submit(index);
			}

			// The terrain's chunks at their chosen level, against this pass's
			// frustum like every other caster (7ap).
			ForEachTerrainChunk([&](Entity, TransformComponent& transform, TerrainComponent&,
									Terrain&, Terrain::Chunk& chunk)
			{
				const RHI::Ref<Mesh>& mesh = chunk.Selected();
				if (!mesh)
					return;
				Vec3 centre, extents;
				Frustum::TransformBounds(chunk.Bounds, transform.World, centre, extents);
				if (!frustum.Intersects(centre, extents))
				{
					Renderer3D::CountCulled();
					return;
				}
				Renderer3D::DrawMeshShadow(mesh, transform.World);
			});

			Renderer3D::EndShadow();
		};

		const bool flip = Renderer::GetDevice().GetBackend() == RHI::Backend::Vulkan;

		// Clip space to lookup coordinates, with the vertical flip one backend
		// needs. The same construction the cascades use.
		Mat4 bias(1.0f);
		bias[0][0] = 0.5f;
		bias[1][1] = flip ? -0.5f : 0.5f;
		bias[3][0] = 0.5f;
		bias[3][1] = 0.5f;

		// The first directional light that asks gets cascades. A second set is
		// four more scene renders for a light that is a fill in every scene
		// this is likely to see.
		int index = 0;
		int casterIndex = -1;
		bool anyCaster = false;
		uint32_t spotSlot = 0;
		uint32_t pointSlot = 0;
		Vec3 direction(0.0f, -1.0f, 0.0f);

		auto lightView = m_Registry.GetView<TransformComponent, LightComponent>();
		for (auto& item : lightView)
		{
			auto [transform, light] = lightView.Get<TransformComponent, LightComponent>(item);
			const int lightIndex = index++;

			if (!light.Light.CastShadows)
				continue;

			if (traced)
			{
				// Every casting light, whatever its kind and however many
				// there are (7an): the shader traces toward it, and there is
				// no map to run out of. The kind still travels, so the
				// shader knows whether to trace to infinity or to a point.
				LocalShadow assigned;
				switch (light.Light.Type)
				{
					case Light::LightType::Directional: assigned.Type = LocalShadow::Kind::Cascades; break;
					case Light::LightType::Spot:        assigned.Type = LocalShadow::Kind::Spot; break;
					case Light::LightType::Point:       assigned.Type = LocalShadow::Kind::Point; break;
				}
				ShadowMap::Assign((uint32_t)lightIndex, assigned);
				anyCaster = true;
				continue;
			}

			const Vec3 position = Vec3(transform.World[3]);
			const Vec3 forward = Math::Normalize(
				Vec3(transform.World * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));

			switch (light.Light.Type)
			{
				case Light::LightType::Directional:
				{
					if (casterIndex >= 0)
					{
						if (!m_ShadowBudgetWarned)
						{
							RV_CORE_WARN("More than one directional light asks to cast; only "
										 "the first gets cascades. The rest light but do not "
										 "shadow.");
							m_ShadowBudgetWarned = true;
						}
						break;
					}

					casterIndex = lightIndex;
					direction = forward;

					LocalShadow assigned;
					assigned.Type = LocalShadow::Kind::Cascades;
					ShadowMap::Assign((uint32_t)lightIndex, assigned);
					break;
				}

				case Light::LightType::Spot:
				{
					if (spotSlot >= ShadowMap::kMaxLocal)
					{
						if (!m_ShadowBudgetWarned)
						{
							RV_CORE_WARN("More than {0} spot lights ask to cast; the rest "
										 "light but do not shadow.", ShadowMap::kMaxLocal);
							m_ShadowBudgetWarned = true;
						}
						break;
					}

					// The light's own cone, widened a little: the outer angle is
					// where the light reaches zero, and a shadow that ends
					// exactly there has a hard edge at the cone's rim.
					const float fov = Math::Radians(
						Math::Clamp(light.Light.OuterCone * 2.2f, 10.0f, 170.0f));
					const float reach = Math::Max(light.Light.Range, 0.5f);

					const Vec3 up = Math::Abs(forward.y) > 0.99f
									   ? Vec3(0.0f, 0.0f, 1.0f)
									   : Vec3(0.0f, 1.0f, 0.0f);

					const Mat4 view = Math::LookAt(position, position + forward, up);
					const Mat4 projection =
						Math::Perspective(fov, 1.0f, kPointShadowNear, reach);

					LocalShadow assigned;
					assigned.Type = LocalShadow::Kind::Spot;
					assigned.Slot = (int)spotSlot;
					assigned.LookupMatrix = bias * projection * view;
					assigned.FarClip = reach;
					assigned.TexelScale = 2.0f * Math::Tan(fov * 0.5f) / (float)localResolution;

					ShadowMap::Assign((uint32_t)lightIndex, assigned);
					ShadowMap::RenderSpot(*cmd, spotSlot, localResolution,
										  projection * view, casters);
					spotSlot++;
					break;
				}

				case Light::LightType::Point:
				{
					if (pointSlot >= ShadowMap::kMaxLocal)
					{
						if (!m_ShadowBudgetWarned)
						{
							RV_CORE_WARN("More than {0} point lights ask to cast; the rest "
										 "light but do not shadow.", ShadowMap::kMaxLocal);
							m_ShadowBudgetWarned = true;
						}
						break;
					}

					const float reach = Math::Max(light.Light.Range, 0.5f);

					LocalShadow assigned;
					assigned.Type = LocalShadow::Kind::Point;
					assigned.Slot = (int)pointSlot;
					assigned.FarClip = reach;
					// A face is 90 degrees, so tan(45) is one and a texel is
					// two units of distance divided by the resolution.
					assigned.TexelScale = 2.0f / (float)pointResolution;

					ShadowMap::Assign((uint32_t)lightIndex, assigned);
					ShadowMap::RenderPoint(*cmd, pointSlot, pointResolution,
										   position, reach, casters);
					pointSlot++;
					break;
				}
			}
		}

		if (traced)
		{
			// The whole scene, once, into this frame's acceleration structure:
			// every mesh with its world transform, and a skinned one with its
			// pose when it has one -- RayShadows poses it in compute and
			// refits a structure of its own (7an); without a pose it traces
			// as the bind pose the mesh caches. No frustum: a caster outside
			// the view still shadows what is inside it, and a ray does not
			// have a frustum. Building is not permitted inside a render pass,
			// and this runs before the graph, so it is recorded here rather
			// than in the scene pass that reads it.
			//
			// Built whether or not any light casts (7ao): reflections and
			// occlusion trace into the same structure, and a scene lit by
			// nothing that casts a shadow can still be reflected in.
			// The material and the entity's scalars ride beside each
			// instance so a reflected hit can be shaded (7ao); the same
			// resolution the lit pass makes, so a hit and a draw of one
			// surface cannot disagree.
			(void)anyCaster;
			RayShadows::ClearInstances();
			for (auto& item : meshView)
			{
				auto [transform, mesh] = meshView.Get<TransformComponent, MeshComponent>(item);
				RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
				if (!resolved)
					continue;

				const std::vector<Mat4>* bones = nullptr;
				if (resolved->IsSkinned())
				{
					const auto* animator = m_Registry.TryGet<AnimatorComponent>(item);
					if (animator && !animator->Skinning.empty())
						bones = &animator->Skinning;
				}

				RHI::Ref<Material> material = Assets::Manager::GetMaterial(mesh.Material);
				if (!material)
					material = Renderer3D::GetDefaultMaterial();

				// **And not into the acceleration structure either**, which is
				// where this actually showed. Every ray in the engine treats
				// the structure as opaque -- `gl_RayFlagsOpaqueEXT` -- so a
				// blended mesh in it stops a shadow ray, a bounce ray and a
				// reflection ray alike.
				//
				// The symptom was a headlamp that was a black bowl on Vulkan
				// and lit on OpenGL, which reads as a backend difference and is
				// nothing of the kind: OpenGL has no ray queries, so it fell
				// back to shadow maps, whose resolution let light leak into the
				// cavity that the exact test correctly refused. The cabin
				// behind the windscreen was the same bug and was reported much
				// earlier as looking hollow.
				//
				// What is given up is a reflection seeing the glass -- a car
				// reflected in the floor loses its windows. That is a smaller
				// wrong than a car with no interior.
				if (!TracedAsGeometry(material->GetBlendMode()))
					continue;

				const MaterialParams params =
					mesh.ResolveParams(material ? material->GetParams() : MaterialParams{});

				RayShadows::AddInstance(resolved, transform.World, bones, material, params,
										(uint64_t)item + 1);
			}

			// The terrain, every chunk, no frustum: a hill outside the view
			// still shadows what is inside it (7ap).
			//
			// ForRays, not Selected: this list is rebuilt by every view that
			// draws the scene, and only the first of them actually builds the
			// structure -- the rest find it built and keep their own list. A
			// camera-dependent level therefore hands the traced passes a list
			// of instances the structure does not contain, in an order it does
			// not share, and a hit resolved through it reads whatever is at
			// that index. That is the editor's device loss (7bp).
			ForEachTerrainChunk([&](Entity, TransformComponent& transform, TerrainComponent& component,
									Terrain&, Terrain::Chunk& chunk)
			{
				const RHI::Ref<Mesh>& mesh = chunk.ForRays();
				if (!mesh)
					return;
				RHI::Ref<Material> material = Assets::Manager::GetMaterial(component.Material);
				if (!material)
					material = Renderer3D::GetDefaultMaterial();
				RayShadows::AddInstance(mesh, transform.World, nullptr, material,
										material ? material->GetParams() : MaterialParams{});
			});

			RayShadows::Build(*cmd);
			return;
		}

		if (casterIndex < 0)
			return;

		const uint32_t count = (uint32_t)Math::Clamp(Project::Render().ShadowCascades, 1,
													(int)ShadowMap::kMaxCascades);
		const uint32_t resolution = (uint32_t)Math::Clamp(Project::Render().ShadowResolution, 256, 8192);

		// Aspect and field of view come from the projection rather than being
		// passed alongside it: an editor camera and a scene camera describe
		// theirs differently and the matrix is what actually gets used.
		const Mat4& projection = camera.GetProjection();
		const float fovY = 2.0f * Math::Atan(1.0f / Math::Max(projection[1][1], 1e-4f));
		const float aspect = Math::Max(projection[1][1] / Math::Max(projection[0][0], 1e-4f), 1e-4f);

		ShadowCascade cascades[ShadowMap::kMaxCascades];
		ShadowMap::ComputeCascades(cameraTransform, fovY, aspect,
								   0.1f, Project::Render().ShadowDistance,
								   direction, count, resolution,
								   Project::Render().ShadowSplitLambda,
								   flip, cascades);

		ShadowMap::SetLightIndex(casterIndex);
		ShadowMap::Render(*cmd, cascades, count, resolution, casters);
	}

	LightList Scene::CollectLights()
	{
		auto lightView = m_Registry.GetView<TransformComponent, LightComponent>();
		LightList lights;

		for (auto& item : lightView)
		{
			auto [transform, light] = lightView.Get<TransformComponent, LightComponent>(item);

			LightRenderData data;
			data.Position = Vec3(transform.World[3]);
			// A light's forward axis is -Z, matching the camera convention.
			// **Unit length, and the lit shaders rely on it.** pbr_fragment
			// takes -Direction as L for a directional light and as the spot
			// cone's axis without normalising either, in both the raster loop
			// and TraceSurface. A non-unit direction here would not look
			// wrong so much as slightly wrong everywhere -- so if this ever
			// stops normalising, those four uses have to grow it back.
			data.Direction = Math::Normalize(Vec3(transform.World * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
			data.Color = light.Light.Color;
			data.Intensity = light.Light.Intensity;
			data.Range = light.Light.Range;
			data.InnerCone = light.Light.InnerCone;
			data.OuterCone = light.Light.OuterCone;
			data.Type = light.Light.Type;
			data.CastShadows = light.Light.CastShadows;
			data.IsBaked = light.Light.IsBaked;

			lights.push_back(data);
		}
		return lights;
	}

	void Scene::UpdateVoxelGI(const Mat4& cameraTransform)
	{
		// Rays first, then voxels, then the screen (7bc): where the traced
		// form runs the grid is not built, and where neither runs a grid left
		// from before must not be read.
		//
		// 8.13 relaxed this for a frame's worth of history -- the hybrid wanted
		// a grid while rays ran -- and it is back, because that gather cost
		// +55.80 ms against the second traced ray's +2.57 (7be). The two forms
		// are exclusive, which is what every other pairing in this renderer
		// already says. The other consequence of this clause is worth knowing
		// before relaxing it again: while it holds, nothing about the voxel
		// path can be measured under ray tracing, which is how a session of
		// probes measured the traced form believing they measured voxels
		// (7bc's fifth finding).
		// The profile's, since 10.6 (7bg): the voxel form and its dials live
		// beside the GI switch they answer for. One answer per frame, because
		// `GetPostSettings` resolves the *primary camera's* profile -- the
		// same one the graph is handed as `desc.Post`, so the grid this builds
		// and the gather that reads it cannot disagree about its size.
		// **And not at all under a bake that can be honoured**, which is the
		// same exclusion rays get one line down and was missing here: the
		// graph drops the whole indirect chain for a Baked source, so the
		// grid this builds is voxelised, lit and then read by nobody. It is
		// not a small waste -- voxelisation walks every static mesh in the
		// cascades and lights the result -- and it was invisible because the
		// frame still looked right. Safe to ask here: this runs at the end of
		// RenderShadows, after the same function set the flag.
		const PostSettings post = GetPostSettings();
		const bool voxel = ResolveVoxelGlobalIllumination(post)
						&& ResolveRayTracedGlobalIllumination(Project::Render()) == RayDetail::Off
						&& !Renderer3D::IsBakedIrradianceOnly();
		if (!voxel || !VoxelGI::IsReady())
		{
			VoxelGI::Invalidate();
			return;
		}

		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
		{
			VoxelGI::Invalidate();
			return;
		}

		UpdateWorldTransforms();

		VoxelGiSettings settings;
		settings.Resolution = post.VoxelGiResolution;
		settings.Cascades = post.VoxelGiCascades;
		settings.VoxelSize = post.VoxelGiVoxelSize;
		settings.Bounces = ResolveGiBounces(post);
		settings.ShadowNormalOffset = Project::Render().ShadowNormalOffset;

		auto meshView = m_Registry.GetView<TransformComponent, MeshComponent>();

		// The walk: every static mesh whose world box overlaps the outermost
		// cascade, with its material and parameters resolved the way the
		// ray-instance walk resolves them. Skinned meshes are not submitted
		// (7bc): voxelised in the bind pose they would be wrong rather than
		// missing.
		VoxelGI::Update(*cmd, settings, Vec3(cameraTransform[3]), CollectLights(),
			[&](const Vec3& boxMin, const Vec3& boxMax)
			{
				auto overlaps = [&](const Vec3& centre, const Vec3& extents)
				{
					return centre.x + extents.x >= boxMin.x && centre.x - extents.x <= boxMax.x
						&& centre.y + extents.y >= boxMin.y && centre.y - extents.y <= boxMax.y
						&& centre.z + extents.z >= boxMin.z && centre.z - extents.z <= boxMax.z;
				};

				for (auto& item : meshView)
				{
					auto [transform, mesh] = meshView.Get<TransformComponent, MeshComponent>(item);
					RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
					if (!resolved || resolved->IsSkinned())
						continue;

					Vec3 centre, extents;
					Frustum::TransformBounds(resolved->GetBounds(), transform.World, centre, extents);
					if (!overlaps(centre, extents))
						continue;

					RHI::Ref<Material> material = Assets::Manager::GetMaterial(mesh.Material);
					if (!material)
						material = Renderer3D::GetDefaultMaterial();

					// **Glass casts no shadow, and it bounces none either.**
					// The shadow walk's rule, mirrored: a blended surface
					// voxelised as a solid caster blocks the bounce through
					// exactly the windscreen the shadow rule keeps direct
					// light passing through, so the cabin would be lit
					// directly and dark indirectly. The honest choice between
					// "all of the light" and "none of it" is all, in both
					// systems, for the same reason.
					if (material && !TracedAsGeometry(material->GetBlendMode()))
						continue;

					const MaterialParams params =
						mesh.ResolveParams(material ? material->GetParams() : MaterialParams{});
					VoxelGI::Submit(resolved, transform.World, material, params);
				}

				// The terrain's chunks at their chosen level, with layer 0's
				// material -- the choice the ray-instance walk makes (7ao).
				ForEachTerrainChunk([&](Entity, TransformComponent& transform, TerrainComponent& component,
										Terrain&, Terrain::Chunk& chunk)
				{
					const RHI::Ref<Mesh>& chunkMesh = chunk.Selected();
					if (!chunkMesh)
						return;
					Vec3 centre, extents;
					Frustum::TransformBounds(chunk.Bounds, transform.World, centre, extents);
					if (!overlaps(centre, extents))
						return;
					RHI::Ref<Material> material = Assets::Manager::GetMaterial(component.Material);
					if (!material)
						material = Renderer3D::GetDefaultMaterial();
					VoxelGI::Submit(chunkMesh, transform.World, material,
									material ? material->GetParams() : MaterialParams{});
				});
			});
	}

	void Scene::CaptureReflectionProbes()
	{
		// Re-entrant only in the sense that must never happen: a capture draws
		// the scene, and drawing the scene must not start another capture.
		if (m_CapturingProbes || !Renderer::HasDevice())
			return;

		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		auto view = m_Registry.GetView<TransformComponent, ReflectionProbeComponent>();
		if (view.begin() == view.end())
		{
			// Not merely nothing to capture: the selection table has to be
			// emptied too, or the frame after the last probe is deleted still
			// points objects at a slot whose contents are gone.
			m_ProbeSlots.clear();
			return;
		}

		// The probes are placed by transforms, and a probe parented to
		// something that moved this frame is at last frame's position until
		// this runs.
		//
		// **Only if one of them is going to capture.** UpdateWorldTransforms
		// raises m_DrawListDirty unconditionally, and a raised flag costs the
		// next RefreshDrawList a full walk of every mesh in the scene -- the
		// resolve, the material lookup, the cull tables, the emitter list. A
		// scene whose probes are all baked and complete skips every one of
		// them a line later, so this was paying for that walk, every frame,
		// to place probes that were not going to move.
		//
		// PackProbes below still runs either way -- it rebuilds the selection
		// table and is what keeps a baked probe readable -- so this is a guard
		// on the transform walk alone, not an early exit.
		// **What a capture is only valid for.** Compared before anything else
		// decides whether to skip, because the answer to "does this probe need
		// re-capturing" depends on it.
		//
		// Nothing used to raise Dirty at all, so a probe was frozen after its
		// first capture for the life of the process. These are the
		// invalidations that are cheap and certain: the probe moved, its reach
		// or its clips changed, the sky changed, or someone asked. Moving
		// geometry and changing lights are deliberately not detected -- finding
		// those precisely costs more than it saves -- and Recapture is the
		// verb for them.
		// The sky, as one number. A byte hash rather than a field-by-field
		// compare because SceneEnvironment is plain data and every field of it
		// changes what a capture sees -- a colour, an intensity, a rotation,
		// the handle of an HDR map. Padding rides along, which can only ever
		// cost one unnecessary capture and never miss a real one.
		const uint64_t environment = [this]()
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(&m_Environment);
			uint64_t hash = 1469598103934665603ull;          // FNV-1a
			for (size_t i = 0; i < sizeof(m_Environment); i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash;
		}();
		for (auto& item : view)
		{
			auto [transform, probe] = view.Get<TransformComponent, ReflectionProbeComponent>(item);
			const Vec3 position = Vec3(transform.World[3]);

			if (probe.Recapture
				|| probe.CapturedInfluence != probe.Influence
				|| probe.CapturedNear != probe.NearClip
				|| probe.CapturedFar != probe.FarClip
				|| probe.CapturedEnvironment != environment
				|| Math::Distance(position, probe.CapturedAt) > 1.0e-4f)
			{
				probe.Dirty = true;
				probe.Recapture = false;
			}
		}

		bool anyCapturing = false;
		for (auto& item : view)
		{
			auto [transform, probe] = view.Get<TransformComponent, ReflectionProbeComponent>(item);
			if (probe.Update == ProbeUpdate::Realtime || !probe.Probe
				|| !probe.Probe->IsComplete() || probe.Dirty)
			{
				anyCapturing = true;
				break;
			}
		}

		if (anyCapturing)
			UpdateWorldTransforms();

		m_CapturingProbes = true;

		for (auto& item : view)
		{
			auto [transform, probe] = view.Get<TransformComponent, ReflectionProbeComponent>(item);

			const bool realtime = probe.Update == ProbeUpdate::Realtime;
			const bool captured = probe.Probe && probe.Probe->IsComplete();

			// **A finished cube that has never been stored is work a bake
			// still owes**, even though there is nothing left to capture.
			//
			// A probe put to Realtime for a few frames and then back to Cached
			// -- which is exactly how a script re-captures a room after
			// changing its lighting -- comes out of that holding a good cube,
			// not dirty, and never written: realtime probes are deliberately
			// never stored, and by the time it is storable the test below has
			// already decided there is nothing to do. So the bake sat waiting
			// for a probe that had finished long before it started.
			const bool storeable = captured && !realtime && !probe.Baked
								&& !m_SourcePath.empty() && BakingLighting();

			// **And a stored cube is worth one look, on the frame the lighting
			// hash exists.**
			//
			// The hash names the file, and it is computed in the scene walk --
			// which runs *after* this, later in the same frame. So on the first
			// frame of a scene there is no hash yet and no file to look for;
			// the probe captures as it always did, and this asks on the next
			// frame, by which time the walk has run. `AdoptChecked` is what
			// makes it one look rather than a reason to fall through this test
			// every frame for the rest of the scene.
			const bool adoptable = !realtime && !probe.AdoptChecked && !probe.Baked
								&& m_FieldEvaluated && !ForcingBake()
								&& !m_SourcePath.empty();

			if (!realtime && captured && !probe.Dirty && !storeable && !adoptable)
				continue;

			// **The cube first, because a stored one needs somewhere to land.**
			//
			// This used to sit below the adopt, which made the adopt dead code
			// and nothing said so. On the frame a scene loads there is no probe
			// object yet, so the adopt's own `probe.Probe &&` guard declined;
			// the probe was then built and captured all six faces; and on every
			// frame after that the "already captured" test above returned
			// before the adopt could be reached a second time. So a baked probe
			// was written on every bake run and read on none of them -- the
			// scene paid six cube faces at load exactly as it had before
			// baking existed, and the file on disk was ballast.
			//
			// It fails silently in the direction that looks like success: the
			// reflections are right, because a capture of the same room is what
			// the file holds.
			const uint32_t resolution = (uint32_t)Math::Clamp(probe.Resolution, 16, 1024);
			if (!probe.Probe || probe.Probe->GetFaceSize() != resolution ||
				probe.Probe->GetSamples() != Renderer::GetTargetSamples())
			{
				probe.Probe = std::make_shared<ReflectionProbe>(Renderer::GetDevice(), resolution);
				probe.NextFace = 0;
			}

			// **A stored cube, if one was baked for this probe.**
			//
			// Only for a probe that is not realtime: one that re-captures every
			// frame is asking for what the scene looks like *now*, and handing
			// it a file would be answering a different question. For the rest
			// this is the whole point of baking -- a scene with fifteen probes
			// renders ninety cube faces before its first frame otherwise.
			//
			// The stamp carries where the probe is and what it was captured
			// under, so a moved probe or changed lighting falls through to the
			// capture below exactly as it would have without a file.
			//
			// **Not when a bake was asked for by hand**, on the same rule the
			// field's load follows: a press means "make this one again", and
			// adopting the file it is about to replace would make the button
			// do nothing.
			if (!realtime && !ForcingBake() && !m_SourcePath.empty() && probe.Probe && cmd
				&& m_FieldEvaluated)
			{
				probe.AdoptChecked = true;
				BakedLighting::Stamp wanted;
				wanted.Centre = Vec3(transform.World[3]);
				wanted.Extents = Vec3(probe.Influence);
				wanted.Width = probe.Probe->GetFaceSize();
				wanted.Height = probe.Probe->GetFaceSize();
				wanted.Depth = CubeFaces::kFaceCount;
				wanted.Tiles = 1;
				wanted.Lighting = environment;

				const std::filesystem::path file =
					ProbeBakePath(Entity{ item, this }.GetUUID());

				BakedLighting::Stamp stored;
				std::vector<uint8_t> payload;
				if (!probe.Baked
					&& BakedLighting::Read(file, BakedLighting::Kind::ReflectionProbe,
										   stored, payload)
					&& stored.Matches(wanted)
					&& probe.Probe->Adopt(*cmd, payload))
				{
					probe.Baked = true;
					probe.Dirty = false;
					probe.CapturedAt = wanted.Centre;
					probe.CapturedInfluence = probe.Influence;
					probe.CapturedNear = probe.NearClip;
					probe.CapturedFar = probe.FarClip;
					probe.CapturedEnvironment = environment;
					continue;
				}
			}

			// A realtime probe at a Hz rate takes its next capture step only
			// when the interval has elapsed. Gated only once the probe is
			// complete -- the first capture is always immediate, because an
			// incomplete cube is not usable at any rate -- and the skipped
			// frames are the saving: a reflection seen through a rough or
			// curved surface can lag the scene by a few frames without anyone
			// noticing, and per-frame capture is where a realtime probe's cost
			// and its frame spikes both come from.
			if (realtime && captured && probe.Rate != ProbeRate::PerFrame)
			{
				static constexpr float kIntervals[] = {
					1.0f / 15.0f, 1.0f / 30.0f, 1.0f / 45.0f, 1.0f / 60.0f,
				};
				const uint32_t rate = Math::Min((uint32_t)probe.Rate, 3u);
				const float interval = kIntervals[rate];

				probe.RateAccumulator += m_FrameDelta;
				if (probe.RateAccumulator < interval)
					continue;

				// fmod rather than -= interval: a long hitch owes one step,
				// not a burst of catch-up captures.
				probe.RateAccumulator = Math::FMod(probe.RateAccumulator, interval);
			}

			// The first capture always does all six. Spreading it would only
			// delay the moment anything can reflect at all, since an incomplete
			// cube is not usable.
			const uint32_t faces = probe.Probe->IsComplete()
								 ? (uint32_t)Math::Clamp(probe.FacesPerFrame, 1, 6)
								 : 6u;

			probe.NextFace = probe.Probe->CaptureFaces(
				*cmd, Vec3(transform.World[3]), probe.NearClip, probe.FarClip,
				probe.NextFace, faces,
				[this](const Camera& faceCamera, const Mat4& faceTransform)
				{
					OnRender(faceCamera, faceTransform);
				});

			probe.Dirty = false;

			// **And write it out, if anybody asked and it is finished.**
			//
			// Realtime probes are never written: one that re-captures every
			// frame is asking what the scene looks like now, and a file cannot
			// answer that. The rest are exactly what baking is for -- fifteen
			// probes is ninety cube faces of scene render before the first
			// frame otherwise.
			// **Not before the field it is photographing exists.**
			//
			// A probe capture renders the scene, and the scene reads the
			// irradiance field -- so a cube stored while that field is still
			// the zeros it was created with is a photograph of a room with no
			// bounced light in it. The two are mutually dependent, because the
			// field's own solve traces against the probe, so a bake has to take
			// them in order: capture, solve, capture again, store.
			//
			// It failed silently and looked like something else entirely. The
			// stored cube was written a minute before the field finished
			// solving; the editor adopted it and drew the showroom seven levels
			// too dark, and hitting Play re-captured the probe and the room
			// jumped. Which reads as "Play changes the lighting", not as "the
			// bake wrote its two halves in the wrong order".
			const bool fieldSettledForProbe =
				m_FieldEvaluated &&
				(m_FieldVolumes == 0
				 || (m_FieldBaked && m_FieldBakedAlt && !m_FieldSolveWanted
					 && !Renderer3D::HasPendingIrradianceSolve()));

			if (BakingLighting() && !probe.Baked && !m_SourcePath.empty()
				&& !realtime && probe.Probe->IsComplete() && fieldSettledForProbe)
			{
				BakedLighting::Stamp stamp;
				stamp.Centre = Vec3(transform.World[3]);
				stamp.Extents = Vec3(probe.Influence);
				stamp.Width = probe.Probe->GetFaceSize();
				stamp.Height = probe.Probe->GetFaceSize();
				stamp.Depth = CubeFaces::kFaceCount;
				stamp.Tiles = 1;
				stamp.Lighting = environment;

				const std::filesystem::path file =
					ProbeBakePath(Entity{ item, this }.GetUUID());

				if (BakedLighting::Write(Renderer::GetDevice(), file,
										 BakedLighting::Kind::ReflectionProbe,
										 stamp, probe.Probe->GetCube()))
				{
					probe.Baked = true;
				}
			}

			// And what it was captured under, so the next frame can tell
			// whether it still holds.
			probe.CapturedAt = Vec3(transform.World[3]);
			probe.CapturedInfluence = probe.Influence;
			probe.CapturedNear = probe.NearClip;
			probe.CapturedFar = probe.FarClip;
			probe.CapturedEnvironment = environment;

		}

		m_CapturingProbes = false;

		PackProbes(*cmd);
	}

	void Scene::UpdateIrradianceVolumes(const LightList& lights)
	{
		// **`--bake=force`, which is the Bake button by another route.**
		//
		// Asked once per scene, here rather than at load, because a request
		// needs the volumes and probes to exist and this is the first place
		// that is guaranteed. Not during a probe capture: the request dirties
		// every probe, and dirtying one from inside its own capture is a loop.
		if (EngineConfig::Get().ForceLightingBake && !m_ForcedBakeAsked
			&& !m_CapturingProbes && !m_SourcePath.empty())
		{
			m_ForcedBakeAsked = true;
			RequestLightingBake();
		}

		// **What the field was solved under.** A stored answer to a lighting
		// question stops being true when the lighting changes, and unlike a
		// moved box nothing about the volume itself says so. The showroom makes
		// it concrete: two lighting modes on a switch, and a field solved under
		// one and kept under the other lights the room from a scene that is no
		// longer there.
		//
		// The lights and the environment, as one number. Both are plain data,
		// so a byte hash catches every field of them -- an intensity, a colour,
		// a cone angle, a light appearing or disappearing -- without anyone
		// having to list which ones matter.
		//
		// It does not catch a moved wall or an edited material, and that is the
		// same line the reflection probe draws: those cost more to detect than
		// they save, and Recapture is the verb for them.
		// **Field by field, and never the bytes between them.**
		//
		// This hashed whole structs -- `mix(lights.data(), count * sizeof(...))`
		// -- which is the same idea and a different function, because a struct
		// is not only its fields. `LightRenderData` has three floats after a
		// bool and an enum, so it carries padding, and padding is whatever the
		// allocation happened to contain. MSVC fills it with 0xCD in a debug
		// build and with nothing in particular in a release one.
		//
		// So the same scene, unchanged, hashed three different ways: the
		// runtime, the debug editor and the release editor each asked for a
		// different bake and each fell back to Realtime saying nothing was on
		// disk. A bake keyed on a value that depends on the *build* is a bake
		// nothing can ever load, and it fails in the direction that looks like
		// a missing file rather than a broken key.
		//
		// Named fields hash the lighting and nothing else, so a file baked by
		// any build loads in every other one.
		uint64_t lighting = 1469598103934665603ull;
		auto mixBytes = [&lighting](const void* data, size_t size)
		{
			const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
			{
				lighting ^= bytes[i];
				lighting *= 1099511628211ull;
			}
		};
		auto mixFloat = [&mixBytes](float value)
		{
			// Through the bit pattern, so 0.1f hashes as 0.1f everywhere, and
			// a negative zero as itself rather than as a positive one.
			mixBytes(&value, sizeof(value));
		};
		auto mixVec = [&mixFloat](const Vec3& v)
		{
			mixFloat(v.x);
			mixFloat(v.y);
			mixFloat(v.z);
		};
		auto mixUint = [&mixBytes](uint64_t value) { mixBytes(&value, sizeof(value)); };

		for (const LightRenderData& light : lights)
		{
			// **A realtime light is not part of the lighting.** It renders as
			// it always did, but the bake neither stores its bounce nor keys a
			// file on it -- so a script flipping a headlamp on and off keeps
			// reading the same .rvfield instead of demanding a bake per
			// combination of switches. Skipped wholesale: a light the solve
			// cannot see must not be able to rename the file either.
			if (!light.IsBaked)
				continue;

			mixVec(light.Position);
			mixVec(light.Direction);
			mixVec(light.Color);
			mixFloat(light.Intensity);
			mixFloat(light.Range);
			mixFloat(light.InnerCone);
			mixFloat(light.OuterCone);
			mixUint((uint64_t)light.Type);
			mixUint(light.CastShadows ? 1u : 0u);
		}

		mixVec(m_Environment.AmbientColor);
		mixFloat(m_Environment.AmbientIntensity);
		mixUint((uint64_t)m_Environment.Sky);
		mixVec(m_Environment.SkyHorizon);
		mixVec(m_Environment.SkyZenith);
		mixVec(m_Environment.SkyGround);
		mixFloat(m_Environment.SkyIntensity);
		mixFloat(m_Environment.SkyRotation);
		mixUint((uint64_t)m_Environment.SkyTexture);

		// **Every volume in the scene, composed into one field.**
		//
		// A shader reads one field: one texture, one box, one lookup, which is
		// what keeps the runtime cost of the whole feature at a fetch. Authors
		// want more than one volume -- a coarse box over a level and a tight
		// one in the room that needs it -- and the way to have both is to
		// compose at *bake* time rather than choose at *shade* time. It is what
		// Unity's adaptive probe volumes and Unreal's volumetric lightmap both
		// do, and for the same reason: the alternative spends per fragment,
		// every frame, forever, on a question that has one answer per scene.
		//
		// So a volume component is a request for coverage and density, not an
		// object the renderer knows about. The composition is:
		//
		//   * **one volume** -- taken as it is, rotation and all, because a
		//     single authored box is exactly the field it describes;
		//   * **several** -- the union of their world bounds at the finest
		//     spacing any of them asked for, axis aligned, because a union of
		//     boxes at different angles has no angle of its own.
		//
		// The cost of that union is cells over empty space between volumes.
		// Measured before worrying about it: a room-sized field is under a
		// megabyte, and sparsity is a complication to add when something
		// actually cannot afford it.
		auto view = m_Registry.GetView<TransformComponent, IrradianceVolumeComponent>();

		// **One region per volume, and no union anywhere.**
		//
		// Every volume in the scene used to be merged into a single box -- the
		// union of their bounds at the finest spacing any of them asked for --
		// which meant two volumes at opposite ends of a level produced one
		// enormous grid spanning the emptiness between them. It made "more
		// volumes" mean "a bigger box", which is the opposite of what an
		// author reaches for a second volume to say. They are independent now:
		// each keeps its own grid, its own spacing and its own rotation, and
		// they share only a texture (see IrradianceVolume::Region for why they
		// must share one).
		std::vector<IrradianceVolume::Region> regions;
		uint32_t count = 0;
		int cap = 2;
		// **The most any volume asked for**, on the same rule the spacing takes
		// the finest: a composed field is one solve, so a volume that wanted
		// four bounces and one that wanted eight get eight. Erring towards the
		// more expensive answer is right for a cost paid once, in a baker.
		int passes = 1;
		int rays = 64;
		bool recapture = false;

		// Stale the moment no volume is fitting itself; set again below when
		// one is. The editor's overlay reads it to draw the derived box.
		m_AutoFitValid = false;

		// The union of the scene's static mesh bounds, computed once and only
		// if some volume asked to fit itself to it.
		bool autoTried = false;
		bool autoValid = false;
		Vec3 autoMin(0.0f), autoMax(0.0f);
		auto sceneBounds = [&]()
		{
			if (autoTried)
				return autoValid;
			autoTried = true;

			auto meshes = m_Registry.GetView<TransformComponent, MeshComponent>();
			for (auto& entry : meshes)
			{
				auto [meshTransform, mesh] =
					meshes.Get<TransformComponent, MeshComponent>(entry);
				RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
				// Skinned meshes move; a fit that follows them renames the
				// bake every animation frame. See the component's comment.
				if (!resolved || resolved->IsSkinned())
					continue;

				Vec3 meshCentre, meshExtents;
				Frustum::TransformBounds(resolved->GetBounds(), meshTransform.World,
										 meshCentre, meshExtents);
				if (!autoValid)
				{
					autoMin = meshCentre - meshExtents;
					autoMax = meshCentre + meshExtents;
					autoValid = true;
				}
				else
				{
					autoMin = Math::Min(autoMin, meshCentre - meshExtents);
					autoMax = Math::Max(autoMax, meshCentre + meshExtents);
				}
			}
			return autoValid;
		};

		for (auto& item : view)
		{
			auto [transform, volume] =
				view.Get<TransformComponent, IrradianceVolumeComponent>(item);

			// **The box is the component's; the transform places and turns it.**
			//
			// The three columns of the world matrix are the box's axes. Their
			// *directions* are which way it faces and are all that is taken
			// from them -- their lengths are the transform's scale, which a
			// volume deliberately ignores now: a scale multiplies a mesh, a
			// volume has no mesh, and a volume parented to something scaled
			// used to change size with nothing having touched it.
			//
			// Normalised rather than assumed unit: a scaled parent still puts
			// length in these columns even though nothing here wants it.
			Vec3 centre = Vec3(transform.World[3]);
			Vec3 extents = Math::Max(volume.Extents, Vec3(0.05f));
			Vec3 axisX = Math::Normalize(Vec3(transform.World[0])) * extents.x * 2.0f;
			Vec3 axisY = Math::Normalize(Vec3(transform.World[1])) * extents.y * 2.0f;
			Vec3 axisZ = Math::Normalize(Vec3(transform.World[2])) * extents.z * 2.0f;

			// **Or the box fits itself to the scene** (see the component): the
			// union of the static meshes' world bounds, axis-aligned, snapped
			// OUTWARD to the cell grid so geometry drifting within a cell
			// cannot move the box and rename the bake. The transform and the
			// authored Extents are ignored while the flag is on; an empty
			// scene keeps the authored box rather than fitting to nothing.
			if (volume.AutoFit && sceneBounds())
			{
				const float snap = Math::Max(volume.Spacing, 0.05f);
				const Vec3 lo(Math::Floor(autoMin.x / snap) * snap,
							  Math::Floor(autoMin.y / snap) * snap,
							  Math::Floor(autoMin.z / snap) * snap);
				const Vec3 hi(Math::Ceil(autoMax.x / snap) * snap,
							  Math::Ceil(autoMax.y / snap) * snap,
							  Math::Ceil(autoMax.z / snap) * snap);
				centre = (lo + hi) * 0.5f;
				extents = Math::Max((hi - lo) * 0.5f, Vec3(0.05f));
				axisX = Vec3(extents.x * 2.0f, 0.0f, 0.0f);
				axisY = Vec3(0.0f, extents.y * 2.0f, 0.0f);
				axisZ = Vec3(0.0f, 0.0f, extents.z * 2.0f);
				m_AutoFitCentre = centre;
				m_AutoFitExtents = extents;
				m_AutoFitValid = true;
			}

			// **Past the cap it is dropped, not merged.** Merging is the
			// behaviour this replaced, so quietly falling back to it would be
			// the one wrong answer; a scene that wants a ninth volume wants to
			// be told.
			if (count >= Renderer3D::kMaxIrradianceVolumes)
			{
				if (!m_WarnedVolumeCap)
				{
					m_WarnedVolumeCap = true;
					RV_CORE_WARN("This scene has more Irradiance Volumes than the {0} one "
								 "field can hold; the rest are ignored. Cover more ground "
								 "with fewer, larger volumes rather than more of them.",
								 Renderer3D::kMaxIrradianceVolumes);
				}
				continue;
			}

			// **Two cells past the authored box, as before**, so geometry drawn
			// on the boundary is not read through the reader's edge fade. Per
			// volume now rather than once over the union.
			const float spacing = Math::Max(volume.Spacing, 0.05f);
			const Vec3 padded = extents + Vec3(2.0f * spacing);

			IrradianceVolume::Region region;
			region.Centre = centre;
			region.Extents = padded;
			region.Rotation = Mat3(
				extents.x > 1.0e-6f ? axisX / (extents.x * 2.0f) : Vec3(1.0f, 0.0f, 0.0f),
				extents.y > 1.0e-6f ? axisY / (extents.y * 2.0f) : Vec3(0.0f, 1.0f, 0.0f),
				extents.z > 1.0e-6f ? axisZ / (extents.z * 2.0f) : Vec3(0.0f, 0.0f, 1.0f));
			region.Spacing = spacing;

			// Two cells a side at minimum: one cannot be interpolated, and a
			// field that cannot be interpolated is a constant with overheads.
			const int volumeCap = Math::Clamp(volume.MaxResolution, 2, 256);
			auto axisCells = [&](float halfExtent)
			{
				const int wanted = (int)Math::Ceil(2.0f * halfExtent / spacing) + 1;
				return (uint32_t)Math::Clamp(wanted, 2, volumeCap);
			};
			region.Width  = axisCells(padded.x);
			region.Height = axisCells(padded.y);
			region.Depth  = axisCells(padded.z);

			regions.push_back(region);

			passes = Math::Max(passes, Math::Clamp(volume.Passes, 1, 64));
			rays = Math::Max(rays, Math::Clamp(volume.RaysPerCell, 64, 4096));
			recapture |= volume.Recapture;
			volume.Recapture = false;
			count++;
		}

		if (count == 0)
		{
			// Nothing to light with, and nothing to hold on to: a scene that
			// had its volume deleted should stop reading the field it built.
			m_Field.reset();
			m_FieldVolumes = 0;
			m_FieldUsable = false;
			m_FieldEvaluated = true;
			m_FieldSolveWanted = false;   // nothing left to solve it into
			Renderer3D::SetIrradianceVolumes(nullptr);
			return;
		}

		// Which flavour of bake the active GI form reads -- and therefore
		// which file the loader wants resident, which the solver makes last,
		// and which the warnings name.
		const bool activeRt = ActiveGiIsTraced();

		// **What the whole set was built for, as one number.**
		//
		// The stamp carries the atlas's own shape, but a texture of the right
		// size can hold a completely different arrangement of boxes -- so the
		// layout is hashed and stamped beside it. Every field a reader depends
		// on goes in: move one volume a centimetre, or renumber them, and the
		// bake stops applying.
		uint64_t layout = 1469598103934665603ull;
		auto mixLayout = [&layout](const void* data, size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
			{
				layout ^= bytes[i];
				layout *= 1099511628211ull;
			}
		};
		for (const IrradianceVolume::Region& region : regions)
		{
			mixLayout(&region.Centre, sizeof(region.Centre));
			mixLayout(&region.Extents, sizeof(region.Extents));
			mixLayout(&region.Rotation, sizeof(region.Rotation));
			mixLayout(&region.Width, sizeof(region.Width));
			mixLayout(&region.Height, sizeof(region.Height));
			mixLayout(&region.Depth, sizeof(region.Depth));
		}

		// What the field was built for, checked the way the reflection probe's
		// capture is: anything that changes a box, a grid, the lighting or how
		// many volumes describe the scene changes which points were solved, so
		// the stored answer stops applying.
		const bool shapeChanged = !m_Field
							   || m_FieldVolumes != count
							   || m_FieldLayout != layout
							   // **Quality invalidates too.** A field solved at one
							   // bounce is a different answer from the same grid at
							   // eight, and nothing else here would notice.
							   || m_FieldPasses != passes
							   || m_FieldRays != rays
							   || m_FieldLighting != lighting
							   // **And so does the GI form.** The traced form reads
							   // the rt flavour, the screen forms the ss one, and a
							   // toggle between them is a different file.
							   || m_FieldFlavourRt != activeRt;

		if (shapeChanged || recapture)
		{
			m_Field = IrradianceVolume::CreateAtlas(Renderer::GetDevice(), regions);
			if (!m_Field)
				return;

			// The laid-out regions, with their atlas offsets filled in.
			regions = m_Field->Regions();

			m_FieldSpacing = regions[0].Spacing;
			m_FieldPasses = passes;
			m_FieldRays = rays;
			m_FieldLighting = lighting;
			m_FieldVolumes = count;
			m_FieldLayout = layout;
			m_FieldBaked = false;
			m_FieldFlavourRt = activeRt;
			// A shape change abandons whatever solve was in flight: its result
			// describes a field that no longer exists, and the store block must
			// not write it under the new name.
			m_FieldSolvePhase = 0;

			BakedLighting::Stamp wanted;
			wanted.Centre = Vec3(0.0f);
			wanted.Extents = Vec3(1.0f);
			wanted.AxisX = Vec3(1.0f, 0.0f, 0.0f);
			wanted.AxisY = Vec3(0.0f, 1.0f, 0.0f);
			wanted.Width = m_Field->Width();
			wanted.Height = m_Field->Height();
			wanted.Depth = m_Field->Depth();
			wanted.Tiles = IrradianceVolume::kTiles;
			wanted.Lighting = lighting;
			wanted.Layout = layout;

			// **A bake, if one is on disk and still describes this scene.**
			//
			// Tried before the solve is asked for, so a baked scene never
			// solves at all: the field arrives filled and the runtime cost of
			// the whole feature is the lookup alone. The stamp is what makes
			// that safe -- box, grid and lighting hash -- so a file made for a
			// moved volume or different lights is refused and the solve happens
			// exactly as it did before.
			//
			// **Except when a bake was asked for by hand.** A press means "make
			// this one again", and the reasons somebody presses it -- a moved
			// wall, an edited material -- are exactly the ones the stamp cannot
			// see. Loading the stale file and calling the job done would make
			// the button do nothing, convincingly.
			BakedLighting::Stamp stored;
			std::vector<uint8_t> payload;
			const bool loaded = !ForcingBake()
							 && !m_SourcePath.empty()
							 && BakedLighting::Read(FieldBakePath(lighting, activeRt),
													BakedLighting::Kind::IrradianceField,
													stored, payload)
							 && stored.Matches(wanted)
							 && m_Field->UploadRaw(payload);

			// **And whether the other flavour's file is already good**, checked
			// here because everything that invalidates the active one -- the
			// box, the grid, the lighting, a forced bake -- invalidates it by
			// the same stamp. A bake run is not finished until both files
			// stand; outside a bake run this is bookkeeping nothing reads.
			BakedLighting::Stamp altStored;
			std::vector<uint8_t> altPayload;
			m_FieldBakedAlt = !ForcingBake()
						   && !m_SourcePath.empty()
						   && BakedLighting::Read(FieldBakePath(lighting, !activeRt),
												  BakedLighting::Kind::IrradianceField,
												  altStored, altPayload)
						   && altStored.Matches(wanted);

			if (loaded)
			{
				m_FieldBaked = true;   // already on disk; nothing to write
				m_FieldSolveWanted = false;
			}
			else
			{
				// **Zero until it is solved, which is what "no bounce known
				// yet" means.** A cell holds bounced light and nothing else --
				// the sky is the probe's to answer and the flat ambient is the
				// scene's -- so an unsolved field contributes nothing and a
				// scene with a volume renders exactly like one without until
				// the pass runs.
				//
				// Uploaded rather than left as the allocator found it: a field
				// is sampled on the frame it is created, and undefined memory
				// read as light is the one thing this must never be.
				const std::vector<IrradianceVolume::Cell> cells(m_Field->CellCount());
				m_Field->Upload(cells);

				// **Recorded as wanted, not asked for here.** The frame that
				// notices a lighting change is very often one that may not act
				// on it -- see m_FieldSolveWanted -- so the want outlives this
				// branch and is served below, on the first frame that can.
				m_FieldSolveWanted = true;
			}
		}

		// **Solved only while baking, and never as a mode of its own.**
		//
		// A field that is solved at runtime and kept in memory is a third state
		// between realtime and baked -- it looks like baking, costs like
		// realtime on the frames it runs, and survives nothing. There is no
		// author-facing option for it, so there is no reason for the engine to
		// enter it: the solve exists to *produce* a bake, and outside that a
		// missing file means the field stays empty and the GI source falls back
		// to Realtime, which says so.
		//
		// Not during a probe capture: that re-enters this whole path, and a
		// field solved from inside a cube face is solved against a scene being
		// rendered for a different purpose. Not while one is already in flight
		// either -- the pass takes several frames and re-asking would restart
		// it -- which is what makes this safe to run every frame rather than
		// only on the frame something changed.
		// A phase left over from a bake that ended -- gave up, or was killed --
		// would block the next one from ever starting.
		if (m_FieldSolvePhase != 0 && !BakingLighting()
			&& !Renderer3D::HasPendingIrradianceSolve())
			m_FieldSolvePhase = 0;

		// **One solve serves both flavours now.** The screen flavour began as
		// a converged single bounce, matching the gather's estimate -- and
		// then the owner ruled that it must chase the *voxel* stack, which
		// measures BRIGHTER than a two-bounce reference over most of a frame
		// (4.08 mean levels off truth on the GI fixture, its cones' own
		// bias). The multi-bounce field is the brightest honest content there
		// is, so both files carry it: the solve runs once with feedback and
		// the store below writes it under both names. The pair of files, the
		// per-flavour loader and the settled bookkeeping all stand -- the day
		// the flavours diverge again, only this request and the store change
		// back.
		if (m_Field && !m_CapturingProbes && BakingLighting()
			&& !Renderer3D::HasPendingIrradianceSolve()
			&& m_FieldSolvePhase == 0 && m_FieldSolveWanted)
		{
			// Every region, in one request: the boxes ride on the volume now,
			// so the solver walks them itself -- and it must, because the
			// sweeps have to advance in lockstep across all of them. See
			// SolvePendingIrradiance.
			Renderer3D::RequestIrradianceSolve(m_Field,
											   (uint32_t)m_FieldPasses,
											   (uint32_t)m_FieldRays, true);
			m_FieldSolvePhase = 2;
			m_FieldSolveWanted = false;
		}

		// **Bound only for a source that asked for it, or while a bake is
		// producing it.** The traced bounce reads whatever field is bound at
		// every hit, so binding one unconditionally made "Realtime" quietly
		// mean "realtime plus whichever bake happens to be on disk" -- and a
		// stale bake, whose stamp covers the lights but not a moved wall,
		// would silently bend a mode whose whole name promises it computes
		// everything fresh. A Baked source opts into the stored answer;
		// Realtime never touches it.
		const bool fieldWanted = WantsBakedGi() || BakingLighting();
		Renderer3D::SetIrradianceVolumes(fieldWanted ? m_Field : nullptr);
		m_FieldUsable = m_FieldBaked;
		m_FieldEvaluated = true;

		// **And write it out, once it is finished and if anybody asked.**
		//
		// After the solve rather than during it: a field is written over
		// several frames and several sweeps, and a file made from a half-solved
		// one would be a worse answer than no file at all -- the loader would
		// trust it, and nothing would ever notice. So the bake waits for the
		// request to clear, which is the renderer saying the last sweep landed.
		// **Only once the lighting has stopped moving.**
		//
		// The hash is not final on the first frames of a scene: lights arrive
		// as the registry is walked, a script may set one in its first update,
		// and each distinct value is a different bake under this naming. The
		// showroom wrote four files for one lighting before this guard existed
		// -- three of them for states that lasted a frame and will never be
		// asked for again.
		if (m_FieldLighting != m_SettledLighting)
		{
			m_SettledLighting = m_FieldLighting;
			m_SettledFrames = 0;
		}
		else if (m_SettledFrames < 1000)
		{
			m_SettledFrames++;
		}

		// **And not before the solve it belongs to has completed.** The phase
		// marker is set when a solve is requested and read back here, so this
		// cannot fire in the window between wanting a solve and being allowed
		// to ask for one -- the probe-capture window a mode switch lands in,
		// where the old guard once let a field of zeros reach the disk.
		//
		// Nor from inside a capture: the write reads the texture back through
		// the device, and a capture has a render pass open.
		if (BakingLighting() && !m_SourcePath.empty()
			&& m_SettledFrames > 30
			&& (m_FieldSolvePhase != 0 || (m_FieldBaked && !m_FieldBakedAlt))
			&& !m_CapturingProbes
			&& !Renderer3D::HasPendingIrradianceSolve())
		{
			// The atlas's own shape, plus the layout hash that says which
			// boxes are arranged inside it -- see m_FieldLayout.
			BakedLighting::Stamp stamp;
			stamp.Centre = Vec3(0.0f);
			stamp.Extents = Vec3(1.0f);
			stamp.AxisX = Vec3(1.0f, 0.0f, 0.0f);
			stamp.AxisY = Vec3(0.0f, 1.0f, 0.0f);
			stamp.Width = m_Field->Width();
			stamp.Height = m_Field->Height();
			stamp.Depth = m_Field->Depth();
			stamp.Tiles = IrradianceVolume::kTiles;
			stamp.Lighting = lighting;
			stamp.Layout = m_FieldLayout;

			if (m_FieldSolvePhase == 2)
			{
				// The completed solve is both flavours' content; two names,
				// one texture. Written active-first so a failure mid-pair
				// leaves the file the scene reads rather than its twin.
				if (BakedLighting::Write(Renderer::GetDevice(),
										 FieldBakePath(lighting, activeRt),
										 BakedLighting::Kind::IrradianceField, stamp,
										 m_Field->Texture()))
				{
					m_FieldSolvePhase = 0;
					m_FieldBaked = true;
					m_FieldBakedAlt =
						BakedLighting::Write(Renderer::GetDevice(),
											 FieldBakePath(lighting, !activeRt),
											 BakedLighting::Kind::IrradianceField,
											 stamp, m_Field->Texture());

					// **And now the probes, again, with the field in the
					// room.** Whatever cube a probe is holding was captured
					// before this light existed, so it is the wrong photograph
					// of the right room -- and it is the one that would be
					// stored and adopted on every later run. Marking them
					// dirty costs six faces once, at the end of a bake nobody
					// is watching, and is the difference between a stored cube
					// that agrees with the stored field and one that does not.
					auto probes = m_Registry.GetView<TransformComponent,
													 ReflectionProbeComponent>();
					for (auto& item : probes)
					{
						ReflectionProbeComponent& probe =
							probes.Get<ReflectionProbeComponent>(item);
						if (probe.Update == ProbeUpdate::Realtime)
							continue;   // answers "now"; there is nothing to store

						probe.Baked = false;
						probe.Dirty = true;
					}
				}
			}
			else if (m_FieldBaked && !m_FieldBakedAlt && !m_FieldSolveWanted)
			{
				// The active file loaded off disk and its twin is missing --
				// same content under the other name, written from the
				// resident texture without a solve.
				m_FieldBakedAlt =
					BakedLighting::Write(Renderer::GetDevice(),
										 FieldBakePath(lighting, !activeRt),
										 BakedLighting::Kind::IrradianceField,
										 stamp, m_Field->Texture());
			}
		}
	}

	std::filesystem::path Scene::FieldBakePath(uint64_t lighting, bool rtFlavour) const
	{
		// **One pair of files per lighting the scene can be in**, not one per
		// scene.
		//
		// A scene has one field, however many volumes compose it -- but it can
		// have more than one *lighting*, and the showroom is the case that
		// proves it: two modes on a switch, nine lights against four, a
		// different fitting emissive. They are two different answers to the
		// same geometry, and a single file could only ever hold one of them.
		//
		// So the lighting hash names the file. Bake under one mode and you get
		// its files; bake under the other and you get a second pair beside
		// them; switch at runtime and the hash changes, which is already how
		// the field knows it is stale -- it now also knows which file to read
		// instead. Nothing has to enumerate the modes, and a mode nobody baked
		// simply has no file and says so.
		//
		// **And the flavour is in the name**, because a bake stands in for a
		// specific realtime form: `rt` is the traced flavour (solved with
		// feedback, multi-bounce, matching realtime RTGI), `ss` the screen
		// flavour (a converged single bounce, matching what the gather and the
		// voxel form estimate). An old unsuffixed file predates the split and
		// is deliberately not read: nothing records which flavour it was.
		char name[64];
		std::snprintf(name, sizeof(name), "field_%016llx_%s.rvfield",
					  (unsigned long long)lighting, rtFlavour ? "rt" : "ss");
		return BakedLighting::DirectoryFor(m_SourcePath) / name;
	}

	std::filesystem::path Scene::ProbeBakePath(UUID probe) const
	{
		// **Named for the probe *and* the lighting**, exactly as a field is.
		//
		// It used to be the probe alone, and a scene with more than one
		// lighting cannot store a cube that way: the showroom baked mode 1,
		// then baked mode 2 over the top of it, and what was left on disk was a
		// photograph of the bright bay that the editor then adopted while
		// standing in the dark studio. The room drew seven levels too dark
		// until Play re-captured it, which reads as "Play changes the
		// lighting" and sent the search a long way from here.
		//
		// A cube is 12.5 MB at 512 faces, so a scene with several lightings
		// pays for several. That is the argument for BC6H in the cooker, not
		// an argument for storing one cube and hoping.
		char name[80];
		std::snprintf(name, sizeof(name), "%llu_%016llx.rvprobe",
					  (unsigned long long)(uint64_t)probe,
					  (unsigned long long)m_FieldLighting);
		return BakedLighting::DirectoryFor(m_SourcePath) / name;
	}

	bool Scene::WantsBakedGi()
	{
		// The same question RenderShadows answers when it decides the chain:
		// which GI source is in force, and does it say Baked. One expression
		// shared by the flag hand-off and the field binding, because the two
		// disagreeing is a field read by a chain that was told there is none.
		const RayDetail giDetail =
			ResolveRayTracedGlobalIllumination(Project::Render());
		return giDetail != RayDetail::Off
			? Project::Render().RayTracedGiSource == GiSource::Baked
			: GetPostSettings().GiSource == GiSource::Baked;
	}

	bool Scene::ActiveGiIsTraced() const
	{
		return ResolveRayTracedGlobalIllumination(Project::Render()) != RayDetail::Off;
	}

	bool Scene::BakingLighting() const
	{
		return EngineConfig::Get().BakeLighting || m_BakeRequested;
	}

	bool Scene::ForcingBake() const
	{
		// **Whether what is already on disk should be ignored.** A press is one
		// lighting -- the one the scene is standing in -- and ends when that
		// file is written. `--bake=force` is the whole run, so a scene that
		// visits several lightings while it is on re-makes every one of them
		// rather than only the first.
		return m_BakeRequested || EngineConfig::Get().ForceLightingBake;
	}

	bool Scene::BakedLightingSettled()
	{
		if (m_BakeRequested || m_FieldSolveWanted
			|| Renderer3D::HasPendingIrradianceSolve())
			return false;

		// Both flavours, not one: a bake is a pair of files, and a run that
		// stops after the first would leave the other GI form falling back to
		// realtime with nothing saying so until somebody toggled it.
		if (m_FieldVolumes > 0 && (!m_FieldBaked || !m_FieldBakedAlt))
			return false;

		auto probes = m_Registry.GetView<TransformComponent, ReflectionProbeComponent>();
		for (auto& item : probes)
		{
			const ReflectionProbeComponent& probe =
				probes.Get<ReflectionProbeComponent>(item);
			if (probe.Update != ProbeUpdate::Realtime && !probe.Baked)
				return false;
		}

		return m_FieldEvaluated;
	}

	void Scene::ReloadBakedLighting()
	{
		// An impossible lighting value, so the next UpdateIrradianceVolumes
		// sees a mismatch, takes the shape-changed path, and tries the disk
		// again -- where the child's file now sits. Zero is not impossible
		// (an empty light list hashes to the FNV basis, not zero, but zero is
		// still a value a hash could take); ~0 is not a value the mixer can
		// land on for any real scene and reads as deliberate.
		m_FieldBaked = false;
		m_FieldBakedAlt = false;
		m_FieldSolvePhase = 0;
		m_FieldLighting = ~0ull;
		m_WarnedMissingBake = false;
		m_UnbakedFrames = 0;

		// And the probes' one-look memory, so the adopt path runs again.
		// Dirty stays as it is: a probe that finds no file will re-capture
		// only if something else asked it to, exactly as at scene load.
		auto probes = m_Registry.GetView<ReflectionProbeComponent>();
		for (auto& item : probes)
		{
			ReflectionProbeComponent& probe =
				probes.Get<ReflectionProbeComponent>(item);
			if (probe.Update == ProbeUpdate::Realtime)
				continue;

			probe.Baked = false;
			probe.AdoptChecked = false;
		}

		RV_CORE_INFO("Baked lighting reloaded from disk.");
	}

	void Scene::RequestLightingBake()
	{
		if (m_SourcePath.empty())
		{
			// A bake is written beside its scene, and a scene that has never
			// been saved has no beside. Said rather than silently dropped: the
			// button is otherwise a button that does nothing.
			RV_CORE_WARN("Bake: save the scene first -- a bake is stored beside "
						 "the scene file, and this one has no file yet.");
			return;
		}

		m_BakeRequested = true;
		m_BakeFrames = 0;
		m_BakeStalledFrames = 0;

		// **Everything stored is asked to make itself again.** The field
		// through the volumes' own verb, so this goes down exactly the path a
		// author pressing "Solve again" goes down; the probes by clearing what
		// makes a capture unnecessary. A bake nobody can force is a bake that
		// silently keeps an answer from before the geometry moved.
		auto volumes = m_Registry.GetView<IrradianceVolumeComponent>();
		uint32_t count = 0;
		for (auto& item : volumes)
		{
			volumes.Get<IrradianceVolumeComponent>(item).Recapture = true;
			count++;
		}

		auto probes = m_Registry.GetView<ReflectionProbeComponent>();
		for (auto& item : probes)
		{
			ReflectionProbeComponent& probe = probes.Get<ReflectionProbeComponent>(item);
			if (probe.Update == ProbeUpdate::Realtime)
				continue;   // a probe answering "now" has no stored answer

			// Under `--bake=force` the stored cube is already being ignored for
			// the whole run, so a probe that has stored itself did so from a
			// fresh capture moments ago. Clearing the flag again would only
			// make it repeat a twelve-megabyte write, and print a second line
			// that reads like a defect.
			if (probe.Baked && EngineConfig::Get().ForceLightingBake)
				continue;

			probe.Baked = false;
			probe.Dirty = true;
		}

		// A warning rather than a refusal. Probes are worth baking on their
		// own, and a scene with no volume that bakes its probes is a scene
		// that starts faster -- but the author almost certainly meant to have
		// a volume, and nothing else would tell them.
		if (count == 0)
		{
			RV_CORE_WARN("Bake: this scene has no Irradiance Volume, so there is no "
						 "indirect light to store. Reflection probes will still bake.");
		}

		RV_CORE_INFO("Bake: started for the lighting the scene is in now.");
	}

	void Scene::UpdateLightingBake()
	{
		if (!m_BakeRequested)
			return;

		m_BakeFrames++;

		// A probe that is not realtime has an answer worth storing, and until
		// every one of them has stored it the bake is not finished.
		//
		// **Against the same view the capture walks**, transform included. A
		// probe on an entity with no transform has nowhere to be captured
		// from, so the capture never visits it -- and a completion test over a
		// wider view than the work it is waiting on waits forever.
		bool probesDone = true;
		std::string waitingOn;
		auto probes = m_Registry.GetView<TransformComponent, ReflectionProbeComponent>();
		for (auto& item : probes)
		{
			const ReflectionProbeComponent& probe =
				probes.Get<ReflectionProbeComponent>(item);
			if (probe.Update != ProbeUpdate::Realtime && !probe.Baked)
			{
				probesDone = false;
				waitingOn = Entity{ item, this }.GetName();
				break;
			}
		}

		const bool fieldDone = m_FieldVolumes == 0
							|| (m_FieldBaked && m_FieldBakedAlt);

		if (fieldDone && probesDone)
		{
			m_BakeRequested = false;
			RV_CORE_INFO("Bake: done in {0} frames -- {1}",
						 m_BakeFrames,
						 m_FieldVolumes == 0
							 ? "probes only, this scene has no volume"
							 : FieldBakePath(m_FieldLighting, ActiveGiIsTraced())
								   .filename().string() + " and its pair");
			return;
		}

		// **A ceiling on being stuck, not on taking a while.**
		//
		// A field solves over as many frames as it needs -- the sweeps are
		// amortised at a couple of thousand cells each, so a 33x12x52 volume is
		// hundreds of frames of honest work. Timing that out gave up on exactly
		// the bakes that most needed one: the fine grids, which are the whole
		// reason to raise the resolution. So the counter resets whenever a
		// solve is in flight or waiting for its turn, and what is left measures
		// the thing a ceiling is actually for -- a lighting hash that never
		// holds still, because a light in the scene moves every frame.
		if (Renderer3D::HasPendingIrradianceSolve() || m_FieldSolveWanted)
			m_BakeStalledFrames = 0;
		else
			m_BakeStalledFrames++;

		if (m_BakeStalledFrames > 600)
		{
			m_BakeRequested = false;

			// **Names what is outstanding, not what is likely.** The first
			// version guessed between the two halves and named the wrong one,
			// which is worse than naming neither: it sent the reading straight
			// past the half that was actually stuck.
			if (!fieldDone)
			{
				RV_CORE_WARN("Bake: gave up after {0} frames. The field never settled "
							 "-- a light that moves or animates changes the lighting "
							 "every frame, and a bake is named after a lighting that "
							 "holds still.", m_BakeFrames);
			}
			else
			{
				RV_CORE_WARN("Bake: gave up after {0} frames. The reflection probe "
							 "'{1}' never stored its capture.", m_BakeFrames, waitingOn);
			}
		}
	}

	Scene::FieldStatus Scene::GetFieldStatus() const
	{
		FieldStatus status;
		status.Volumes = m_FieldVolumes;
		status.Spacing = m_FieldSpacing;
		status.Lighting = m_FieldLighting;
		status.Loaded = m_FieldUsable;
		status.Solving = m_FieldSolveWanted || Renderer3D::HasPendingIrradianceSolve();

		if (m_Field)
		{
			status.Width = m_Field->Width();
			status.Height = m_Field->Height();
			status.Depth = m_Field->Depth();
		}

		if (!m_SourcePath.empty())
		{
			status.Directory = BakedLighting::DirectoryFor(m_SourcePath);
			status.File = FieldBakePath(m_FieldLighting, ActiveGiIsTraced());
		}

		return status;
	}

	void Scene::PackProbes(RHI::RHICommandList& cmd)
	{
		m_ProbeSlots.clear();

		ProbeArray::Begin(ProbeFaceSize(), 1 + ProbeCount());
		if (!ProbeArray::IsReady())
			return;

		// The sky as well as the probes. Begin reallocates when the face size
		// changes -- which happens the first time a 256-pixel probe joins a
		// scene of 128s -- and that empties slot 0 along with everything else.
		// Refilling it is a pointer comparison on every frame that did not.
		ProbeArray::SetSky(cmd, ResolveSky());

		uint32_t slot = ProbeArray::kSkySlot + 1;

		auto view = m_Registry.GetView<TransformComponent, ReflectionProbeComponent>();
		for (auto& item : view)
		{
			auto [transform, probe] = view.Get<TransformComponent, ReflectionProbeComponent>(item);

			// An incomplete probe is black on the faces it has not reached. The
			// sky is a better answer than a hole, and it is what an object gets
			// by not appearing in this table at all.
			if (!probe.Probe || !probe.Probe->IsComplete())
				continue;

			if (slot >= ProbeArray::kSlots)
			{
				if (!m_WarnedProbeCount)
				{
					RV_CORE_WARN("Scene has more than {0} reflection probes; the rest "
								 "reflect the sky", ProbeArray::kMaxProbes);
					m_WarnedProbeCount = true;
				}
				break;
			}

			// Convolved into its slice, once per capture. A baked probe pays
			// this on the frame it completes and never again; a realtime one
			// pays it every sixth frame, which is what its generation counter
			// is for.
			if (!ProbeArray::SetProbe(cmd, slot, *probe.Probe))
				continue;

			// Recorded here rather than recomputed at selection time, so the
			// table that says which slot a probe went into is the same table
			// that decides which slot an object reads. Two walks of the
			// registry in the same order would agree today and drift the first
			// time either one grows a condition the other does not.
			ProbeSlot entry;
			entry.Position = Vec3(transform.World[3]);
			entry.Influence = probe.Influence;
			entry.Slot = slot;
			m_ProbeSlots.push_back(entry);

			slot++;
		}
	}

	uint32_t Scene::ProbeSlotFor(const Vec3& position) const
	{
		// A probe capture reflects the sky, never another probe. Two probes
		// facing each other would otherwise capture each other capturing each
		// other, one frame deeper every frame.
		if (m_CapturingProbes)
			return ProbeArray::kSkySlot;

		// Per object, against the object -- not against the camera. Choosing
		// against the viewer was the shape this replaced: it gives one answer
		// for the whole scene, so a mirror at the far end of a room reflects
		// the probe standing next to the *camera*. With one probe the two agree,
		// which is exactly why the old answer looked right for as long as it did.
		uint32_t best = ProbeArray::kSkySlot;
		float nearest = std::numeric_limits<float>::max();

		for (const ProbeSlot& probe : m_ProbeSlots)
		{
			const float distance = Math::Distance(position, probe.Position);
			if (distance > probe.Influence || distance >= nearest)
				continue;

			nearest = distance;
			best = probe.Slot;
		}

		return best;
	}

	void Scene::UpdateAnimators(Timestep ts, bool editing)
	{
		auto view = m_Registry.GetView<MeshComponent, AnimatorComponent>();

		for (auto& item : view)
		{
			auto [mesh, animator] = view.Get<MeshComponent, AnimatorComponent>(item);

			// Editing, and this animator did not ask to preview: hold still.
			//
			// The pose is *cleared* rather than left where it was, because the
			// renderer draws an empty pose as the bind pose -- so unticking
			// the box returns the character to the shape it was modelled in
			// rather than freezing it wherever the preview happened to stop.
			// A stale pose would also be the one thing on screen that no
			// longer corresponds to anything in the scene.
			if (editing && !animator.RunInEditor)
			{
				animator.Skinning.clear();
				continue;
			}

			const Skeleton* skeleton = Assets::Manager::GetSkeleton(mesh.Mesh);
			if (!skeleton || skeleton->IsEmpty())
			{
				animator.Skinning.clear();
				continue;
			}

			const std::vector<Anim::Clip>* clips = Assets::Manager::GetClips(mesh.Mesh);

			// A clip index of -1, or one past the end, is the bind pose. Both
			// are states a person can reach in the inspector and neither is an
			// error worth a log line every frame.
			auto clipAt = [&](int index) -> const Anim::Clip*
			{
				if (!clips || index < 0 || index >= (int)clips->size())
					return nullptr;
				return &(*clips)[index];
			};

			// A change of Clip starts a cross-fade. Detected here rather than
			// pushed by a Play call, so the inspector, a script and a
			// deserialized scene all reach the same behaviour without any of
			// them knowing the blend exists.
			if (!animator.Started)
			{
				// The first update adopts whatever is authored. Fading in from
				// clip 0 would be a transition out of something that never
				// played.
				animator.Active = animator.Clip;
				animator.Started = true;
			}
			else if (animator.Clip != animator.Active)
			{
				// Only worth a fade if there is somewhere to fade from and
				// time to do it in.
				if (animator.BlendTime > 0.0f && clipAt(animator.Active))
				{
					animator.FadingFrom = animator.Active;
					animator.FadingTime = animator.Time;
					animator.FadeElapsed = 0.0f;
				}
				else
				{
					animator.FadingFrom = -1;
				}

				animator.Active = animator.Clip;
				animator.Time = 0.0f;
			}

			const Anim::Clip* clip = clipAt(animator.Active);
			const float dt = ts.GetSeconds();

			if (animator.Playing && clip)
				animator.Time += dt * animator.Speed;

			Pose pose;
			if (clip)
				SamplePose(*skeleton, *clip, animator.Time, animator.Loop, pose);
			else
				RestPose(*skeleton, pose);

			// The outgoing clip, still running, mixed out over BlendTime.
			if (animator.FadingFrom >= 0)
			{
				animator.FadeElapsed += dt;

				const float weight = animator.BlendTime > 0.0f
								   ? Math::Clamp(animator.FadeElapsed / animator.BlendTime,
												 0.0f, 1.0f)
								   : 1.0f;

				if (weight >= 1.0f)
				{
					animator.FadingFrom = -1;
				}
				else if (const Anim::Clip* outgoing = clipAt(animator.FadingFrom))
				{
					if (animator.Playing)
						animator.FadingTime += dt * animator.Speed;

					Pose previous;
					SamplePose(*skeleton, *outgoing, animator.FadingTime,
							   animator.Loop, previous);

					// Weight runs 0 to 1 *towards the new clip*, so the
					// outgoing pose is the `a` argument. Reversing these is a
					// blend that plays backwards and looks like the clips are
					// swapped.
					Pose blended;
					BlendPoses(previous, pose, weight, blended);
					pose = std::move(blended);
				}
			}

			// Last frame's pose becomes the previous one before this frame's
			// is composed. A swap, not a copy: ComposeSkinning overwrites
			// every element it is given, so the vector it hands back is only
			// storage, and reusing it keeps this allocation-free in the steady
			// state.
			animator.PreviousSkinning.swap(animator.Skinning);
			ComposeSkinning(*skeleton, pose, animator.Skinning);

			// The first frame has no previous pose. Standing in the current
			// one makes the deformation contribute no velocity for one frame,
			// which is right -- a pose that has only just existed has not
			// moved.
			if (animator.PreviousSkinning.size() != animator.Skinning.size())
				animator.PreviousSkinning = animator.Skinning;
		}
	}

	void Scene::OnRender(const Camera& viewCamera, const Mat4& cameraTransform,
						 const ViewportGridSettings* grid,
						 const EditorIconSettings* icons)
	{
		// The sub-pixel offset this frame is drawn with, applied here and
		// nowhere else.
		//
		// Here because this is where the one camera every part of the scene
		// pass draws through is chosen -- meshes, sky, grid, world text, icons
		// and particles all take it from this function, so offsetting it once
		// moves them together. A renderer that jittered its own projection
		// would have to be remembered by the next renderer somebody writes,
		// and the symptom of forgetting is a half-pixel misregistration that
		// reads as a soft filter rather than as a missing line of code.
		//
		// Zero during a probe capture, for two independent reasons: this test,
		// and the fact that the graph only sets the jitter for the duration of
		// its scene pass while a capture runs outside the graph entirely. Six
		// faces of a cube drawn with six different offsets do not meet at the
		// seams, and the cube is reused for many frames afterwards. Shadow
		// cascades are safe by construction -- they never see this camera.
		const Vec2 jitter = m_CapturingProbes ? Vec2(0.0f, 0.0f) : Renderer::GetJitter();
		const bool jittered = jitter.x != 0.0f || jitter.y != 0.0f;

		// Branched rather than always multiplied, so that every mode but TAA
		// gets the identical matrix it got before this existed -- and the
		// screenshot checks that compare against stored images stay exact
		// rather than nearly exact.
		const Camera camera(jittered ? JitterProjection(viewCamera.GetProjection(), jitter)
									 : viewCamera.GetProjection());

		// The lights, as the voxel grid's injection also collects them (7bc).
		const LightList lights = CollectLights();

		// What the scene reflects, resolved once: the same cube feeds the
		// surfaces and the background, so a mirror cannot disagree with what is
		// behind it.
		RHI::Ref<RHI::RHITexture> sky;
		RHI::Ref<RHI::RHITexture> irradiance;
		if (Renderer::HasDevice())
		{
			if (m_Environment.Sky == SkyType::Cubemap)
			{
				sky = Assets::Manager::GetCubemap(m_Environment.SkyTexture);
				irradiance = Assets::Manager::GetIrradiance(m_Environment.SkyTexture);
			}

			// Before ResolveEnvironment replaces `sky` with the gradient cube:
			// what the ambient should be depends on whether the scene has a
			// real cube, and after that line every path has one.
			irradiance = Skybox::ResolveIrradiance(m_Environment, sky, irradiance);
			sky = Skybox::ResolveEnvironment(m_Environment, sky);
		}

		// The sky still draws the sky; only the surfaces reflect a probe. A
		// probe's capture contains the sky already, so drawing the background
		// from it would be a lower-resolution copy of what is there anyway.
		//
		// One binding for the whole pass, whatever the scene's probe count:
		// surfaces reflect a *slice* of these, chosen per object below. The
		// arrays already hold the prefiltered convolution -- nothing is filtered
		// at bind time, and there is no per-probe cube left to look up.
		RHI::Ref<RHI::RHITexture> environment = ProbeArray::GetRadiance();
		RHI::Ref<RHI::RHITexture> probeIrradiance = ProbeArray::GetIrradiance();

		// A probe capture draws the scene, and the arrays are filled after the
		// captures finish -- so during one they hold the previous frame's
		// contents and slot 0 is the only slice a face may read. Everything a
		// capture draws is pinned to it below.
		if (environment && probeIrradiance)
			irradiance = probeIrradiance;

		// One frustum for this pass, shared by the meshes and the quads below.
		// Built here rather than inside the mesh block because it belongs to
		// the pass, not to one kind of geometry -- keeping it in there is how
		// the quads ended up being the one thing in the renderer that drew
		// whatever the registry held.
		//
		// Against the *unjittered* projection: whether an object is drawn
		// should not depend on a sub-pixel offset, or something standing
		// exactly on the screen edge would appear and disappear on alternate
		// frames -- which is a flicker a temporal filter would then smear
		// across the whole edge of the screen.
		const Frustum frustum(viewCamera.GetProjection() * Math::Inverse(cameraTransform));

		// Meshes first: they are opaque and depth-tested, so drawing them ahead
		// of the alpha-blended quads means the quads blend against a complete
		// depth buffer rather than over each other arbitrarily.
		// Built here as well as in the shadow pass: this can be entered without
		// one -- a reflection probe's capture, or a project with shadows off --
		// and the flag makes the second call free when it was not.
		RefreshDrawList();

		auto meshView = m_Registry.GetView<TransformComponent, MeshComponent>();
		const bool anyTerrain = Renderer::HasDevice() && HasTerrain();
		if ((meshView.begin() != meshView.end() || anyTerrain) && Renderer::HasDevice())
		{
			// The jitter goes in as well as into the camera, because the
			// velocity attachment has to come back out *without* it: a motion
			// vector is where the surface moved, and half a pixel of camera
			// offset is not the surface moving. Passed rather than read from
			// the renderer, so a caller that did not jitter its camera cannot
			// accidentally get the correction applied to it.
			// The irradiance field, before BeginScene for the same reason the
			// probes are: the block that carries its bounds is uploaded there.
			//
			// **Sizing only.** A field that needs solving is asked for here and
			// solved by the frame graph's fill pass, because there is nowhere
			// inside OnRender to solve one: OnRender *is* a render-graph pass's
			// body, so a render pass is open at every point in it, and the
			// solve opens one of its own and records barriers, neither of which
			// is legal inside another. See Renderer3D::RequestIrradianceSolve.
			UpdateIrradianceVolumes(lights);

			// **Once a frame, not once a cube face.** A probe capture re-enters
			// OnRender six times over, and a bake that counted those would time
			// itself out six times faster than the clock it is written against.
			if (!m_CapturingProbes)
				UpdateLightingBake();

			// **Before BeginScene, because the scene block carries the probes
			// now and BeginScene uploads it.** Two readers: the lit shader
			// blends between them per fragment, and the traced bounce picks
			// one per hit -- the same table for both, so the two forms cannot
			// disagree about which probe lights a place.
			//
			// Empty while capturing, which reproduces exactly what
			// ProbeSlotFor does then: a capture reflects the sky and never
			// another probe, or two probes facing each other capture each
			// other one frame deeper every frame.
			Renderer3D::SetProbeVolumes(m_CapturingProbes ? std::vector<ProbeSlot>{}
														  : m_ProbeSlots);

			Renderer3D::BeginScene(camera, cameraTransform, lights, m_Environment,
								   Project::Render(), environment, irradiance, jitter);

			// **The frustum test alone**, as the cascades do -- the mesh and
			// the material were resolved once by RefreshDrawList, and the
			// bounds with them. What is left here is what genuinely depends on
			// this view: whether the object is in it, and which probe it
			// reflects.
			// **The GPU-driven half** (roadmap 8.3). The cull pass decided
			// which of these are visible and in what order; what is left here
			// is filling the table it hands out indices into -- every object
			// in it, not just the visible ones, because the CPU does not know
			// which those are and asking would mean reading device memory back.
			//
			// More rows built than the walk below would have built, and no
			// sort at all. The sort was 4.1 ms of a 13.9 ms render graph at
			// sixty thousand objects; the extra rows are the difference
			// between the object count and the visible count.
			// The same camera this was culled for, and not a probe face or the
			// other viewport. The transform is the one RenderShadows was handed
			// and the one this render was handed, so an exact comparison is
			// asking whether they are the same call's, not whether two floats
			// are close.
			bool sameCamera = true;
			for (int column = 0; column < 4 && sameCamera; column++)
				for (int row = 0; row < 4; row++)
					if (cameraTransform[column][row] != m_CulledLitFor[column][row])
					{
						sameCamera = false;
						break;
					}

			const bool gpuLit = sameCamera && m_CulledLit.IsValid() && m_CullObjectCount > 0;

			// The blended table rides the same conditions: it was culled for
			// this camera in the same call, and its rows live in the same
			// instance buffer after the opaque ones.
			const bool gpuBlend = sameCamera && m_CulledBlend.IsValid()
							   && m_BlendObjectCount > 0;

			if (gpuLit || gpuBlend)
			{
				// **Both tables' rows, in one reservation.** The vertex stage
				// reads one instance buffer and a draw cannot bind two, so the
				// blended rows are simply the ones after the opaque rows -- and
				// the cull that wrote their indices was told the same base.
				Renderer3D::ReserveSceneInstances(m_CullObjectCount + m_BlendObjectCount);

				const size_t itemCount = m_DrawItems.size();
				for (size_t index = 0; index < itemCount; index++)
				{
					const DrawItem& entry = m_DrawItems[index];
					if (entry.CullSlot == kNoCullSlot)
						continue;

					RHI::Ref<Material> material =
						Assets::Manager::GetMaterial(entry.Source->Material);
					if (!material)
						material = Renderer3D::GetDefaultMaterial();

					const MaterialParams params = entry.Source->ResolveParams(
						material ? material->GetParams() : MaterialParams{});

					Renderer3D::SetSceneInstance(entry.CullIndex, entry.Transform->World,
												 entry.Transform->PreviousWorld, material,
												 params, ProbeSlotFor(m_DrawBounds[index].Centre));
				}

				// The blended rows, at their own base.
				for (size_t index = 0; index < itemCount; index++)
				{
					const DrawItem& entry = m_DrawItems[index];
					if (entry.BlendSlot == kNoCullSlot)
						continue;

					RHI::Ref<Material> material =
						Assets::Manager::GetMaterial(entry.Source->Material);
					if (!material)
						material = Renderer3D::GetDefaultMaterial();

					const MaterialParams params = entry.Source->ResolveParams(
						material ? material->GetParams() : MaterialParams{});

					Renderer3D::SetSceneInstance(m_CullObjectCount + entry.BlendIndex,
												 entry.Transform->World,
												 entry.Transform->PreviousWorld, material,
												 params, ProbeSlotFor(m_DrawBounds[index].Centre));
				}

				if (gpuLit)
					Renderer3D::DrawSceneIndirect(m_CulledLit, m_CullMeshes);

				// Recorded, not issued: the transparent pass runs later in the
				// graph and issues it there, exactly as the CPU transparent
				// list is held until then.
				if (gpuBlend)
					Renderer3D::DrawTransparentIndirect(m_CulledBlend, m_BlendMeshes);
			}

			// **The emitters this frame's bounce may aim at.** Handed over here
			// rather than at the draw-list refresh, because the refresh is
			// cached across frames and the renderer's copy is per frame.
			//
			// It went into RenderShadows' own submit branch first, where it
			// compiled, ran on some frames, and left the count at zero on the
			// ones that mattered -- which looked exactly like the estimator not
			// working.
			Renderer3D::SetAreaEmitters(m_Emitters);

			// Everything the table does not hold: the skinned meshes, anything
			// with too few indices to draw indexed, and -- when there is no
			// cull pass -- all of it.
			const size_t drawCount = m_DrawBounds.size();
			const bool anyTable = gpuLit || gpuBlend;
			for (size_t step = 0; step < (anyTable ? m_CpuDraws.size() : drawCount); step++)
			{
				const size_t index = anyTable ? m_CpuDraws[step] : step;

				const DrawBounds& bounds = m_DrawBounds[index];
				if (!frustum.Intersects(bounds.Centre, bounds.Extents))
				{
					Renderer3D::CountCulled();
					continue;
				}

				const DrawItem& entry = m_DrawItems[index];
				const Mat4& world = entry.Transform->World;

				// Which probe this object reflects. Against the object's own
				// centre, not its origin: a long wall's origin can sit outside
				// every probe's influence while most of the wall is inside one.
				const uint32_t probe = ProbeSlotFor(bounds.Centre);

				// This entity's scalars on top of the material's own. Resolved
				// here rather than in the draw list because it is cheap, it is
				// eighty bytes an object to carry, and only what survives the
				// test above ever needs it.
				RHI::Ref<Material> material =
					Assets::Manager::GetMaterial(entry.Source->Material);
				if (!material)
					material = Renderer3D::GetDefaultMaterial();

				const MaterialParams params = entry.Source->ResolveParams(
					material ? material->GetParams() : MaterialParams{});

				// A skinned mesh has to reach the skinned pipeline whether or
				// not anything is animating it: its vertex layout is the wider
				// one, and the static pipeline would read joint indices as
				// texture coordinates. Without an animator it draws its bind
				// pose.
				if (entry.Skinned)
				{
					const auto* animator = m_Registry.TryGet<AnimatorComponent>(entry.Entity);

					// A skinned mesh with no animator still needs a full pose,
					// not an empty one: its vertices name bones by index, and a
					// short run leaves them reading past the end of their own
					// instance's bones into the next character's. The bind pose
					// is every bone at identity -- which is exactly what
					// ComposeSkinning produces at rest, and is why that is the
					// property the tests assert.
					if (animator && !animator->Skinning.empty())
					{
						Renderer3D::DrawSkinnedMesh(entry.Resolved, world, material, params,
													animator->Skinning, probe,
													&entry.Transform->PreviousWorld,
													&animator->PreviousSkinning);
					}
					else if (const Skeleton* skeleton =
								 Assets::Manager::GetSkeleton(entry.Source->Mesh))
					{
						const std::vector<Mat4> bind(skeleton->Size(), Mat4(1.0f));
						Renderer3D::DrawSkinnedMesh(entry.Resolved, world, material, params,
													bind, probe, &entry.Transform->PreviousWorld);
					}
				}
				else
				{
					Renderer3D::DrawMesh(entry.Resolved, world, material, params, probe,
										 &entry.Transform->PreviousWorld);
				}
			}

			// The terrain: each chunk at the level RenderShadows chose for this
			// camera, culled against the same frustum, drawn through the layered
			// material PrepareTerrains refreshed -- four layers in the asset's
			// painted proportions, layer 0 alone where nothing is painted (7aq)
			// -- and otherwise the ordinary mesh it is (7ap), whole or without
			// its skirts as SelectLod decided from where the camera stands
			// (under the ground, a skirt is a wall along every seam). Refreshed
			// again here rather than trusted: a probe capture or a first frame
			// can reach this draw before any RenderShadows has, and the refresh
			// is a compare when nothing changed.
			if (anyTerrain)
			{
				ForEachTerrain([&](Entity, TransformComponent& transform, TerrainComponent& component,
								   Terrain& terrain)
				{
					const RHI::Ref<LayeredMaterial>& layers = terrain.RefreshLayers(component);
					if (!layers)
						return;

					for (Terrain::Chunk& chunk : terrain.GetChunks())
					{
						const RHI::Ref<Mesh>& mesh = chunk.Selected();
						if (!mesh)
							continue;

						Vec3 centre, extents;
						Frustum::TransformBounds(chunk.Bounds, transform.World, centre, extents);
						if (!frustum.Intersects(centre, extents))
						{
							Renderer3D::CountCulled();
							continue;
						}

						Renderer3D::DrawLayeredMesh(mesh, transform.World, layers,
													ProbeSlotFor(centre), terrain.DrawIndexCount(chunk),
													&transform.PreviousWorld);
					}
				});
			}

			Renderer3D::EndScene();

		}

		// After the meshes. The depth test is what keeps the sky out of the
		// pixels the scene already covers, and it has nothing to test against
		// until they are written.
		//
		// Unconditional, unlike the mesh block above: a scene with no meshes in
		// it still has a sky, and an empty scene showing the clear colour is
		// how this looked before.
		if (Renderer::HasDevice() && m_Environment.Sky != SkyType::Color)
			Skybox::Draw(camera, cameraTransform, m_Environment, sky, jitter);

		// After the sky and before the particles, and neither half of that is
		// interchangeable. After the sky, because the sky is drawn at the far
		// plane against the depth test and would be rejected wherever the grid
		// had already claimed a pixel. Before the particles, because the grid
		// is scenery to them: drawn the other way round, a grid line paints
		// over the smoke standing in front of it.
		if (grid && Renderer::HasDevice())
			ViewportGrid::Draw(camera, cameraTransform, *grid);

		// Before the particles, for the same reason the grid is: a label is
		// part of the scene a blended particle blends *against*. Drawn after,
		// a nameplate would paint over the smoke standing in front of it.
		//
		// Depth-tested either way, so geometry occludes it -- which is the
		// entire difference between this and the HUD.
		UI::DrawWorldText(*this, camera.GetProjection() * Math::Inverse(cameraTransform),
						  cameraTransform);

		// The gizmo marks, beside the world text and for the same reasons:
		// depth-tested so geometry occludes them, and before the particles so
		// they are scenery rather than an overlay. Editor only, structurally
		// -- `icons` is null on every path a player's picture goes through.
		if (icons && Renderer::HasDevice())
		{
			EditorIcons::Draw(*this, camera.GetProjection() * Math::Inverse(cameraTransform),
							  cameraTransform, *icons);
		}

		// Last, so a blended particle has the whole scene to blend against --
		// including the sky.
		auto emitters = m_Registry.GetView<ParticleEmitterComponent, TransformComponent>();
		if (emitters.begin() != emitters.end() && ParticleRenderer::IsReady())
		{
			ParticleRenderer::BeginScene(camera, cameraTransform);

			for (auto handle : emitters)
			{
				auto [emitter, transform] =
					emitters.Get<ParticleEmitterComponent, TransformComponent>(handle);

				// Same draw either way; only who filled the instances differs.
				if (emitter.SimulateOnGpu)
				{
					uint32_t count = 0;
					Entity entity{ handle, this };
					if (auto instances = Particles::Gpu::GetInstances(entity.GetUUID(), count))
						ParticleRenderer::DrawEmitterGpu(emitter, transform.World,
														 instances, count);
				}
				else
				{
					ParticleRenderer::DrawEmitter(emitter, transform.World);
				}
			}

			ParticleRenderer::EndScene();
		}
	}
}
