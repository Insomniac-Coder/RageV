#pragma once

// FBX import (8.9, ENGINE-NOTES 7bn).
//
// A second *front end*, not a second pipeline. It fills the same
// `ImportedModel` the glTF importer fills, and everything downstream of that
// struct -- GPU upload, the entity tree, cooking to `.rvmesh`, packing, and
// the checks -- is already written and does not know which parser ran.
//
// **Stage 1: static geometry, materials, textures and the node hierarchy.**
// Skinning and animation are stage 2 and are stated as absent rather than
// half-done: a skeleton that imports but poses wrongly is worse than one that
// says it is not there.
//
// What this format needs that glTF does not, all of it recorded in 7bn:
// axes and units (an FBX carries its own, and the three common exporters
// disagree), a material model that is Phong underneath, n-gon faces, and
// texture paths recorded from the machine the artist was sitting at.

#include "GltfImporter.h"
#include <filesystem>

namespace RageV::Assets
{
	class FbxImporter
	{
	public:
		// Returns false and logs on failure. Accepts `.fbx`, binary or ASCII,
		// versions 6100 through 7500.
		//
		// Answers from cooked bytes when there are any, exactly as the glTF
		// importer does -- the import cache and a shipped pak both store the
		// same `.rvmesh`, and which parser originally produced it stopped
		// mattering the moment it was cooked.
		static bool Import(const std::filesystem::path& path, ImportedModel& out);

		// The same, from the source file only. For the import cache, whose job
		// is to *produce* the cooked form and which would otherwise ask itself
		// for the answer it is computing.
		static bool ImportSource(const std::filesystem::path& path, ImportedModel& out);
	};
}
