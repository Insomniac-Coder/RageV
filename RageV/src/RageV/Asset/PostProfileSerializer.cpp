#include <rvpch.h>
#include "PostProfileSerializer.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Scene/FieldSerializer.h"

#include "yaml-cpp/yaml.h"

#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		// The key the file is identified by, so a `.rvpostprofile` that is
		// actually something else fails to load rather than loading as a
		// profile of pure defaults -- which would render, and would be wrong
		// in a way nobody could see.
		constexpr const char* kRoot = "PostProfile";
		constexpr int kVersion = 1;
	}

	bool PostProfileSerializer::Save(const PostSettings& settings, const std::filesystem::path& path)
	{
		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << kRoot << YAML::Value << kVersion;

		// Every registered field, in registry order. Nothing lists them here.
		WriteFields(emitter, PostSettingsRegistry::Fields(),
					const_cast<PostSettings*>(&settings));

		emitter << YAML::EndMap;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not write post profile {0}", path.string());
			return false;
		}

		file << emitter.c_str();
		return true;
	}

	bool PostProfileSerializer::Load(PostSettings& out, const std::filesystem::path& path)
	{
		std::string text;
		if (!IO::VFS::ReadText(path, text))
			return false;

		YAML::Node root;
		try
		{
			root = YAML::Load(text);
		}
		catch (const YAML::Exception& e)
		{
			RV_CORE_ERROR("Post profile {0} is not valid YAML: {1}", path.string(), e.what());
			return false;
		}

		if (!root || !root[kRoot])
			return false;

		// Into a local seeded with the defaults, then handed over: a key the
		// file does not carry keeps the default rather than becoming zero, and
		// a file that parses halfway cannot leave the caller with a grade that
		// is part old and part new.
		PostSettings loaded;
		ReadFields(root, PostSettingsRegistry::Fields(), &loaded);

		out = loaded;
		return true;
	}
}
