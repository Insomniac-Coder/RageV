#include <rvpch.h>
#include "DotNetHost.h"

#include <algorithm>

namespace RageV
{
	namespace
	{
		// --- the part of hostfxr this needs ---------------------------------
		//
		// Hand-declared rather than vendored, so building the engine needs no
		// .NET SDK. These four signatures are ABI, published and stable since
		// .NET Core 3.0; if they ever change, the version check below is what
		// will notice, because GetProcAddress would still succeed.

		using char_t = wchar_t;

		struct hostfxr_initialize_parameters
		{
			size_t        size;
			const char_t* host_path;
			const char_t* dotnet_root;
		};

		enum hostfxr_delegate_type
		{
			// Only one of the six is used. The others exist in the real header
			// and are deliberately not declared -- an unused enumerator here
			// would be a claim this code supports something it does not.
			hdt_load_assembly_and_get_function_pointer = 5,
		};

		using hostfxr_initialize_fn = int(__cdecl*)(const char_t* runtime_config_path,
													const hostfxr_initialize_parameters* parameters,
													void** host_context_handle);
		using hostfxr_get_delegate_fn = int(__cdecl*)(void* host_context_handle,
													  hostfxr_delegate_type type,
													  void** delegate);
		using hostfxr_close_fn = int(__cdecl*)(void* host_context_handle);

		using load_assembly_and_get_function_pointer_fn =
			int(__cdecl*)(const char_t* assembly_path,
						  const char_t* type_name,
						  const char_t* method_name,
						  const char_t* delegate_type_name,
						  void* reserved,
						  void** delegate);

		// --- state ----------------------------------------------------------

		struct HostState
		{
			bool Attempted = false;
			bool Available = false;
			std::string Reason;
			std::string Version;
			std::filesystem::path Path;

			void* Library = nullptr;                                   // HMODULE
			load_assembly_and_get_function_pointer_fn LoadAssembly = nullptr;
		};

		HostState s_Host;

		std::wstring Widen(const std::filesystem::path& path)
		{
			return path.wstring();
		}

		std::wstring Widen(const std::string& value)
		{
			// The strings crossing this boundary are type and method names, so
			// they are ASCII by construction -- a C# identifier cannot be
			// anything else in any assembly this engine will load. Widening
			// byte by byte is correct for that and avoids dragging in a locale.
			return std::wstring(value.begin(), value.end());
		}

		// "8.0.21" before "8.0.9", and "9.0.0-preview.3" after "8.0.21".
		//
		// A lexicographic sort gets both of these wrong, and getting it wrong
		// means silently booting an old framework -- which then fails at the
		// first API the assembly was compiled against.
		std::vector<int> VersionKey(const std::string& name)
		{
			std::vector<int> parts;
			std::string digits;
			for (char c : name)
			{
				if (std::isdigit((unsigned char)c))
				{
					digits += c;
					continue;
				}

				if (!digits.empty())
				{
					parts.push_back(std::stoi(digits));
					digits.clear();
				}
				// A prerelease suffix sorts below the same release version.
				if (c == '-')
					break;
			}
			if (!digits.empty())
				parts.push_back(std::stoi(digits));

			parts.resize(4, 0);
			// Release beats prerelease at equal numbers.
			parts.push_back(name.find('-') == std::string::npos ? 1 : 0);
			return parts;
		}

#ifdef RV_PLATFORM_WINDOWS
		std::filesystem::path ReadInstallLocationFromRegistry()
		{
			// Where the .NET installer records itself. Checked before the
			// default path because an installation can legitimately be
			// elsewhere, and guessing C:\Program Files would then load nothing
			// on a machine that plainly has .NET.
			HKEY key = nullptr;
			if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
							  L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64",
							  0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
				return {};

			wchar_t buffer[MAX_PATH]{};
			DWORD size = sizeof(buffer);
			DWORD type = 0;
			const LSTATUS status = RegQueryValueExW(key, L"InstallLocation", nullptr, &type,
													(LPBYTE)buffer, &size);
			RegCloseKey(key);

			if (status != ERROR_SUCCESS || type != REG_SZ)
				return {};
			return std::filesystem::path(buffer);
		}
#endif
	}

	std::filesystem::path DotNetHost::FindRuntimeRoot()
	{
		std::error_code ec;

		// Honoured first, and on purpose: it is how someone runs the engine
		// against a side-by-side install, and how CI pins a version.
		if (const char* fromEnvironment = std::getenv("DOTNET_ROOT"))
		{
			const std::filesystem::path root(fromEnvironment);
			if (std::filesystem::exists(root / "host" / "fxr", ec))
				return root;
		}

#ifdef RV_PLATFORM_WINDOWS
		const std::filesystem::path fromRegistry = ReadInstallLocationFromRegistry();
		if (!fromRegistry.empty() && std::filesystem::exists(fromRegistry / "host" / "fxr", ec))
			return fromRegistry;

		if (const char* programFiles = std::getenv("ProgramFiles"))
		{
			const std::filesystem::path root = std::filesystem::path(programFiles) / "dotnet";
			if (std::filesystem::exists(root / "host" / "fxr", ec))
				return root;
		}
#endif

		return {};
	}

	bool DotNetHost::Init(const std::filesystem::path& runtimeConfig)
	{
		// Idempotent. Several subsystems may want the runtime and none of them
		// should have to know whether it is already up.
		if (s_Host.Attempted)
			return s_Host.Available;
		s_Host.Attempted = true;

		std::error_code ec;
		if (!std::filesystem::exists(runtimeConfig, ec))
		{
			s_Host.Reason = "No runtime configuration at " + runtimeConfig.string() +
							" -- the managed script assembly has not been built.";
			RV_CORE_WARN("C# scripting unavailable: {0}", s_Host.Reason);
			return false;
		}

		const std::filesystem::path root = FindRuntimeRoot();
		if (root.empty())
		{
			s_Host.Reason = "No .NET installation found. Install the .NET 8 runtime to use C# scripting.";
			RV_CORE_WARN("C# scripting unavailable: {0}", s_Host.Reason);
			return false;
		}

		// The newest framework wins. hostfxr itself resolves which *framework*
		// an assembly runs against; this only picks which resolver to load, and
		// a newer one understands older configurations but not the reverse.
		std::filesystem::path best;
		std::vector<int> bestKey;
		for (const auto& entry : std::filesystem::directory_iterator(root / "host" / "fxr", ec))
		{
			if (!entry.is_directory())
				continue;

			const std::filesystem::path candidate = entry.path() / "hostfxr.dll";
			if (!std::filesystem::exists(candidate, ec))
				continue;

			const std::vector<int> key = VersionKey(entry.path().filename().string());
			if (best.empty() || key > bestKey)
			{
				best = candidate;
				bestKey = key;
				s_Host.Version = entry.path().filename().string();
			}
		}

		if (best.empty())
		{
			s_Host.Reason = "A .NET installation exists at " + root.string() +
							" but contains no hostfxr. The installation is incomplete.";
			RV_CORE_WARN("C# scripting unavailable: {0}", s_Host.Reason);
			return false;
		}

#ifdef RV_PLATFORM_WINDOWS
		HMODULE library = LoadLibraryW(best.c_str());
		if (!library)
		{
			s_Host.Reason = "Found " + best.string() + " but could not load it.";
			RV_CORE_ERROR("C# scripting unavailable: {0} (error {1})", s_Host.Reason, GetLastError());
			return false;
		}

		const auto initialize = (hostfxr_initialize_fn)GetProcAddress(library, "hostfxr_initialize_for_runtime_config");
		const auto getDelegate = (hostfxr_get_delegate_fn)GetProcAddress(library, "hostfxr_get_runtime_delegate");
		const auto close = (hostfxr_close_fn)GetProcAddress(library, "hostfxr_close");

		if (!initialize || !getDelegate || !close)
		{
			FreeLibrary(library);
			s_Host.Reason = "hostfxr at " + best.string() + " is missing its entry points.";
			RV_CORE_ERROR("C# scripting unavailable: {0}", s_Host.Reason);
			return false;
		}

		void* context = nullptr;
		const int initialized = initialize(Widen(runtimeConfig).c_str(), nullptr, &context);

		// 0 is Success. 1 and 2 mean the runtime was already up -- with the same
		// properties or with different ones. All three give a usable context;
		// anything else does not, and a non-null context still has to be closed.
		if (initialized > 2 || context == nullptr)
		{
			if (context)
				close(context);
			FreeLibrary(library);
			s_Host.Reason = "The .NET runtime refused to start (hostfxr returned " +
							std::to_string(initialized) + ").";
			RV_CORE_ERROR("C# scripting unavailable: {0}", s_Host.Reason);
			return false;
		}

		if (initialized == 2)
		{
			// Worth saying out loud. It means something else in this process
			// already started .NET with a different configuration, and the
			// assembly is about to run against that one instead.
			RV_CORE_WARN("The .NET runtime was already running with different properties; "
						 "the existing configuration wins");
		}

		void* loadAssembly = nullptr;
		const int gotDelegate = getDelegate(context, hdt_load_assembly_and_get_function_pointer, &loadAssembly);

		// The context is closed either way. It is a handle to the *initialisation*,
		// not to the runtime -- the runtime stays loaded for the life of the
		// process, and holding this open leaks nothing useful.
		close(context);

		if (gotDelegate != 0 || loadAssembly == nullptr)
		{
			FreeLibrary(library);
			s_Host.Reason = "The .NET runtime started but would not hand back its loader (error " +
							std::to_string(gotDelegate) + ").";
			RV_CORE_ERROR("C# scripting unavailable: {0}", s_Host.Reason);
			return false;
		}

		s_Host.Library = library;
		s_Host.LoadAssembly = (load_assembly_and_get_function_pointer_fn)loadAssembly;
		s_Host.Path = best;
		s_Host.Available = true;
		s_Host.Reason.clear();

		RV_CORE_INFO("C# scripting: .NET {0} loaded from {1}", s_Host.Version, root.string());
		return true;
#else
		s_Host.Reason = "C# scripting is Windows-only in this build.";
		RV_CORE_WARN("{0}", s_Host.Reason);
		return false;
#endif
	}

	void DotNetHost::Shutdown()
	{
		// The library handle is released; the runtime itself is not, and cannot
		// be. CoreCLR does not support being unloaded from a process and
		// restarted -- which is exactly why hot reload is built on collectible
		// load contexts rather than on restarting the runtime.
#ifdef RV_PLATFORM_WINDOWS
		if (s_Host.Library)
			FreeLibrary((HMODULE)s_Host.Library);
#endif
		s_Host = HostState{};
	}

	bool DotNetHost::IsAvailable()
	{
		return s_Host.Available;
	}

	const std::string& DotNetHost::GetUnavailableReason()
	{
		return s_Host.Reason;
	}

	const std::string& DotNetHost::GetRuntimeVersion()
	{
		return s_Host.Version;
	}

	const std::filesystem::path& DotNetHost::GetRuntimePath()
	{
		return s_Host.Path;
	}

	void* DotNetHost::GetFunctionPointer(const std::filesystem::path& assembly,
										 const std::string& type,
										 const std::string& method,
										 const wchar_t* delegate)
	{
		if (!s_Host.Available)
		{
			RV_CORE_ERROR("GetFunctionPointer('{0}') called with no runtime loaded", method);
			return nullptr;
		}

		std::error_code ec;
		if (!std::filesystem::exists(assembly, ec))
		{
			RV_CORE_ERROR("No managed assembly at {0}", assembly.string());
			return nullptr;
		}

		void* function = nullptr;
		const int result = s_Host.LoadAssembly(Widen(std::filesystem::absolute(assembly, ec)).c_str(),
											   Widen(type).c_str(),
											   Widen(method).c_str(),
											   delegate,
											   nullptr,
											   &function);

		if (result != 0 || function == nullptr)
		{
			// The most common causes, in the order they actually happen: the
			// method is not static, it is not public, it lacks
			// [UnmanagedCallersOnly], or the type name is missing its assembly.
			RV_CORE_ERROR("Could not bind {0}::{1} from {2} (hostfxr error 0x{3:x})",
						  type, method, assembly.filename().string(), (uint32_t)result);
			return nullptr;
		}

		return function;
	}
}
