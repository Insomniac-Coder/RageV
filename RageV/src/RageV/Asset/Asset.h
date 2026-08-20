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
		Audio,
		// A keyed ramp over normalised time: size and alpha over a
		// particle's life, and the colour gradient. One type for all three,
		// distinguished by how many channels a key carries.
		Curve,
		// A `.rvfont`: the metrics table beside a distance-field atlas, both
		// produced by tools/rvfont. The `.ttf` it was baked from is *not* an
		// asset -- nothing at runtime can read one, and treating it as one
		// would offer a handle that never resolves.
		Font,
		// A `.rvpostprofile`: how a camera's frame is graded. Exposure, bloom,
		// and everything roadmap phase 9 adds.
		//
		// An asset rather than a field on the camera, because a grade is
		// authored content that several cameras -- and eventually several
		// scenes -- should be able to share, and because sharing one is the
		// difference between changing a look once and changing it everywhere
		// it was pasted. ENGINE-NOTES 7s.
		PostProfile,
		// A `.cube`: a colour-grading lookup table, uploaded as a 3D texture.
		// The look a grading tool exported, accepted unchanged.
		ColorLut,
		// An `.rvterrain`: a square grid of 16-bit heights (ENGINE-NOTES 7ap).
		// Its own type rather than a texture because a height needs sixteen
		// bits the cook policy would take away, is read on the CPU, and is
		// written back by sculpting.
		Terrain,
		// An `.rvgraph`: a visual script (8.10, ENGINE-NOTES 7bh). An asset
		// rather than a component's payload because a graph is authored
		// content that outlives the entity it was first dropped on, and
		// because **it generates a C# file** -- the graph and its output are
		// two files that have to be diffable side by side.
		ScriptGraph,
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
