#include <rvpch.h>
#include "Project.h"
#include "RageV/Core/Log.h"
#include "RageV/Core/EngineConfig.h"
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
		loaded->Config.FixedHz = std::clamp(ReadUInt(project, "FixedHz", 60u), 20u, 240u);

		s_Active = std::move(loaded);

		RV_CORE_INFO("Project '{0}' opened at {1}", s_Active->Config.Name,
					 s_Active->Root.string());
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
// OnUpdate runs once per fixed simulation step, not once per frame. A frame may
// run zero steps, one, or several -- so multiply rates by deltaTime and the
// behaviour stays the same at any simulation frequency.
public class Example : Script
{
	private float m_Speed = 1.2f;

	public override void OnCreate()
	{
		Log.Info($"{Entity.Name} is ready");
	}

	public override void OnUpdate(float deltaTime)
	{
		Rotate(new Vector3(0.0f, m_Speed * deltaTime, 0.0f));
	}
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
		}

		auto created = std::make_unique<ActiveProject>();
		created->File = std::filesystem::absolute(file);
		created->Root = created->File.parent_path();
		created->Config.Name = name;

		s_Active = std::move(created);
		return Save();
	}

	std::filesystem::path Project::BinaryRoot()
	{
		return s_Active ? s_Active->Root / "bin" : std::filesystem::path{};
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

		// Beside the executable. This is the shape a packaged game has, so it
		// is checked before the development fallback.
		std::error_code error;
		const std::filesystem::path beside =
			FindIn(std::filesystem::current_path(error));
		if (!beside.empty())
			return Load(beside);

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
