#pragma once

// Compiling a project's C++ game module.
//
// The same shape as ScriptBuild, deliberately: the editor shells out to the
// build tool people already have, captures everything it prints, parses the
// diagnostics into file/line/message, and hands the result to the same panel.
// A C++ error and a C# error should read the same way in the editor, because
// to the person fixing them they are the same kind of thing.
//
// The tool here is CMake driving MSVC. Which cmake: the one that configured
// the engine, baked in as RV_CMAKE_EXE -- if it built the engine it can build
// a module against it -- with PATH as the fallback for an editor that was
// moved to a machine the engine was not built on.
//
// The result reuses Managed::BuildResult rather than defining a twin. The
// struct is build-tool agnostic -- file, line, code, message -- and a second
// identical type would just mean the panel rendering it twice.

#include "RageV/Managed/ScriptBuild.h"

#include <filesystem>
#include <string>

namespace RageV
{
	class ModuleBuild
	{
	public:
		// Where cmake is, or empty.
		static std::filesystem::path FindCMake();
		static bool IsAvailable();

		// True when the project has a Source/CMakeLists.txt -- the thing that
		// decides whether Build Scripts touches C++ at all. A project made
		// before modules existed simply has no Source/ and builds nothing.
		static bool ProjectHasModule(const std::filesystem::path& projectRoot);

		// The configuration this build of the engine was compiled as:
		// "Debug", "Release" or "Dist".
		//
		// The module must be built as the same one. The import library is
		// per-config, and mixing runtimes is worse than a link error: a Release
		// module in a Debug editor allocates std::strings on one CRT and frees
		// them on the other, which corrupts a heap rather than failing.
		static const char* Configuration();

		// Where the built module lands: <projectRoot>/bin/<Config>/<name>.dll.
		static std::filesystem::path ModuleFor(const std::filesystem::path& projectRoot,
											   const std::string& projectName);

		// Configures (first time only) and builds Source/ into bin/<Config>/.
		// Blocking -- the editor runs it on a worker thread and hands it the
		// cancel flag, which terminates the whole compiler tree, and the tee,
		// which is what the live console drinks from.
		static Managed::BuildResult Build(const std::filesystem::path& projectRoot,
										  const std::string& projectName,
										  const std::atomic<bool>* cancel = nullptr,
										  const ChildProcess::OutputSink& tee = {});

		// Parses one MSVC compile, link or CMake diagnostic line. Exposed for
		// the test, like ScriptBuild's: the format is the only thing here that
		// can quietly stop matching.
		static bool ParseDiagnostic(const std::string& line, Managed::BuildDiagnostic& out);
	};
}
