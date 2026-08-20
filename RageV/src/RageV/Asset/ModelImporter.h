#pragma once

// One door to model import, whichever format is behind it (8.9, 7bn).
//
// `ImportedModel` was already format-neutral before FBX existed -- nothing in
// it is glTF -- and its five consumers never asked what parsed them. This
// makes that explicit rather than leaving every call site to name a parser it
// does not care about: the extension decides, and adding a third format is a
// case here plus a front end, not a change to anything downstream.

#include "GltfImporter.h"
#include <filesystem>

namespace RageV::Assets
{
	class ModelImporter
	{
	public:
		// Every caller that wants *a model* wants this one. Answers from
		// cooked bytes where the format's importer can, and parses the source
		// otherwise.
		static bool Import(const std::filesystem::path& path, ImportedModel& out);

		// Source only, no cooked shortcut -- for the import cache, whose job
		// is to produce the cooked form.
		static bool ImportSource(const std::filesystem::path& path, ImportedModel& out);

		// Whether this extension names a model at all. One list, so the
		// registry, the import cache and the packager cannot drift about what
		// counts as a mesh.
		static bool IsModelExtension(const std::string& lowercaseExtension);
	};
}
