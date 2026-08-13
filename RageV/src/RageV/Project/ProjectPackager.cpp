#include <rvpch.h>
#include "ProjectPackager.h"
#include "Project.h"
#include "ModuleBuild.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/PakFile.h"
#include "RageV/IO/VFS.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/SceneSerializer.h"
#include "RageV/UI/Interaction.h"
#include "yaml-cpp/yaml.h"
#include <fstream>

namespace RageV
{
	namespace
	{
		namespace fs = std::filesystem;

		// Where the project's assets end up, and what the shipped .rvproject
		// is rewritten to point at. Not "assets", which is the engine's.
		constexpr const char* kContentDirectory = "content";

		// A name safe to use as a filename, derived from the project's.
		//
		// A project may legitimately be called "Bob's Game: Reloaded"; a file
		// may not. Substituting rather than rejecting, because the name is the
		// user's to choose and the filename is ours to derive.
		std::string SafeFileName(const std::string& name)
		{
			std::string out;
			out.reserve(name.size());

			for (char c : name)
			{
				const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
								(c >= '0' && c <= '9') || c == '-' || c == '_';
				out += ok ? c : '_';
			}

			// Trailing underscores from punctuation at the end read as a typo.
			while (!out.empty() && out.back() == '_')
				out.pop_back();

			return out.empty() ? "Game" : out;
		}

		// Every scene in the project, checked for buttons whose OnClick names
		// something unreachable.
		//
		// **Every scene, not only the start scene.** A menu is usually its own
		// scene and is exactly where the buttons are; checking only the one the
		// game opens with would miss the case this was written for.
		//
		// The scripts are already loaded -- Project::Load brings in both the
		// C++ module and the C# assembly -- so both languages resolve here the
		// same way they will at runtime. That is the whole reason this can live
		// in the packager rather than needing a running game.
		void CheckSceneBindings(const PackageDesc& desc, PackageResult& result)
		{
			std::error_code error;
			const fs::path assets = Project::AssetRoot();
			if (!fs::is_directory(assets, error))
				return;

			for (const auto& entry : fs::recursive_directory_iterator(assets, error))
			{
				if (error || !entry.is_regular_file() || entry.path().extension() != ".rage")
					continue;

				// A scene that will not parse is a separate problem, and one
				// the runtime already reports. Skipped rather than reported
				// twice.
				auto scene = std::make_shared<Scene>();
				SceneSerializer serializer(scene);
				if (!serializer.Deserialize(entry.path().string()))
					continue;

				const fs::path relative = fs::relative(entry.path(), assets, error);
				const std::string name = error ? entry.path().filename().string()
											   : relative.generic_string();

				for (const UI::BindingProblem& problem : UI::ValidateBindings(*scene))
				{
					const std::string message = name + ": " + problem.Describe();

					if (desc.AllowDeadBindings)
						result.Warnings.push_back(message);
					else
						result.Errors.push_back(message);
				}
			}
		}

		bool DirectoryHasContent(const fs::path& path)
		{
			std::error_code error;
			if (!fs::is_directory(path, error))
				return false;
			return fs::directory_iterator(path, error) != fs::directory_iterator();
		}

		// Copies a tree, counting as it goes.
		bool CopyTree(const fs::path& from, const fs::path& to, PackageResult& result)
		{
			std::error_code error;
			fs::create_directories(to, error);

			for (const auto& entry : fs::recursive_directory_iterator(from, error))
			{
				if (error)
					break;

				const fs::path relative = fs::relative(entry.path(), from, error);
				const fs::path target = to / relative;

				if (entry.is_directory(error))
				{
					fs::create_directories(target, error);
					continue;
				}

				fs::create_directories(target.parent_path(), error);
				fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, error);

				if (error)
				{
					result.Errors.push_back("could not copy " + entry.path().string() +
											": " + error.message());
					return false;
				}

				result.FilesCopied++;
				result.BytesCopied += fs::file_size(entry.path(), error);
			}

			return true;
		}

		bool CopyOne(const fs::path& from, const fs::path& to, PackageResult& result)
		{
			std::error_code error;
			fs::create_directories(to.parent_path(), error);
			fs::copy_file(from, to, fs::copy_options::overwrite_existing, error);

			if (error)
			{
				result.Errors.push_back("could not copy " + from.string() + ": " + error.message());
				return false;
			}

			result.FilesCopied++;
			result.BytesCopied += fs::file_size(from, error);
			return true;
		}

		void WriteProjectFile(const fs::path& file, const ProjectConfig& config)
		{
			YAML::Emitter out;
			out << YAML::BeginMap;
			out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap;
			out << YAML::Key << "Name" << YAML::Value << config.Name;
			// The one field that differs from the source project: assets moved.
			out << YAML::Key << "AssetDirectory" << YAML::Value << kContentDirectory;
			out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
			out << YAML::Key << "FixedHz" << YAML::Value << config.FixedHz;
			out << YAML::EndMap;
			out << YAML::EndMap;

			std::ofstream stream(file);
			stream << out.c_str();
		}

		void WriteConfigFile(const fs::path& file)
		{
			std::ofstream stream(file);
			stream << "# Settings for this game. Edit and restart to change.\n"
				   << "\n"
				   << "# Graphics backend: vulkan | opengl\n"
				   << "rhi = vulkan\n"
				   << "\n"
				   << "vsync = on\n"
				   << "\n"
				   << "# Validation layers cost real performance and are of no use to a\n"
				   << "# player. On only for diagnosing a problem.\n"
				   << "validation = off\n";
		}
	}

	std::filesystem::path FindRuntime()
	{
		std::error_code error;
		const fs::path here = fs::current_path(error);
		if (error)
			return {};

		// Beside this executable: what a shipped editor looks like.
		const fs::path sibling = here / "RageVRuntime.exe";
		if (fs::exists(sibling, error))
			return sibling;

		// One directory over: the build tree, where every application gets its
		// own output folder so their staged assets do not overwrite each other.
		const fs::path inTree = here.parent_path() / "RageVRuntime" / "RageVRuntime.exe";
		if (fs::exists(inTree, error))
			return inTree;

		return {};
	}

	PackageResult PackageProject(const PackageDesc& desc)
	{
		std::error_code error;
		PackageResult result;

		// --- validate, completely, before writing anything --------------------
		// Every problem at once. A packager that reports one, gets fixed, then
		// reports the next wastes the time it was meant to save.
		if (!Project::GetActive())
			result.Errors.push_back("no project is open");

		if (desc.OutputDirectory.empty())
			result.Errors.push_back("no output directory");

		const fs::path runtime = desc.RuntimeExecutable.empty() ? FindRuntime()
																: desc.RuntimeExecutable;
		if (runtime.empty())
			result.Errors.push_back("could not find RageVRuntime.exe; pass one explicitly");
		else if (!fs::exists(runtime, error))
			result.Errors.push_back("no runtime at " + runtime.string());

		// The engine is a DLL, so it is part of the build rather than something
		// the player's machine is assumed to have. Checked here, with the other
		// refusals, so a package that cannot start is never half-written: the
		// failure would otherwise surface on the first machine that is not the
		// one it was built on.
		const fs::path engineDll = runtime.empty() ? fs::path()
												   : runtime.parent_path() / "RageV.dll";
		if (!runtime.empty() && !fs::exists(engineDll, error))
		{
			result.Errors.push_back("no RageV.dll beside " + runtime.filename().string() +
									"; the packaged game would not start");
		}

		const fs::path engineAssets = desc.EngineAssets.empty()
									? runtime.parent_path() / "assets"
									: desc.EngineAssets;
		if (!runtime.empty() && !fs::is_directory(engineAssets, error))
		{
			result.Errors.push_back("no engine assets at " + engineAssets.string() +
									"; the renderer will not start without shaders");
		}

		if (Project::GetActive())
		{
			const ProjectConfig& config = Project::Config();

			if (config.StartScene.empty())
			{
				// Fatal, not a warning: a runtime with no start scene reports
				// the problem and exits, so packaging one produces something
				// that cannot possibly work.
				result.Errors.push_back("the project has no start scene; set one with "
										"File > Set Start Scene");
			}
			else if (!fs::exists(Project::AssetPath(config.StartScene), error))
			{
				result.Errors.push_back("the start scene " + config.StartScene +
										" does not exist");
			}

			if (!fs::is_directory(Project::AssetRoot(), error))
				result.Errors.push_back("the project's asset folder is missing");

			CheckSceneBindings(desc, result);
		}

		if (!desc.Overwrite && DirectoryHasContent(desc.OutputDirectory))
		{
			result.Errors.push_back(desc.OutputDirectory.string() +
									" is not empty; pass overwrite to build into it anyway");
		}

		if (!result.Errors.empty())
			return result;

		// --- write ------------------------------------------------------------
		const ProjectConfig& config = Project::Config();
		const std::string name = SafeFileName(config.Name);

		fs::create_directories(desc.OutputDirectory, error);
		if (error)
		{
			result.Errors.push_back("could not create " + desc.OutputDirectory.string() +
									": " + error.message());
			return result;
		}

		// Named after the game, not after the engine. The executable is the
		// thing a player double-clicks, and "RageVRuntime.exe" tells them
		// nothing about what they are running.
		const fs::path executable = desc.OutputDirectory / (name + ".exe");
		if (!CopyOne(runtime, executable, result))
			return result;

		// Named as the engine names it, not after the game: the import table in
		// the executable says "RageV.dll", and Windows resolves that by name.
		if (!CopyOne(engineDll, desc.OutputDirectory / "RageV.dll", result))
			return result;

		// The game module -- the project's C++ scripts -- goes beside the
		// .rvproject, which is where GameModule looks in a package. Optional,
		// because a project without C++ is normal; but a project that *has*
		// Source/ and no built module gets a warning, because its scenes may
		// name scripts the shipped game will not have.
		{
			const fs::path module = ModuleBuild::ModuleFor(Project::Root(), name);
			if (fs::exists(module, error))
			{
				if (!CopyOne(module, desc.OutputDirectory / (name + ".dll"), result))
					return result;
			}
			else if (fs::exists(Project::Root() / "Source" / "CMakeLists.txt", error))
			{
				result.Warnings.push_back("this project has C++ scripts but no built "
										  + std::string(ModuleBuild::Configuration())
										  + " module; entities using them will do nothing "
										  "in the packaged game. Build Scripts first.");
			}
		}

		// The project's C# assembly, on the same terms as the module. It goes
		// into managed/ rather than the root: the root already holds the C++
		// module, and both are named after the game. The class library and its
		// runtime config travel along, because the loader looks for
		// managed/RageV.ScriptCore.dll beside the executable -- the same place
		// the editor keeps its copy.
		{
			const fs::path assembly =
				Project::Root() / "Scripts" / "bin" / (name + ".dll");
			if (fs::exists(assembly, error))
			{
				const fs::path managed = runtime.parent_path() / "managed";
				if (!CopyTree(managed, desc.OutputDirectory / "managed", result))
					return result;
				if (!CopyOne(assembly, desc.OutputDirectory / "managed" / (name + ".dll"),
							 result))
					return result;
			}
			else if (fs::exists(Project::Root() / "Scripts" / (name + ".csproj"), error))
			{
				result.Warnings.push_back("this project has C# scripts but no built "
										  "assembly; entities using them will do nothing "
										  "in the packaged game. Build Scripts first.");
			}
		}

		if (!CopyTree(engineAssets, desc.OutputDirectory / "assets", result))
			return result;

		// .meta sidecars come with it. They are the identity: a scene refers to
		// an asset by handle and the handle lives in the sidecar, so a build
		// without them is a build where nothing resolves.
		//
		// One archive by default: content.pak, which Project::Load mounts over
		// content/ so the same paths resolve either way. Loose is the
		// debugging escape hatch -- swap one file to test a fix -- and both
		// forms carry exactly the tree the VFS enumeration sees.
		if (desc.LooseContent)
		{
			if (!CopyTree(Project::AssetRoot(), desc.OutputDirectory / kContentDirectory, result))
				return result;
		}
		else
		{
			IO::PakWriter writer;
			for (const std::string& relative : IO::VFS::Enumerate(Project::AssetRoot()))
			{
				// Bytes through the VFS, not fopen: the project being packed
				// may itself be running from a pak, and the enumeration just
				// promised these paths resolve.
				std::vector<uint8_t> bytes;
				if (!IO::VFS::ReadBytes(Project::AssetRoot() / relative, bytes) ||
					!writer.AddBytes(relative, std::move(bytes)))
				{
					result.Errors.push_back("could not pack " + relative);
					return result;
				}
			}

			const fs::path pak =
				desc.OutputDirectory / (std::string(kContentDirectory) + ".pak");
			if (!writer.Write(pak))
			{
				result.Errors.push_back("could not write " + pak.string());
				return result;
			}

			result.FilesCopied += writer.Count();
			result.BytesCopied += writer.TotalBytes();
		}

		WriteProjectFile(desc.OutputDirectory / (name + Project::kExtension), config);
		WriteConfigFile(desc.OutputDirectory / "ragev.ini");
		result.FilesCopied += 2;

		// --- warn about things that work but should not ship ------------------
		const std::string runtimeName = runtime.string();
		if (runtimeName.find("\\Debug\\") != std::string::npos ||
			runtimeName.find("/Debug/") != std::string::npos)
		{
			result.Warnings.push_back("packaged a Debug runtime: it is slow, it carries "
									  "asserts, and it expects a debugger. Build Dist and "
									  "package that.");
		}

		if (fs::exists(runtime.parent_path() / "RageVRuntime.pdb", error))
			result.Warnings.push_back("a .pdb sits beside the runtime; it is not copied");

		result.Executable = executable;
		result.Success = true;
		return result;
	}
}
