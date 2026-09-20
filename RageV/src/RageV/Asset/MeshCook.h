#pragma once
#include "GltfImporter.h"

#include <cstdint>
#include <vector>

namespace RageV::Assets
{
	// The cooked form of a model: the ImportedModel a glTF parse produces,
	// serialized. Loading one is a read and a handful of memcpys where the
	// source form is a JSON parse, a buffer resolve and a reindex -- and the
	// cooker runs the same GltfImporter the editor uses, so there is one
	// parser and nothing to drift. See ENGINE-NOTES 7i.
	//
	// On disk ("RVMS", version 1, little-endian): strings as length + bytes,
	// vectors of plain data as count + raw bytes, in the order the fields
	// are declared. The format holds engine types directly, which is the
	// deliberate trade: a cooked mesh is a cache, rebuilt by repackaging,
	// not an interchange format anyone else reads.
	class MeshCook
	{
	public:
		static std::vector<uint8_t> Serialize(const ImportedModel& model);
		static bool Deserialize(ImportedModel& out, const uint8_t* bytes, size_t size);

		// The sniff GltfImporter does before handing bytes to cgltf.
		static bool IsCooked(const uint8_t* bytes, size_t size);

		// And whether it is a cook *this build can read*. The import cache asks
		// before serving an entry: without it a version bump leaves every cached
		// mesh being refused by the importer and re-imported from source on every
		// launch, for ever, because the cache only ever checked that a file was
		// there (found 2026-09-20 -- 68 of 70 entries were a version behind).
		static bool IsCurrentVersion(const uint8_t* bytes, size_t size);
	};
}
