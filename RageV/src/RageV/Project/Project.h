#pragma once
#include "RageV/Asset/Asset.h"
#include "RageV/Renderer/RenderSettings.h"
#include <filesystem>
#include <memory>
#include <string>

namespace RageV
{
	// What a project is made of. Everything here goes to disk.
	struct ProjectConfig
	{
		std::string Name = "Untitled";

		// Relative to the project folder, always. An absolute path in a project
		// file is a project that only opens on the machine that made it.
		std::string AssetDirectory = "assets";

		// What the runtime opens. Relative to the asset directory, so it is an
		// asset path like any other.
		std::string StartScene;

		// Simulation rate. Here rather than only on the command line because it
		// changes how a game behaves, so it belongs with the game rather than
		// with whoever happens to launch it.
		uint32_t FixedHz = 60;

		// What the frame costs: anti-aliasing and shadows.
		//
		// Here rather than on each scene, which is where they used to be. They
		// are a judgement about the hardware, and that judgement does not
		// change because a different level loaded -- while storing them per
		// scene meant changing the shadow resolution forty times in a project
		// with forty scenes. ENGINE-NOTES 7s.
		//
		// Read and written through `RenderSettingsRegistry`, not by a list in
		// the project's serializer: a hand-written list is what let
		// `TemporalFeedback` be registered, inspectable, and saved nowhere.
		RenderSettings Render;
	};

	// A folder is a project.
	//
	// This is the thing every part of the engine has quietly been missing. The
	// asset registry currently roots itself at the folder beside the
	// executable, which CMake overwrites on every build -- so a `.meta` minted
	// there is destroyed by the next compile and every scene referring to that
	// asset breaks. Rooting the registry at a project folder instead is the
	// actual fix rather than a workaround, and it is the same change that lets
	// a runtime be pointed at a game.
	//
	// One project is open at a time. Not a limitation worth designing around:
	// no engine with a scene graph, an asset registry and a physics world as
	// process-wide state can host two at once without those becoming
	// per-project, which is a much larger change than anything it would buy.
	class Project
	{
	public:
		// Null until something is loaded.
		static Project* GetActive();

		// Reads a .rvproject. Returns false and changes nothing if the file is
		// missing or unreadable, so a failed open leaves the editor on whatever
		// it already had.
		static bool Load(const std::filesystem::path& projectFile);

		// Creates the folder, an assets directory inside it, and the project
		// file. Fails rather than overwriting an existing project.
		static bool Create(const std::filesystem::path& directory, const std::string& name);

		static bool Save();
		static void Close();

		// The folder holding the .rvproject.
		static std::filesystem::path Root();
		// Root() / "bin". Where a build of this project lands.
		//
		// A convention rather than a setting, and per project rather than
		// somewhere central: a build belongs next to the thing it was built
		// from, so a project folder can be zipped, moved or handed over with
		// its output intact. Created by Create(); a project made before this
		// existed simply has the folder made on first build.
		static std::filesystem::path BinaryRoot();
		// Root() / AssetDirectory. Where the asset registry is pointed.
		static std::filesystem::path AssetRoot();
		// Root() / "Cache". Where cooked assets are kept between launches.
		//
		// Deliberately *outside* AssetRoot(): the registry walks the asset
		// directory and mints a handle plus a `.meta` for everything it finds,
		// so a cooked file living there would become content -- content that
		// shadows the source it was cooked from. Outside, it is invisible to
		// every part of the engine except the one that reads it.
		//
		// Derived data, and git-ignored for a sharper reason than size: the
		// hash a cooked file is keyed by is in its name, so a copy arriving
		// over a checkout claims to be current for a source it never saw.
		static std::filesystem::path CacheRoot();
		// Absolute path for something stored relative to the asset directory.
		static std::filesystem::path AssetPath(const std::string& relative);

		static ProjectConfig& Config();

		// Shorthand for Config().Render, and the thing almost every caller
		// actually wants. Without a project open this is a static default,
		// which is what lets a headless tool build a frame.
		static RenderSettings& Render();

		static const std::filesystem::path& File();

		// Absolute -> relative to the asset directory, with forward slashes.
		// Empty when the path is outside the project, which is the case worth
		// refusing rather than storing.
		static std::string MakeRelative(const std::filesystem::path& absolute);

		// Opens whatever the configuration points at, in one place, so the
		// editor, the tests and the runtime all resolve a project identically.
		//
		// In order: --project= or ragev.ini; a .rvproject beside the
		// executable, which is what a packaged game has; then
		// RV_DEFAULT_PROJECT, which CMake bakes in so a build run out of the
		// build tree finds the sample project without being told.
		//
		// Returns false when there is no project to open, which is a state the
		// editor is expected to handle rather than an error.
		static bool OpenConfigured();

		// A .rvproject path given a file or a folder containing one. Empty when
		// the folder holds none.
		static std::filesystem::path FindIn(const std::filesystem::path& fileOrDirectory);

		static constexpr const char* kExtension = ".rvproject";
	};
}
