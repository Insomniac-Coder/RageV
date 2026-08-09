#include <rvpch.h>
#include "Interop.h"
#include "DotNetHost.h"

#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/Application.h"

#include <cstring>

namespace RageV::Managed
{
	namespace
	{
		Scene* s_Scene = nullptr;
		NativeApi s_Api{};
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
		s_Ready = false;
	}

	bool Interop::IsReady()          { return s_Ready; }
	void Interop::SetScene(Scene* s) { s_Scene = s; }
	Scene* Interop::GetScene()       { return s_Scene; }
	const NativeApi& Interop::Api()  { return s_Api; }
}
