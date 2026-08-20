#include <rvpch.h>
#include "ImportCache.h"
#include "AssetRegistry.h"
#include "FontSerializer.h"
#include "GltfImporter.h"
#include "ModelImporter.h"
#include "MeshCook.h"
#include "RageV/Core/EngineConfig.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/TextureCook.h"
#include "RageV/IO/VFS.h"
#include "RageV/Project/Project.h"

#include "stb_image.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <unordered_set>

namespace RageV::Assets
{
	namespace
	{
		namespace fs = std::filesystem;

		// Guards the cooked-file writes and the atlas set. The boot worker is
		// the usual caller and the main thread is the usual *other* caller,
		// and a parallel import pass makes several workers of the first --
		// so this is a mutex from the start rather than after the crash that
		// would have introduced it.
		std::mutex s_Mutex;

		std::string ToLower(std::string text)
		{
			std::transform(text.begin(), text.end(), text.begin(),
						   [](unsigned char c) { return (char)std::tolower(c); });
			return text;
		}

		// Fixed width, so entries for one source sort together and a cache
		// directory can be read by a person.
		std::string HashHex(uint64_t hash)
		{
			char buffer[17];
			std::snprintf(buffer, sizeof(buffer), "%016llx", (unsigned long long)hash);
			return buffer;
		}

		// The cooked extension for a source extension, or empty when there is
		// no cooker for it.
		//
		// `.gltf` is deliberately absent while `.glb` is present. A `.gltf`
		// references its geometry from a sibling `.bin`, and the hash this
		// cache is keyed by covers only the `.gltf` itself -- so a re-export
		// that moves vertices without changing the JSON would keep serving
		// the old mesh forever. A `.glb` carries its buffers inside the file
		// the hash is taken over, so it has no such gap. A slow load is worth
		// avoiding; a silently stale mesh is not.
		// Self-contained model formats only. A `.gltf` keeps its geometry in a
		// sibling `.bin` that this cache's key does not cover, so it is always
		// re-imported; `.glb` and `.fbx` are one file each.
		bool IsCookedMesh(const std::string& extension)
		{
			return extension == ".glb" || extension == ".fbx";
		}

		bool IsCookedTexture(const std::string& extension)
		{
			return extension == ".png" || extension == ".jpg" || extension == ".jpeg";
		}

		// **One predicate each, because there were two and they disagreed.**
		// When the FBX front end landed, this function learned that a `.fbx`
		// cooks to a `.rvmesh` and `Cook` below did not learn what a `.fbx`
		// is -- so every one of them fell past the model branch into the
		// *image* decoder, warned that it would not decode, and was re-parsed
		// from source on every load. The camp scene made that loud: 41 props,
		// 82 warnings a run, and the cache doing nothing whatsoever for the
		// format it had just been taught to name.
		const char* CookedExtension(const std::string& extension)
		{
			if (IsCookedTexture(extension))
				return ".rvtex";
			if (IsCookedMesh(extension))
				return ".rvmesh";
			return nullptr;
		}

		struct Key
		{
			std::string Relative;      // as the registry knows it
			fs::path    File;          // where the cooked bytes live
			bool        Valid = false;
		};

		// Everything Fetch needs to decide, resolved once so the fast path and
		// the "is it cached" query cannot drift apart.
		Key Resolve(const fs::path& absoluteSource)
		{
			Key key;

			if (!Project::GetActive())
				return key;

			// --import-cache=off. Turns every query into a miss, so the whole
			// engine loads from source exactly as it did before this existed
			// -- which is what makes "cooked versus not" a measurable
			// difference rather than a remembered one.
			if (!EngineConfig::Get().UseImportCache)
				return key;

			// A pak already holds cooked bytes under this very path (7.1),
			// or holds source bytes that were deliberately shipped raw.
			// Either way, re-cooking into a local cache would be work done to
			// produce what the archive already answers with.
			if (IO::VFS::Origin(absoluteSource) == IO::FileOrigin::Pak)
				return key;

			const std::string relative = Project::MakeRelative(absoluteSource);
			if (relative.empty())
				return key;   // outside the project; not ours to cache

			const char* cooked = CookedExtension(ToLower(absoluteSource.extension().string()));
			if (!cooked)
				return key;

			if (CookPolicy::IsFontAtlas(relative))
				return key;

			// The hash the registry already keeps. Without it there is
			// nothing to key on, and inventing one here would mean reading
			// the whole file to decide whether reading the whole file could
			// be skipped.
			const AssetMetadata& metadata =
				Registry::GetMetadata(Registry::GetHandle(relative));
			if (!metadata.IsValid() || metadata.SourceHash == 0)
				return key;

			const fs::path relativePath(relative);
			const std::string name =
				relativePath.filename().string() + "." + HashHex(metadata.SourceHash) + cooked;

			key.Relative = relative;
			key.File = Project::CacheRoot() / ImportCache::kVersion /
					   relativePath.parent_path() / name;
			key.Valid = true;
			return key;
		}

		bool ReadFile(const fs::path& path, std::vector<uint8_t>& out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;

			const std::streamsize size = file.tellg();
			file.seekg(0);

			out.resize((size_t)size);
			return size == 0 || (bool)file.read((char*)out.data(), size);
		}

		// Every other entry for the same source. A project edited all week
		// would otherwise keep one cooked copy per version of every texture
		// it ever had, and nothing would ever delete them.
		void SweepStale(const fs::path& cacheFile)
		{
			std::error_code error;

			const fs::path directory = cacheFile.parent_path();
			const std::string name = cacheFile.filename().string();
			const std::string extension = cacheFile.extension().string();

			// "soil_normal.png." -- the part before the hash.
			const size_t hashStart = name.size() - extension.size() - 16;
			const std::string prefix = name.substr(0, hashStart);

			for (const auto& entry : fs::directory_iterator(directory, error))
			{
				if (error)
					return;

				const std::string other = entry.path().filename().string();
				if (other == name || other.rfind(prefix, 0) != 0)
					continue;
				if (entry.path().extension() != extension)
					continue;

				fs::remove(entry.path(), error);
			}
		}

		bool WriteEntry(const fs::path& cacheFile, const std::vector<uint8_t>& bytes)
		{
			std::error_code error;
			fs::create_directories(cacheFile.parent_path(), error);
			if (error)
			{
				RV_CORE_WARN("Import cache: could not create {0} ({1}); this asset will "
							 "be cooked again next launch",
							 cacheFile.parent_path().string(), error.message());
				return false;
			}

			// Written beside the target and moved into place, so a launch
			// interrupted mid-write cannot leave a truncated file whose name
			// claims it is a complete cook of that hash. Existence is
			// validity here, which only holds if a file that exists is whole.
			const fs::path temporary = cacheFile.string() + ".partial";
			{
				std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
				if (!out || !out.write((const char*)bytes.data(), (std::streamsize)bytes.size()))
				{
					RV_CORE_WARN("Import cache: could not write {0}", temporary.string());
					return false;
				}
			}

			fs::rename(temporary, cacheFile, error);
			if (error)
			{
				fs::remove(temporary, error);
				return false;
			}

			SweepStale(cacheFile);
			return true;
		}

		// Source bytes in, cooked bytes out. Empty means "this would not
		// cook", which the caller answers by loading the source.
		std::vector<uint8_t> Cook(const fs::path& absoluteSource, const std::string& relative,
								  const std::vector<uint8_t>& source)
		{
			const std::string extension = ToLower(absoluteSource.extension().string());

			if (IsCookedMesh(extension))
			{
				// ImportSource, not Import: Import asks this cache first, and
				// this *is* the cache, mid-answer. The explicit entry point
				// is what stops that being a recursion. It dispatches on the
				// extension itself, so this branch does not care which model
				// format arrived.
				ImportedModel model;
				if (!ModelImporter::ImportSource(absoluteSource, model))
					return {};

				return MeshCook::Serialize(model);
			}

			// Guarded rather than assumed. Nothing else should reach here --
			// `Fetch` only calls this when `CookedExtension` named one -- but
			// the failure mode of getting that wrong is a warning that blames
			// the file for not being a picture, which is what a `.fbx` spent
			// two versions being told.
			if (!IsCookedTexture(extension))
				return {};

			int width = 0, height = 0, channels = 0;
			stbi_uc* pixels = stbi_load_from_memory(source.data(), (int)source.size(),
													&width, &height, &channels, 4);
			if (!pixels)
			{
				RV_CORE_WARN("Import cache: {0} would not decode ({1}); it will be loaded "
							 "from source", relative, stbi_failure_reason());
				return {};
			}

			// The encode is chosen from the *name* (see 7i), which is why the
			// relative path is threaded down here rather than the absolute
			// one: an absolute path from another machine would still work,
			// but the relative one is what the packager passes, and the two
			// pipelines must make identical choices or a cooked editor and a
			// cooked package would disagree about a texture.
			const IO::CookedTexture cooked =
				IO::TextureCook::Cook(pixels, (uint32_t)width, (uint32_t)height, relative);
			stbi_image_free(pixels);

			return IO::TextureCook::Serialize(cooked);
		}
	}

	bool ImportCache::IsCookable(const fs::path& absoluteSource)
	{
		return Resolve(absoluteSource).Valid;
	}

	bool ImportCache::IsCached(const fs::path& absoluteSource)
	{
		const Key key = Resolve(absoluteSource);
		if (!key.Valid)
			return false;

		std::error_code error;
		return fs::exists(key.File, error);
	}

	bool ImportCache::Fetch(const fs::path& absoluteSource, std::vector<uint8_t>& out)
	{
		const Key key = Resolve(absoluteSource);
		if (!key.Valid)
			return false;

		// The whole point: a stat, not a parse. The hash is in the name, so a
		// file that is there is a current cook of this source and a file that
		// is not is the only signal needed to rebuild.
		std::error_code error;
		if (fs::exists(key.File, error) && ReadFile(key.File, out))
			return true;

		std::vector<uint8_t> source;
		if (!IO::VFS::ReadBytes(absoluteSource, source))
			return false;

		std::vector<uint8_t> cooked = Cook(absoluteSource, key.Relative, source);
		if (cooked.empty())
			return false;

		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			WriteEntry(key.File, cooked);
		}

		// Answered from what was just cooked rather than by reading back what
		// was just written -- and deliberately still answered when the write
		// failed, so a read-only or full disk costs this launch its cache and
		// nothing else.
		out = std::move(cooked);
		return true;
	}

	void ImportCache::Clear()
	{
		if (!Project::GetActive())
			return;

		std::lock_guard<std::mutex> lock(s_Mutex);

		std::error_code error;
		const uintmax_t removed = fs::remove_all(Project::CacheRoot(), error);
		if (error)
		{
			RV_CORE_WARN("Import cache: could not clear {0} ({1})",
						 Project::CacheRoot().string(), error.message());
			return;
		}

		RV_CORE_INFO("Import cache cleared ({0} file(s)); assets will be cooked again "
					 "on the next load", removed);
	}

	uint64_t ImportCache::SizeOnDisk()
	{
		if (!Project::GetActive())
			return 0;

		std::error_code error;
		const fs::path root = Project::CacheRoot();
		if (!fs::exists(root, error))
			return 0;

		uint64_t total = 0;
		for (const auto& entry : fs::recursive_directory_iterator(root, error))
		{
			if (error)
				break;
			if (entry.is_regular_file(error))
				total += entry.file_size(error);
		}

		return total;
	}

	namespace CookPolicy
	{
		namespace
		{
			std::unordered_set<std::string> s_Atlases;
			bool s_Collected = false;

			// Every `.rvfont` in the project, and the atlas each one names.
			//
			// Through the VFS and the filesystem rather than the registry,
			// because the packager runs without a registry at all -- and the
			// packager is the caller this most needs to agree with.
			void Collect()
			{
				s_Collected = true;
				s_Atlases.clear();

				if (!Project::GetActive())
					return;

				const fs::path root = Project::AssetRoot();

				for (const std::string& relative : IO::VFS::Enumerate(root))
				{
					const fs::path file(relative);
					if (ToLower(file.extension().string()) != ".rvfont")
						continue;

					Font font;
					if (!FontSerializer::Load(font, root / relative) || font.AtlasFile.empty())
						continue;

					// The atlas is named relative to its `.rvfont`, so a font
					// in a subfolder resolves inside that subfolder.
					const fs::path atlas = file.parent_path() / font.AtlasFile;
					s_Atlases.insert(ToLower(atlas.generic_string()));
				}
			}
		}

		bool IsFontAtlas(const std::string& relativePath)
		{
			std::lock_guard<std::mutex> lock(s_Mutex);

			if (!s_Collected)
				Collect();

			return s_Atlases.count(ToLower(fs::path(relativePath).generic_string())) > 0;
		}

		void Reset()
		{
			std::lock_guard<std::mutex> lock(s_Mutex);
			s_Collected = false;
			s_Atlases.clear();
		}
	}
}
