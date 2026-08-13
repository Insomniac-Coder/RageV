#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace RageV::IO
{
	// Where a path's bytes actually come from. The asset registry branches on
	// this: a pak's content cannot have changed since it was written, so
	// hashing it at boot answers a question that cannot come up.
	enum class FileOrigin
	{
		Missing,
		Loose,
		Pak,
	};

	// The virtual file system: a mount shadows a directory.
	//
	// Every reader in the engine builds absolute paths through
	// `Project::AssetPath`, and the VFS answers *those same paths*: mounting
	// `content.pak` over `<install>/content` means a request for
	// `<install>/content/scenes/menu.rage` is answered from the archive, and a
	// request nothing shadows falls through to the real filesystem untouched.
	// A development run mounts nothing and costs nothing; the call sites
	// cannot tell and do not care. Design: ENGINE-NOTES 7h.
	//
	// Precedence: the pak wins, loose files are fallthrough for entries the
	// pak lacks. A stale loose file silently overriding shipped content is the
	// harder bug to see; additive patch paks can layer by mount order later
	// (the latest mount over a root is consulted first).
	//
	// Mount and Unmount happen at project open and close; everything else is
	// read-only over immutable state and safe from any thread, which is what
	// the audio thread requires.
	class VFS
	{
	public:
		static bool MountPak(const std::filesystem::path& shadowedRoot,
							 const std::filesystem::path& pakFile);
		static void UnmountAll();
		static uint32_t MountCount();

		static bool Exists(const std::filesystem::path& path);
		static FileOrigin Origin(const std::filesystem::path& path);

		static bool ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& out);
		static bool ReadText(const std::filesystem::path& path, std::string& out);

		// Every file under `directory`, recursive, as normalized paths
		// relative to it -- pak entries and loose files merged, the pak's
		// answer kept where both hold the same path. Sorted, so two calls
		// agree about order.
		static std::vector<std::string> Enumerate(const std::filesystem::path& directory);

		// For the one consumer that cannot take a byte blob: audio reads a
		// file in pieces over the sound's lifetime, from its own thread.
		class Stream
		{
		public:
			virtual ~Stream() = default;
			virtual uint64_t Size() const = 0;
			virtual uint64_t Tell() const = 0;
			virtual bool Seek(uint64_t position) = 0;
			virtual size_t Read(void* out, size_t bytes) = 0;
		};

		static std::unique_ptr<Stream> OpenStream(const std::filesystem::path& path);
	};
}

namespace RageV
{
	using IO::VFS;
	using IO::FileOrigin;
}
