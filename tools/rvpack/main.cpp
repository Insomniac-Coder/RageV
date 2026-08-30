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
			"  --rhi=<a,b>        which backends the game runs on, preferred first:\n"
			"                     vulkan, opengl, or both. Defaults to vulkan; two\n"
			"                     means the game falls back rather than failing on\n"
			"                     a machine with no driver for the first.\n"
			"  --scenes=<a,b>     only these scenes, asset-relative. Every scene\n"
			"                     ships when this is left off; the start scene\n"
			"                     always ships whatever this says.\n"
			"  --start-scene=<a>  the scene the game opens with, asset-relative.\n"
			"                     Defaults to the project's own; it must be one of\n"
			"                     the scenes that ship, and is folded in if it is\n"
			"                     not among --scenes.\n"
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
	desc.Backends = { "vulkan" };

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
		else if (argument.rfind("--rhi=", 0) == 0)
		{
			// Comma separated and ordered: the first is what the game starts
			// on, the rest are what it falls through to. "vulkan,opengl" is a
			// build that survives a machine with no Vulkan driver.
			desc.Backends.clear();

			std::stringstream stream(argument.substr(std::string("--rhi=").size()));
			std::string entry;
			while (std::getline(stream, entry, ','))
			{
				if (!entry.empty())
					desc.Backends.push_back(entry);
			}
		}
		else if (argument.rfind("--scenes=", 0) == 0)
		{
			// Asset-relative, comma separated. Left off, every scene ships.
			std::stringstream stream(argument.substr(std::string("--scenes=").size()));
			std::string entry;
			while (std::getline(stream, entry, ','))
			{
				if (!entry.empty())
					desc.Scenes.push_back(entry);
			}
		}
		else if (argument.rfind("--start-scene=", 0) == 0)
		{
			desc.StartScene = argument.substr(std::string("--start-scene=").size());
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
