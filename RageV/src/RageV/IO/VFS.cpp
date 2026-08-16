#include <rvpch.h>
#include "VFS.h"
#include "PakFile.h"
#include "RageV/Core/Log.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace RageV::IO
{
	namespace
	{
		struct Mount
		{
			// Normalized like every lookup, so matching is a string prefix.
			std::string Root;
			// Shared with any stream still open on it, so an unmount cannot
			// pull the archive out from under the audio thread.
			std::shared_ptr<PakReader> Pak;
		};

		// Later mounts are consulted first, which is what lets a patch pak
		// layer over the base one.
		std::vector<Mount> s_Mounts;

		// The path inside a mount, or empty when the path is not under it.
		// A prefix match alone is not enough: "content2/a.png" starts with
		// "content" and is not inside it.
		std::string KeyUnder(const std::string& root, const std::string& normalized)
		{
			if (normalized.size() <= root.size() + 1)
				return {};
			if (normalized.compare(0, root.size(), root) != 0)
				return {};
			if (normalized[root.size()] != '/')
				return {};

			return normalized.substr(root.size() + 1);
		}

		struct Resolved
		{
			const Mount* In = nullptr;
			std::string Key;
		};

		Resolved Resolve(const std::filesystem::path& path)
		{
			if (s_Mounts.empty())
				return {};

			const std::string normalized = NormalizePath(path.generic_string());

			for (auto it = s_Mounts.rbegin(); it != s_Mounts.rend(); ++it)
			{
				std::string key = KeyUnder(it->Root, normalized);
				if (!key.empty() && it->Pak->Contains(key))
					return { &*it, std::move(key) };
			}

			return {};
		}

		bool ReadLooseBytes(const std::filesystem::path& path, std::vector<uint8_t>& out)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;

			const std::streamsize size = file.tellg();
			file.seekg(0);

			out.resize((size_t)size);
			if (size > 0 && !file.read((char*)out.data(), size))
				return false;

			return true;
		}

		// A loose file behind the Stream interface, so audio does not care
		// where a clip lives.
		class LooseStream : public VFS::Stream
		{
		public:
			explicit LooseStream(const std::filesystem::path& path)
				: m_File(path, std::ios::binary | std::ios::ate)
			{
				if (m_File)
				{
					m_Size = (uint64_t)m_File.tellg();
					m_File.seekg(0);
				}
			}

			bool IsOpen() const { return m_File.good(); }

			uint64_t Size() const override { return m_Size; }
			uint64_t Tell() const override { return m_Cursor; }

			bool Seek(uint64_t position) override
			{
				if (position > m_Size)
					return false;
				m_Cursor = position;
				return true;
			}

			size_t Read(void* out, size_t bytes) override
			{
				if (m_Cursor >= m_Size)
					return 0;

				m_File.clear();
				m_File.seekg((std::streamoff)m_Cursor);
				m_File.read((char*)out,
							(std::streamsize)Math::Min<uint64_t>(bytes, m_Size - m_Cursor));

				const size_t got = (size_t)m_File.gcount();
				m_Cursor += got;
				return got;
			}

		private:
			std::ifstream m_File;
			uint64_t m_Size = 0;
			uint64_t m_Cursor = 0;
		};

		// A pak entry behind the same interface. Keeps the reader alive so an
		// unmount mid-playback frees nothing a stream still needs.
		class PakStream : public VFS::Stream
		{
		public:
			PakStream(std::shared_ptr<PakReader> pak,
					  std::unique_ptr<PakReader::Stream> stream)
				: m_Pak(std::move(pak)), m_Stream(std::move(stream))
			{
			}

			uint64_t Size() const override { return m_Stream->Size(); }
			uint64_t Tell() const override { return m_Stream->Tell(); }
			bool Seek(uint64_t position) override { return m_Stream->Seek(position); }
			size_t Read(void* out, size_t bytes) override { return m_Stream->Read(out, bytes); }

		private:
			std::shared_ptr<PakReader> m_Pak;
			std::unique_ptr<PakReader::Stream> m_Stream;
		};
	}

	bool VFS::MountPak(const std::filesystem::path& shadowedRoot,
					   const std::filesystem::path& pakFile)
	{
		auto pak = std::make_shared<PakReader>();
		if (!pak->Open(pakFile))
			return false;

		Mount mount;
		mount.Root = NormalizePath(shadowedRoot.generic_string());
		mount.Pak = std::move(pak);

		RV_CORE_INFO("VFS: mounted {0} ({1} entries) over {2}",
					 pakFile.filename().string(), mount.Pak->Entries().size(), mount.Root);

		s_Mounts.push_back(std::move(mount));
		return true;
	}

	void VFS::UnmountAll()
	{
		s_Mounts.clear();
	}

	uint32_t VFS::MountCount()
	{
		return (uint32_t)s_Mounts.size();
	}

	bool VFS::Exists(const std::filesystem::path& path)
	{
		return Origin(path) != FileOrigin::Missing;
	}

	FileOrigin VFS::Origin(const std::filesystem::path& path)
	{
		if (Resolve(path).In)
			return FileOrigin::Pak;

		std::error_code error;
		if (std::filesystem::is_regular_file(path, error))
			return FileOrigin::Loose;

		return FileOrigin::Missing;
	}

	bool VFS::ReadBytes(const std::filesystem::path& path, std::vector<uint8_t>& out)
	{
		if (Resolved hit = Resolve(path); hit.In)
			return hit.In->Pak->ReadBytes(hit.Key, out);

		return ReadLooseBytes(path, out);
	}

	bool VFS::ReadText(const std::filesystem::path& path, std::string& out)
	{
		std::vector<uint8_t> bytes;
		if (!ReadBytes(path, bytes))
			return false;

		out.assign((const char*)bytes.data(), bytes.size());
		return true;
	}

	std::vector<std::string> VFS::Enumerate(const std::filesystem::path& directory)
	{
		const std::string root = NormalizePath(directory.generic_string());

		// A set, because the same path may exist in a pak and loose on disk,
		// and the answer is one file (the pak's -- but for listing purposes
		// they are the same name either way).
		std::set<std::string> files;

		for (const Mount& mount : s_Mounts)
		{
			// The asked-for directory may be the mount root or somewhere
			// under it; either way pak keys are relative to the mount, so
			// they need the difference stripped.
			std::string prefix;
			if (root == mount.Root)
				prefix = {};
			else if (std::string key = KeyUnder(mount.Root, root); !key.empty())
				prefix = key + "/";
			else
				continue;

			for (const PakEntry& entry : mount.Pak->Entries())
			{
				if (prefix.empty())
					files.insert(entry.Path);
				else if (entry.Path.size() > prefix.size() &&
						 entry.Path.compare(0, prefix.size(), prefix) == 0)
					files.insert(entry.Path.substr(prefix.size()));
			}
		}

		std::error_code error;
		if (std::filesystem::is_directory(directory, error))
		{
			for (const auto& entry :
				 std::filesystem::recursive_directory_iterator(directory, error))
			{
				if (error || !entry.is_regular_file())
					continue;

				const std::filesystem::path relative =
					std::filesystem::relative(entry.path(), directory, error);
				if (!error)
					files.insert(NormalizePath(relative.generic_string()));
			}
		}

		return { files.begin(), files.end() };
	}

	std::unique_ptr<VFS::Stream> VFS::OpenStream(const std::filesystem::path& path)
	{
		if (Resolved hit = Resolve(path); hit.In)
		{
			auto stream = hit.In->Pak->OpenStream(hit.Key);
			if (!stream)
				return nullptr;
			return std::make_unique<PakStream>(hit.In->Pak, std::move(stream));
		}

		auto loose = std::make_unique<LooseStream>(path);
		return loose->IsOpen() ? std::move(loose) : nullptr;
	}
}
