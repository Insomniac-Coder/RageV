#pragma once
#include <filesystem>
#include <string>

namespace RageV
{
	class FileDialogs {
	public:
		static std::string OpenFile(const char* filter);
		static std::string SaveFile(const char* filter);
	};

	class Shell {
	public:
		// Shows `path` in the desktop's file manager, and answers whether the
		// request was handed over.
		//
		// **A directory is opened, a file is revealed with itself selected.**
		// Those are different requests and the difference is what somebody
		// means by "show me this": pointing the manager at a file's parent and
		// leaving them to find it among four hundred siblings is not it.
		//
		// True means the manager was launched, not that the user saw anything
		// -- nothing here waits for a window to appear.
		static bool ShowInFileManager(const std::filesystem::path& path);
	};

	class Process {
	public:
		// Starts this executable again, with `arguments`, and returns true if
		// the new process was created.
		//
		// The caller closes itself afterwards; this deliberately does not, so
		// a failed launch leaves the running editor alone rather than closing
		// it and leaving nothing.
		static bool RelaunchSelf(const std::string& arguments);
	};
}