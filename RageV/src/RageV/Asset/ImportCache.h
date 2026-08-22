#pragma once

// Cooked assets, kept between launches.
//
// 7.2 built the cookers and measured what they save -- the material closeup
// dropped 3.7s to 2.0s, which was "the entire decode share" -- and then wired
// them to exactly one caller: the packager. So a *shipped* game boots from
// cooked bytes, while the editor, where somebody opens the same project
// twenty times a day, decodes every PNG in the scene every single time.
//
// This is the second caller. Cook on first sight into `<project>/Cache`,
// keyed on the content hash the registry already maintains for exactly this
// purpose, and every launch after the first reads a finished mip chain
// instead of building one.
//
// Design and measurements: ENGINE-NOTES 7l.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace RageV::Assets
{
	class ImportCache
	{
	public:
		// Cooked bytes for a source asset, cooking them first if no entry
		// matches the source's current hash.
		//
		// **False means "read the source yourself", and every caller must be
		// able to.** No open project, no registry entry, an extension with no
		// cooker, a source already inside a pak (those bytes are cooked
		// already, or were deliberately shipped raw), a font atlas, or a cook
		// that failed all answer false. A cache that cannot answer makes
		// loading slow; it must never make it wrong.
		//
		// `absoluteSource` is the same path the loader would have read, so a
		// call site is one line ahead of its existing VFS read.
		static bool Fetch(const std::filesystem::path& absoluteSource,
						  std::vector<uint8_t>& out);

		// Whether Fetch could answer for this path at all. Lets the loading
		// bar be sized, and the cold-import pass find its work, without
		// cooking anything.
		static bool IsCookable(const std::filesystem::path& absoluteSource);

		// Whether a *current* entry is already on disk. The difference
		// between this and IsCookable is exactly the cold work remaining,
		// which is what makes a first-open bar honest about being slower.
		static bool IsCached(const std::filesystem::path& absoluteSource);

		// Deletes every cooked file for the open project. Behind the editor's
		// "Rebuild Import Cache", and what a test calls to force the cold
		// path deliberately rather than by deleting a folder by hand.
		static void Clear();

		// What the cache currently occupies, for the editor to show. Zero
		// when there is no project or no cache yet.
		static uint64_t SizeOnDisk();

		// Bumped when the cooked formats or the encode rules change, which
		// invalidates every entry at once. A per-file check could not do
		// that: the files themselves are still perfectly readable, they are
		// just no longer what this build would have produced.
		//
		// **The importer counts as an encode rule.** v2 was here because
		// `GltfImporter` started extracting images that a `.glb` carries in
		// its binary chunk: the source hash of the model has not moved, so a
		// v1 `.rvmesh` would keep being served with the empty texture list it
		// was cooked with, and the fix would look like it had not worked.
		//
		// **And v3 is here because `MeshCook::kVersion` went to 2 for the
		// blend field and this did not follow it.** The two versions are not
		// the same thing and both have to move: MeshCook's says what a
		// `.rvmesh` contains, and this says which directory of them a build
		// will look in. Bumping only the first left every load reading a v1
		// file, refusing it, re-importing from source, and never writing the
		// result anywhere -- so it happened again on the next launch, and
		// again, with a red line in the log each time.
		//
		// The rule that follows: **a MeshCook version bump always bumps this
		// too.** A cooked format that changed and a cache that still points at
		// the old one is not a slow cache, it is a cache that cannot ever be
		// right again.
		static constexpr const char* kVersion = "v3";
	};

	// What must never be cooked. Shared by the cache and the packager so that
	// the two cannot quietly disagree about it.
	namespace CookPolicy
	{
		// An MSDF atlas is *data* that happens to be shaped like an image:
		// the shader reads signed distances out of it.
		//
		// Block compression quantizes those distances to a two-endpoint
		// palette per 4x4 block, and a mip chain averages distances from
		// opposite sides of a stroke into a number that describes nothing.
		// Both render as text that is soft for no visible reason -- the same
		// silent failure the colour-space rule in AssetManager::GetFontAtlas
		// exists to prevent, which is how that one was eventually found.
		//
		// Recognised by *reading* the `.rvfont` files, each of which names
		// its atlas, rather than by a filename convention: that field exists
		// precisely so the pairing does not have to be guessed.
		bool IsFontAtlas(const std::string& relativePath);

		// Drops the remembered atlas set. Called when the project changes,
		// because the answer belongs to a project.
		void Reset();
	}
}
