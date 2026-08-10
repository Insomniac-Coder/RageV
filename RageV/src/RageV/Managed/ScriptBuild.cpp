#include <rvpch.h>
#include "ScriptBuild.h"
#include "DotNetHost.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <regex>

namespace RageV::Managed
{
	namespace
	{
		// MSBuild's diagnostic format, which every C# tool emits:
		//
		//   C:\path\Player.cs(12,7): error CS0103: The name 'x' ... [C:\proj.csproj]
		//
		// The trailing project in brackets is dropped -- it is the same for
		// every line of a build and takes up the width an editor would rather
		// spend on the message.
		const std::regex& DiagnosticPattern()
		{
			static const std::regex pattern(
				R"(^\s*(.+?)\((\d+),(\d+)\)\s*:\s*(error|warning)\s+([A-Za-z]+\d+)\s*:\s*(.*?)(?:\s*\[[^\]]*\])?\s*$)");
			return pattern;
		}

	}

	std::string ScriptBuild::RunAndCapture(const std::string& command, bool& launched,
										   int* exitCode)
	{
		// _popen rather than CreateProcess with pipes: this needs the child's
		// output and its exit code and nothing else, and the twenty lines of
		// handle plumbing the alternative costs buy nothing here.
		//
		// stderr is folded into stdout because MSBuild puts diagnostics on
		// both depending on the failure, and a build log split across two
		// streams reorders itself unhelpfully.
		FILE* pipe = _popen((command + " 2>&1").c_str(), "r");
		if (!pipe)
		{
			launched = false;
			return {};
		}

		launched = true;
		std::string output;
		std::array<char, 512> chunk{};
		while (std::fgets(chunk.data(), (int)chunk.size(), pipe))
			output += chunk.data();

		// _pclose hands back the child's exit status -- cmd's, which is the
		// build tool's own.
		const int status = _pclose(pipe);
		if (exitCode)
			*exitCode = status;

		return output;
	}

	std::string ScriptBuild::Quote(const std::filesystem::path& path)
	{
		return "\"" + path.string() + "\"";
	}

	size_t BuildResult::ErrorCount() const
	{
		size_t count = 0;
		for (const BuildDiagnostic& diagnostic : Diagnostics)
			count += diagnostic.IsError ? 1 : 0;
		return count;
	}

	size_t BuildResult::WarningCount() const
	{
		return Diagnostics.size() - ErrorCount();
	}

	std::filesystem::path ScriptBuild::FindDotnet()
	{
		std::error_code ec;

		// The same order DotNetHost uses to find the runtime, so an engine that
		// can run C# can usually also build it.
		const std::filesystem::path root = DotNetHost::FindRuntimeRoot();
		if (!root.empty())
		{
			const std::filesystem::path candidate = root / "dotnet.exe";
			if (std::filesystem::exists(candidate, ec))
				return candidate;
		}

		// Otherwise trust PATH. Verified by asking it for its version rather
		// than assuming: a `dotnet` that is on PATH but broken should read as
		// missing, not as a build that fails mysteriously later.
		bool launched = false;
		const std::string probe = RunAndCapture("dotnet --version", launched);
		if (launched && !probe.empty() && probe.find("not recognized") == std::string::npos)
			return "dotnet";

		return {};
	}

	bool ScriptBuild::IsAvailable()
	{
		return !FindDotnet().empty();
	}

	std::filesystem::path ScriptBuild::ProjectFileFor(const std::filesystem::path& projectRoot,
													  const std::string& projectName)
	{
		return projectRoot / "Scripts" / (projectName + ".csproj");
	}

	bool ScriptBuild::ParseDiagnostic(const std::string& line, BuildDiagnostic& out)
	{
		std::smatch match;
		if (!std::regex_match(line, match, DiagnosticPattern()))
			return false;

		out.File = match[1].str();
		out.Line = std::atoi(match[2].str().c_str());
		out.Column = std::atoi(match[3].str().c_str());
		out.IsError = match[4].str() == "error";
		out.Code = match[5].str();
		out.Message = match[6].str();
		return true;
	}

	BuildResult ScriptBuild::Build(const std::filesystem::path& csproj,
								   const std::filesystem::path& output,
								   const std::filesystem::path& scriptCore)
	{
		BuildResult result;

		std::error_code ec;
		if (!std::filesystem::exists(csproj, ec))
		{
			result.Output = "No script project at " + csproj.string();
			return result;
		}

		const std::filesystem::path dotnet = FindDotnet();
		if (dotnet.empty())
		{
			result.SdkMissing = true;
			result.Output = "No .NET SDK found. Install the .NET 8 SDK to build C# scripts.";
			return result;
		}

		std::filesystem::create_directories(output, ec);

		// The reference is passed in rather than written into the .csproj,
		// because where RageV.ScriptCore.dll lives depends on where the engine
		// is installed -- and a project file that only builds on the machine
		// that generated it is not a project file.
		std::string command = Quote(dotnet) + " build " + Quote(csproj)
							+ " --configuration Release"
							+ " --output " + Quote(output)
							+ " --nologo"
							+ " -consoleLoggerParameters:NoSummary"
							+ " -p:RageVScriptCore=" + Quote(scriptCore);

		// Wrapped in one more pair of quotes, which looks wrong and is required.
		//
		// _popen runs the command through `cmd /c`, and cmd strips the first and
		// last quote of a command line that begins with one. So a quoted program
		// path arrives as `C:\Program Files\...` unquoted and cmd reports that
		// 'C:\Program' is not a command. Wrapping the whole line gives cmd a
		// pair to eat and leaves the inner quoting intact.
		command = "\"" + command + "\"";

		const auto started = std::chrono::steady_clock::now();
		bool launched = false;
		result.Output = RunAndCapture(command, launched);
		result.Seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count();

		if (!launched)
		{
			result.SdkMissing = true;
			result.Output = "Could not run " + dotnet.string();
			return result;
		}

		std::istringstream stream(result.Output);
		std::string line;
		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();

			BuildDiagnostic diagnostic;
			if (ParseDiagnostic(line, diagnostic))
			{
				// MSBuild repeats a diagnostic once per project that saw it.
				// Reporting the same error three times is how a build log stops
				// being read.
				const bool duplicate = std::any_of(
					result.Diagnostics.begin(), result.Diagnostics.end(),
					[&diagnostic](const BuildDiagnostic& seen)
					{
						return seen.Line == diagnostic.Line && seen.Column == diagnostic.Column
							&& seen.Code == diagnostic.Code && seen.File == diagnostic.File;
					});

				if (!duplicate)
					result.Diagnostics.push_back(std::move(diagnostic));
			}
		}

		// The assembly is the evidence, not the exit code: a build can print
		// nothing alarming and still not have produced anything.
		const std::filesystem::path assembly = output / (csproj.stem().string() + ".dll");
		if (std::filesystem::exists(assembly, ec) && result.ErrorCount() == 0)
		{
			result.Assembly = assembly;
			result.Success = true;
		}

		return result;
	}
}
