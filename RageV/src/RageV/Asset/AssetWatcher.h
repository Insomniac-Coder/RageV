#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace RageV::Assets
{
	// Notices when a file under the project's asset root changes on disk.
	//
	// **Polled, not subscribed.** `<filesystem>` has no change notification --
	// that is `ReadDirectoryChangesW`, `inotify` and `FSEvents`, three APIs with
	// three lifetimes and three failure modes, and the one thing they have in
	// common is that none of them is portable. A recursive walk is the same
	// code everywhere and, done twice a second, costs little enough to sit on
	// the main thread.
	//
	// **Measured, because the first version was not:** 340 files in the sample
	// project scanned in **20.5 ms**, which is five frames' worth of budget
	// landing every half second and showed up as a 25 ms spike in the editor's
	// frame maximum. Almost all of it was one line -- `fs::relative` per file,
	// which normalises both paths and may touch the filesystem to do it. A
	// prefix strip gives the identical answer for paths that came out of an
	// iterator rooted at the root, and the same scan is now **0.78 ms**. The
	// frame maximum went 25.7 -> 11.2 ms with the mean unmoved.
	//
	// The remaining cost scales with the *number* of files rather than their
	// size, and the status bar reports it every scan rather than leaving it to
	// be rediscovered. A project large enough for 0.78 ms to become a problem
	// wants a platform watcher, and the shape of this class -- collect changes,
	// hand them back in a batch -- is what that would slot into.
	class AssetWatcher
	{
	public:
		struct Change
		{
			enum class Kind { Added, Modified, Removed };

			Kind What = Kind::Modified;
			// Relative to the watched root, with forward slashes: what the
			// registry names an asset by, so a caller can look one up.
			std::string Path;
		};

		// Takes the first snapshot without reporting anything. Every file that
		// already exists is not news.
		void Begin(const std::filesystem::path& root);
		void Stop();

		bool IsWatching() const { return !m_Root.empty(); }
		const std::filesystem::path& Root() const { return m_Root; }

		// Nothing until `interval` has passed, then whatever changed. Safe to
		// call every frame; that is what it is for.
		std::vector<Change> Poll(float deltaSeconds, float interval = 0.5f);

		// How long the last scan took, in milliseconds. Exposed so the cost can
		// be looked at rather than assumed -- if this stops being a rounding
		// error, that is the number that says so.
		float LastScanMs() const { return m_LastScanMs; }
		size_t WatchedCount() const { return m_Entries.size(); }

	private:
		struct Entry
		{
			std::filesystem::file_time_type Written{};
			// The write time changed and has not yet been seen to hold still.
			//
			// **A file is reported one poll after it stops moving**, and that
			// delay is the point. A tool writing a mesh appears on disk long
			// before it is finished, and a watcher that fires on the first
			// sight of a new timestamp hands the importer half a file --
			// which fails, caches the failure, and leaves the asset broken
			// until something touches it again.
			bool Settling = false;
			// Whether this file is new rather than changed. Kept because the
			// distinction is free here and unrecoverable later: by the time it
			// settles, "was there one scan ago" is a question nothing can
			// answer any more, and a caller wants to say "imported" for one
			// and "reloaded" for the other.
			bool Added = false;
			// Cleared at the top of each scan; whatever is still false at the
			// end has been deleted.
			bool Seen = false;
		};

		std::filesystem::path m_Root;
		std::unordered_map<std::string, Entry> m_Entries;
		float m_Elapsed = 0.0f;
		float m_LastScanMs = 0.0f;
	};
}
