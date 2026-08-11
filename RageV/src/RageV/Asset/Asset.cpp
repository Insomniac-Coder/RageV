#include <rvpch.h>
#include "Asset.h"
#include <algorithm>

namespace RageV
{
	namespace
	{
		struct TypeName { AssetType Type; const char* Name; };

		// By name on disk, not by index: inserting a value into the enum should
		// not reinterpret every asset already imported.
		constexpr TypeName kTypeNames[] = {
			{ AssetType::None,     "None" },
			{ AssetType::Mesh,     "Mesh" },
			{ AssetType::Texture,  "Texture" },
			{ AssetType::Material, "Material" },
			{ AssetType::Prefab,   "Prefab" },
			{ AssetType::Scene,    "Scene" },
			{ AssetType::Audio,    "Audio" },
			{ AssetType::Curve,    "Curve" },
			{ AssetType::Font,     "Font" },
		};
	}

	const char* AssetTypeName(AssetType type)
	{
		for (const TypeName& entry : kTypeNames)
		{
			if (entry.Type == type)
				return entry.Name;
		}
		return "None";
	}

	AssetType AssetTypeFromName(const std::string& name)
	{
		for (const TypeName& entry : kTypeNames)
		{
			if (name == entry.Name)
				return entry.Type;
		}
		return AssetType::None;
	}

	AssetType AssetTypeFromExtension(const std::string& extension)
	{
		std::string lower = extension;
		std::transform(lower.begin(), lower.end(), lower.begin(),
					   [](unsigned char c) { return (char)std::tolower(c); });

		if (lower == ".gltf" || lower == ".glb")
			return AssetType::Mesh;

		if (lower == ".png" || lower == ".jpg" || lower == ".jpeg" ||
			lower == ".tga" || lower == ".bmp" || lower == ".hdr")
			return AssetType::Texture;

		// What miniaudio decodes without a second library bolted on. Vorbis
		// (.ogg) needs stb_vorbis and is deliberately not claimed here -- a
		// file that imports and then will not play is worse than one that does
		// not import.
		if (lower == ".wav" || lower == ".mp3" || lower == ".flac")
			return AssetType::Audio;

		if (lower == ".rmat")    return AssetType::Material;
		if (lower == ".rprefab") return AssetType::Prefab;
		if (lower == ".rage")    return AssetType::Scene;
		if (lower == ".rcurve")  return AssetType::Curve;
		if (lower == ".rvfont")  return AssetType::Font;

		// A `.ttf` is deliberately absent. It is the *input* to tools/rvfont
		// and nothing at runtime can read one, so importing it would hand out
		// a handle that resolves to nothing.

		// Shaders and anything else are files the engine reads directly rather
		// than assets it hands out by handle.
		return AssetType::None;
	}
}
