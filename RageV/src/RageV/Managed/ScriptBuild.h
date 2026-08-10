#pragma once

// Compiling a project's C# scripts.
//
// The editor shells out to `dotnet build` rather than hosting Roslyn. Hosting
// the compiler would mean shipping it, tracking its version against the SDK
// people already have installed, and owning the difference -- for the one
// benefit of not spawning a process. `dotnet build` is what every C# developer
// already has, already knows how to run by hand when something is strange, and
// already produces the diagnostics an editor wants to show.
//
// What is *not* acceptable is what shelling out usually looks like: output
// going to a console nobody sees, and a boolean at the end. Compiler errors are
// the main way a script author learns anything, so the output is captured,
// parsed into file/line/message, and handed back for the editor to render.

#include <filesystem>
#include <string>
#include <vector>

namespace RageV::Managed
{
	// One line the compiler had something to say about.
	struct BuildDiagnostic
	{
		std::filesystem::path File;
		int Line = 0;
		int Column = 0;

		// "CS0103" and the rest. Kept because it is what somebody types into a
		// search engine, and because a code is stable where a message is not.
		std::string Code;
		std::string Message;

		bool IsError = false;
	};

	struct BuildResult
	{
		bool Success = false;

		// True when there is no `dotnet` at all, which is a different problem
		// from a compile error and gets a different sentence in the editor.
		bool SdkMissing = false;

		// Errors and warnings, in the order the compiler reported them.
		std::vector<BuildDiagnostic> Diagnostics;

		// Everything the compiler printed. Kept whole because parsing is
		// best-effort: an MSBuild failure that is not a CS diagnostic -- a
		// missing reference, a bad SDK -- has to still be readable.
		std::string Output;

		std::filesystem::path Assembly;
		float Seconds = 0.0f;

		size_t ErrorCount() const;
		size_t WarningCount() const;
	};

	class ScriptBuild
	{
	public:
		// Where `dotnet` is, or empty. Looks at DOTNET_ROOT, then the standard
		// install, then PATH -- the same order DotNetHost uses to find the
		// runtime, so an engine that can run C# can usually also build it.
		static std::filesystem::path FindDotnet();
		static bool IsAvailable();

		// Compiles `csproj` into `output`. Blocking: a script assembly is a few
		// files and takes about a second, and a build running in the background
		// while somebody presses Play is a worse problem than a brief pause.
		//
		// `scriptCore` is the RageV.ScriptCore.dll the project references. It
		// is passed on the command line rather than written into the csproj,
		// because the path depends on where the engine is installed and a
		// project file that only builds on one machine is not a project file.
		static BuildResult Build(const std::filesystem::path& csproj,
								 const std::filesystem::path& output,
								 const std::filesystem::path& scriptCore);

		// The .csproj a generated project gets. Static so the project scaffold
		// and the build agree on the name without either owning the other.
		static std::filesystem::path ProjectFileFor(const std::filesystem::path& projectRoot,
													const std::string& projectName);

		// Parses one MSBuild diagnostic line. Exposed for the test: the format
		// is the only thing here that can quietly stop matching.
		static bool ParseDiagnostic(const std::string& line, BuildDiagnostic& out);

		// Shared with ModuleBuild, which shells out the same way to a different
		// compiler. Public so the cmd.exe quoting trap lives in one place: the
		// caller must wrap the *whole* command line in one extra pair of quotes,
		// because cmd strips the first and last quote of a line that begins with
		// one -- see the comment in Build().
		//
		// `exitCode`, when asked for, is the child's. The C# build ignores it
		// and trusts the artifact instead; the module build cannot, because its
		// artifact survives from the previous build and an up-to-date "nothing
		// to do" is a success that writes no file.
		static std::string RunAndCapture(const std::string& command, bool& launched,
										 int* exitCode = nullptr);
		static std::string Quote(const std::filesystem::path& path);
	};
}
