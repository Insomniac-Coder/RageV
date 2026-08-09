#pragma once
#include <string>

namespace RageV
{
	class FileDialogs {
	public:
		static std::string OpenFile(const char* filter);
		static std::string SaveFile(const char* filter);
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