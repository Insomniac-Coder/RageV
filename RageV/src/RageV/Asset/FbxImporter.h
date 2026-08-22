#pragma once

// FBX import (8.9, ENGINE-NOTES 7bn).
//
// A second *front end*, not a second pipeline. It fills the same
// `ImportedModel` the glTF importer fills, and everything downstream of that
// struct -- GPU upload, the entity tree, cooking to `.rvmesh`, packing, and
// the checks -- is already written and does not know which parser ran.
//
// Static geometry, materials, textures, the node hierarchy, skinning and
// animation -- the whole row (stage 1 on 2026-08-20, stage 2 on the 22nd).
//
// What this format needs that glTF does not, all of it recorded in 7bn and
// 7bz: axes and units (an FBX carries its own, and the three common exporters
// disagree), a material model that is Phong underneath, n-gon faces, texture
// paths recorded from the machine the artist was sitting at -- and, for
// animation, a node transform that is not a TRS triple but a ten-part chain
// with pivots and a pre-rotation in it. That last one is why clips are baked
// through `ufbx_bake_anim` rather than read off the curves.

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
