#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace RageV
{
	struct PackageDesc
	{
		// Where the packaged game goes. Created if absent.
		std::filesystem::path OutputDirectory;

		// The runtime to ship. Empty means "find one" -- see FindRuntime.
		std::filesystem::path RuntimeExecutable;

		// The folder holding shaders/ and Fonts/. Empty means the `assets`
		// folder beside the runtime, which is where its own build staged them.
		std::filesystem::path EngineAssets;

		// Refuse to write into a directory that already has something in it.
		// A packager that empties a folder it was pointed at by mistake is a
		// packager that eventually deletes someone's work.
		bool Overwrite = false;

		// Ship anyway when a UI button's OnClick names a method nothing
		// answers to.
		//
		// **The default refuses**, and that is the point of the check. A
		// method name in a scene file has no compiler behind it, so a rename
		// leaves a button that does nothing and says nothing until a player
		// clicks it. Packaging is the last moment anyone looks, so it is where
		// the missing compiler goes.
		//
		// The escape exists because a project may be mid-refactor and want a
		// build regardless -- but it has to be asked for, so nobody ships one
		// by not noticing.
		bool AllowDeadBindings = false;
	};

	struct PackageResult
	{
		bool Success = false;

		// Everything that went wrong, in the order it was found. Validation
		// runs to completion before anything is written, so a broken setup
		// reports all of its problems at once rather than one per attempt.
		std::vector<std::string> Errors;
		std::vector<std::string> Warnings;

		std::filesystem::path Executable;   // what to run
		uint64_t FilesCopied = 0;
		uint64_t BytesCopied = 0;
	};

	// Turns the open project into a folder someone else can run.
	//
	// The output is a directory, not an archive. An archive needs a virtual
	// file system on the loading side to be worth anything, and every asset
	// path in the engine currently goes through std::filesystem -- so packing
	// would mean either a pack format nothing can read or a VFS layer, which
	// is a larger feature than this one. A folder that runs is the whole of
	// the value and none of that cost.
	//
	// Layout:
	//     <output>/<Name>.exe          the runtime
	//     <output>/<Name>.rvproject    rewritten to point at content/
	//     <output>/assets/             engine assets: shaders, fonts
	//     <output>/content/            the project's assets, .meta included
	//     <output>/ragev.ini           backend and vsync, editable by the player
	//
	// The project's assets land in `content/` rather than merging into
	// `assets/`, so the split 4.1 established -- engine assets the renderer
	// needs versus project assets addressed by handle -- survives into the
	// shipped game.
	//
	// `.meta` sidecars ship. They are the identity: a scene refers to an asset
	// by handle, and the handle lives in the sidecar. Leaving them out produces
	// a game whose every asset reference resolves to nothing.
	PackageResult PackageProject(const PackageDesc& desc);

	// Where the runtime probably is, given where this executable is.
	//
	// Beside it in a shipped editor; a sibling directory in the build tree,
	// where each application gets its own output folder. Empty when neither
	// holds one.
	std::filesystem::path FindRuntime();
}
