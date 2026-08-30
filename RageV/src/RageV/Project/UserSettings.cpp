#include <rvpch.h>
#include "UserSettings.h"
#include "RageV/Core/Log.h"

#include "yaml-cpp/yaml.h"

#include <fstream>

namespace RageV
{
	std::filesystem::path UserSettings::PathFor(const std::filesystem::path& projectRoot)
	{
		return projectRoot / "Cache" / "user.rvstate";
	}

	UserSettings UserSettings::Load(const std::filesystem::path& projectRoot)
	{
		UserSettings settings;

		const std::filesystem::path file = PathFor(projectRoot);

		std::error_code error;
		if (!std::filesystem::exists(file, error))
			return settings;

		try
		{
			const YAML::Node root = YAML::LoadFile(file.string());
			const YAML::Node user = root["User"];
			if (!user)
				return settings;

			if (const YAML::Node last = user["LastScene"])
				settings.LastScene = last.as<std::string>();
		}
		catch (const std::exception& e)
		{
			// Not an error the user has to do anything about: the file is a
			// convenience and the editor's fallbacks cover its absence. Said
			// at trace volume so a corrupted one can still be found.
			RV_CORE_WARN("Could not read {0} ({1}); starting without it",
						 file.string(), e.what());
		}

		return settings;
	}

	bool UserSettings::Save(const std::filesystem::path& projectRoot) const
	{
		const std::filesystem::path file = PathFor(projectRoot);

		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "User" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "LastScene" << YAML::Value << LastScene;
		out << YAML::EndMap;
		out << YAML::EndMap;

		std::ofstream stream(file);
		if (!stream)
		{
			RV_CORE_WARN("Could not write {0}", file.string());
			return false;
		}

		stream << out.c_str();
		return true;
	}
}
