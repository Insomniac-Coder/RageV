// Package a project into a folder someone else can run.
//
// A tool rather than only an editor menu, for two reasons. Packaging touches
// no GPU -- it is file copying and one YAML rewrite -- so requiring a device
// and a window to do it would be an odd tax. And a build step that can be
// scripted is a build step that gets run.
//
//     rvpack <project> <output> [--overwrite] [--runtime=<exe>]
//
// <project> is a .rvproject or a folder containing one.

#include <rvpch.h>
#include "RageV/Core/Log.h"
#include "RageV/Project/Project.h"
#include "RageV/Project/ProjectPackager.h"

#include <filesystem>
#include <iostream>

using namespace RageV;

namespace
{
	void PrintUsage()
	{
		std::cout <<
			"rvpack -- package a RageV project\n"
			"\n"
			"  rvpack <project> <output> [options]\n"
			"\n"
			"  <project>          a .rvproject file, or a folder holding one\n"
			"  <output>           where the packaged game goes\n"
			"\n"
			"  --overwrite        build into a directory that already has files in it\n"
			"  --runtime=<exe>    the runtime to ship; found automatically otherwise\n"
			"  --engine-assets=<dir>\n"
			"                     shaders and fonts; taken from beside the runtime otherwise\n"
			"  --rhi=<backend>    what the packaged ragev.ini names: vulkan or opengl.\n"
			"                     Defaults to vulkan. The editor ships whichever\n"
			"                     backend it is running instead -- a headless tool\n"
			"                     has no such answer to give.\n"
			"  --loose            ship content/ as a folder instead of content.pak,\n"
			"                     so single files can be swapped while debugging\n"
			"  --raw              pack source bytes without cooking them; a raw pak\n"
			"                     renders pixel-identically to a loose folder\n";
	}
}

int main(int argc, char** argv)
{
	Log::Init();

	std::filesystem::path projectPath;
	std::filesystem::path outputPath;
	PackageDesc desc;

	// **Stated rather than inherited.** Left empty the packager asks
	// EngineConfig, which in a tool with no engine running answers with defaults
	// and warns -- so the CLI would ship whatever the default happened to be,
	// silently, and a packaging script would change behaviour the day that
	// default moved. Vulkan is what this wrote before it was a choice.
	desc.Backend = "vulkan";

	for (int i = 1; i < argc; i++)
	{
		const std::string argument = argv[i];

		if (argument == "--help" || argument == "-h")
		{
			PrintUsage();
			return 0;
		}

		if (argument == "--overwrite")
		{
			desc.Overwrite = true;
		}
		else if (argument == "--loose")
		{
			desc.LooseContent = true;
		}
		else if (argument == "--raw")
		{
			desc.RawContent = true;
		}
		else if (argument == "--allow-dead-bindings")
		{
			desc.AllowDeadBindings = true;
		}
		else if (argument.rfind("--runtime=", 0) == 0)
		{
			desc.RuntimeExecutable = argument.substr(std::string("--runtime=").size());
		}
		else if (argument.rfind("--engine-assets=", 0) == 0)
		{
			desc.EngineAssets = argument.substr(std::string("--engine-assets=").size());
		}
		else if (argument.rfind("--", 0) == 0)
		{
			std::cerr << "error: unknown option " << argument << '\n';
			return 2;
		}
		else if (projectPath.empty())
		{
			projectPath = argument;
		}
		else if (outputPath.empty())
		{
			outputPath = argument;
		}
	}

	if (projectPath.empty() || outputPath.empty())
	{
		PrintUsage();
		return 2;
	}

	const std::filesystem::path found = Project::FindIn(projectPath);
	if (found.empty())
	{
		std::cerr << "error: no .rvproject at " << projectPath.string() << '\n';
		return 1;
	}

	if (!Project::Load(found))
		return 1;

	desc.OutputDirectory = outputPath;

	const PackageResult result = PackageProject(desc);

	// Straight to stdout rather than through the logger. A tool's output is the
	// whole reason it was run, and the logger's level is a build-time decision
	// -- Dist sets it to warn, which made this silent on success.
	for (const std::string& warning : result.Warnings)
		std::cout << "warning: " << warning << '\n';

	if (!result.Success)
	{
		// Every problem, not just the first: fixing them one build at a time is
		// what the batched validation exists to avoid.
		for (const std::string& error : result.Errors)
			std::cerr << "error: " << error << '\n';

		std::cerr << "Packaging failed." << '\n';
		return 1;
	}

	std::cout << "Packaged '" << Project::Config().Name << "' to "
			  << outputPath.string() << '\n'
			  << result.FilesCopied << " files, "
			  << (result.BytesCopied / (1024 * 1024)) << " MB" << '\n'
			  << "Run: " << result.Executable.string() << '\n';
	return 0;
}
