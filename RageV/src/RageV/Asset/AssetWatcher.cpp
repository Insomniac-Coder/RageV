#include <rvpch.h>
#include "AssetWatcher.h"

#include <chrono>

namespace RageV::Assets
{
	namespace
	{
		namespace fs = std::filesystem;

		// A `.meta` is the importer's own output, so watching one is watching
		// this system's exhaust: importing rewrites it, the rewrite is a
		// change, the change asks for an import. That loop is why it is not
		// merely wasteful to include them.
		//
		// The cost is that editing import settings by hand is not noticed. The
		// editor writes them through the registry, which knows what it changed.
		bool Ignored(const fs::path& path)
		{
			const std::string extension = path.extension().string();

			// `.partial` is what this engine calls a file it is still writing;
			// the other two are what everything else calls one.
			return extension == ".meta" || extension == ".partial" ||
				   extension == ".tmp" || extension == ".swp";
		}

		// **A prefix strip, not fs::relative.** They agree here and cost
		// wildly different amounts: `fs::relative` normalises both paths and
		// is free to touch the filesystem to do it, which at one call per file
		// per scan was most of the scan. Every path handed to this came out of
		// an iterator rooted at `root`, so the prefix is there by
		// construction and removing it is a substring.
		std::string RelativeSlashed(const std::string& root, const fs::path& file)
		{
			std::string text = file.generic_string();
			if (text.size() <= root.size() || text.compare(0, root.size(), root) != 0)
				return {};

			size_t start = root.size();
			if (start < text.size() && text[start] == '/')
				start++;

			return text.substr(start);
		}
	}

	void AssetWatcher::Begin(const fs::path& root)
	{
		Stop();

		std::error_code error;
		if (root.empty() || !fs::is_directory(root, error))
			return;

		m_Root = root;

		// The opening snapshot records everything and reports nothing: a
		// project that has just been opened has not changed.
		Poll(1.0e6f, 0.0f);
		m_Elapsed = 0.0f;
	}

	void AssetWatcher::Stop()
	{
		m_Root.clear();
		m_Entries.clear();
		m_Elapsed = 0.0f;
		m_LastScanMs = 0.0f;
	}

	std::vector<AssetWatcher::Change> AssetWatcher::Poll(float deltaSeconds, float interval)
	{
		std::vector<Change> changes;
		if (m_Root.empty())
			return changes;

		m_Elapsed += deltaSeconds;
		if (m_Elapsed < interval)
			return changes;
		m_Elapsed = 0.0f;

		const auto started = std::chrono::steady_clock::now();
		const bool opening = m_Entries.empty();
		const std::string rootText = m_Root.generic_string();

		for (auto& entry : m_Entries)
			entry.second.Seen = false;

		std::error_code error;
		fs::recursive_directory_iterator walk(
			m_Root, fs::directory_options::skip_permission_denied, error);
		if (error)
			return changes;

		// Errors are stepped past rather than thrown. A directory that vanishes
		// mid-walk is exactly what a tool rewriting a folder looks like, and it
		// must not take the editor with it.
		const fs::recursive_directory_iterator end;
		for (; walk != end; walk.increment(error))
		{
			if (error)
			{
				error.clear();
				continue;
			}

			if (!walk->is_regular_file(error) || error)
			{
				error.clear();
				continue;
			}

			if (Ignored(walk->path()))
				continue;

			const std::string relative = RelativeSlashed(rootText, walk->path());
			if (relative.empty())
				continue;

			const fs::file_time_type written = walk->last_write_time(error);
			if (error)
			{
				// Locked by whoever is writing it. The next poll will see it,
				// and until then it is neither seen nor forgotten -- so leave
				// Seen alone rather than reporting a deletion.
				error.clear();
				auto held = m_Entries.find(relative);
				if (held != m_Entries.end())
					held->second.Seen = true;
				continue;
			}

			auto existing = m_Entries.find(relative);
			if (existing == m_Entries.end())
			{
				Entry fresh;
				fresh.Written = written;
				fresh.Seen = true;
				fresh.Added = !opening;
				// Nothing is news in the opening snapshot, so nothing there
				// settles into a report.
				fresh.Settling = !opening;
				m_Entries.emplace(relative, fresh);
				continue;
			}

			Entry& entry = existing->second;
			entry.Seen = true;

			if (entry.Written != written)
			{
				// Moved since last time: note the new time and wait for it to
				// hold still before telling anybody.
				entry.Written = written;
				entry.Settling = true;
				continue;
			}

			if (entry.Settling)
			{
				entry.Settling = false;
				changes.push_back({ entry.Added ? Change::Kind::Added : Change::Kind::Modified,
									relative });
				entry.Added = false;
			}
		}

		for (auto entry = m_Entries.begin(); entry != m_Entries.end();)
		{
			if (entry->second.Seen)
			{
				++entry;
				continue;
			}

			changes.push_back({ Change::Kind::Removed, entry->first });
			entry = m_Entries.erase(entry);
		}

		const std::chrono::duration<float, std::milli> spent =
			std::chrono::steady_clock::now() - started;
		m_LastScanMs = spent.count();

		return changes;
	}
}
