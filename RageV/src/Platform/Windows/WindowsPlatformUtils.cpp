#include <rvpch.h>
#include "RageV/Utils/PlatformUtils.h"
#include "RageV/Core/Application.h"

#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
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

	bool Shell::ShowInFileManager(const std::filesystem::path& path)
	{
		std::error_code error;
		if (path.empty() || !std::filesystem::exists(path, error))
		{
			RV_CORE_WARN("Cannot show {0}: there is nothing there", path.string());
			return false;
		}

		// ShellExecute rather than spawning explorer.exe: it goes through the
		// shell's own association, so it is the file manager the user actually
		// has, and it does not leave a process handle to reap.
		//
		// `/select,` needs the *command line* form, which is why a file takes
		// the other branch. Both are ShellExecute -- one opens a folder, one
		// asks Explorer to open a folder with something highlighted.
		const bool directory = std::filesystem::is_directory(path, error);
		const std::wstring native = path.native();

		// **ShellExecute documents that COM must be initialised on the calling
		// thread, and nothing in this engine ever initialised it.**
		//
		// That is why the folder opened some of the time and not others: the
		// process picks COM up as a side effect of other things -- the common
		// file dialog initialises OLE, so a build started through
		// "Build Game As..." had it and a build started through "Build Game"
		// might not. An API used outside its documented contract is not a
		// coin toss anybody should be reading results from.
		//
		// RPC_E_CHANGED_MODE means COM is already up in the other apartment
		// model, which is fine to call through; it is the one case that must
		// *not* be balanced with CoUninitialize, since this call did not
		// initialise anything.
		const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED
												  | COINIT_DISABLE_OLE1DDE);
		const bool balance = SUCCEEDED(com);

		HINSTANCE result;
		if (directory)
		{
			result = ShellExecuteW(nullptr, L"open", native.c_str(),
								   nullptr, nullptr, SW_SHOWNORMAL);
		}
		else
		{
			const std::wstring select = L"/select,\"" + native + L"\"";
			result = ShellExecuteW(nullptr, L"open", L"explorer.exe",
								   select.c_str(), nullptr, SW_SHOWNORMAL);
		}

		if (balance)
			CoUninitialize();

		// ShellExecute returns a fake HINSTANCE; anything over 32 is success.
		// The cast is what the API documents, odd as it looks.
		if ((INT_PTR)result <= 32)
		{
			RV_CORE_WARN("Could not open {0} in the file manager: ShellExecute "
						 "returned {1}, CoInitializeEx returned 0x{2:08X}",
						 path.string(), (INT_PTR)result, (uint32_t)com);
			return false;
		}
		return true;
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
