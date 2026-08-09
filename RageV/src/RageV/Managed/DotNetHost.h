#pragma once

// Loading the .NET runtime into this process, so C# can be a scripting
// language without the engine becoming a .NET application.
//
// Three ways exist to do this and only one of them is right here.
//
//   - Mono, embedded. The classic game-engine answer, and the reason Unity
//     spent a decade on an old runtime. Not chosen.
//   - CoreCLR through `coreclr_initialize`. Works, but it is the low-level
//     entry point: the caller becomes responsible for probing paths and the
//     framework resolution that hostfxr exists to do.
//   - **hostfxr**, which is what `dotnet` itself uses. Chosen. It finds the
//     framework, honours the runtimeconfig.json beside the assembly, and hands
//     back a function pointer to a managed method.
//
// The engine does not link against nethost, and building it does not require
// the .NET SDK. That is deliberate: this repository builds from a clone with
// nothing installed but a compiler, and one optional feature is not worth
// giving that up. Instead hostfxr.dll is located and loaded at *runtime*, and
// the four types needed from its API are declared below rather than vendored --
// the surface is small enough that a copy of Microsoft's headers would be more
// to keep in sync than to write.
//
// When no runtime is installed, this reports that and the engine runs exactly
// as it does today. A missing optional dependency is a state to describe, not
// a crash.

#include <filesystem>
#include <string>

namespace RageV
{
	// Passed as the delegate type name to say "the managed method is
	// [UnmanagedCallersOnly]". The value is hostfxr's, not ours: it is a
	// sentinel pointer, not a string, and it is the documented way to ask for a
	// plain function pointer rather than a marshalled delegate.
	inline const wchar_t* const kUnmanagedCallersOnly = (const wchar_t*)-1;

	class DotNetHost
	{
	public:
		// Boots the runtime described by a .runtimeconfig.json. Safe to call
		// more than once; the second call is a no-op that reports the first
		// call's result.
		//
		// False means the runtime is unavailable, never that the engine is
		// broken. Ask GetUnavailableReason for a sentence fit to show a user.
		static bool Init(const std::filesystem::path& runtimeConfig);
		static void Shutdown();

		static bool IsAvailable();

		// Empty while available. Otherwise a plain-English reason, already
		// logged once -- the editor shows this rather than inventing its own.
		static const std::string& GetUnavailableReason();

		// The framework version that was actually loaded, for the about box and
		// for bug reports. Empty when unavailable.
		static const std::string& GetRuntimeVersion();

		// Where the runtime was found. Empty when unavailable.
		static const std::filesystem::path& GetRuntimePath();

		// A function pointer to a static managed method.
		//
		//   assembly   path to the managed .dll
		//   type       assembly-qualified, e.g. "RageV.Interop, RageV.ScriptCore"
		//   method     the static method's name
		//   delegate   kUnmanagedCallersOnly, or an assembly-qualified delegate
		//              type for a marshalled call
		//
		// Returns nullptr on failure, having logged what went wrong. Callers
		// cast this to the signature they declared on the managed side, and
		// getting that wrong is a silent stack corruption -- so every use of
		// this should be in one place that owns the signatures, not scattered.
		static void* GetFunctionPointer(const std::filesystem::path& assembly,
										const std::string& type,
										const std::string& method,
										const wchar_t* delegate = kUnmanagedCallersOnly);

		// Which .NET installation would be used, without booting anything.
		// Exposed so the editor can say "install the .NET 8 runtime" before a
		// project is even opened, rather than at the moment someone presses
		// Play.
		static std::filesystem::path FindRuntimeRoot();
	};
}
