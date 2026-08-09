#include <rvpch.h>
#include "AssetRegistry.h"
#include "RageV/Core/Log.h"
#include "yaml-cpp/yaml.h"
#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		std::filesystem::path s_Root;
		bool s_Initialised = false;

		// Ordered by relative path, so listings are stable between runs instead
		// of following hash order.
		std::map<std::string, AssetMetadata> s_ByPath;
		std::unordered_map<AssetHandle, std::string> s_PathByHandle;

		const AssetMetadata s_Invalid;

		// Forward slashes, relative to the root. A path written on one machine
		// has to mean the same thing on another, and Windows will happily hand
		// back backslashes.
		std::string ToRelative(const std::filesystem::path& file)
		{
			std::error_code error;
			std::filesystem::path relative = std::filesystem::relative(file, s_Root, error);
			if (error)
				return file.filename().generic_string();
			return relative.generic_string();
		}

		void Index(const AssetMetadata& metadata)
		{
			s_ByPath[metadata.Path] = metadata;
			s_PathByHandle[metadata.Handle] = metadata.Path;
		}
	}

	bool Registry::IsInitialised() { return s_Initialised; }
	const std::filesystem::path& Registry::Root() { return s_Root; }
	const std::map<std::string, AssetMetadata>& Registry::All() { return s_ByPath; }

	void Registry::Init(const std::filesystem::path& assetsRoot)
	{
		s_Root = std::filesystem::absolute(assetsRoot);
		s_Initialised = true;

		std::error_code error;
		if (!std::filesystem::exists(s_Root, error))
		{
			RV_CORE_WARN("Assets root '{0}' does not exist; the registry will be empty",
						 s_Root.string());
			return;
		}

		Refresh();
	}

	void Registry::Shutdown()
	{
		s_ByPath.clear();
		s_PathByHandle.clear();
		s_Root.clear();
		s_Initialised = false;
	}

	void Registry::Refresh()
	{
		if (!s_Initialised)
			return;

		// Virtual assets have no file, so a rescan must not drop them.
		std::map<std::string, AssetMetadata> virtuals;
		for (const auto& [path, metadata] : s_ByPath)
		{
			if (metadata.Path.rfind("virtual:", 0) == 0)
				virtuals[path] = metadata;
		}

		s_ByPath.clear();
		s_PathByHandle.clear();

		for (const auto& [path, metadata] : virtuals)
			Index(metadata);

		ScanDirectory(s_Root);
	}

	void Registry::ScanDirectory(const std::filesystem::path& directory)
	{
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(directory, error))
		{
			if (error)
				break;

			if (entry.is_directory())
			{
				ScanDirectory(entry.path());
				continue;
			}

			const std::filesystem::path& file = entry.path();
			// The sidecars themselves are not assets.
			if (file.extension() == ".meta")
				continue;

			const AssetType type = AssetTypeFromExtension(file.extension().string());
			if (type == AssetType::None)
				continue;

			Index(ReadOrCreateMeta(file, type));
		}
	}

	AssetMetadata Registry::ReadOrCreateMeta(const std::filesystem::path& file, AssetType type)
	{
		const std::filesystem::path metaPath = file.string() + ".meta";

		AssetMetadata metadata;
		metadata.Type = type;
		metadata.Path = ToRelative(file);

		if (std::filesystem::exists(metaPath))
		{
			try
			{
				const YAML::Node node = YAML::LoadFile(metaPath.string());
				if (node["Handle"])
					metadata.Handle = AssetHandle(node["Handle"].as<uint64_t>());
				if (node["Type"])
					metadata.Type = AssetTypeFromName(node["Type"].as<std::string>());
				if (node["SourceHash"])
					metadata.SourceHash = node["SourceHash"].as<uint64_t>();
			}
			catch (const YAML::Exception& e)
			{
				RV_CORE_WARN("Could not read '{0}' ({1}); a new handle will be minted",
							 metaPath.string(), e.what());
			}
		}

		// A missing or unreadable sidecar means a new identity. Everything that
		// referred to the old one is already broken at that point, so there is
		// nothing to preserve.
		const bool minted = !metadata.Handle.IsValid();
		if (minted)
			metadata.Handle = AssetHandle();

		const uint64_t hash = HashFile(file);
		const bool changed = hash != metadata.SourceHash;
		metadata.SourceHash = hash;

		if (minted || changed)
			WriteMeta(file, metadata);

		return metadata;
	}

	void Registry::WriteMeta(const std::filesystem::path& file, const AssetMetadata& metadata)
	{
		const std::filesystem::path metaPath = file.string() + ".meta";

		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Handle" << YAML::Value << (uint64_t)metadata.Handle;
		emitter << YAML::Key << "Type" << YAML::Value << AssetTypeName(metadata.Type);
		emitter << YAML::Key << "SourceHash" << YAML::Value << metadata.SourceHash;
		emitter << YAML::EndMap;

		std::ofstream out(metaPath);
		if (!out)
		{
			RV_CORE_WARN("Could not write '{0}'; this asset's handle will not survive a restart",
						 metaPath.string());
			return;
		}

		out << emitter.c_str();
	}

	const AssetMetadata& Registry::GetMetadata(AssetHandle handle)
	{
		const auto path = s_PathByHandle.find(handle);
		if (path == s_PathByHandle.end())
			return s_Invalid;

		const auto metadata = s_ByPath.find(path->second);
		return metadata == s_ByPath.end() ? s_Invalid : metadata->second;
	}

	AssetHandle Registry::GetHandle(const std::string& relativePath)
	{
		const auto it = s_ByPath.find(relativePath);
		return it == s_ByPath.end() ? AssetHandle::Invalid() : it->second.Handle;
	}

	std::filesystem::path Registry::GetAbsolutePath(AssetHandle handle)
	{
		const AssetMetadata& metadata = GetMetadata(handle);
		if (!metadata.IsValid() || metadata.Path.rfind("virtual:", 0) == 0)
			return {};

		return s_Root / metadata.Path;
	}

	AssetHandle Registry::RegisterVirtual(AssetHandle handle, AssetType type,
											   const std::string& name)
	{
		AssetMetadata metadata;
		metadata.Handle = handle;
		metadata.Type = type;
		// Prefixed so it cannot collide with a real file and so
		// GetAbsolutePath can refuse to invent one.
		metadata.Path = "virtual:" + name;

		Index(metadata);
		return handle;
	}

	// FNV-1a over the file's bytes.
	//
	// Content, not modification time: a checkout, a copy or a touch all move
	// the timestamp without changing anything, and re-importing every model
	// after a git operation is exactly the kind of thing that makes an asset
	// pipeline feel slow.
	uint64_t Registry::HashFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return 0;

		constexpr uint64_t kOffsetBasis = 14695981039346656037ull;
		constexpr uint64_t kPrime = 1099511628211ull;

		uint64_t hash = kOffsetBasis;
		char buffer[8192];

		while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0)
		{
			const std::streamsize read = file.gcount();
			for (std::streamsize i = 0; i < read; i++)
			{
				hash ^= (uint64_t)(unsigned char)buffer[i];
				hash *= kPrime;
			}
		}

		return hash;
	}
}
