#include <rvpch.h>
#include "ProjectPackager.h"
#include "Project.h"
#include "ModuleBuild.h"
#include "RageV/Asset/GltfImporter.h"
#include "RageV/Asset/ModelImporter.h"
#include "RageV/Asset/ImportCache.h"
#include "RageV/Asset/MeshCook.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/PakFile.h"
#include "RageV/IO/TextureCook.h"
#include "RageV/IO/VFS.h"
#include "stb_image.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Scene/FieldSerializer.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Scene/SceneSerializer.h"
#include "RageV/UI/Interaction.h"
#include "yaml-cpp/yaml.h"
#include <fstream>
#include <unordered_set>

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
		// A line to whoever is watching, if anyone is. Newline included, so a
		// console can append the string it is handed and nothing has to agree
		// about who adds it.
		void Say(const PackageDesc& desc, const std::string& line)
		{
			if (desc.Log)
				desc.Log(line + "\n");
		}

		bool Cancelled(const PackageDesc& desc)
		{
			return desc.Cancel && desc.Cancel->load();
		}

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
		void CheckSceneBindings(const PackageDesc& desc, const fs::path& assets,
								const std::vector<std::string>& scenes,
								const UI::MethodTable& methods, PackageResult& result)
		{
			std::error_code error;
			if (!fs::is_directory(assets, error))
				return;

			// **Only what ships.** Refusing a build over a broken button in a
			// scene it was never going to carry is a refusal nobody can act on
			// -- and on a project with ninety scenes, checking the three that
			// ship is most of what makes a narrowed build fast.
			for (const std::string& relative : scenes)
			{
				const fs::path file = assets / relative;
				if (!fs::is_regular_file(file, error))
					continue;

				// A scene that will not parse is a separate problem, and one
				// the runtime already reports. Skipped rather than reported
				// twice.
				auto scene = std::make_shared<Scene>();
				SceneSerializer serializer(scene);
				if (!serializer.Deserialize(file.string()))
					continue;

				for (const UI::BindingProblem& problem : UI::ValidateBindings(*scene, &methods))
				{
					const std::string message = relative + ": " + problem.Describe();

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

			// **Every render setting, and this was missing entirely.** The
			// packaged `.rvproject` carried four keys, so a shipped game loaded
			// the *defaults* for ray tracing, anti-aliasing, shadows and the
			// rest -- a project authored with traced reflections shipped
			// without them, and nothing said so at any point.
			//
			// Through the registry, not a list, for the reason Project::Save
			// gives at the identical call: a field added to the struct and to
			// the registry but not to a writer is a field that resets on every
			// load, and enumerating them here is how this diverged in the first
			// place.
			out << YAML::Key << "RenderSettings" << YAML::Value << YAML::BeginMap;
			WriteFields(out, RenderSettingsRegistry::Fields(),
						const_cast<RenderSettings*>(&config.Render));
			out << YAML::EndMap;

			out << YAML::EndMap;
			out << YAML::EndMap;

			std::ofstream stream(file);
			stream << out.c_str();
		}

		// The cook step (7.2): source bytes in, load-ready bytes out, same
		// path either way. Textures decode once here instead of on every
		// boot, and come back with a full mip chain, block-compressed; models
		// come back as the import, serialized. Anything that fails to cook
		// ships raw with a warning -- a shipped game that loads slowly beats
		// one missing an asset.
		std::vector<uint8_t> CookForPak(const fs::path& assetRoot,
										const std::string& relative,
										std::vector<uint8_t> bytes,
										PackageResult& result)
		{
			const fs::path path(relative);
			const std::string extension = path.extension().string();

			// A font's MSDF atlas ships exactly as authored.
			//
			// It is a distance field, not a picture: the shader reads
			// distances out of it, block compression quantizes those to two
			// endpoints per 4x4 block, and a mip chain averages distances
			// from opposite sides of a stroke. This cooked every `.png` it
			// found for a while, atlases included, and the only symptom was
			// packaged text that looked slightly soft -- the same silent
			// class of failure as reading one as sRGB, which is why
			// GetFontAtlas already carries a paragraph about it.
			if (Assets::CookPolicy::IsFontAtlas(relative))
				return bytes;

			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg")
			{
				int width = 0, height = 0, channels = 0;
				stbi_uc* pixels = stbi_load_from_memory(bytes.data(), (int)bytes.size(),
														&width, &height, &channels, 4);
				if (!pixels)
				{
					result.Warnings.push_back(relative + " would not decode; shipped raw");
					return bytes;
				}

				const IO::CookedTexture cooked =
					IO::TextureCook::Cook(pixels, (uint32_t)width, (uint32_t)height, relative);
				stbi_image_free(pixels);

				return IO::TextureCook::Serialize(cooked);
			}

			if (Assets::ModelImporter::IsModelExtension(extension))
			{
				Assets::ImportedModel model;
				if (!Assets::ModelImporter::Import(assetRoot / relative, model))
				{
					result.Warnings.push_back(relative + " would not import; shipped raw");
					return bytes;
				}

				return Assets::MeshCook::Serialize(model);
			}

			// Everything else -- scenes, materials, sidecars, audio, .hdr
			// skies -- ships as-is. The .bin a .gltf references ships too:
			// a model that fell back raw still needs it, and a few kilobytes
			// of buffer is not worth a bookkeeping pass that could drop the
			// wrong one.
			return bytes;
		}

		void WriteConfigFile(const fs::path& file, const PackagePlan& plan)
		{
			std::string list;
			for (const std::string& backend : plan.Backends)
				list += (list.empty() ? "" : ", ") + backend;

			std::ofstream stream(file);
			stream << "# Settings for this game. Edit and restart to change.\n"
				   << "\n"
				   << "# Graphics backend: vulkan | opengl\n"
				   << "rhi = " << (plan.Backends.empty() ? "vulkan" : plan.Backends.front())
				   << "\n"
				   << "\n"
				   << "# Every backend this game was built for, in the order it tries\n"
				   << "# them. `rhi` above picks which one goes first; if that one has\n"
				   << "# no working driver the game falls through to the next rather\n"
				   << "# than refusing to start. A single entry means exactly that:\n"
				   << "# there is nothing to fall through to.\n"
				   << "backends = " << list << "\n"
				   << "\n"
				   << "vsync = " << (plan.VSync ? "on" : "off") << "\n"
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

	PackagePlan PlanPackage(const PackageDesc& desc)
	{
		std::error_code error;
		PackagePlan plan;

		// --- validate, completely, before anything is written -----------------
		// Every problem at once. A packager that reports one, gets fixed, then
		// reports the next wastes the time it was meant to save.
		//
		// **And all of it here rather than in the writing half**, because this
		// is the half that runs on the main thread: it reads the open project,
		// and the binding check below asks the C# runtime what methods a script
		// has. Neither is a thing to do from a worker.
		if (!Project::GetActive())
			plan.Errors.push_back("no project is open");

		if (desc.OutputDirectory.empty())
			plan.Errors.push_back("no output directory");

		plan.Runtime = desc.RuntimeExecutable.empty() ? FindRuntime()
													  : desc.RuntimeExecutable;
		if (plan.Runtime.empty())
			plan.Errors.push_back("could not find RageVRuntime.exe; pass one explicitly");
		else if (!fs::exists(plan.Runtime, error))
			plan.Errors.push_back("no runtime at " + plan.Runtime.string());

		// The engine is a DLL, so it is part of the build rather than something
		// the player's machine is assumed to have. Checked here, with the other
		// refusals, so a package that cannot start is never half-written: the
		// failure would otherwise surface on the first machine that is not the
		// one it was built on.
		plan.EngineDll = plan.Runtime.empty() ? fs::path()
											  : plan.Runtime.parent_path() / "RageV.dll";
		if (!plan.Runtime.empty() && !fs::exists(plan.EngineDll, error))
		{
			plan.Errors.push_back("no RageV.dll beside " + plan.Runtime.filename().string() +
								  "; the packaged game would not start");
		}

		// The backends and vsync the packaged game starts on. Read here rather
		// than in the writing half, which is not allowed globals.
		//
		// Lower case, because that is what the file's own comment tells a
		// player to type. The reader lowercases anyway, so this is legibility
		// rather than correctness.
		plan.Backends = desc.Backends;
		if (plan.Backends.empty())
		{
			std::string current = EngineConfig::BackendName(EngineConfig::Get().Backend);
			for (char& c : current)
				c = (char)std::tolower((unsigned char)c);
			plan.Backends.push_back(current);
		}

		for (const std::string& backend : plan.Backends)
		{
			if (backend != "vulkan" && backend != "opengl")
				plan.Errors.push_back("unknown graphics backend '" + backend + "'");
		}

		plan.VSync = EngineConfig::Get().VSync;

		plan.EngineAssets = desc.EngineAssets.empty()
						  ? plan.Runtime.parent_path() / "assets"
						  : desc.EngineAssets;
		if (!plan.Runtime.empty() && !fs::is_directory(plan.EngineAssets, error))
		{
			plan.Errors.push_back("no engine assets at " + plan.EngineAssets.string() +
								  "; the renderer will not start without shaders");
		}

		if (Project::GetActive())
		{
			// The snapshot. Copied rather than referenced: by the time the
			// writing half runs, the main thread may have moved on.
			plan.Config = Project::Config();
			plan.ProjectRoot = Project::Root();
			plan.AssetRoot = Project::AssetRoot();
			plan.Name = SafeFileName(plan.Config.Name);

			// **The build's own answer wins over the project's.** Everything
			// below -- the checks, the fold-in, and the `.rvproject` written
			// into the package -- reads `plan.Config.StartScene`, so
			// overriding it here is the whole of the feature: a build that
			// ships one scene boots into that scene rather than into whatever
			// the project happens to name.
			if (!desc.StartScene.empty())
				plan.Config.StartScene = desc.StartScene;

			if (plan.Config.StartScene.empty())
			{
				// Fatal, not a warning: a runtime with no start scene reports
				// the problem and exits, so packaging one produces something
				// that cannot possibly work.
				plan.Errors.push_back("the project has no start scene; set one with "
									  "File > Set Start Scene");
			}
			else if (!fs::exists(Project::AssetPath(plan.Config.StartScene), error))
			{
				plan.Errors.push_back("the start scene " + plan.Config.StartScene +
									  " does not exist");
			}

			if (!fs::is_directory(plan.AssetRoot, error))
				plan.Errors.push_back("the project's asset folder is missing");

			// The answers, not the walk. See PackagePlan::ScriptMethods.
			plan.ScriptMethods = UI::CollectScriptMethods();

			// --- which scenes ship ---------------------------------------
			//
			// **The start scene is folded in whatever was asked for.** A game
			// whose first scene is missing cannot start, and a build option
			// that silently produces one would be worse than no option.
			plan.Scenes = desc.Scenes;

			if (plan.Scenes.empty())
			{
				// Nothing named: every scene in the project, which is what the
				// CLI does and what the editor did before there was a dialog.
				for (const std::string& relative : IO::VFS::Enumerate(plan.AssetRoot))
				{
					if (fs::path(relative).extension() == ".rage")
						plan.Scenes.push_back(relative);
				}
			}
			else if (!plan.Config.StartScene.empty()
					 && std::find(plan.Scenes.begin(), plan.Scenes.end(),
								  plan.Config.StartScene) == plan.Scenes.end())
			{
				plan.Scenes.push_back(plan.Config.StartScene);
				plan.Warnings.push_back("the start scene " + plan.Config.StartScene +
										" was not selected; it ships anyway, because a "
										"game cannot start without it");
			}
		}

		if (!desc.Overwrite && DirectoryHasContent(desc.OutputDirectory))
		{
			plan.Errors.push_back(desc.OutputDirectory.string() +
								  " is not empty; pass overwrite to build into it anyway");
		}

		plan.Ok = plan.Errors.empty();
		return plan;
	}

	PackageResult WritePackage(const PackageDesc& desc, const PackagePlan& plan)
	{
		std::error_code error;
		PackageResult result;
		result.Warnings = plan.Warnings;

		if (!plan.Ok)
		{
			result.Errors = plan.Errors;
			return result;
		}

		const std::string& name = plan.Name;

		// A scene ships only if it was selected, and so does its sidecar --
		// leaving a `.meta` behind for a scene that is not there would put a
		// handle in the registry pointing at nothing.
		//
		// A set rather than a linear search: the enumeration below is every
		// file in the project, and on a large one that is the difference
		// between a filter and a quadratic.
		const std::unordered_set<std::string> shipped(plan.Scenes.begin(), plan.Scenes.end());

		auto excluded = [&](const std::string& relative)
		{
			std::string path = relative;
			if (path.size() > 5 && path.compare(path.size() - 5, 5, ".meta") == 0)
				path.resize(path.size() - 5);

			return fs::path(path).extension() == ".rage" && !shipped.count(path);
		};

		Say(desc, "Packaging " + plan.Config.Name + " into " +
				  desc.OutputDirectory.string());

		// **Before anything is written, and on this thread.** A dead binding is
		// a reason not to ship, so it has to be found before the first file
		// lands -- and finding it means parsing every scene in the project,
		// which is why this is here rather than beside the rest of the
		// validation in PlanPackage. The answers it needs came from there.
		Say(desc, "  checking scene bindings in " + std::to_string(plan.Scenes.size()) +
				  " scene(s)");
		CheckSceneBindings(desc, plan.AssetRoot, plan.Scenes, plan.ScriptMethods, result);

		if (!result.Errors.empty())
		{
			// Into the console as well as into the result. A build that stops
			// should say why in the place somebody is already looking, rather
			// than only in a summary that replaces the log.
			for (const std::string& error : result.Errors)
				Say(desc, "  error: " + error);
			return result;
		}

		if (Cancelled(desc))
		{
			result.Cancelled = true;
			return result;
		}

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
		Say(desc, "  runtime -> " + executable.filename().string());
		if (!CopyOne(plan.Runtime, executable, result))
			return result;

		// Named as the engine names it, not after the game: the import table in
		// the executable says "RageV.dll", and Windows resolves that by name.
		if (!CopyOne(plan.EngineDll, desc.OutputDirectory / "RageV.dll", result))
			return result;

		// The game module -- the project's C++ scripts -- goes beside the
		// .rvproject, which is where GameModule looks in a package. Optional,
		// because a project without C++ is normal; but a project that *has*
		// Source/ and no built module gets a warning, because its scenes may
		// name scripts the shipped game will not have.
		{
			const fs::path module = ModuleBuild::ModuleFor(plan.ProjectRoot, name);
			if (fs::exists(module, error))
			{
				Say(desc, "  C++ module -> " + name + ".dll");
				if (!CopyOne(module, desc.OutputDirectory / (name + ".dll"), result))
					return result;
			}
			else if (fs::exists(plan.ProjectRoot / "Source" / "CMakeLists.txt", error))
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
				plan.ProjectRoot / "Scripts" / "bin" / (name + ".dll");
			if (fs::exists(assembly, error))
			{
				Say(desc, "  C# assembly -> managed/" + name + ".dll");
				const fs::path managed = plan.Runtime.parent_path() / "managed";
				if (!CopyTree(managed, desc.OutputDirectory / "managed", result))
					return result;
				if (!CopyOne(assembly, desc.OutputDirectory / "managed" / (name + ".dll"),
							 result))
					return result;
			}
			else if (fs::exists(plan.ProjectRoot / "Scripts" / (name + ".csproj"), error))
			{
				result.Warnings.push_back("this project has C# scripts but no built "
										  "assembly; entities using them will do nothing "
										  "in the packaged game. Build Scripts first.");
			}
		}

		Say(desc, "  engine assets");
		if (!CopyTree(plan.EngineAssets, desc.OutputDirectory / "assets", result))
			return result;

		if (Cancelled(desc))
		{
			result.Cancelled = true;
			return result;
		}

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
			// A loose build copies the tree wholesale and then removes what was
			// not selected, rather than filtering the copy: CopyTree is the one
			// used by every other target here, and a second filtered version of
			// it is a second thing to keep correct.
			Say(desc, "  content (loose)");
			if (!CopyTree(plan.AssetRoot, desc.OutputDirectory / kContentDirectory, result))
				return result;

			for (const std::string& relative : IO::VFS::Enumerate(plan.AssetRoot))
			{
				if (excluded(relative))
					fs::remove(desc.OutputDirectory / kContentDirectory / relative, error);
			}
		}
		else
		{
			IO::PakWriter writer;

			std::vector<std::string> files;
			uint64_t skipped = 0;
			for (const std::string& relative : IO::VFS::Enumerate(plan.AssetRoot))
			{
				if (excluded(relative))
					skipped++;
				else
					files.push_back(relative);
			}

			Say(desc, "  content: " + std::to_string(files.size()) + " files" +
					  (desc.RawContent ? " (raw)" : " (cooking)") +
					  (skipped ? ", " + std::to_string(skipped) + " left out" : ""));

			uint64_t done = 0;
			for (const std::string& relative : files)
			{
				// Between files rather than inside one: a half-cooked texture
				// is not a state anything downstream would know what to do
				// with, and the granularity is already fine enough that a
				// cancel lands within a moment.
				if (Cancelled(desc))
				{
					result.Cancelled = true;
					return result;
				}

				// Bytes through the VFS, not fopen: the project being packed
				// may itself be running from a pak, and the enumeration just
				// promised these paths resolve.
				std::vector<uint8_t> bytes;
				if (!IO::VFS::ReadBytes(plan.AssetRoot / relative, bytes))
				{
					result.Errors.push_back("could not pack " + relative);
					return result;
				}

				if (!desc.RawContent)
					bytes = CookForPak(plan.AssetRoot, relative, std::move(bytes), result);

				if (!writer.AddBytes(relative, std::move(bytes)))
				{
					result.Errors.push_back("could not pack " + relative);
					return result;
				}

				// Every fiftieth, not every one. The console is read by a
				// person and a line per file on a project of any size is a
				// wall nobody looks at -- and the lock it takes is per line.
				if (++done % 50 == 0)
				{
					Say(desc, "    " + std::to_string(done) + " / " +
							  std::to_string(files.size()));
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

		WriteProjectFile(desc.OutputDirectory / (name + Project::kExtension), plan.Config);
		WriteConfigFile(desc.OutputDirectory / "ragev.ini", plan);
		result.FilesCopied += 2;

		// --- warn about things that work but should not ship ------------------
		const std::string runtimeName = plan.Runtime.string();
		if (runtimeName.find("\\Debug\\") != std::string::npos ||
			runtimeName.find("/Debug/") != std::string::npos)
		{
			result.Warnings.push_back("packaged a Debug runtime: it is slow, it carries "
									  "asserts, and it expects a debugger. Build Dist and "
									  "package that.");
		}

		if (fs::exists(plan.Runtime.parent_path() / "RageVRuntime.pdb", error))
			result.Warnings.push_back("a .pdb sits beside the runtime; it is not copied");

		result.Executable = executable;
		result.Success = true;

		for (const std::string& warning : result.Warnings)
			Say(desc, "  warning: " + warning);

		Say(desc, "Done: " + std::to_string(result.FilesCopied) + " files, " +
				  std::to_string(result.BytesCopied / (1024 * 1024)) + " MB");
		Say(desc, "  " + executable.string());
		return result;
	}

	PackageResult PackageProject(const PackageDesc& desc)
	{
		return WritePackage(desc, PlanPackage(desc));
	}
}
