#include <rvpch.h>
#include "ModelImporter.h"
#include "FbxImporter.h"
#include "RageV/Core/Log.h"

#include <algorithm>
#include <cctype>

namespace RageV::Assets
{
	namespace
	{
		std::string LowerExtension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
			return extension;
		}
	}

	bool ModelImporter::IsModelExtension(const std::string& lowercaseExtension)
	{
		return lowercaseExtension == ".gltf"
			|| lowercaseExtension == ".glb"
			|| lowercaseExtension == ".fbx";
	}

	bool ModelImporter::Import(const std::filesystem::path& path, ImportedModel& out)
	{
		const std::string extension = LowerExtension(path);

		if (extension == ".fbx")
			return FbxImporter::Import(path, out);

		if (extension == ".gltf" || extension == ".glb")
			return GltfImporter::Import(path, out);

		RV_CORE_ERROR("'{0}' is not a model this build can read (.gltf, .glb, .fbx)",
					  path.string());
		return false;
	}

	bool ModelImporter::ImportSource(const std::filesystem::path& path, ImportedModel& out)
	{
		const std::string extension = LowerExtension(path);

		if (extension == ".fbx")
			return FbxImporter::ImportSource(path, out);

		if (extension == ".gltf" || extension == ".glb")
			return GltfImporter::ImportSource(path, out);

		RV_CORE_ERROR("'{0}' is not a model this build can read (.gltf, .glb, .fbx)",
					  path.string());
		return false;
	}
}
