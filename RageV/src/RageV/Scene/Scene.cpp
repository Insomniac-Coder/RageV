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
#include "RageV/Renderer/Renderer3D.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/Skybox.h"
#include "RageV/Renderer/ShadowMap.h"
#include "RageV/Renderer/EnvironmentIBL.h"
#include "RageV/Renderer/Frustum.h"
#include "RageV/Renderer/EditorCamera.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	namespace
	{
		const std::vector<UUID> s_NoChildren;
	}

	Scene::Scene()
	{
		m_Registry.on_destroy<NativeScriptComponent>().connect<&Scene::OnNativeScriptDestroyed>(this);
		m_Registry.on_destroy<AudioSourceComponent>().connect<&Scene::OnAudioSourceDestroyed>(this);
		m_Registry.on_destroy<IDComponent>().connect<&Scene::OnIDDestroyed>(this);
	}

	Scene::~Scene()
	{
		// Explicit, in the destructor body, rather than left to the registry's
		// own destructor: this way the on_destroy handlers run while every
		// member is still alive. Without it script instances leaked -- nothing
		// in the engine ever called the destroy hooks at all.
		m_Registry.clear();
	}

	void Scene::OnNativeScriptDestroyed(entt::registry& registry, entt::entity handle)
	{
		auto& component = registry.get<NativeScriptComponent>(handle);
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
	void Scene::OnAudioSourceDestroyed(entt::registry& registry, entt::entity handle)
	{
		Audio::Engine::Stop(registry.get<AudioSourceComponent>(handle).Voice);
	}

	void Scene::OnIDDestroyed(entt::registry& registry, entt::entity handle)
	{
		m_EntityMap.erase(registry.get<IDComponent>(handle).ID);
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
		Entity entity = { m_Registry.create(), this };
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
		auto view = m_Registry.view<TagComponent>();
		for (auto handle : view)
		{
			if (view.get<TagComponent>(handle).Name == name)
				return { handle, this };
		}
		return {};
	}

	std::vector<Entity> Scene::FindEntitiesByName(const std::string& name)
	{
		std::vector<Entity> found;
		auto view = m_Registry.view<TagComponent>();
		for (auto handle : view)
		{
			if (view.get<TagComponent>(handle).Name == name)
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
		m_Registry.destroy(entity);
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

	void Scene::PropagateTransform(entt::entity handle, const Mat4& parentWorld)
	{
		auto& transform = m_Registry.get<TransformComponent>(handle);
		transform.World = parentWorld * transform.GetLocalTransform();

		// Copied out before recursing: the reference above stays valid only
		// while the registry is not restructured, and the copy costs one matrix.
		const Mat4 world = transform.World;

		for (UUID childID : m_Registry.get<RelationshipComponent>(handle).Children)
		{
			if (Entity child = GetEntityByUUID(childID))
				PropagateTransform(child, world);
		}
	}

	void Scene::UpdateWorldTransforms()
	{
		auto view = m_Registry.view<TransformComponent, RelationshipComponent>();
		for (auto handle : view)
		{
			// Roots only; children are reached by recursion, and starting from
			// every entity would compute deep nodes once per ancestor.
			if (!view.get<RelationshipComponent>(handle).Parent.IsValid())
				PropagateTransform(handle, Mat4(1.0f));
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

		auto view = m_Registry.view<CameraComponent, IDComponent>();
		for (auto item : view)
		{
			const int rank = view.get<CameraComponent>(item).ViewRank;
			const uint64_t id = view.get<IDComponent>(item).ID;

			if (rank < bestRank || (rank == bestRank && id < bestID))
			{
				best = { item, this };
				bestRank = rank;
				bestID = id;
			}
		}

		return best;
	}

	void Scene::OnViewportResize(float width, float height)
	{
		auto view = m_Registry.view<CameraComponent>();

		for (auto& item : view)
		{
			CameraComponent& cam = view.get<CameraComponent>(item);

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

		auto view = m_Registry.view<AudioListenerComponent, IDComponent>();
		for (auto item : view)
		{
			const int rank = view.get<AudioListenerComponent>(item).ListenerRank;
			const uint64_t id = view.get<IDComponent>(item).ID;

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

		auto view = m_Registry.view<AudioSourceComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [source, transform] = view.get<AudioSourceComponent, TransformComponent>(handle);

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
		auto view = m_Registry.view<AudioSourceComponent>();
		for (auto handle : view)
		{
			AudioSourceComponent& source = view.get<AudioSourceComponent>(handle);
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
		auto view = m_Registry.view<AudioSourceComponent, TransformComponent>();
		for (auto handle : view)
		{
			auto [source, transform] = view.get<AudioSourceComponent, TransformComponent>(handle);

			if (source.Voice != 0 && source.Spatial)
				Audio::Engine::SetVoicePosition(source.Voice, Vec3(transform.World[3]));
		}
	}

	void Scene::OnRuntimeStart()
	{
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

	void Scene::OnUpdateEditor(Timestep ts)
	{
		// Animated in the editor too, so a character previews without pressing
		// Play. Scripts and physics deliberately do not run here; an animation
		// is presentation and changes nothing anyone can save.
		UpdateAnimators(ts);

		// Nothing. Editing a scene must not run it.
		(void)ts;
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		// On the frame rather than the fixed step, for the same reason the
		// audio positions are: an animation is presentation, and pinning it to
		// the simulation rate would make it stutter at any other frame rate.
		UpdateAnimators(ts);

		(void)ts;

		// Per frame, not per step: this is where the blend between the last two
		// simulation states is applied, and it is the frame that needs it.
		if (m_Physics)
			m_Physics->SyncTransforms(*this, Application::GetInterpolationAlpha());

		// After the sync, so a sound on a simulated body is placed where it is
		// being drawn rather than one step behind it. Audio is presentation,
		// like rendering, and belongs on the frame for the same reason.
		UpdateWorldTransforms();
		UpdateAudio();
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
		if (!managed.Create || !managed.InvokeUpdate)
			return;

		// The scene the interop functions act on. Set every step rather than
		// once, because play mode swaps the scene underneath and a stale binding
		// would have scripts editing the scene that is no longer running.
		Managed::Interop::SetScene(this);

		std::vector<entt::entity> scripted;
		m_Registry.view<ManagedScriptComponent>().each(
			[&](auto handle, ManagedScriptComponent&) { scripted.push_back(handle); });

		for (entt::entity handle : scripted)
		{
			auto* script = m_Registry.try_get<ManagedScriptComponent>(handle);
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
						script = m_Registry.try_get<ManagedScriptComponent>(handle);
						if (!script)
							continue;
					}
				}
			}

			if (script->Handle != 0)
				managed.InvokeUpdate(script->Handle, dt.GetSeconds());
		}
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

		m_Registry.view<ManagedScriptComponent>().each(
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
			auto simulated = m_Registry.view<RigidBodyComponent, ColliderComponent>();
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
		std::vector<entt::entity> scripted;
		m_Registry.view<NativeScriptComponent>().each(
			[&](auto handle, NativeScriptComponent&) { scripted.push_back(handle); });

		for (entt::entity handle : scripted)
		{
			// May have been destroyed by an earlier script in this same step.
			auto* script = m_Registry.try_get<NativeScriptComponent>(handle);
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
						script = m_Registry.try_get<NativeScriptComponent>(handle);
						if (!script)
							continue;
					}
				}
			}

			if (!script->Instance)
				continue;

			// On the fixed step, not the frame: a script that moves something
			// has to agree with the physics that will push it.
			script->Instance->OnUpdate(dt);
		}

		StepManagedScripts(dt);

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
		if (auto* script = m_Registry.try_get<NativeScriptComponent>(entity);
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

		auto* managed = m_Registry.try_get<ManagedScriptComponent>(entity);
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

	void Scene::OnRenderEditor(const EditorCamera& camera)
	{
		UpdateWorldTransforms();
		OnRender(camera, camera.GetTransform());
	}

	void Scene::PrepareEnvironment()
	{
		if (m_CapturingProbes || !Renderer::HasDevice() || !EnvironmentIBL::IsReady())
			return;

		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		RHI::Ref<RHI::RHITexture> sky;
		if (m_Environment.Sky == SkyType::Cubemap)
			sky = Assets::Manager::GetCubemap(m_Environment.SkyTexture);

		sky = Skybox::ResolveEnvironment(m_Environment, sky);

		// Cached on the source, so this is a map lookup on every frame after
		// the one that built it.
		EnvironmentIBL::Prefilter(*cmd, sky);
	}

	void Scene::RenderShadows(const Camera& camera, const Mat4& cameraTransform)
	{
		// A probe capture draws the scene, and the scene samples shadows. Doing
		// this during one would fit cascades to a cube face's 90-degree frustum
		// and then leave them there for the real camera.
		if (m_CapturingProbes || !m_Environment.ShadowsEnabled || !Renderer::HasDevice())
			return;

		if (!ShadowMap::IsReady())
			return;

		RHI::RHICommandList* cmd = Renderer::GetCommandList();
		if (!cmd)
			return;

		UpdateWorldTransforms();

		// Nothing to shadow, and a shadow pass over an empty scene is a render
		// pass that clears and stops.
		auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
		if (meshView.begin() == meshView.end())
			return;

		const uint32_t localResolution =
			(uint32_t)Math::Clamp(m_Environment.ShadowResolution / 2, 256, 4096);
		const uint32_t pointResolution =
			(uint32_t)Math::Clamp(m_Environment.ShadowResolution / 4, 128, 2048);

		// Drawing every caster, once per map. Culling is roadmap 3.6, and until
		// it exists a shadow map costs a full scene walk.
		auto drawCasters = [this, &meshView](const Mat4& viewProjection)
		{
			// Against this pass's own frustum, not the camera's. A cascade sees
			// a different volume from the viewer, and culling it against the
			// camera would drop exactly the casters standing outside the view
			// whose shadows fall inside it.
			const Frustum frustum(viewProjection);

			Renderer3D::BeginShadow(viewProjection);

			for (auto& item : meshView)
			{
				auto [transform, mesh] = meshView.get<TransformComponent, MeshComponent>(item);

				RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
				if (!resolved)
					continue;

				Vec3 centre, extents;
				Frustum::TransformBounds(resolved->GetBounds(), transform.World, centre, extents);

				if (!frustum.Intersects(centre, extents))
				{
					Renderer3D::CountCulled();
					continue;
				}

				// The same pose the lit pass will be given. Without this a
				// skinned figure walks and its shadow stands still in the bind
				// pose -- which reads as a shadow bug rather than a skinning
				// one, and is why the depth pass has a skinned shader at all.
				if (resolved->IsSkinned())
				{
					const auto* animator = m_Registry.try_get<AnimatorComponent>(item);

					if (animator && !animator->Skinning.empty())
					{
						Renderer3D::DrawSkinnedMeshShadow(resolved, transform.World,
														  animator->Skinning);
					}
					else if (const Skeleton* skeleton = Assets::Manager::GetSkeleton(mesh.Mesh))
					{
						const std::vector<Mat4> bind(skeleton->Size(), Mat4(1.0f));
						Renderer3D::DrawSkinnedMeshShadow(resolved, transform.World, bind);
					}

					continue;
				}

				Renderer3D::DrawMeshShadow(resolved, transform.World);
			}

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
		uint32_t spotSlot = 0;
		uint32_t pointSlot = 0;
		Vec3 direction(0.0f, -1.0f, 0.0f);

		auto lightView = m_Registry.view<TransformComponent, LightComponent>();
		for (auto& item : lightView)
		{
			if (index >= (int)ShadowMap::kMaxLights)   // the shader's light cap
				break;

			auto [transform, light] = lightView.get<TransformComponent, LightComponent>(item);
			const int lightIndex = index++;

			if (!light.Light.CastShadows)
				continue;

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

					const Vec3 up = std::fabs(forward.y) > 0.99f
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
					assigned.TexelScale = 2.0f * std::tan(fov * 0.5f) / (float)localResolution;

					ShadowMap::Assign((uint32_t)lightIndex, assigned);
					ShadowMap::RenderSpot(*cmd, spotSlot, localResolution,
										  projection * view, drawCasters);
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
										   position, reach, drawCasters);
					pointSlot++;
					break;
				}
			}
		}

		if (casterIndex < 0)
			return;

		const uint32_t count = (uint32_t)Math::Clamp(m_Environment.ShadowCascades, 1,
													(int)ShadowMap::kMaxCascades);
		const uint32_t resolution = (uint32_t)Math::Clamp(m_Environment.ShadowResolution, 256, 8192);

		// Aspect and field of view come from the projection rather than being
		// passed alongside it: an editor camera and a scene camera describe
		// theirs differently and the matrix is what actually gets used.
		const Mat4& projection = camera.GetProjection();
		const float fovY = 2.0f * std::atan(1.0f / Math::Max(projection[1][1], 1e-4f));
		const float aspect = Math::Max(projection[1][1] / Math::Max(projection[0][0], 1e-4f), 1e-4f);

		ShadowCascade cascades[ShadowMap::kMaxCascades];
		ShadowMap::ComputeCascades(cameraTransform, fovY, aspect,
								   0.1f, m_Environment.ShadowDistance,
								   direction, count, resolution,
								   m_Environment.ShadowSplitLambda,
								   flip, cascades);

		ShadowMap::SetLightIndex(casterIndex);
		ShadowMap::Render(*cmd, cascades, count, resolution, drawCasters);
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

		auto view = m_Registry.view<TransformComponent, ReflectionProbeComponent>();
		if (view.begin() == view.end())
			return;

		// The probes are placed by transforms, and a probe parented to
		// something that moved this frame is at last frame's position until
		// this runs.
		UpdateWorldTransforms();

		m_CapturingProbes = true;

		for (auto& item : view)
		{
			auto [transform, probe] = view.get<TransformComponent, ReflectionProbeComponent>(item);

			const bool realtime = probe.Update == ProbeUpdate::Realtime;
			const bool captured = probe.Probe && probe.Probe->IsComplete();

			if (!realtime && captured && !probe.Dirty)
				continue;

			const uint32_t resolution = (uint32_t)Math::Clamp(probe.Resolution, 16, 1024);
			if (!probe.Probe || probe.Probe->GetFaceSize() != resolution)
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

			// A probe's cube gets the same GGX convolution the sky's does, once
			// it holds a complete set of faces and each time it finishes a new
			// round of them. Leaving it box filtered meant a rough metal
			// reflecting a probe got a blur rather than a lobe -- which is the
			// same gap that went unnoticed on the default sky, kept on purpose
			// and no better for it.
			//
			// Thirty-six small renders per full capture. A baked probe pays it
			// once; a realtime one updating a face a frame pays it every sixth,
			// which is six renders a frame amortised.
			if (probe.Probe->IsComplete() && probe.NextFace == 0)
			{
				EnvironmentIBL::Invalidate(probe.Probe->GetCube());
				EnvironmentIBL::Prefilter(*cmd, probe.Probe->GetCube());
			}
		}

		m_CapturingProbes = false;
	}

	RHI::Ref<RHI::RHITexture> Scene::ResolveEnvironment(const Mat4& cameraTransform,
														const RHI::Ref<RHI::RHITexture>& sky)
	{
		// A probe capture reflects the sky, never another probe. Two probes
		// facing each other would otherwise capture each other capturing each
		// other, one frame deeper every frame.
		if (m_CapturingProbes)
			return sky;

		// Chosen against the viewer rather than per surface. Per surface is the
		// right answer and needs the scene descriptor set rebound per draw, or
		// a cube array indexed per object; neither is worth building before one
		// probe works. With one probe in a scene the two agree anyway.
		const Vec3 eye = Vec3(cameraTransform[3]);

		RHI::Ref<RHI::RHITexture> best = sky;
		float nearest = std::numeric_limits<float>::max();

		auto view = m_Registry.view<TransformComponent, ReflectionProbeComponent>();
		for (auto& item : view)
		{
			auto [transform, probe] = view.get<TransformComponent, ReflectionProbeComponent>(item);

			// An incomplete probe is black on the faces it has not reached. The
			// sky is a better answer than a hole.
			if (!probe.Probe || !probe.Probe->IsComplete())
				continue;

			const float distance = Math::Distance(eye, Vec3(transform.World[3]));
			if (distance > probe.Influence || distance >= nearest)
				continue;

			nearest = distance;
			best = probe.Probe->GetCube();
		}

		return best;
	}

	void Scene::UpdateAnimators(Timestep ts)
	{
		auto view = m_Registry.view<MeshComponent, AnimatorComponent>();

		for (auto& item : view)
		{
			auto [mesh, animator] = view.get<MeshComponent, AnimatorComponent>(item);

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
			const Anim::Clip* clip = nullptr;
			if (clips && animator.Clip >= 0 && animator.Clip < (int)clips->size())
				clip = &(*clips)[animator.Clip];

			if (animator.Playing && clip)
				animator.Time += ts.GetSeconds() * animator.Speed;

			Pose pose;
			if (clip)
				SamplePose(*skeleton, *clip, animator.Time, animator.Loop, pose);
			else
				RestPose(*skeleton, pose);

			ComposeSkinning(*skeleton, pose, animator.Skinning);
		}
	}

	void Scene::OnRender(const Camera& camera, const Mat4& cameraTransform)
	{
		auto lightView = m_Registry.view<TransformComponent, LightComponent>();
		LightList lights;

		for (auto& item : lightView)
		{
			auto [transform, light] = lightView.get<TransformComponent, LightComponent>(item);

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

		// The sky still draws the sky; only the surfaces reflect the probe.
		// A probe's capture contains the sky already, so drawing the background
		// from it would be a lower-resolution copy of what is there anyway.
		RHI::Ref<RHI::RHITexture> environment = ResolveEnvironment(cameraTransform, sky);

		// Surfaces reflect the prefiltered cube; the sky still draws the sharp
		// one. Probes are filtered too, on the frame each completes a round of
		// faces.
		environment = EnvironmentIBL::GetPrefiltered(environment);

		// One frustum for this pass, shared by the meshes and the quads below.
		// Built here rather than inside the mesh block because it belongs to
		// the pass, not to one kind of geometry -- keeping it in there is how
		// the quads ended up being the one thing in the renderer that drew
		// whatever the registry held.
		const Frustum frustum(camera.GetProjection() * Math::Inverse(cameraTransform));

		// Meshes first: they are opaque and depth-tested, so drawing them ahead
		// of the alpha-blended quads means the quads blend against a complete
		// depth buffer rather than over each other arbitrarily.
		auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
		if (meshView.begin() != meshView.end() && Renderer::HasDevice())
		{
			Renderer3D::BeginScene(camera, cameraTransform, lights, m_Environment,
								   environment, irradiance);

			for (auto& item : meshView)
			{
				auto [transform, mesh] = meshView.get<TransformComponent, MeshComponent>(item);

				// Null when the handle points at nothing loadable -- a deleted
				// model, or a scene opened without its assets. Skipped rather
				// than substituted, so the gap is visible.
				RHI::Ref<Mesh> resolved = Assets::Manager::GetMesh(mesh.Mesh);
				if (!resolved)
					continue;

				Vec3 centre, extents;
				Frustum::TransformBounds(resolved->GetBounds(), transform.World, centre, extents);

				if (!frustum.Intersects(centre, extents))
				{
					Renderer3D::CountCulled();
					continue;
				}

			// A skinned mesh has to reach the skinned pipeline whether or not
				// anything is animating it: its vertex layout is the wider one,
				// and the static pipeline would read joint indices as texture
				// coordinates. Without an animator it draws its bind pose.
				if (resolved->IsSkinned())
				{
					const auto* animator = m_Registry.try_get<AnimatorComponent>(item);

					// A skinned mesh with no animator still needs a full pose,
					// not an empty one: its vertices name bones by index, and a
					// short run leaves them reading past the end of their own
					// instance's bones into the next character's. The bind pose
					// is every bone at identity -- which is exactly what
					// ComposeSkinning produces at rest, and is why that is the
					// property the tests assert.
					if (animator && !animator->Skinning.empty())
					{
						Renderer3D::DrawSkinnedMesh(resolved, transform.World, mesh.Material,
													animator->Skinning);
					}
					else if (const Skeleton* skeleton = Assets::Manager::GetSkeleton(mesh.Mesh))
					{
						const std::vector<Mat4> bind(skeleton->Size(), Mat4(1.0f));
						Renderer3D::DrawSkinnedMesh(resolved, transform.World, mesh.Material, bind);
					}
				}
				else
				{
					Renderer3D::DrawMesh(resolved, transform.World, mesh.Material);
				}
			}

			Renderer3D::EndScene();
		}

		// After the meshes and before the quads. After, because the depth test
		// is what keeps the sky out of the pixels the scene already covers, and
		// it has nothing to test against until they are written. Before,
		// because a blended quad needs something behind it.
		//
		// Unconditional, unlike the mesh block above: a scene with no meshes in
		// it still has a sky, and an empty scene showing the clear colour is
		// how this looked before.
		if (Renderer::HasDevice() && m_Environment.Sky != SkyType::Color)
			Skybox::Draw(camera, cameraTransform, m_Environment, sky);

		Renderer2D::BeginScene(camera, cameraTransform, lights);

		auto group = m_Registry.group<TransformComponent>(entt::get<ColorComponent>);

		// The unit quad every one of these is, in its own space. Zero depth:
		// TransformBounds takes the absolute value of the basis rows, so a
		// rotated quad still gets a box that contains it.
		static const AABB kQuadBounds{ Vec3(-0.5f, -0.5f, 0.0f),
									   Vec3( 0.5f,  0.5f, 0.0f) };

		for (auto& item : group)
		{
			auto [transform, color] = group.get<TransformComponent, ColorComponent>(item);

			Vec3 centre, extents;
			Frustum::TransformBounds(kQuadBounds, transform.World, centre, extents);

			if (!frustum.Intersects(centre, extents))
			{
				Renderer3D::CountCulled();
				continue;
			}

			Renderer2D::DrawQuad(transform.World, color.Color);
		}

		Renderer2D::EndScene();
	}
}
