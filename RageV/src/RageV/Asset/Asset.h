#pragma once
#include "RageV/Core/UUID.h"
#include <string>

namespace RageV
{
	// An asset is referenced by handle, never by path.
	//
	// This is the single mechanism that makes drag-and-drop assignment
	// trustworthy: a scene stores the handle, the handle lives in a .meta file
	// beside the source, and moving or renaming the source keeps both. A path
	// stored in a scene breaks the moment anyone reorganises a folder, and it
	// breaks silently.
	using AssetHandle = UUID;

	enum class AssetType : uint16_t
	{
		None = 0,
		Mesh,
		Texture,
		Material,
		Prefab,
		Scene,
	};

	const char* AssetTypeName(AssetType type);
	AssetType AssetTypeFromName(const std::string& name);
	// Guessed from a file extension when a source file is first seen.
	AssetType AssetTypeFromExtension(const std::string& extension);

	// Built-in assets have fixed handles rather than files. The values are
	// arbitrary but must stay stable: scenes on disk refer to them.
	namespace BuiltinAssets
	{
		constexpr uint64_t kPrimitiveBase = 0x7261676556000000ull;   // 'ragevV'
	}

	class Asset
	{
	public:
		virtual ~Asset() = default;
		virtual AssetType GetAssetType() const = 0;

		AssetHandle Handle = AssetHandle::Invalid();
	};
}
