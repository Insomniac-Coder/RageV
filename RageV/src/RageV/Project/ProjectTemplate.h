#pragma once

// What a new project starts as, and the only place that decides it.
//
// **Moved out of the editor's layer because the startup screen needs it and
// the editor's layer does not exist yet.** Creating a project is now something
// that can happen before there is a project to attach an editor to -- it is
// the first thing a person does with the engine -- so the knowledge of what a
// project contains on its first day belongs here, next to Project itself.
// EditorLayer's File > New Project calls the same function, which is what
// keeps the two from drifting into two different starter scenes.

#include "RageV/Core/Boot.h"
#include "RageV/Core/Core.h"

#include <filesystem>
#include <string>

namespace RageV
{
	class RV_API ProjectTemplate
	{
	public:
		// Creates the folder, the project file, the asset directories and a
		// first scene, and leaves the project open with its start scene set.
		//
		// **Reports through `Boot::Progress`, because it is slow enough to
		// need to.** Writing the file is instant; pointing the asset registry
		// at a new root and cooking the first scene is not, and a window that
		// stops repainting for several seconds after a click reads as a hang.
		// The phases are the same shape the loading screen shows, so the two
		// screens run together as one sequence rather than as two.
		//
		// `sceneOut` receives the absolute path of the scene it wrote, which
		// is what the caller opens.
		//
		// Returns false and leaves whatever project was open alone on any
		// failure -- a half-created project that the editor then adopted would
		// be worse than none.
		static bool Create(const std::filesystem::path& directory,
						   const std::string& name,
						   Boot::Progress& progress,
						   std::filesystem::path& sceneOut);

		// The scene a new project opens on, relative to its assets.
		static constexpr const char* kFirstScene = "scenes/Main.rage";
	};
}
