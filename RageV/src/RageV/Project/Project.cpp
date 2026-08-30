#include <rvpch.h>
#include "Project.h"
#include "GameModule.h"
#include "RageV/Asset/ImportCache.h"
#include "RageV/Core/Log.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/IO/VFS.h"
#include "RageV/Managed/Interop.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Scene/FieldSerializer.h"
#include "yaml-cpp/yaml.h"
#include <algorithm>
#include <fstream>

namespace RageV
{
	namespace
	{
		struct ActiveProject
		{
			ProjectConfig Config;
			std::filesystem::path File;
			std::filesystem::path Root;
		};

		std::unique_ptr<ActiveProject> s_Active;
		Project s_Handle;   // GetActive returns a pointer; the state is here

		// yaml-cpp's non-const operator[] inserts, and Node::operator= assigns
		// into the document rather than rebinding. Reading through a const
		// reference is what avoids both, and both have cost this project a
		// debugging session before.
		std::string ReadString(const YAML::Node& node, const char* key, const std::string& fallback)
		{
			const YAML::Node& constNode = node;
			if (const YAML::Node value = constNode[key])
				return value.as<std::string>(fallback);
			return fallback;
		}

		uint32_t ReadUInt(const YAML::Node& node, const char* key, uint32_t fallback)
		{
			const YAML::Node& constNode = node;
			if (const YAML::Node value = constNode[key])
				return value.as<uint32_t>(fallback);
			return fallback;
		}

		// Where a project's compiled C# lives: the dev tree's Scripts/bin
		// first, then a package's managed/ -- the same two-step search
		// GameModule runs for the C++ module.
		std::filesystem::path FindScriptAssembly(const std::filesystem::path& root,
												 const std::string& name)
		{
			std::error_code error;

			const std::filesystem::path built = root / "Scripts" / "bin" / (name + ".dll");
			if (std::filesystem::exists(built, error))
				return built;

			const std::filesystem::path packaged = root / "managed" / (name + ".dll");
			if (std::filesystem::exists(packaged, error))
				return packaged;

			return {};
		}

		// The C# counterpart of GameModule::Load, and here for the same
		// reason: a packaged game and the runtime must get their scripts by
		// the same line of code the editor does. Building and hot reload stay
		// editor features; *having* the scripts does not.
		void LoadProjectScriptAssembly(const std::filesystem::path& root, const std::string& name)
		{
			const std::filesystem::path assembly = FindScriptAssembly(root, name);
			if (assembly.empty())
				return;   // a project with no built C# is normal

			// Boots the .NET host on first use; if it is already running this
			// is a cheap early-out. The class library sits in managed/ beside
			// the executable, which is also where a package places it.
			if (!Managed::Interop::Init("managed/RageV.ScriptCore.dll"))
			{
				RV_CORE_WARN("'{0}' has C# scripts, but the .NET runtime could not be "
							 "started; entities using them will do nothing", name);
				return;
			}

			const int32_t types =
				Managed::Interop::Managed().LoadAssembly(assembly.string().c_str());
			if (types < 0)
				RV_CORE_ERROR("Could not load {0}", assembly.string());
			else
				RV_CORE_INFO("{0}: {1} C# script(s)", assembly.filename().string(), types);
		}
	}

	Project* Project::GetActive()
	{
		return s_Active ? &s_Handle : nullptr;
	}

	bool Project::Load(const std::filesystem::path& projectFile)
	{
		std::error_code error;
		if (!std::filesystem::exists(projectFile, error))
		{
			RV_CORE_ERROR("Project: {0} does not exist", projectFile.string());
			return false;
		}

		YAML::Node root;
		try
		{
			root = YAML::LoadFile(projectFile.string());
		}
		catch (const std::exception& e)
		{
			RV_CORE_ERROR("Project: {0} could not be parsed: {1}", projectFile.string(), e.what());
			return false;
		}

		const YAML::Node& constRoot = root;
		const YAML::Node project = constRoot["Project"];
		if (!project)
		{
			RV_CORE_ERROR("Project: {0} has no Project block", projectFile.string());
			return false;
		}

		// Built to one side and moved in at the end, so a half-parsed file
		// cannot leave the editor holding a project that is partly the old one
		// and partly the new.
		auto loaded = std::make_unique<ActiveProject>();
		loaded->File = std::filesystem::absolute(projectFile);
		loaded->Root = loaded->File.parent_path();

		loaded->Config.Name = ReadString(project, "Name", "Untitled");
		loaded->Config.AssetDirectory = ReadString(project, "AssetDirectory", "assets");
		loaded->Config.StartScene = ReadString(project, "StartScene", "");
		// Same bounds the flag enforces: below 20 the simulation is visibly
		// steppy and fast collisions tunnel, above 240 it burns CPU for nothing
		// a display can show.
		loaded->Config.FixedHz = Math::Clamp(ReadUInt(project, "FixedHz", 60u), 20u, 240u);

		// Sparse and registry-driven: a project written before a setting
		// existed keeps that setting's default rather than zeroing it, and a
		// project written before render settings existed at all reads as the
		// defaults, which is what it was rendering with.
		ReadFields(project["RenderSettings"], RenderSettingsRegistry::Fields(),
				   &loaded->Config.Render);

		// 10.6 (ENGINE-NOTES 7bg) moved the five GI settings to the post
		// profile. A project written before that still names them here, where
		// nothing reads them any more.
		//
		// **Reported rather than migrated**, and **reported rather than
		// dropped** -- the rule SceneSerializer::ReportMovedSettings set for
		// the 7s move, for the reasons it gives: migrating means a load
		// silently rewriting an asset, and dropping is exactly what happened
		// to TemporalFeedback and cost a diagnosis to find.
		if (const YAML::Node render = project["RenderSettings"])
		{
			static const char* kMoved[] = {
				"GiBounces", "VoxelGlobalIllumination", "VoxelGiResolution",
				"VoxelGiCascades", "VoxelGiVoxelSize"
			};

			std::string named;
			for (const char* key : kMoved)
			{
				if (!render[key])
					continue;
				if (!named.empty())
					named += ", ";
				named += key;
			}

			if (!named.empty())
				RV_CORE_WARN("This project's Render Settings still name {0}, which moved "
							 "to the .rvpostprofile attached to a camera (10.6, "
							 "ENGINE-NOTES 7bg). Those values were not applied; set them "
							 "on the profile instead.", named);
		}

		// A check's affordance (ENGINE-NOTES 7ba). The project's Render Settings
		// are a fixture the checks never asked for: for a day in 2026-08 the
		// sample project carried every ray-tracing switch on, and every
		// screen-space claim in check_ssao and check_ssr silently measured the
		// traced twin. With this, a run's settings are the struct's defaults
		// plus whatever the command line says, and nothing else.
		if (EngineConfig::Get().RenderDefaults)
			loaded->Config.Render = RenderSettings{};

		s_Active = std::move(loaded);

		// Which files are font atlases is a fact about a project, and the
		// previous one's answer is worse than no answer: it would exempt the
		// wrong paths from cooking and cook the right ones.
		Assets::CookPolicy::Reset();

		// A packaged project's content is an archive named after its asset
		// directory -- content.pak where content/ would have been. Mounted
		// here, inside Load, so every caller's registry scan is downstream of
		// it; and over whatever the previous project mounted, which is why the
		// old mounts go first. A development project has no pak and mounts
		// nothing, which costs nothing.
		IO::VFS::UnmountAll();
		const std::filesystem::path pak =
			s_Active->Root / (s_Active->Config.AssetDirectory + ".pak");
		if (std::filesystem::exists(pak, error))
			IO::VFS::MountPak(AssetRoot(), pak);

		RV_CORE_INFO("Project '{0}' opened at {1}", s_Active->Config.Name,
					 s_Active->Root.string());

		// The project's C++ scripts, if it has built any. Part of opening a
		// project rather than an editor feature, so a packaged game and the
		// runtime get their module by the same line of code the editor does.
		// GameModule unloads any previous project's module itself.
		GameModule::Load(s_Active->Root, s_Active->Config.Name);

		// And its C#, on the same terms. Loading replaces whatever assembly
		// was loaded before, so switching projects swaps rather than piles.
		LoadProjectScriptAssembly(s_Active->Root, s_Active->Config.Name);

		return true;
	}

	namespace
	{
		// The .csproj and first script a generated project gets.
		//
		// The reference to RageV.ScriptCore is an MSBuild *property* rather than
		// a literal path, because where the engine is installed is not knowable
		// when the project is created -- the editor supplies it at build time
		// with -p:RageVScriptCore=<path>. A project file that only builds on the
		// machine that generated it is not a project file.
		//
		// Raw string literals throughout: this is a file full of quotes and
		// angle brackets, and escaping every one of them would make it
		// unreadable and unreviewable.
		void WriteScriptProject(const std::filesystem::path& directory, const std::string& name)
		{
			const std::filesystem::path scripts = directory / "Scripts";

			if (std::ofstream project(scripts / (name + ".csproj")); project)
			{
				project << R"(<Project Sdk="Microsoft.NET.Sdk">

  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <AssemblyName>)" << name << R"(</AssemblyName>
    <RootNamespace>)" << name << R"(</RootNamespace>
    <Nullable>enable</Nullable>
    <LangVersion>12</LangVersion>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
  </PropertyGroup>

  <!-- The editor passes -p:RageVScriptCore=<path> when it builds this. The
       fallback exists only so that a hand-run `dotnet build` fails with
       something a person can act on rather than a missing-type cascade. -->
  <PropertyGroup Condition="'$(RageVScriptCore)' == ''">
    <RageVScriptCore>RageV.ScriptCore.dll</RageVScriptCore>
  </PropertyGroup>

  <ItemGroup>
    <Reference Include="RageV.ScriptCore">
      <HintPath>$(RageVScriptCore)</HintPath>
      <!-- The engine already has it loaded; copying it next to the game's
           assembly would give the process two of them. -->
      <Private>false</Private>
    </Reference>
  </ItemGroup>

</Project>
)";
			}

			// One script that already compiles and does something, for the same
			// reason the starter scene is not empty: the first five minutes
			// should not be spent working out what a script has to look like.
			if (std::ofstream sample(scripts / "Example.cs"); sample)
			{
				sample << R"(using RageV;

// Attach this to an entity and press Play.
//
// There are two rates, and picking the right one is most of getting a game to
// behave. OnTick runs once per fixed simulation step: a frame may run zero of
// them, one, or several, and deltaTime is always the same number -- so gameplay
// goes here, where it behaves identically on every machine. OnFrame runs once
// per frame with the real elapsed time, for things nothing else has to agree
// about: a camera, a fade, a number counting up on the screen.
public class Example : Script
{
	private float m_Speed = 1.2f;

	public override void OnCreate()
	{
		Log.Info($"{Entity.Name} is ready");
	}

	public override void OnTick(float deltaTime)
	{
		Rotate(new Vector3(0.0f, m_Speed * deltaTime, 0.0f));
	}

	// Delete it if this script has nothing that belongs on the frame.
	public override void OnFrame(float deltaTime)
	{
	}
}
)";
			}
		}
	}

	namespace
	{
		// The C++ half of a project: a Source/ folder that builds to
		// <project>/bin/<Config>/<name>.dll, the game module the engine loads.
		//
		// The same portability rule the .csproj follows: where the engine lives
		// is not knowable when the project is created, so the generated build
		// takes it as -DRAGEV_ENGINE=<engine build dir> rather than baking in a
		// path that is only true on this machine. The editor supplies it when it
		// builds; the fatal-error fallback exists so a hand-run cmake fails with
		// a sentence rather than a missing-target cascade.
		void WriteSourceModule(const std::filesystem::path& directory, const std::string& name)
		{
			const std::filesystem::path source = directory / "Source";

			if (std::ofstream lists(source / "CMakeLists.txt"); lists)
			{
				lists << R"(cmake_minimum_required(VERSION 3.21)

# The same three configurations as the engine. They must match: an import
# library is per-config, and a config the engine does not have is a link error.
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Dist" CACHE STRING "" FORCE)

project()" << name << R"(Module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dist inherits Release codegen, exactly as the engine's top level does.
foreach(lang C CXX)
    set(CMAKE_${lang}_FLAGS_DIST "${CMAKE_${lang}_FLAGS_RELEASE}" CACHE STRING "" FORCE)
endforeach()
foreach(kind EXE SHARED MODULE STATIC)
    set(CMAKE_${kind}_LINKER_FLAGS_DIST "${CMAKE_${kind}_LINKER_FLAGS_RELEASE}" CACHE STRING "" FORCE)
endforeach()

# The editor passes -DRAGEV_ENGINE=<engine build directory> when it configures
# this. The engine's build writes RageVTargets.cmake there, and importing it is
# what makes RageV::RageV exist here with its include paths, definitions and
# import libraries carried along -- nothing about the engine is spelled out in
# this file, so this file does not go stale when the engine changes.
if(NOT DEFINED RAGEV_ENGINE OR NOT EXISTS "${RAGEV_ENGINE}/RageVTargets.cmake")
    message(FATAL_ERROR
        "RAGEV_ENGINE must point at a RageV build directory "
        "(one containing RageVTargets.cmake). The editor passes it "
        "automatically; by hand: cmake -S Source -B bin/module "
        "-DRAGEV_ENGINE=<engine>/build")
endif()

# GLFW's exported interface names Threads::Threads, which comes from
# find_package rather than from the export file -- so it has to exist before
# the include, or the import fails naming a target this file never mentions.
find_package(Threads REQUIRED)

include("${RAGEV_ENGINE}/RageVTargets.cmake")

# Scripts sit flat in Source/, the way C# scripts sit flat in Scripts/.
# A plain GLOB, not GLOB_RECURSE: recursing would sweep up any build
# directory somebody configures inside Source/, including CMake's own
# compiler-probe .cpp files.
file(GLOB MODULE_SOURCES CONFIGURE_DEPENDS "*.cpp" "*.h")

# Declaration-site script markers (RVShowInEditor / RVCallable / RVScript).
#
# rvgen scans Source/ and, for every file that marks something, writes a
# wrapper TU that #includes the file and appends the registrations the
# markers describe -- so a marked file compiles through its wrapper instead
# of directly, and a script says `RVShowInEditor` above a field instead of
# ending the file with an RV_REGISTER_SCRIPT block.
#
# It runs at *configure* time because the source list depends on its answer,
# and only a configure can change the source list. The CONFIGURE_DEPENDS
# loop below is the other half: editing any script re-runs the scan, so the
# first marker added to a file cannot sit registering nothing until somebody
# happens to reconfigure.
foreach(script ${MODULE_SOURCES})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${script}")
endforeach()

# Any config's rvgen: its output does not depend on how it was compiled.
set(RVGEN_EXE "")
foreach(config Release Debug Dist)
    if(EXISTS "${RAGEV_ENGINE}/bin/${config}/rvgen/rvgen.exe")
        set(RVGEN_EXE "${RAGEV_ENGINE}/bin/${config}/rvgen/rvgen.exe")
        break()
    endif()
endforeach()
if(NOT RVGEN_EXE)
    message(FATAL_ERROR
        "rvgen.exe was not found under ${RAGEV_ENGINE}/bin/<config>/rvgen. "
        "It generates script registrations from RVShowInEditor markers; "
        "build the engine once, then configure this module again.")
endif()

execute_process(
    COMMAND "${RVGEN_EXE}" --source "${CMAKE_CURRENT_SOURCE_DIR}"
                           --out "${CMAKE_CURRENT_BINARY_DIR}/rvgen"
    RESULT_VARIABLE RVGEN_RESULT
    ERROR_VARIABLE RVGEN_ERRORS)
if(NOT RVGEN_RESULT EQUAL 0)
    # The error lines are MSVC-shaped, so the editor's build panel parses
    # them out of this message like any other compile error.
    message(FATAL_ERROR "rvgen refused a marked declaration:\n${RVGEN_ERRORS}")
endif()

include("${CMAKE_CURRENT_BINARY_DIR}/rvgen/rvgen.manifest.cmake")
if(RVGEN_WRAPPED_SOURCES)
    list(REMOVE_ITEM MODULE_SOURCES ${RVGEN_WRAPPED_SOURCES})
endif()

add_library()" << name << R"( SHARED ${MODULE_SOURCES} ${RVGEN_GENERATED})
target_link_libraries()" << name << R"( PRIVATE RageV::RageV)

# The DLL lands beside the project's other build output, where the engine
# looks for it: <project>/bin/<Config>/. Because this property contains a
# generator expression, CMake does not append a per-config subdirectory of
# its own.
set_target_properties()" << name << R"( PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/$<CONFIG>")

if(MSVC)
    # /utf-8: MSVC's default source charset is the build machine's ANSI
    # codepage, so this makes the encoding a property of your project rather
    # than of whoever compiles it. It was originally here because spdlog's
    # bundled fmt would not build without it; that dependency is gone and the
    # flag is worth keeping on its own.
    target_compile_options()" << name << R"( PRIVATE /MP /W3 /utf-8)
endif()
)";
			}

			// One script that already compiles and registers, for the same
			// reason Scripts/ gets Example.cs: the first five minutes should
			// not be spent working out what a script has to look like.
			if (std::ofstream sample(source / "Rotator.cpp"); sample)
			{
				sample << R"(#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

namespace RageV
{
	// Attach this to an entity and press Play.
	class Rotator : public ScriptableEntity
	{
	public:
		// Public so the registration below can name it. C++ has no
		// reflection, so an editable field has to be declared explicitly --
		// unlike C#, where the inspector finds private fields on its own.
		float Speed = 1.0f;

		void OnCreate() override
		{
		}

		// Every fixed simulation step, not every frame. A frame may run zero
		// steps, one, or several -- so multiply rates by dt and the behaviour
		// stays the same at any simulation frequency. Gameplay goes here.
		void OnTick(Timestep dt) override
		{
			Rotate({ 0.0f, Speed * dt.GetSeconds(), 0.0f });
		}

		// Every frame, with the real elapsed time, which varies. For things
		// nothing else has to agree about: a camera, a fade, a number counting
		// up on the screen. Delete it if this script has none.
		void OnFrame(Timestep dt) override
		{
		}
	};

	// The name here is what scene files store, so renaming it breaks every
	// scene that used it. Fields declared on the same line show up in the
	// inspector.
	RV_REGISTER_SCRIPT(Rotator).Field<&Rotator::Speed>("Speed");
}
)";
			}
		}
	}

	bool Project::Create(const std::filesystem::path& directory, const std::string& name)
	{
		std::error_code error;

		const std::filesystem::path file = directory / (name + kExtension);
		if (std::filesystem::exists(file, error))
		{
			// Refused rather than overwritten. A project file is the only thing
			// that says where a game's assets are; replacing one silently is
			// how a folder full of work becomes unreachable.
			RV_CORE_ERROR("Project: {0} already exists", file.string());
			return false;
		}

		// The skeleton, not just a folder.
		//
		// An empty project is technically valid and practically useless: the
		// content browser opens on nothing, there is nowhere obvious to put a
		// model, and the first thing anybody does is invent a folder layout the
		// next person will not share. Generating the same one every time is
		// what makes a path like `models/rock.gltf` mean something across
		// projects.
		static const char* const kAssetFolders[] = {
			"scenes", "models", "textures", "audio", "prefabs"
		};

		for (const char* folder : kAssetFolders)
		{
			std::filesystem::create_directories(directory / "assets" / folder, error);
			if (error)
			{
				RV_CORE_ERROR("Project: could not create {0}: {1}",
							  directory.string(), error.message());
				return false;
			}
		}

		// Where a project's C# lives. A .csproj and one script, so that adding
		// a script is editing a file that already exists.
		std::filesystem::create_directories(directory / "Scripts", error);
		WriteScriptProject(directory, name);

		// And its C++: a Source/ that builds to bin/<Config>/<name>.dll, the
		// game module. Same reasoning -- adding a C++ script should be adding a
		// file to a build that already exists and already links.
		std::filesystem::create_directories(directory / "Source", error);
		WriteSourceModule(directory, name);

		// Builds land here. Created up front so it is visible in the content
		// browser and in Explorer before the first build rather than appearing
		// mysteriously after it.
		std::filesystem::create_directories(directory / "bin", error);

		// Generated projects are the kind that end up in version control, and
		// bin/ holds a copy of the runtime and every asset. Nobody wants that
		// in a diff.
		if (std::ofstream ignore(directory / ".gitignore"); ignore)
		{
			ignore << "# Build output: the runtime and a copy of every asset.\n";
			ignore << "bin/\n";
			ignore << "\n# C# build intermediates.\n";
			ignore << "Scripts/obj/\n";
			ignore << "Scripts/bin/\n";
			// Derived from the assets beside it and rebuilt whenever their
			// content hash moves, so committing it is all cost. The real
			// hazard is not size: a cooked file arriving over a checkout
			// carries the hash it was cooked from on another machine, and
			// would be accepted as current for a source it never saw.
			ignore << "\n# Import cache: cooked assets, rebuilt on demand.\n";
			ignore << "Cache/\n";
		}

		auto created = std::make_unique<ActiveProject>();
		created->File = std::filesystem::absolute(file);
		created->Root = created->File.parent_path();
		created->Config.Name = name;

		s_Active = std::move(created);

		// A brand-new project has no module to load, but the *previous* project
		// may have had one -- and its scripts must not linger in the registry of
		// a project they do not belong to. GameModule::Load unloads it either
		// way, exactly as Project::Load relies on.
		GameModule::Load(s_Active->Root, s_Active->Config.Name);

		return Save();
	}

	std::filesystem::path Project::BinaryRoot()
	{
		return s_Active ? s_Active->Root / "bin" : std::filesystem::path{};
	}

	std::filesystem::path Project::CacheRoot()
	{
		return s_Active ? s_Active->Root / "Cache" : std::filesystem::path{};
	}

	std::filesystem::path Project::CaptureRoot()
	{
		return s_Active ? s_Active->Root / "captures" : std::filesystem::path{};
	}

	std::filesystem::path Project::NextCapturePath(const std::string& stem)
	{
		const std::filesystem::path directory = CaptureRoot();
		if (directory.empty())
			return {};

		// Local time rather than UTC: these are looked at by the person who
		// took them, on the machine they took them on, and "which of these is
		// the one from just now" is the only question ever asked of the name.
		const std::time_t now = std::time(nullptr);
		std::tm parts{};
#ifdef _WIN32
		localtime_s(&parts, &now);
#else
		localtime_r(&now, &parts);
#endif
		char stamp[32]{};
		std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &parts);

		const std::string base = (stem.empty() ? std::string("untitled") : stem)
							   + "_" + stamp;

		std::error_code error;
		std::filesystem::path candidate = directory / (base + ".png");
		for (int index = 2; std::filesystem::exists(candidate, error) && index < 1000; index++)
			candidate = directory / (base + "_" + std::to_string(index) + ".png");

		return candidate;
	}

	bool Project::Save()
	{
		if (!s_Active)
			return false;

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << s_Active->Config.Name;
		out << YAML::Key << "AssetDirectory" << YAML::Value << s_Active->Config.AssetDirectory;
		out << YAML::Key << "StartScene" << YAML::Value << s_Active->Config.StartScene;
		out << YAML::Key << "FixedHz" << YAML::Value << s_Active->Config.FixedHz;

		// Every registered render setting, and nothing enumerated here. The
		// list this replaced is the shape of bug §7s exists to close: a field
		// added to the struct and to the registry but not to a writer is a
		// field that resets on every load.
		out << YAML::Key << "RenderSettings" << YAML::Value << YAML::BeginMap;
		WriteFields(out, RenderSettingsRegistry::Fields(), &s_Active->Config.Render);
		out << YAML::EndMap;

		out << YAML::EndMap;
		out << YAML::EndMap;

		std::ofstream file(s_Active->File);
		if (!file)
		{
			RV_CORE_ERROR("Project: could not write {0}", s_Active->File.string());
			return false;
		}

		file << out.c_str();
		return true;
	}

	void Project::Close()
	{
		GameModule::Unload();
		Assets::CookPolicy::Reset();
		// Every mount was filled from this project; none of them can answer
		// for the next one.
		IO::VFS::UnmountAll();
		s_Active.reset();
	}

	std::filesystem::path Project::Root()
	{
		return s_Active ? s_Active->Root : std::filesystem::path{};
	}

	std::filesystem::path Project::AssetRoot()
	{
		if (!s_Active)
			return {};
		return s_Active->Root / s_Active->Config.AssetDirectory;
	}

	std::filesystem::path Project::AssetPath(const std::string& relative)
	{
		if (!s_Active || relative.empty())
			return {};
		return AssetRoot() / relative;
	}

	ProjectConfig& Project::Config()
	{
		static ProjectConfig fallback;
		return s_Active ? s_Active->Config : fallback;
	}

	RenderSettings& Project::Render()
	{
		return Config().Render;
	}

	const std::filesystem::path& Project::File()
	{
		static const std::filesystem::path none;
		return s_Active ? s_Active->File : none;
	}

	std::filesystem::path Project::FindIn(const std::filesystem::path& fileOrDirectory)
	{
		std::error_code error;

		if (fileOrDirectory.empty())
			return {};

		if (std::filesystem::is_regular_file(fileOrDirectory, error))
			return fileOrDirectory;

		if (!std::filesystem::is_directory(fileOrDirectory, error))
			return {};

		// The first one, in directory order. A folder with two projects in it
		// is a mistake rather than a configuration, and picking one
		// deterministically beats refusing to open either.
		for (const auto& entry : std::filesystem::directory_iterator(fileOrDirectory, error))
		{
			if (entry.is_regular_file(error) && entry.path().extension() == kExtension)
				return entry.path();
		}

		return {};
	}

	bool Project::OpenConfigured()
	{
		const EngineConfig& config = EngineConfig::Get();

		if (!config.ProjectPath.empty())
		{
			const std::filesystem::path found = FindIn(config.ProjectPath);
			if (found.empty())
			{
				// Asked for explicitly and not there: worth an error, unlike
				// the fallbacks below, which are guesses.
				RV_CORE_ERROR("Project: nothing at {0}", config.ProjectPath);
				return false;
			}
			return Load(found);
		}

		// Beside the executable. This is the shape a packaged game has, and it
		// is the last place this looks: a project is either named or found
		// where a shipped game keeps its own.
		//
		// **There is deliberately no fallback below this.** The engine used to
		// finish here by opening whatever `RV_DEFAULT_PROJECT` was baked into
		// it at configure time, which on a development build is this
		// repository's `SampleProject` -- so the editor, launched with no
		// arguments, always opened one particular project and had no way to
		// start without one. That made a sample into the engine's default
		// state. `OpenConfiguredOrDefault` still does it for the tools that
		// genuinely want a project to be found for them; an application that
		// can ask a person instead should ask.
		std::error_code error;
		const std::filesystem::path beside =
			FindIn(std::filesystem::current_path(error));
		if (!beside.empty())
			return Load(beside);

		return false;
	}

	bool Project::OpenConfiguredOrDefault()
	{
		if (OpenConfigured())
			return true;

		// The development fallback, and only for a command-line tool.
		// `scenetest` and `rvimport` run out of the build tree with no project
		// named and no `.rvproject` beside them, and want to exercise a real
		// one rather than a folder that happens to be there.
		//
		// Absent from an installed build, where the macro is not defined.
#ifdef RV_DEFAULT_PROJECT
		const std::filesystem::path fallback = FindIn(RV_DEFAULT_PROJECT);
		if (!fallback.empty())
			return Load(fallback);
#endif

		RV_CORE_WARN("No project opened; assets will not resolve");
		return false;
	}

	std::string Project::MakeRelative(const std::filesystem::path& absolute)
	{
		if (!s_Active)
			return {};

		std::error_code error;
		const std::filesystem::path relative =
			std::filesystem::relative(absolute, AssetRoot(), error);

		if (error || relative.empty())
			return {};

		std::string text = relative.generic_string();

		// A path that climbs out of the asset directory is outside the project,
		// and storing it would produce a project that only works on this
		// machine -- exactly what asset handles exist to prevent.
		if (text.rfind("..", 0) == 0)
			return {};

		return text;
	}
}
