#include <rvpch.h>
#include "Interop.h"
#include "DotNetHost.h"

#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/Application.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Math/Math.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Scene/ScriptRegistry.h"

#include <sstream>

#include <cstring>

namespace RageV::Managed
{
	namespace
	{
		Scene* s_Scene = nullptr;
		NativeApi s_Api{};
		ManagedApi s_Managed{};
		bool s_Ready = false;

		// Every entity function starts here. Returning an invalid Entity rather
		// than asserting is deliberate: a script holding a UUID whose entity was
		// destroyed is normal -- Destroy is deferred and scripts run in an
		// unspecified order -- so "not found" is an answer, not a fault.
		Entity Resolve(uint64_t entity)
		{
			if (!s_Scene || entity == 0)
				return {};
			return s_Scene->GetEntityByUUID(UUID(entity));
		}

		// --- the table's implementations -------------------------------------

		void __cdecl Log(int32_t level, const char* message)
		{
			if (!message)
				return;

			switch (level)
			{
				case 0:  RV_CORE_TRACE("[C#] {0}", message); break;
				case 2:  RV_CORE_WARN("[C#] {0}", message);  break;
				case 3:  RV_CORE_ERROR("[C#] {0}", message); break;
				default: RV_CORE_INFO("[C#] {0}", message);  break;
			}
		}

		uint64_t __cdecl FindEntityByName(const char* name)
		{
			if (!s_Scene || !name)
				return 0;

			Entity found = s_Scene->FindEntityByName(name);
			return found ? (uint64_t)found.GetUUID() : 0;
		}

		int32_t __cdecl EntityExists(uint64_t entity)
		{
			return Resolve(entity) ? 1 : 0;
		}

		int32_t __cdecl GetEntityName(uint64_t entity, char* buffer, int32_t capacity)
		{
			Entity found = Resolve(entity);
			if (!found || !found.HasComponent<TagComponent>())
				return -1;

			const std::string& name = found.GetComponent<TagComponent>().Name;
			const int32_t length = (int32_t)name.size();

			// The full length is returned even when it did not fit, so a caller
			// that was truncated can allocate and ask again rather than shipping
			// a clipped name it has no way to notice.
			if (buffer && capacity > 0)
			{
				const int32_t copied = std::min(length, capacity - 1);
				std::memcpy(buffer, name.data(), (size_t)copied);
				buffer[copied] = '\0';
			}
			return length;
		}

		// The three transform pairs are identical but for the member, and are
		// written out rather than generated: a macro here would save nine lines
		// and cost every future reader the ability to grep for SetRotation.
		int32_t __cdecl GetPosition(uint64_t entity, Vec3* out)
		{
			Entity found = Resolve(entity);
			if (!found || !out || !found.HasComponent<TransformComponent>())
				return 0;
			*out = found.GetComponent<TransformComponent>().Position;
			return 1;
		}

		int32_t __cdecl SetPosition(uint64_t entity, const Vec3* value)
		{
			Entity found = Resolve(entity);
			if (!found || !value || !found.HasComponent<TransformComponent>())
				return 0;
			found.GetComponent<TransformComponent>().Position = *value;
			return 1;
		}

		int32_t __cdecl GetRotation(uint64_t entity, Vec3* out)
		{
			Entity found = Resolve(entity);
			if (!found || !out || !found.HasComponent<TransformComponent>())
				return 0;
			*out = found.GetComponent<TransformComponent>().Rotation;
			return 1;
		}

		int32_t __cdecl SetRotation(uint64_t entity, const Vec3* value)
		{
			Entity found = Resolve(entity);
			if (!found || !value || !found.HasComponent<TransformComponent>())
				return 0;
			found.GetComponent<TransformComponent>().Rotation = *value;
			return 1;
		}

		int32_t __cdecl GetScale(uint64_t entity, Vec3* out)
		{
			Entity found = Resolve(entity);
			if (!found || !out || !found.HasComponent<TransformComponent>())
				return 0;
			*out = found.GetComponent<TransformComponent>().Scale;
			return 1;
		}

		int32_t __cdecl SetScale(uint64_t entity, const Vec3* value)
		{
			Entity found = Resolve(entity);
			if (!found || !value || !found.HasComponent<TransformComponent>())
				return 0;
			found.GetComponent<TransformComponent>().Scale = *value;
			return 1;
		}

		int32_t __cdecl IsActionDown(const char* action)
		{
			return (action && InputMap::IsActionDown(action)) ? 1 : 0;
		}

		int32_t __cdecl WasActionPressed(const char* action)
		{
			return (action && InputMap::WasActionPressed(action)) ? 1 : 0;
		}

		float __cdecl GetAxis(const char* axis)
		{
			return axis ? InputMap::GetAxis(axis) : 0.0f;
		}

		float __cdecl GetFixedDeltaTime()
		{
			return Application::GetFixedTimestep();
		}

		float __cdecl GetTime()
		{
			return Application::GetElapsedTime();
		}

		// --- appended for protocol 2 -----------------------------------------

		int32_t __cdecl WasActionReleased(const char* action)
		{
			return (action && InputMap::WasActionReleased(action)) ? 1 : 0;
		}

		// The world transform, and the entity's own axes derived from it. Native
		// rather than computed in C# from the local rotation: the local rotation
		// says nothing about where a parented entity actually points, and a
		// script asking for "forward" means the direction it faces in the world.
		int32_t __cdecl GetWorldPosition(uint64_t entity, Vec3* out)
		{
			Entity found = Resolve(entity);
			if (!found || !out || !s_Scene)
				return 0;
			*out = Vec3(s_Scene->GetWorldTransform(found)[3]);
			return 1;
		}

		int32_t __cdecl AxisOf(uint64_t entity, Vec3* out, const Vec4& axis)
		{
			Entity found = Resolve(entity);
			if (!found || !out || !s_Scene)
				return 0;
			*out = Math::Normalize(Vec3(s_Scene->GetWorldTransform(found) * axis));
			return 1;
		}

		int32_t __cdecl GetForward(uint64_t e, Vec3* out) { return AxisOf(e, out, { 0.0f, 0.0f, -1.0f, 0.0f }); }
		int32_t __cdecl GetRight(uint64_t e, Vec3* out)   { return AxisOf(e, out, { 1.0f, 0.0f,  0.0f, 0.0f }); }
		int32_t __cdecl GetUp(uint64_t e, Vec3* out)      { return AxisOf(e, out, { 0.0f, 1.0f,  0.0f, 0.0f }); }

		uint64_t __cdecl Spawn(const char* name)
		{
			if (!s_Scene)
				return 0;
			Entity created = s_Scene->CreateEntity(name ? name : "Entity");
			return (uint64_t)created.GetUUID();
		}

		void __cdecl Destroy(uint64_t entity)
		{
			if (Entity found = Resolve(entity))
				s_Scene->DestroyDeferred(found);
		}

		Physics::World* PhysicsOf()
		{
			return s_Scene ? s_Scene->GetPhysics() : nullptr;
		}

		void __cdecl AddForce(uint64_t entity, const Vec3* force)
		{
			if (Physics::World* physics = PhysicsOf(); physics && force && Resolve(entity))
				physics->AddForce(UUID(entity), *force);
		}

		void __cdecl AddImpulse(uint64_t entity, const Vec3* impulse)
		{
			if (Physics::World* physics = PhysicsOf(); physics && impulse && Resolve(entity))
				physics->AddImpulse(UUID(entity), *impulse);
		}

		void __cdecl SetLinearVelocity(uint64_t entity, const Vec3* velocity)
		{
			if (Physics::World* physics = PhysicsOf(); physics && velocity && Resolve(entity))
				physics->SetLinearVelocity(UUID(entity), *velocity);
		}

		int32_t __cdecl GetLinearVelocity(uint64_t entity, Vec3* out)
		{
			if (!out)
				return 0;
			Physics::World* physics = PhysicsOf();
			if (!physics || !Resolve(entity))
			{
				*out = Vec3(0.0f);
				return 0;
			}
			*out = physics->GetLinearVelocity(UUID(entity));
			return 1;
		}

		// --- protocol 4: the rest of the native surface ----------------------

		int32_t __cdecl SetEntityName(uint64_t entity, const char* name)
		{
			Entity found = Resolve(entity);
			if (!found || !name || !found.HasComponent<TagComponent>())
				return 0;
			found.GetComponent<TagComponent>().Name = name;
			return 1;
		}

		void __cdecl LookAt(uint64_t entity, const Vec3* target, const Vec3* up)
		{
			Entity found = Resolve(entity);
			if (!found || !target || !up || !s_Scene ||
				!found.HasComponent<TransformComponent>())
				return;

			const Vec3 from = Vec3(s_Scene->GetWorldTransform(found)[3]);
			const Vec3 direction = *target - from;

			// The same zero-length guard the native LookAt has: no direction
			// means no rotation to describe, and normalising it would seed a
			// NaN that spreads through every transform below.
			if (Math::Dot(direction, direction) < 1e-12f)
				return;

			const Mat4 view = Math::LookAt(from, *target, *up);
			found.GetComponent<TransformComponent>().Rotation =
				Math::ToEuler(Math::ToQuat(Math::Inverse(view)));
		}

		// UUIDs into the buffer, count-that-would-fit returned -- the string
		// contract, for entities.
		int32_t WriteIds(const std::vector<UUID>& ids, uint64_t* buffer, int32_t capacity)
		{
			const int32_t count = (int32_t)ids.size();
			if (buffer && capacity > 0)
			{
				const int32_t copied = std::min(count, capacity);
				for (int32_t i = 0; i < copied; i++)
					buffer[i] = (uint64_t)ids[(size_t)i];
			}
			return count;
		}

		int32_t __cdecl FindEntitiesByName(const char* name, uint64_t* buffer, int32_t capacity)
		{
			if (!s_Scene || !name)
				return 0;

			std::vector<UUID> ids;
			for (Entity found : s_Scene->FindEntitiesByName(name))
				ids.push_back(found.GetUUID());
			return WriteIds(ids, buffer, capacity);
		}

		uint64_t __cdecl GetParent(uint64_t entity)
		{
			Entity found = Resolve(entity);
			if (!found || !s_Scene)
				return 0;
			Entity parent = s_Scene->GetParent(found);
			return parent ? (uint64_t)parent.GetUUID() : 0;
		}

		void __cdecl SetParent(uint64_t entity, uint64_t parent)
		{
			Entity found = Resolve(entity);
			if (!found || !s_Scene)
				return;
			// parent == 0 resolves to an invalid Entity, which Scene::SetParent
			// reads as "move to the root" -- exactly what the native API does.
			s_Scene->SetParent(found, Resolve(parent));
		}

		int32_t __cdecl GetChildren(uint64_t entity, uint64_t* buffer, int32_t capacity)
		{
			Entity found = Resolve(entity);
			if (!found || !s_Scene)
				return 0;
			return WriteIds(s_Scene->GetChildren(found), buffer, capacity);
		}

		uint64_t __cdecl SpawnPrefab(const char* assetPath)
		{
			if (!s_Scene || !assetPath)
				return 0;

			const AssetHandle prefab = Assets::Registry::GetHandle(assetPath);
			if (!prefab.IsValid())
			{
				RV_CORE_WARN("[C#] SpawnPrefab: no asset at '{0}'", assetPath);
				return 0;
			}

			Entity spawned = Assets::Manager::InstantiatePrefab(*s_Scene, prefab);
			return spawned ? (uint64_t)spawned.GetUUID() : 0;
		}

		int32_t __cdecl Raycast(const Vec3* origin, const Vec3* direction, RayHitData* out)
		{
			if (!origin || !direction || !out)
				return 0;

			*out = {};
			Physics::World* physics = PhysicsOf();
			if (!physics)
				return 0;

			const RayHit hit = physics->CastRay(*origin, *direction);
			out->Entity = (uint64_t)hit.Entity;
			out->Position = hit.Position;
			out->Normal = hit.Normal;
			out->Distance = hit.Distance;
			return hit.Hit ? 1 : 0;
		}

		// The audio four mirror ScriptableEntity's implementations line for
		// line, component checks included: no source component is a no-op, not
		// an error, exactly as it is for a native script.

		uint64_t __cdecl PlaySource(uint64_t entity)
		{
			Entity found = Resolve(entity);
			if (!found || !found.HasComponent<AudioSourceComponent>() || !s_Scene)
				return 0;

			auto& source = found.GetComponent<AudioSourceComponent>();

			// Restart rather than overlap, same as the native PlaySource.
			Audio::Engine::Stop(source.Voice);

			AudioPlayback playback;
			playback.Clip = source.Clip;
			playback.Bus = source.Bus;
			playback.Volume = source.Volume;
			playback.Pitch = source.Pitch;
			playback.Loop = source.Loop;
			playback.Stream = source.Stream;
			playback.Spatial = source.Spatial;
			playback.Position = Vec3(s_Scene->GetWorldTransform(found)[3]);
			playback.MinDistance = source.MinDistance;
			playback.MaxDistance = source.MaxDistance;

			source.Voice = Audio::Engine::Play(playback);
			return source.Voice;
		}

		void __cdecl StopSource(uint64_t entity)
		{
			Entity found = Resolve(entity);
			if (!found || !found.HasComponent<AudioSourceComponent>())
				return;

			auto& source = found.GetComponent<AudioSourceComponent>();
			Audio::Engine::Stop(source.Voice);
			source.Voice = 0;
		}

		int32_t __cdecl IsSourcePlaying(uint64_t entity)
		{
			Entity found = Resolve(entity);
			if (!found || !found.HasComponent<AudioSourceComponent>())
				return 0;
			return Audio::Engine::IsPlaying(found.GetComponent<AudioSourceComponent>().Voice) ? 1 : 0;
		}

		uint64_t __cdecl PlayOneShot(uint64_t entity, const char* clipPath, float volume)
		{
			Entity found = Resolve(entity);
			if (!found || !s_Scene)
				return 0;

			// An empty path plays the entity's own source clip -- the
			// ImpactSound pattern, without needing component access.
			AssetHandle clip;
			if (clipPath && clipPath[0] != '\0')
				clip = Assets::Registry::GetHandle(clipPath);
			else if (found.HasComponent<AudioSourceComponent>())
				clip = found.GetComponent<AudioSourceComponent>().Clip;

			if (!clip.IsValid())
				return 0;

			AudioPlayback playback;
			playback.Clip = clip;
			playback.Volume = volume;
			playback.Spatial = true;
			playback.Position = Vec3(s_Scene->GetWorldTransform(found)[3]);
			return Audio::Engine::Play(playback);
		}

		uint64_t __cdecl PlayOneShot2D(const char* clipPath, float volume)
		{
			if (!clipPath || clipPath[0] == '\0')
				return 0;

			const AssetHandle clip = Assets::Registry::GetHandle(clipPath);
			if (!clip.IsValid())
				return 0;

			AudioPlayback playback;
			playback.Clip = clip;
			playback.Volume = volume;
			playback.Spatial = false;
			return Audio::Engine::Play(playback);
		}

		void __cdecl StopVoice(uint64_t voice)
		{
			Audio::Engine::Stop(voice);
		}

		// --- components, through the registry --------------------------------

		// The component instance a name refers to on an entity, or null.
		void* ResolveComponent(uint64_t entity, const char* component,
							   const ComponentDesc** outDesc)
		{
			Entity found = Resolve(entity);
			if (!found || !component)
				return nullptr;

			const ComponentDesc* desc = ComponentRegistry::Find(component);
			if (!desc)
				return nullptr;

			if (outDesc)
				*outDesc = desc;
			return desc->TryGet(found);
		}

		int32_t __cdecl HasComponent(uint64_t entity, const char* component)
		{
			return ResolveComponent(entity, component, nullptr) ? 1 : 0;
		}

		int32_t __cdecl AddComponent(uint64_t entity, const char* component)
		{
			Entity found = Resolve(entity);
			if (!found || !component)
				return 0;

			const ComponentDesc* desc = ComponentRegistry::Find(component);
			if (!desc || !desc->Add)
				return 0;

			// Adding twice is refused rather than asserted: a script cannot be
			// trusted to know, and EnTT treats a duplicate add as a fault.
			if (desc->TryGet(found))
				return 0;

			desc->Add(found);
			return 1;
		}

		int32_t __cdecl RemoveComponent(uint64_t entity, const char* component)
		{
			Entity found = Resolve(entity);
			if (!found || !component)
				return 0;

			const ComponentDesc* desc = ComponentRegistry::Find(component);
			// The same rule the inspector enforces: a component the editor
			// calls essential -- the transform, the tag -- refuses removal from
			// a script exactly as it refuses the X button.
			if (!desc || !desc->Remove || !desc->Removable || !desc->TryGet(found))
				return 0;

			desc->Remove(found);
			return 1;
		}

		// One field's value as text, in the forms the scene file uses: floats
		// invariant via to_chars, vectors space-separated, booleans
		// "true"/"false", enums as their number, assets as their path.
		std::string ComponentFieldToText(const FieldDesc& field, void* component)
		{
			void* value = field.Access(component);
			switch (field.Type)
			{
				case FieldType::Bool:   return Detail::ScriptFieldToText(*(bool*)value);
				case FieldType::Int:    return Detail::ScriptFieldToText(*(int*)value);
				case FieldType::Enum:   return Detail::ScriptFieldToText(*(int*)value);
				case FieldType::Float:  return Detail::ScriptFieldToText(*(float*)value);
				case FieldType::Vec3:   return Detail::ScriptFieldToText(*(Vec3*)value);
				case FieldType::Vec4:
				{
					const Vec4& v = *(Vec4*)value;
					return Detail::ScriptFieldToText(v.x) + " " + Detail::ScriptFieldToText(v.y)
						 + " " + Detail::ScriptFieldToText(v.z) + " " + Detail::ScriptFieldToText(v.w);
				}
				case FieldType::String: return *(std::string*)value;
				case FieldType::Asset:
					// The path, not the handle: handles are the engine's
					// internal names, and a script holding one could do
					// nothing honest with it.
					return Assets::Registry::GetMetadata(*(AssetHandle*)value).Path;
			}
			return {};
		}

		bool ComponentFieldFromText(const FieldDesc& field, void* component, const char* text)
		{
			void* value = field.Access(component);
			switch (field.Type)
			{
				case FieldType::Bool:   Detail::ScriptFieldFromText(text, *(bool*)value); return true;
				case FieldType::Int:    Detail::ScriptFieldFromText(text, *(int*)value); return true;
				case FieldType::Enum:   Detail::ScriptFieldFromText(text, *(int*)value); return true;
				case FieldType::Float:  Detail::ScriptFieldFromText(text, *(float*)value); return true;
				case FieldType::Vec3:   Detail::ScriptFieldFromText(text, *(Vec3*)value); return true;
				case FieldType::Vec4:
				{
					Vec4& v = *(Vec4*)value;
					std::istringstream stream(text);
					stream >> v.x >> v.y >> v.z >> v.w;
					return true;
				}
				case FieldType::String: *(std::string*)value = text; return true;
				case FieldType::Asset:
				{
					const AssetHandle handle = Assets::Registry::GetHandle(text);
					if (!handle.IsValid())
						return false;   // an unknown path must not null a valid reference
					*(AssetHandle*)value = handle;
					return true;
				}
			}
			return false;
		}

		const FieldDesc* FindField(const ComponentDesc& desc, const char* field)
		{
			for (const FieldDesc& candidate : desc.Fields)
			{
				if (field && candidate.Name && std::strcmp(candidate.Name, field) == 0)
					return &candidate;
			}
			return nullptr;
		}

		int32_t __cdecl GetComponentField(uint64_t entity, const char* component,
										  const char* field, char* buffer, int32_t capacity)
		{
			const ComponentDesc* desc = nullptr;
			void* instance = ResolveComponent(entity, component, &desc);
			if (!instance)
				return -1;

			const FieldDesc* found = FindField(*desc, field);
			if (!found)
				return -1;

			const std::string text = ComponentFieldToText(*found, instance);
			const int32_t length = (int32_t)text.size();

			if (buffer && capacity > 0)
			{
				const int32_t copied = std::min(length, capacity - 1);
				std::memcpy(buffer, text.data(), (size_t)copied);
				buffer[copied] = '\0';
			}
			return length;
		}

		int32_t __cdecl SetComponentField(uint64_t entity, const char* component,
										  const char* field, const char* value)
		{
			if (!value)
				return 0;

			const ComponentDesc* desc = nullptr;
			void* instance = ResolveComponent(entity, component, &desc);
			if (!instance)
				return 0;

			const FieldDesc* found = FindField(*desc, field);
			if (!found || !ComponentFieldFromText(*found, instance, value))
				return 0;

			// The same hook the inspector fires after a write: derived state --
			// a camera's cached projection, for one -- lives behind it.
			if (desc->OnChanged)
				desc->OnChanged(instance);
			return 1;
		}

		NativeApi BuildApi()
		{
			NativeApi api{};
			api.Log = &Log;
			api.FindEntityByName = &FindEntityByName;
			api.EntityExists = &EntityExists;
			api.GetEntityName = &GetEntityName;
			api.GetPosition = &GetPosition;
			api.SetPosition = &SetPosition;
			api.GetRotation = &GetRotation;
			api.SetRotation = &SetRotation;
			api.GetScale = &GetScale;
			api.SetScale = &SetScale;
			api.IsActionDown = &IsActionDown;
			api.WasActionPressed = &WasActionPressed;
			api.GetAxis = &GetAxis;
			api.GetFixedDeltaTime = &GetFixedDeltaTime;
			api.GetTime = &GetTime;

			api.WasActionReleased = &WasActionReleased;
			api.GetWorldPosition = &GetWorldPosition;
			api.GetForward = &GetForward;
			api.GetRight = &GetRight;
			api.GetUp = &GetUp;
			api.Spawn = &Spawn;
			api.Destroy = &Destroy;
			api.AddForce = &AddForce;
			api.AddImpulse = &AddImpulse;
			api.SetLinearVelocity = &SetLinearVelocity;
			api.GetLinearVelocity = &GetLinearVelocity;

			api.SetEntityName = &SetEntityName;
			api.LookAt = &LookAt;
			api.FindEntitiesByName = &FindEntitiesByName;
			api.GetParent = &GetParent;
			api.SetParent = &SetParent;
			api.GetChildren = &GetChildren;
			api.SpawnPrefab = &SpawnPrefab;
			api.Raycast = &Raycast;
			api.PlaySource = &PlaySource;
			api.StopSource = &StopSource;
			api.IsSourcePlaying = &IsSourcePlaying;
			api.PlayOneShot = &PlayOneShot;
			api.PlayOneShot2D = &PlayOneShot2D;
			api.StopVoice = &StopVoice;
			api.HasComponent = &HasComponent;
			api.AddComponent = &AddComponent;
			api.RemoveComponent = &RemoveComponent;
			api.GetComponentField = &GetComponentField;
			api.SetComponentField = &SetComponentField;
			return api;
		}
	}

	bool Interop::Init(const std::filesystem::path& assembly)
	{
		if (s_Ready)
			return true;

		const std::filesystem::path config =
			assembly.parent_path() / (assembly.stem().string() + ".runtimeconfig.json");

		if (!DotNetHost::Init(config))
			return false;

		using BootstrapFn = int32_t(__cdecl*)(const NativeApi*, int32_t);
		const auto bootstrap = (BootstrapFn)DotNetHost::GetFunctionPointer(
			assembly, "RageV.Interop, RageV.ScriptCore", "Bootstrap");

		if (!bootstrap)
		{
			RV_CORE_ERROR("C# scripting: the script assembly has no Bootstrap entry point");
			return false;
		}

		s_Api = BuildApi();

		// The table is a pointer into this translation unit's static storage, so
		// it outlives every call managed code will make through it. Handing over
		// a stack copy would work exactly until the first garbage collection
		// moved nothing and the stack frame went away anyway.
		const int32_t agreed = bootstrap(&s_Api, Interop::kProtocolVersion);

		if (agreed != Interop::kProtocolVersion)
		{
			RV_CORE_ERROR("C# scripting: protocol mismatch -- the engine speaks {0}, "
						  "the script assembly speaks {1}. Rebuild RageV.ScriptCore.",
						  Interop::kProtocolVersion, agreed < 0 ? -agreed : agreed);
			return false;
		}

		// The lifecycle entry points. Bound after the handshake, because a
		// protocol mismatch means these signatures are in doubt too -- and
		// binding a function pointer to a signature you no longer trust is the
		// failure the version check exists to prevent.
		const auto bind = [&assembly](const char* method) -> void*
		{
			return DotNetHost::GetFunctionPointer(
				assembly, "RageV.ScriptHost, RageV.ScriptCore", method);
		};

		s_Managed.Create        = (decltype(s_Managed.Create))bind("Create");
		s_Managed.Destroy       = (decltype(s_Managed.Destroy))bind("Destroy");
		s_Managed.InvokeCreate  = (decltype(s_Managed.InvokeCreate))bind("InvokeCreate");
		s_Managed.InvokeUpdate  = (decltype(s_Managed.InvokeUpdate))bind("InvokeUpdate");
		s_Managed.InvokeDestroy = (decltype(s_Managed.InvokeDestroy))bind("InvokeDestroy");
		s_Managed.InvokeContact = (decltype(s_Managed.InvokeContact))bind("InvokeContact");
		s_Managed.LiveCount     = (decltype(s_Managed.LiveCount))bind("LiveCount");
		s_Managed.LoadAssembly  = (decltype(s_Managed.LoadAssembly))bind("LoadAssembly");
		s_Managed.ListScriptTypes = (decltype(s_Managed.ListScriptTypes))bind("ListScriptTypes");

		// A different managed type, so a different assembly-qualified name.
		const auto bindFields = [&assembly](const char* method) -> void*
		{
			return DotNetHost::GetFunctionPointer(
				assembly, "RageV.ScriptFields, RageV.ScriptCore", method);
		};

		s_Managed.GetFieldCount  = (decltype(s_Managed.GetFieldCount))bindFields("GetFieldCount");
		s_Managed.DescribeField  = (decltype(s_Managed.DescribeField))bindFields("DescribeField");
		s_Managed.GetFieldValue  = (decltype(s_Managed.GetFieldValue))bindFields("GetFieldValue");
		s_Managed.SetFieldValue  = (decltype(s_Managed.SetFieldValue))bindFields("SetFieldValue");

		if (!s_Managed.Create || !s_Managed.InvokeUpdate || !s_Managed.Destroy)
		{
			RV_CORE_ERROR("C# scripting: the script assembly has no ScriptHost entry points");
			s_Managed = ManagedApi{};
			return false;
		}

		s_Ready = true;
		RV_CORE_INFO("C# scripting ready: protocol {0}, .NET {1}",
					 Interop::kProtocolVersion, DotNetHost::GetRuntimeVersion());
		return true;
	}

	void Interop::Shutdown()
	{
		// The runtime itself is not unloaded -- CoreCLR does not support being
		// restarted in a process, which is why hot reload is built on
		// collectible load contexts rather than on this.
		s_Scene = nullptr;
		s_Api = NativeApi{};
		s_Managed = ManagedApi{};
		s_Ready = false;
	}

	bool Interop::IsReady()          { return s_Ready; }
	void Interop::SetScene(Scene* s) { s_Scene = s; }
	Scene* Interop::GetScene()       { return s_Scene; }
	std::vector<ScriptFieldDesc> Interop::DescribeFields(const std::string& typeName)
	{
		std::vector<ScriptFieldDesc> fields;

		if (!s_Ready || !s_Managed.GetFieldCount || !s_Managed.DescribeField || typeName.empty())
			return fields;

		const int32_t count = s_Managed.GetFieldCount(typeName.c_str());
		if (count <= 0)
			return fields;

		for (int32_t index = 0; index < count; index++)
		{
			// Sized generously and then checked, rather than asked twice. A
			// field name and a default value are short, and the second call
			// exists for the case where they are not.
			std::string buffer(256, '\0');
			int32_t needed = s_Managed.DescribeField(typeName.c_str(), index,
													 buffer.data(), (int32_t)buffer.size());
			if (needed < 0)
				continue;

			const auto describe = [&](std::string& target) -> int32_t
			{
				return s_Managed.DescribeField(typeName.c_str(), index,
											   target.data(), (int32_t)target.size());
			};

			// The type came back in `needed` on the first call; re-reading a
			// longer buffer gives the same type, so only the text is refetched.
			if ((size_t)std::strlen(buffer.c_str()) + 1 >= buffer.size())
			{
				buffer.assign(4096, '\0');
				needed = describe(buffer);
			}

			const std::string packed(buffer.c_str());
			const size_t split = packed.find('\n');

			ScriptFieldDesc field;
			field.Type = (ScriptFieldType)needed;
			field.Name = (split == std::string::npos) ? packed : packed.substr(0, split);
			field.Default = (split == std::string::npos) ? "" : packed.substr(split + 1);

			if (field.Type != ScriptFieldType::Unsupported && !field.Name.empty())
				fields.push_back(std::move(field));
		}

		return fields;
	}

	const NativeApi& Interop::Api()  { return s_Api; }
	const ManagedApi& Interop::Managed() { return s_Managed; }
}
