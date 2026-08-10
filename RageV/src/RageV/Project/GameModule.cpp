#include <rvpch.h>
#include "GameModule.h"
#include "ModuleBuild.h"
#include "RageV/Core/Log.h"
#include "RageV/Scene/ScriptRegistry.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace RageV
{
	namespace
	{
		HMODULE s_Module = nullptr;
		int s_Scope = 0;
		std::filesystem::path s_Path;
	}

	bool GameModule::Load(const std::filesystem::path& projectRoot, const std::string& name)
	{
		Unload();

		if (projectRoot.empty() || name.empty())
			return false;

		// The development layout first, then the packaged one: a shipped game
		// has the module beside its .rvproject, because the packager put it
		// there and there is no bin/ in a package.
		std::error_code ec;
		std::filesystem::path module = ModuleBuild::ModuleFor(projectRoot, name);
		if (!std::filesystem::exists(module, ec))
			module = projectRoot / (name + ".dll");
		if (!std::filesystem::exists(module, ec))
			return false;   // no module is a normal state, not a failure

		// The bracket is what attributes the registrations: everything that
		// registers between here and EndModuleScope came from this DLL's
		// static initialisers, and is what UnregisterScope will remove.
		const int scope = ScriptRegistry::BeginModuleScope();
		HMODULE loaded = LoadLibraryW(module.wstring().c_str());
		ScriptRegistry::EndModuleScope();

		if (!loaded)
		{
			// Whatever partially registered before the load failed must not
			// stay: its code is not mapped.
			ScriptRegistry::UnregisterScope(scope);
			RV_CORE_ERROR("Could not load {0} (error {1})", module.string(), GetLastError());
			return false;
		}

		s_Module = loaded;
		s_Scope = scope;
		s_Path = module;

		const std::vector<std::string> scripts = ScriptRegistry::NamesInScope(scope);
		if (scripts.empty())
		{
			// Loaded and registered nothing: worth saying, because it is what
			// a module whose registrars were somehow discarded would look
			// like, and that failure is otherwise silent.
			RV_CORE_WARN("{0} loaded but registered no scripts", module.filename().string());
		}
		else
		{
			std::string joined;
			for (const std::string& script : scripts)
				joined += (joined.empty() ? "" : ", ") + script;
			RV_CORE_INFO("{0}: {1} script(s) -- {2}",
						 module.filename().string(), scripts.size(), joined);
		}

		return true;
	}

	void GameModule::Unload()
	{
		if (!s_Module)
			return;

		// Strictly before FreeLibrary: a factory outliving its module is a
		// pointer into unmapped code behind a map that still looks correct.
		const size_t removed = ScriptRegistry::UnregisterScope(s_Scope);

		FreeLibrary(s_Module);
		RV_CORE_INFO("Unloaded {0} ({1} script(s) unregistered)",
					 s_Path.filename().string(), removed);

		s_Module = nullptr;
		s_Scope = 0;
		s_Path.clear();
	}

	bool GameModule::IsLoaded()
	{
		return s_Module != nullptr;
	}

	const std::filesystem::path& GameModule::LoadedFrom()
	{
		return s_Path;
	}
}
