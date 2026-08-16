#include <rvpch.h>
#include "PakFile.h"
#include "RageV/Core/Log.h"

namespace RageV::IO
{
	namespace
	{
		constexpr char kMagic[4] = { 'R', 'V', 'P', 'K' };
		constexpr uint32_t kVersion = 1;
		// magic + version + entryCount + tableOffset
		constexpr uint64_t kHeaderSize = 4 + 4 + 8 + 8;

		struct TableRow
		{
			uint64_t Hash;
			uint64_t Offset;
			uint64_t Size;
			uint32_t Flags;
			uint32_t PathLength;
		};
		static_assert(sizeof(TableRow) == 32, "the table is the file format");
	}

	std::string NormalizePath(std::string_view path)
	{
		// lexically_normal resolves dot segments and unifies separators
		// without touching the filesystem -- a lookup must not cost syscalls.
		std::string normal =
			std::filesystem::path(path).lexically_normal().generic_string();

		// ASCII only, deliberately: this has to agree with itself on store and
		// lookup, which a locale-dependent lowering does not guarantee.
		for (char& c : normal)
		{
			if (c >= 'A' && c <= 'Z')
				c = (char)(c - 'A' + 'a');
		}

		return normal;
	}

	uint64_t HashPath(std::string_view normalized)
	{
		// FNV-1a, 64-bit.
		uint64_t hash = 14695981039346656037ull;
		for (unsigned char c : normalized)
		{
			hash ^= c;
			hash *= 1099511628211ull;
		}
		return hash;
	}

	// --- writer ---------------------------------------------------------------

	bool PakWriter::AddFile(std::string_view relativePath, const std::filesystem::path& source)
	{
		std::ifstream file(source, std::ios::binary | std::ios::ate);
		if (!file)
		{
			RV_CORE_ERROR("Pak: could not read '{0}'", source.string());
			return false;
		}

		const std::streamsize size = file.tellg();
		file.seekg(0);

		std::vector<uint8_t> bytes((size_t)size);
		if (size > 0 && !file.read((char*)bytes.data(), size))
		{
			RV_CORE_ERROR("Pak: could not read '{0}'", source.string());
			return false;
		}

		return AddBytes(relativePath, std::move(bytes));
	}

	bool PakWriter::AddBytes(std::string_view relativePath, std::vector<uint8_t> bytes)
	{
		std::string path = NormalizePath(relativePath);
		if (path.empty())
			return false;

		// Refused rather than replaced: two files landing on one normalized
		// path -- "Rock.png" and "rock.png" -- would mean the pak answers one
		// of them with the other's bytes, silently, forever.
		if (m_Taken.count(path))
		{
			RV_CORE_ERROR("Pak: '{0}' is already in the archive", path);
			return false;
		}

		m_Taken[path] = m_Entries.size();
		m_Entries.push_back({ std::move(path), std::move(bytes) });
		return true;
	}

	uint64_t PakWriter::TotalBytes() const
	{
		uint64_t total = 0;
		for (const Pending& entry : m_Entries)
			total += entry.Bytes.size();
		return total;
	}

	bool PakWriter::Write(const std::filesystem::path& file) const
	{
		std::error_code error;
		if (file.has_parent_path())
			std::filesystem::create_directories(file.parent_path(), error);

		std::ofstream out(file, std::ios::binary);
		if (!out)
		{
			RV_CORE_ERROR("Pak: could not write '{0}'", file.string());
			return false;
		}

		// Blobs start right after the header; the table follows them, so it
		// can be written in this single pass with offsets already known.
		uint64_t offset = kHeaderSize;
		std::vector<TableRow> table;
		table.reserve(m_Entries.size());

		for (const Pending& entry : m_Entries)
		{
			TableRow row;
			row.Hash = HashPath(entry.Path);
			row.Offset = offset;
			row.Size = entry.Bytes.size();
			row.Flags = 0;
			row.PathLength = (uint32_t)entry.Path.size();
			table.push_back(row);

			offset += entry.Bytes.size();
		}

		const uint64_t entryCount = m_Entries.size();
		const uint64_t tableOffset = offset;

		out.write(kMagic, 4);
		out.write((const char*)&kVersion, 4);
		out.write((const char*)&entryCount, 8);
		out.write((const char*)&tableOffset, 8);

		for (const Pending& entry : m_Entries)
			out.write((const char*)entry.Bytes.data(), entry.Bytes.size());

		out.write((const char*)table.data(), table.size() * sizeof(TableRow));

		for (const Pending& entry : m_Entries)
			out.write(entry.Path.data(), entry.Path.size());

		if (!out)
		{
			RV_CORE_ERROR("Pak: writing '{0}' failed", file.string());
			return false;
		}

		return true;
	}

	// --- reader ---------------------------------------------------------------

	bool PakReader::Open(const std::filesystem::path& file)
	{
		std::ifstream in(file, std::ios::binary);
		if (!in)
		{
			RV_CORE_ERROR("Pak: could not open '{0}'", file.string());
			return false;
		}

		char magic[4] = {};
		uint32_t version = 0;
		uint64_t entryCount = 0;
		uint64_t tableOffset = 0;

		in.read(magic, 4);
		in.read((char*)&version, 4);
		in.read((char*)&entryCount, 8);
		in.read((char*)&tableOffset, 8);

		if (!in || std::memcmp(magic, kMagic, 4) != 0)
		{
			RV_CORE_ERROR("Pak: '{0}' is not a pak file", file.string());
			return false;
		}

		if (version != kVersion)
		{
			RV_CORE_ERROR("Pak: '{0}' is version {1}; this engine reads {2}",
						  file.string(), version, kVersion);
			return false;
		}

		std::vector<TableRow> table(entryCount);
		in.seekg((std::streamoff)tableOffset);
		if (entryCount > 0)
			in.read((char*)table.data(), entryCount * sizeof(TableRow));

		m_Entries.clear();
		m_Lookup.clear();
		m_Entries.reserve(entryCount);

		for (const TableRow& row : table)
		{
			std::string path(row.PathLength, '\0');
			in.read(path.data(), row.PathLength);

			PakEntry entry;
			entry.Path = std::move(path);
			entry.Offset = row.Offset;
			entry.Size = row.Size;
			entry.Flags = row.Flags;

			m_Lookup[entry.Path] = m_Entries.size();
			m_Entries.push_back(std::move(entry));
		}

		if (!in)
		{
			RV_CORE_ERROR("Pak: '{0}' ends before its table does", file.string());
			m_Entries.clear();
			m_Lookup.clear();
			return false;
		}

		m_File = file;
		return true;
	}

	bool PakReader::Contains(const std::string& normalizedPath) const
	{
		return m_Lookup.count(normalizedPath) != 0;
	}

	bool PakReader::ReadBytes(const std::string& normalizedPath, std::vector<uint8_t>& out) const
	{
		const auto it = m_Lookup.find(normalizedPath);
		if (it == m_Lookup.end())
			return false;

		const PakEntry& entry = m_Entries[it->second];

		// A handle of its own per read, so concurrent readers never share a
		// file cursor. Asset loads are not a hot loop; correctness under the
		// audio thread is worth an open.
		std::ifstream in(m_File, std::ios::binary);
		if (!in)
			return false;

		out.resize((size_t)entry.Size);
		in.seekg((std::streamoff)entry.Offset);
		if (entry.Size > 0 && !in.read((char*)out.data(), entry.Size))
			return false;

		return true;
	}

	std::unique_ptr<PakReader::Stream> PakReader::OpenStream(const std::string& normalizedPath) const
	{
		const auto it = m_Lookup.find(normalizedPath);
		if (it == m_Lookup.end())
			return nullptr;

		const PakEntry& entry = m_Entries[it->second];
		auto stream = std::make_unique<Stream>(m_File, entry.Offset, entry.Size);
		return stream->IsOpen() ? std::move(stream) : nullptr;
	}

	// --- stream ---------------------------------------------------------------

	PakReader::Stream::Stream(const std::filesystem::path& pak, uint64_t offset, uint64_t size)
		: m_File(std::make_unique<std::ifstream>(pak, std::ios::binary))
		, m_Offset(offset)
		, m_Size(size)
	{
	}

	bool PakReader::Stream::IsOpen() const
	{
		return m_File && m_File->good();
	}

	bool PakReader::Stream::Seek(uint64_t position)
	{
		if (position > m_Size)
			return false;

		m_Cursor = position;
		return true;
	}

	size_t PakReader::Stream::Read(void* out, size_t bytes)
	{
		if (!m_File || m_Cursor >= m_Size)
			return 0;

		const uint64_t available = m_Size - m_Cursor;
		const size_t toRead = (size_t)Math::Min<uint64_t>(bytes, available);

		// Reads never cross the entry's end: the bytes after it belong to the
		// next asset, and a decoder that over-reads would decode them.
		m_File->clear();
		m_File->seekg((std::streamoff)(m_Offset + m_Cursor));
		m_File->read((char*)out, toRead);

		const size_t got = (size_t)m_File->gcount();
		m_Cursor += got;
		return got;
	}
}
