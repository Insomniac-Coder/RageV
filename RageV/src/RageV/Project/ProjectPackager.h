#pragma once
#include "Project.h"
#include "RageV/UI/Interaction.h"

#include <atomic>
#include <filesystem>
#include <functional>
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

		// Ship the content as a loose folder instead of content.pak.
		//
		// A debugging affordance: a loose build is one where a single file can
		// be swapped to test a fix without repacking. The default is the
		// archive, because the archive is what players get and the thing that
		// ships should be the thing that was tested. Loose content is always
		// raw -- source files are what loose means.
		bool LooseContent = false;

		// Pack source bytes instead of cooking them (7.2). The default cooks:
		// textures become .rvtex (pre-decoded, pre-mipped, block-compressed)
		// and models become .rvmesh, each under the path and name the source
		// had, so nothing that references them can tell. Raw exists for
		// debugging a cook-shaped problem -- a raw pak renders
		// pixel-identically to a loose folder, a cooked one is lossy by
		// design and bounded by measurement.
		bool RawContent = false;

		// What the packaged game's `ragev.ini` should name as its backend:
		// "vulkan", "opengl", or empty.
		//
		// **Empty means "whatever this process is running"**, which is what the
		// editor wants -- a project authored and looked at on one backend
		// should ship with it. A headless tool has no such answer:
		// `EngineConfig::Get()` there returns defaults and says so, so `rvpack`
		// passes one explicitly rather than shipping whatever the default
		// happened to be.
		std::string Backend;

		// --- for a caller running this off the main thread --------------------

		// One line at a time, as it happens. The editor tees this into the
		// Build Log so a package build reads like the script build does; the
		// CLI leaves it null and prints its own summary at the end.
		//
		// **Called from whichever thread WritePackage runs on**, so an
		// implementation has to be the one doing the locking.
		std::function<void(const std::string&)> Log;

		// Checked between files. Set it and the build stops at the next one and
		// reports Cancelled rather than leaving a half-written folder claiming
		// success.
		const std::atomic<bool>* Cancel = nullptr;
	};

	struct PackageResult
	{
		bool Success = false;

		// Stopped by the caller rather than by a problem. Distinct from
		// !Success with errors: there is nothing to fix and nothing to report
		// as broken, and a console that says "build failed" after somebody
		// pressed Cancel is a console nobody believes twice.
		bool Cancelled = false;

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
	// Layout:
	//     <output>/<Name>.exe          the runtime
	//     <output>/<Name>.rvproject    rewritten to point at content/
	//     <output>/assets/             engine assets: shaders, fonts
	//     <output>/content.pak         the project's assets, .meta included
	//     <output>/ragev.ini           backend and vsync, editable by the player
	//
	// The content is one archive (7.1) -- `content/` as a loose folder only
	// under LooseContent -- and `Project::Load` mounts it over the asset root,
	// so the same paths resolve either way. Engine assets stay a loose tree:
	// the renderer needs them before any project opens, and shader iteration
	// in development is precious.
	//
	// `.meta` sidecars ship, inside the pak. They are the identity: a scene
	// refers to an asset by handle, and the handle lives in the sidecar.
	// Leaving them out produces a game whose every asset reference resolves
	// to nothing.
	// Everything the writing half needs, read out of the open project once.
	//
	// **This exists so the writing half can run on a worker thread.** The
	// packager used to read `Project::*` throughout, which is fine on the main
	// thread and a race off it -- the project can be closed or switched while
	// a build runs, and half of one project with half of another is worse than
	// a build against a stale snapshot. The script build had already drawn that
	// line for itself; this is the same line, drawn for the packager.
	struct PackagePlan
	{
		// False when validation found something. The errors are in Errors and
		// nothing has been written.
		bool Ok = false;

		std::vector<std::string> Errors;
		std::vector<std::string> Warnings;

		// The engine's side: what to ship and where it was found.
		std::filesystem::path Runtime;
		std::filesystem::path EngineDll;
		std::filesystem::path EngineAssets;

		// The project's side, copied rather than referenced.
		std::filesystem::path ProjectRoot;
		std::filesystem::path AssetRoot;
		ProjectConfig Config;

		// Config.Name reduced to something a file system will take.
		std::string Name;

		// Which methods each script type answers to, taken from the C# runtime
		// and the C++ registry on the main thread.
		//
		// **The scene walk that uses this is the slow half of packaging** --
		// every `.rage` in the project, parsed -- and it was on the main
		// thread purely because the answer came from .NET. With the answers
		// taken once up front, the walk moves to the worker and the editor
		// stops freezing for the length of it.
		UI::MethodTable ScriptMethods;

		// What the packaged game's `ragev.ini` says, taken from the editor
		// that is building it rather than assumed.
		//
		// **This used to be hardcoded to Vulkan**, so a project built by an
		// editor running OpenGL shipped a game that started on Vulkan -- and on
		// a machine without it, a game that did not start at all. The backend a
		// project is being authored and looked at on is the one it should ship
		// with, and the player can still edit the file.
		std::string Backend = "vulkan";
		bool VSync = true;
	};

	// Validates the open project and takes the snapshot. **Main thread**: it
	// reads `Project::*`, and its scene-binding check calls into the C# runtime
	// to ask which methods a script has.
	PackagePlan PlanPackage(const PackageDesc& desc);

	// Writes the package. Touches no globals -- everything it needs is in the
	// plan -- so it is safe on a worker while the editor carries on.
	PackageResult WritePackage(const PackageDesc& desc, const PackagePlan& plan);

	// Both halves, for a caller with no reason to separate them. What the CLI
	// packager uses, and what the editor used before the build became
	// asynchronous.
	PackageResult PackageProject(const PackageDesc& desc);

	// Where the runtime probably is, given where this executable is.
	//
	// Beside it in a shipped editor; a sibling directory in the build tree,
	// where each application gets its own output folder. Empty when neither
	// holds one.
	std::filesystem::path FindRuntime();
}
