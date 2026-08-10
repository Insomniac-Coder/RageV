#pragma once

// A project's C++ scripts, loaded into the running engine.
//
// The module is a DLL the project's Source/ builds -- see ModuleBuild -- and
// loading it is what makes its scripts exist: RV_REGISTER_SCRIPT runs from
// static initialisers during LoadLibrary, so the moment the load returns, the
// scripts are in the registry and the inspector's dropdown. No manifest, no
// reflection, no list to keep in step with the code.
//
// Unloading is the half that bites. The registry holds function pointers into
// the module's code, so they are unregistered *before* FreeLibrary -- see
// ScriptRegistry's module scopes -- and the caller must ensure no script
// instance from the module is alive. Instances only exist while a scene
// plays, which is why the editor refuses to swap the module mid-play rather
// than trying to be clever about it.
//
// One module at a time, like one project at a time, and for the same reason.

#include <filesystem>
#include <string>

namespace RageV
{
	class GameModule
	{
	public:
		// Loads the project's module if it has been built: bin/<Config>/ in a
		// development tree, or beside the .rvproject in a packaged game. A
		// project with no module is normal, returns false, and logs nothing
		// alarming. Any previously loaded module is unloaded first.
		static bool Load(const std::filesystem::path& projectRoot, const std::string& name);

		// Unregisters the module's scripts, then frees the DLL. Safe to call
		// with nothing loaded.
		static void Unload();

		static bool IsLoaded();
		static const std::filesystem::path& LoadedFrom();
	};
}
