#include <rvpch.h>
#include "Scene.h"
#include "Entity.h"
#include "ScriptableEntity.h"
#include "Components.h"
#include "RageV/Managed/Interop.h"
#include "ScriptRegistry.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Core/Application.h"
#include "RageV/Renderer/Renderer2D.h"
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

		return Assets::Manager::GetPostSettings(
			camera.GetComponent<CameraComponent>().PostProfile);
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

		// Last, so everything this step could run -- scripts in both languages
		// and the contact handlers -- has already had its look at the click.
		// The same contract InputMap::EndFixedStep enforces for action edges:
		// one press, one step.
		UI::EndFixedStep(*this);
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
				if (button.Clicked && !button.OnClickMethod.empty())
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
		Renderer3D::SetRayTracedGlobalIllumination(
			ResolveRayTracedGlobalIllumination(Project::Render()) != RayDetail::Off);

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
		m_CulledLitFor = Mat4(0.0f);
		RefreshDrawList();
		if (RHI::RHICommandList* cull = Renderer::GetCommandList())
		{
			m_CulledLit = GpuCull::CullLit(
				*cull, camera.GetProjection() * Math::Inverse(cameraTransform));
			if (m_CulledLit.IsValid())
				m_CulledLitFor = cameraTransform;
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
		m_CullObjectCount = 0;

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

		// **The slot lookup, and why it is a scan.** A scene has tens of
		// thousands of objects and a handful of distinct meshes -- the scale
		// scenes have four -- so the answer is almost always the one before
		// it, and the fallback is a walk over a vector that fits in a cache
		// line or two. A hash per object would be a second hash per object:
		// resolving the mesh handle above is already one.
		const Mesh* lastMesh = nullptr;
		uint32_t lastSlot = kNoCullSlot;

		// Which row of the cull table the next static object takes. Counted
		// rather than read off m_CullObjects.size(), because the table is only
		// *built* on the frame's first refresh -- later ones still have to
		// number the objects the same way to point at it.
		uint32_t cullRow = 0;

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

			// Static meshes only, and only when there is a pass to read them.
			// A skinned caster is posed from bones the CPU composed this
			// frame, and an indexless mesh has nothing for an indexed draw to
			// draw.
			if (gpuCull && !entry.Skinned && resolved->GetIndexCount() >= 3)
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

				Renderer3D::DrawMeshShadow(entry.Resolved, world);
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
				const MaterialParams params =
					mesh.ResolveParams(material ? material->GetParams() : MaterialParams{});

				RayShadows::AddInstance(resolved, transform.World, bones, material, params);
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
			data.Direction = Math::Normalize(Vec3(transform.World * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
			data.Color = light.Light.Color;
			data.Intensity = light.Light.Intensity;
			data.Range = light.Light.Range;
			data.InnerCone = light.Light.InnerCone;
			data.OuterCone = light.Light.OuterCone;
			data.Type = light.Light.Type;
			data.CastShadows = light.Light.CastShadows;

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
		const PostSettings post = GetPostSettings();
		const bool voxel = ResolveVoxelGlobalIllumination(post)
						&& ResolveRayTracedGlobalIllumination(Project::Render()) == RayDetail::Off;
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
		UpdateWorldTransforms();

		m_CapturingProbes = true;

		for (auto& item : view)
		{
			auto [transform, probe] = view.Get<TransformComponent, ReflectionProbeComponent>(item);

			const bool realtime = probe.Update == ProbeUpdate::Realtime;
			const bool captured = probe.Probe && probe.Probe->IsComplete();

			if (!realtime && captured && !probe.Dirty)
				continue;

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

			const uint32_t resolution = (uint32_t)Math::Clamp(probe.Resolution, 16, 1024);
			if (!probe.Probe || probe.Probe->GetFaceSize() != resolution ||
				probe.Probe->GetSamples() != Renderer::GetTargetSamples())
			{
				probe.Probe = std::make_shared<ReflectionProbe>(Renderer::GetDevice(), resolution);
				probe.NextFace = 0;
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

		}

		m_CapturingProbes = false;

		PackProbes(*cmd);
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

			ComposeSkinning(*skeleton, pose, animator.Skinning);
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
			if (gpuLit)
			{
				Renderer3D::ReserveSceneInstances(m_CullObjectCount);

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

				Renderer3D::DrawSceneIndirect(m_CulledLit, m_CullMeshes);
			}

			// Everything the table does not hold: the skinned meshes, anything
			// with too few indices to draw indexed, and -- when there is no
			// cull pass -- all of it.
			const size_t drawCount = m_DrawBounds.size();
			for (size_t step = 0; step < (gpuLit ? m_CpuDraws.size() : drawCount); step++)
			{
				const size_t index = gpuLit ? m_CpuDraws[step] : step;

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
													&entry.Transform->PreviousWorld);
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
