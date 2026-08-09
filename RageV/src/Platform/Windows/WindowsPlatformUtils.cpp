#include <rvpch.h>
#include "RageV/Utils/PlatformUtils.h"
#include "RageV/Core/Application.h"

#include <commdlg.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

namespace RageV
{
	std::string FileDialogs::OpenFile(const char* filter)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260] = { 0 };
		ZeroMemory(&ofn, sizeof(OPENFILENAME));
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = glfwGetWin32Window((GLFWwindow*)Application::Get().GetWindow().GetNativeWindow());
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameA(&ofn) == TRUE)
		{
			return ofn.lpstrFile;
		}
		return std::string();
	}

	std::string FileDialogs::SaveFile(const char* filter)
	{
		OPENFILENAMEA ofn;
		CHAR szFile[260] = { 0 };
		ZeroMemory(&ofn, sizeof(OPENFILENAME));
		ofn.lStructSize = sizeof(OPENFILENAME);
		ofn.hwndOwner = glfwGetWin32Window((GLFWwindow*)Application::Get().GetWindow().GetNativeWindow());
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter = filter;
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetSaveFileNameA(&ofn) == TRUE)
		{
			return ofn.lpstrFile;
		}
		return std::string();
	}

	bool Process::RelaunchSelf(const std::string& arguments)
	{
		CHAR executable[MAX_PATH] = { 0 };
		if (GetModuleFileNameA(nullptr, executable, MAX_PATH) == 0)
		{
			RV_CORE_ERROR("Could not find this executable's own path; not relaunching");
			return false;
		}

		// Quoted, because the path routinely contains spaces and CreateProcess
		// would otherwise treat the first one as the end of the program name.
		std::string command = "\"" + std::string(executable) + "\" " + arguments;

		STARTUPINFOA startup = {};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process = {};

		// The working directory is inherited, which matters: ragev.ini is read
		// from it, and a relaunch that started somewhere else would ignore the
		// preference just written.
		if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE,
							0, nullptr, nullptr, &startup, &process))
		{
			RV_CORE_ERROR("Could not relaunch: CreateProcess failed with {0}", GetLastError());
			return false;
		}

		// Neither handle is waited on -- the point is to leave a new process
		// running after this one exits -- but both have to be closed or this
		// process leaks them for the few moments it has left.
		CloseHandle(process.hProcess);
		CloseHandle(process.hThread);
		return true;
	}

}
