#pragma once

// The boundary between the engine and managed code.
//
// Two directions, and they use different mechanisms on purpose.
//
// **Managed calling native: a table of function pointers, handed over once.**
// Not `[DllImport]`. P/Invoke by name needs the native symbols exported from a
// shared library, and this engine is a static library linked into an executable
// -- there is no `RageV.dll` to import from, and inventing one so that C# can
// find `Entity_SetPosition` would be a build-system change to work around a
// choice nobody made. A table also skips the marshalling stub P/Invoke
// generates per call, which matters when a script touches a transform every
// step of every entity.
//
// **Native calling managed: `[UnmanagedCallersOnly]` entry points**, reached by
// the function pointers hostfxr hands back. Those methods must be static, take
// only blittable arguments, and must not let an exception escape -- an
// exception crossing an unmanaged frame terminates the process rather than
// unwinding, so every entry point on the managed side catches at its edge.
//
// **Everything crossing is blittable.** An entity is a UUID, not a pointer:
// play mode restores a scene by recreating entities, so a pointer handed to a
// script would dangle the moment Stop is pressed. Vectors are three floats with
// a matching [StructLayout(LayoutKind.Sequential)] on the other side. Strings
// are UTF-8 pointers, copied at the boundary in whichever direction they go,
// because a managed string's storage may move and a std::string's certainly
// does not survive the call.
//
// If anything in NativeApi changes shape -- an argument, an order, a struct
// layout -- bump Interop.ProtocolVersion on both sides. That check is the only
// thing standing between a partial rebuild and a stack corruption somewhere
// unrelated, minutes later.

#include "RageV/Core/UUID.h"
#include "RageV/Math/Math.h"

#include <cstdint>
#include <filesystem>

namespace RageV
{
	class Scene;
}

namespace RageV::Managed
{
	// The functions managed code may call, in a fixed order.
	//
	// Layout is ABI. Adding to the end is the only safe edit; inserting in the
	// middle silently rebinds every field after it on one side only.
	struct NativeApi
	{
		// --- diagnostics -----------------------------------------------------
		// level: 0 trace, 1 info, 2 warn, 3 error. Message is UTF-8.
		void (__cdecl* Log)(int32_t level, const char* message);

		// --- entities --------------------------------------------------------
		// Zero when nothing matches, which is also the invalid entity.
		uint64_t (__cdecl* FindEntityByName)(const char* name);
		int32_t  (__cdecl* EntityExists)(uint64_t entity);

		// Writes at most `capacity` bytes including the terminator, and returns
		// the length that *would* have been written -- so a caller that got
		// truncated can tell, rather than silently shipping a clipped name.
		int32_t  (__cdecl* GetEntityName)(uint64_t entity, char* buffer, int32_t capacity);

		// --- transform -------------------------------------------------------
		// Local, relative to the parent, matching the native script API.
		int32_t (__cdecl* GetPosition)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* SetPosition)(uint64_t entity, const Vec3* value);
		int32_t (__cdecl* GetRotation)(uint64_t entity, Vec3* out);   // radians
		int32_t (__cdecl* SetRotation)(uint64_t entity, const Vec3* value);
		int32_t (__cdecl* GetScale)(uint64_t entity, Vec3* out);
		int32_t (__cdecl* SetScale)(uint64_t entity, const Vec3* value);

		// --- input -----------------------------------------------------------
		// By action name, never by key code -- the same rule the native API has.
		int32_t (__cdecl* IsActionDown)(const char* action);
		int32_t (__cdecl* WasActionPressed)(const char* action);
		float   (__cdecl* GetAxis)(const char* axis);

		// --- time ------------------------------------------------------------
		float (__cdecl* GetFixedDeltaTime)();
		float (__cdecl* GetTime)();
	};

	class Interop
	{
	public:
		// Boots the runtime if it is not already up, checks the protocol, and
		// hands the table over. False means C# is unavailable; ask
		// DotNetHost::GetUnavailableReason for a sentence fit to show a user.
		static bool Init(const std::filesystem::path& assembly);
		static void Shutdown();

		static bool IsReady();

		// Which scene the entity functions act on. Set when play mode starts
		// and cleared when it stops.
		//
		// A raw pointer rather than a shared_ptr on purpose: managed code must
		// not be able to keep a scene alive past Stop, and this is the one place
		// the lifetime rule can be stated.
		static void SetScene(Scene* scene);
		static Scene* GetScene();

		// The table, for tests and for anything that wants to call the same
		// functions managed code sees.
		static const NativeApi& Api();

		// The version both sides were built against.
		static constexpr int32_t kProtocolVersion = 1;
	};
}
