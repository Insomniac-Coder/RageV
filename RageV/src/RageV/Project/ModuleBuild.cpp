#include <rvpch.h>
#include "ModuleBuild.h"
#include "RageV/Core/Log.h"

#include <chrono>
#include <regex>
#include <sstream>

namespace RageV
{
	namespace
	{
		// MSVC's compile diagnostic:
		//
		//   C:\p\Rotator.cpp(12,5): error C2065: 'x': undeclared identifier [C:\p\Sample.vcxproj]
		//
		// The column is optional -- older toolsets print (12) alone -- and the
		// trailing project in brackets is dropped for the same reason
		// ScriptBuild drops it: it is the same for every line of the build.
		const std::regex& CompilePattern()
		{
			static const std::regex pattern(
				R"(^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(?:fatal )?(error|warning)\s+([A-Z]+\d+)\s*:\s*(.*?)(?:\s*\[[^\]]*\])?\s*$)");
			return pattern;
		}

		// The linker's, which has a file but no line:
		//
		//   Rotator.obj : error LNK2019: unresolved external symbol ...
		const std::regex& LinkPattern()
		{
			static const std::regex pattern(
				R"(^\s*(.+?)\s*:\s*(?:fatal )?(error|warning)\s+(LNK\d+)\s*:\s*(.*?)(?:\s*\[[^\]]*\])?\s*$)");
			return pattern;
		}

		// CMake's own, from a failed configure:
		//
		//   CMake Error at CMakeLists.txt:33 (message):
		//
		// Only the location line matches; the message follows on later lines
		// and stays in the raw output, which the panel shows whenever there
		// are errors. Best-effort on purpose -- a diagnostic that points at
		// the right file beats a parser that understands every shape.
		const std::regex& CMakePattern()
		{
			static const std::regex pattern(
				R"(^CMake (Error|Warning)(?: \(dev\))? at (.+?):(\d+).*$)");
			return pattern;
		}

		// Every diagnostic in the captured output, deduplicated. MSBuild
		// repeats a diagnostic once per project that saw it, and /MP
		// interleaves them -- same dedup as the C# build, same reason.
		void ParseAllDiagnostics(Managed::BuildResult& result)
		{
			std::istringstream stream(result.Output);
			std::string line;
			while (std::getline(stream, line))
			{
				if (!line.empty() && line.back() == '\r')
					line.pop_back();

				Managed::BuildDiagnostic diagnostic;
				if (!ModuleBuild::ParseDiagnostic(line, diagnostic))
					continue;

				const bool duplicate = std::any_of(
					result.Diagnostics.begin(), result.Diagnostics.end(),
					[&diagnostic](const Managed::BuildDiagnostic& seen)
					{
						return seen.Line == diagnostic.Line && seen.Column == diagnostic.Column
							&& seen.Code == diagnostic.Code && seen.File == diagnostic.File;
					});

				if (!duplicate)
					result.Diagnostics.push_back(std::move(diagnostic));
			}
		}
	}

	std::filesystem::path ModuleBuild::FindCMake()
	{
		std::error_code ec;

		// The cmake that configured the engine. If it built the engine, it can
		// build a module against it, and it is guaranteed to exist on the
		// machine the engine was built on.
#ifdef RV_CMAKE_EXE
		const std::filesystem::path configured = RV_CMAKE_EXE;
		if (std::filesystem::exists(configured, ec))
			return configured;
#endif

		// Otherwise trust PATH, verified by asking rather than assuming --
		// same as ScriptBuild's dotnet probe.
		bool launched = false;
		const std::string probe = Managed::ScriptBuild::RunAndCapture("cmake --version", launched);
		if (launched && probe.find("cmake version") != std::string::npos)
			return "cmake";

		return {};
	}

	bool ModuleBuild::IsAvailable()
	{
		return !FindCMake().empty();
	}

	bool ModuleBuild::ProjectHasModule(const std::filesystem::path& projectRoot)
	{
		std::error_code ec;
		return !projectRoot.empty()
			&& std::filesystem::exists(projectRoot / "Source" / "CMakeLists.txt", ec);
	}

	const char* ModuleBuild::Configuration()
	{
		// The engine's own build config, which the module must match: the
		// import library is per-config, and a mixed CRT corrupts a heap
		// rather than failing to link.
#if defined(RV_DIST)
		return "Dist";
#elif defined(RV_RELEASE)
		return "Release";
#else
		return "Debug";
#endif
	}

	std::filesystem::path ModuleBuild::ModuleFor(const std::filesystem::path& projectRoot,
												 const std::string& projectName)
	{
		return projectRoot / "bin" / Configuration() / (projectName + ".dll");
	}

	bool ModuleBuild::ParseDiagnostic(const std::string& line, Managed::BuildDiagnostic& out)
	{
		std::smatch match;

		if (std::regex_match(line, match, CompilePattern()))
		{
			out.File = match[1].str();
			out.Line = std::atoi(match[2].str().c_str());
			out.Column = match[3].matched ? std::atoi(match[3].str().c_str()) : 0;
			out.IsError = match[4].str() == "error";
			out.Code = match[5].str();
			out.Message = match[6].str();
			return true;
		}

		if (std::regex_match(line, match, LinkPattern()))
		{
			out.File = match[1].str();
			out.Line = 0;
			out.Column = 0;
			out.IsError = match[2].str() == "error";
			out.Code = match[3].str();
			out.Message = match[4].str();
			return true;
		}

		if (std::regex_match(line, match, CMakePattern()))
		{
			out.File = match[2].str();
			out.Line = std::atoi(match[3].str().c_str());
			out.Column = 0;
			out.IsError = match[1].str() == "Error";
			out.Code = "CMake";
			// The message is on the lines that follow; pointing at the raw
			// output beats pretending this line is the whole story.
			out.Message = "configure failed here -- see the full output";
			return true;
		}

		return false;
	}

	Managed::BuildResult ModuleBuild::Build(const std::filesystem::path& projectRoot,
											const std::string& projectName)
	{
		using Managed::ScriptBuild;

		Managed::BuildResult result;

		std::error_code ec;
		const std::filesystem::path source = projectRoot / "Source";
		if (!std::filesystem::exists(source / "CMakeLists.txt", ec))
		{
			result.Output = "No game module at " + source.string();
			return result;
		}

		const std::filesystem::path cmake = FindCMake();
		if (cmake.empty())
		{
			// The same flag ScriptBuild uses for a missing dotnet: it is the
			// "the tool is absent" case, and the panel words it accordingly.
			result.SdkMissing = true;
			result.Output = "No CMake found. The engine's own build has one; install "
							"CMake or put it on PATH to build C++ scripts.";
			return result;
		}

		const std::filesystem::path buildDir = projectRoot / "bin" / "module";
		const auto started = std::chrono::steady_clock::now();

		// Configure once, when there is no cache yet. After that the build
		// step re-runs CMake itself whenever the glob changes -- that is what
		// CONFIGURE_DEPENDS in the generated CMakeLists is for -- so
		// configuring every build would only add seconds for nothing. A
		// broken cache is repaired by deleting bin/module.
		if (!std::filesystem::exists(buildDir / "CMakeCache.txt", ec))
		{
#ifndef RV_ENGINE_EXPORT_DIR
			result.Output = "This editor was built without an engine export directory, "
							"so it cannot configure a module.";
			return result;
#else
			std::string configure = ScriptBuild::Quote(cmake)
								  + " -S " + ScriptBuild::Quote(source)
								  + " -B " + ScriptBuild::Quote(buildDir)
								  + " -DRAGEV_ENGINE=" + ScriptBuild::Quote(RV_ENGINE_EXPORT_DIR);

			// The whole line wrapped in one extra pair of quotes -- the
			// cmd.exe trap documented in ScriptBuild::Build. cmd strips the
			// first and last quote of a line that begins with one.
			configure = "\"" + configure + "\"";

			RV_CORE_INFO("Configuring {0}'s game module (first build; this is the slow one)",
						 projectName);

			bool launched = false;
			int configureExit = 0;
			result.Output = ScriptBuild::RunAndCapture(configure, launched, &configureExit);
			if (!launched)
			{
				result.SdkMissing = true;
				result.Output = "Could not run " + cmake.string();
				return result;
			}

			// A failed configure leaves a cache that "configure once" would
			// then trust forever. Parse what it said and stop here.
			if (configureExit != 0)
			{
				ParseAllDiagnostics(result);
				std::filesystem::remove(buildDir / "CMakeCache.txt", ec);
				return result;
			}
#endif
		}

		std::string build = ScriptBuild::Quote(cmake)
						  + " --build " + ScriptBuild::Quote(buildDir)
						  + " --config " + Configuration();
		build = "\"" + build + "\"";

		bool launched = false;
		int buildExit = 0;
		result.Output += ScriptBuild::RunAndCapture(build, launched, &buildExit);
		result.Seconds = std::chrono::duration<float>(
			std::chrono::steady_clock::now() - started).count();

		if (!launched)
		{
			result.SdkMissing = true;
			result.Output = "Could not run " + cmake.string();
			return result;
		}

		ParseAllDiagnostics(result);

		// The exit code and the DLL together, not either alone. The C# build
		// can trust its artifact because its output folder starts empty; the
		// module DLL survives from the previous build, so a failed build with
		// yesterday's DLL beside it would look like success. And the exit code
		// alone would miss the opposite: an "up to date, nothing to do" build
		// exits zero without writing a thing, and is a success.
		const std::filesystem::path module = ModuleFor(projectRoot, projectName);
		if (buildExit == 0 && result.ErrorCount() == 0
			&& std::filesystem::exists(module, ec))
		{
			result.Assembly = module;
			result.Success = true;
		}

		return result;
	}
}
