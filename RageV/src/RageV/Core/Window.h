#pragma once
#include <rvpch.h>
#include "RageV/Events/Event.h"
#include "Core.h"
#include "RageV/Core/GraphicsInformation.h"

namespace RageV {
	
	struct WindowProps
	{
		std::string Title;
		unsigned int Width;
		unsigned int Height;

		// A window nobody is meant to look at, for a test or a tool that needs a
		// device rather than a display. It exists because the alternative --
		// creating the window with GLFW directly -- means a second copy of GLFW
		// in the calling executable, and GLFW keeps its state in globals: the
		// engine then cannot see the window at all, and reports it as a null
		// HWND or a context it cannot load functions from.
		bool Visible = true;

		WindowProps(const std::string& title = "RageV Engine", unsigned int width = 1600, unsigned int height = 900)
			: Title(title), Width(width), Height(height) {}
	};

	class RV_API Window {
	public:
		using EventCallbackFn = std::function<void(Event&)>;

		virtual ~Window() {}

		virtual void OnUpdate() = 0;

		virtual unsigned int GetWidth() const = 0;
		virtual unsigned int GetHeight() const = 0;

		virtual void SetEventCallback(const EventCallbackFn& callback) = 0;
		virtual void SetVsync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual void* GetNativeWindow() const = 0;
		virtual GraphicsInfo GetGraphicsInfo() const = 0;

		static Window* Create(const WindowProps& props = WindowProps());
	};
}