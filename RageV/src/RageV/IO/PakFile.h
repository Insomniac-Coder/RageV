#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace RageV::IO
{
	// The archive a shipped game reads its content from. See ENGINE-NOTES 7h
	// for why this exists and what deliberately stays out of it.
	//
	// On disk (`.rvpak`, little-endian):
	//
	//     char     magic[4]      "RVPK"
	//     uint32   version       1
	//     uint64   entryCount
	//     uint64   tableOffset
	//     ...entry blobs, raw bytes...
	//     table:   entryCount * { uint64 pathHash; uint64 offset;
	//                             uint64 size; uint32 flags; uint32 pathLength; }
	//     ...path bytes, concatenated in entry order...
	//
	// The hash is FNV-1a of the normalized path, kept so a future reader can
	// look entries up without loading the string table; this reader builds a
	// string-keyed map and checks paths exactly, so a collision is a non-event.
	// Flags are reserved -- a compression method goes there when one is worth
	// having (version 1 stores raw bytes on purpose: the heavy payloads are
	// PNGs, already compressed, and cooking will replace those bytes wholesale).

	// One entry, as the table describes it.
	struct PakEntry
	{
		// Normalized, relative to the root the archive shadows.
		std::string Path;
		uint64_t Offset = 0;
		uint64_t Size = 0;
		uint32_t Flags = 0;
	};

	// The normalization every path in this system goes through, on store and
	// on lookup both: forward slashes, dot segments resolved, ASCII lowered.
	// Lowered because the loose filesystem this replaces was case-insensitive,
	// and a pak that suddenly is not would break scenes that worked for months.
	std::string NormalizePath(std::string_view path);

	uint64_t HashPath(std::string_view normalized);

	// Gathers files, then writes the archive in one pass.
	class PakWriter
	{
	public:
		// False when the source cannot be read or the path is already taken --
		// two files landing on one normalized path means the pak would answer
		// one of them with the other's bytes, silently, forever.
		bool AddFile(std::string_view relativePath, const std::filesystem::path& source);
		bool AddBytes(std::string_view relativePath, std::vector<uint8_t> bytes);

		bool Write(const std::filesystem::path& file) const;

		size_t Count() const { return m_Entries.size(); }
		uint64_t TotalBytes() const;

	private:
		struct Pending
		{
			std::string Path;
			std::vector<uint8_t> Bytes;
		};

		std::vector<Pending> m_Entries;
		std::unordered_map<std::string, size_t> m_Taken;
	};

	// Opens an archive and answers reads from it. Immutable once opened, which
	// is what makes it safe to read from any thread -- the audio thread
	// streams from the same archive the main thread loads textures from, and
	// every stream owns its own handle to the file.
	class PakReader
	{
	public:
		bool Open(const std::filesystem::path& file);

		bool Contains(const std::string& normalizedPath) const;
		bool ReadBytes(const std::string& normalizedPath, std::vector<uint8_t>& out) const;

		const std::vector<PakEntry>& Entries() const { return m_Entries; }
		const std::filesystem::path& File() const { return m_File; }

		// An independent view of one entry. Reads never cross the entry's end.
		class Stream
		{
		public:
			Stream(const std::filesystem::path& pak, uint64_t offset, uint64_t size);

			bool IsOpen() const;
			uint64_t Size() const { return m_Size; }
			uint64_t Tell() const { return m_Cursor; }
			bool Seek(uint64_t position);
			size_t Read(void* out, size_t bytes);

		private:
			std::unique_ptr<std::ifstream> m_File;
			uint64_t m_Offset = 0;
			uint64_t m_Size = 0;
			uint64_t m_Cursor = 0;
		};

		std::unique_ptr<Stream> OpenStream(const std::string& normalizedPath) const;

	private:
		std::filesystem::path m_File;
		std::vector<PakEntry> m_Entries;
		std::unordered_map<std::string, size_t> m_Lookup;
	};
}
