#pragma once
#include "Asset.h"
#include "RageV/Renderer/RHI/RHITypes.h"
#include <filesystem>
#include <map>
#include <unordered_map>

namespace RageV
{
	struct AssetMetadata
	{
		AssetHandle Handle = AssetHandle::Invalid();
		AssetType Type = AssetType::None;

		// Relative to the assets root, with forward slashes, so a .meta written
		// on one machine means the same thing on another.
		std::string Path;

		// Hash of the source file's bytes. An importer compares it against what
		// it cached last time; a differing hash is the only reliable signal
		// that a re-import is needed, since timestamps move for reasons that
		// have nothing to do with content.
		uint64_t SourceHash = 0;

		bool IsValid() const { return Handle.IsValid(); }
	};

	// Maps handles to files, and files to handles.
	//
	// Handles are minted once and stored in a `<file>.meta` sidecar next to the
	// source. Rename or move the pair and every reference still resolves; that
	// is the whole point, and it is why the handle cannot live in a central
	// database keyed by path. **`.meta` files belong in version control** --
	// they are the identity, and regenerating them means every scene that
	// referred to an asset now refers to nothing.
	//
	// KNOWN LIMITATION: the root is currently the `assets` folder beside the
	// executable, which CMake copies from the source tree on every build. A
	// sidecar minted in that copy is lost on a clean build, so an asset added
	// there gets a fresh handle and any scene referring to it breaks. Assets
	// added to the *source* tree keep their handle, because the copy carries
	// the sidecar along. The real fix is the project concept in roadmap 4.1 --
	// a project folder that is the root, with no copying.
	class AssetRegistry
	{
	public:
		// Scans the root and reads or creates a sidecar for every asset file
		// found. Safe to call repeatedly.
		static void Init(const std::filesystem::path& assetsRoot);
		static void Shutdown();

		// Picks up files added or removed since the last scan.
		static void Refresh();

		static bool IsInitialised();
		static const std::filesystem::path& Root();

		// Invalid metadata when the handle is unknown.
		static const AssetMetadata& GetMetadata(AssetHandle handle);
		static AssetHandle GetHandle(const std::string& relativePath);

		// Absolute path for a handle; empty when unknown.
		static std::filesystem::path GetAbsolutePath(AssetHandle handle);

		// Ordered by path so the content browser and any listing are stable
		// between runs rather than following hash order.
		static const std::map<std::string, AssetMetadata>& All();

		// Registers an asset that has no source file of its own -- the built-in
		// primitives, for one. These get a fixed handle and are never written
		// to disk.
		static AssetHandle RegisterVirtual(AssetHandle handle, AssetType type, const std::string& name);

		static uint64_t HashFile(const std::filesystem::path& path);

	private:
		static void ScanDirectory(const std::filesystem::path& directory);
		static AssetMetadata ReadOrCreateMeta(const std::filesystem::path& file, AssetType type);
		static void WriteMeta(const std::filesystem::path& file, const AssetMetadata& metadata);
	};
}
