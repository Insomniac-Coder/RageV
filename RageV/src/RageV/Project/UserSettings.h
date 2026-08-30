#pragma once
#include <filesystem>
#include <string>

namespace RageV
{
	// What belongs to *this person on this machine*, rather than to the
	// project.
	//
	// **The distinction is the whole reason this file exists.** A project file
	// is committed: everything in it is a statement the whole team agrees to,
	// and the start scene is one of those -- it is what the game ships opening
	// on, so it has to travel with the project. Which scene somebody happened
	// to be looking at when they closed the editor is not that. Storing it
	// beside the start scene would mean every person on a project rewriting a
	// committed file every time they opened a level, and each of them seeing
	// the others' answer arrive in a merge.
	//
	// So it lives in `Cache/`, which is already the folder a project expects
	// to lose without consequence and is already ignored by version control.
	// A project moved to another machine arrives without one, which is not an
	// error -- it is the case the project's own start scene is the answer to.
	class UserSettings
	{
	public:
		// Asset-relative, like ProjectConfig::StartScene, and empty when there
		// is none. Stored relative for the same reason: an absolute path is a
		// path into one computer.
		std::string LastScene;

		// `<project>/Cache/user.rvstate`.
		static std::filesystem::path PathFor(const std::filesystem::path& projectRoot);

		// A missing or unreadable file is an empty settings object, never a
		// failure. Nothing here is worth refusing to open an editor over.
		static UserSettings Load(const std::filesystem::path& projectRoot);

		bool Save(const std::filesystem::path& projectRoot) const;
	};
}
