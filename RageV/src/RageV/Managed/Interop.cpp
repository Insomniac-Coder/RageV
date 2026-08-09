#include <rvpch.h>
#include "Interop.h"
#include "DotNetHost.h"

#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/Application.h"
#include "RageV/Physics/PhysicsWorld.h"

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
