#pragma once
#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include <RageV/Window.h>

namespace RageV
{

	class WindowsWindow : public Window
	{
	public:
		WindowsWindow(const WindowProps& props);
		virtual ~WindowsWindow();

		void OnUpdate() override;

		inline unsigned int GetWidth() const override { return m_Data.Width; }
		inline unsigned int GetHeight() const override { return m_Data.Height; }

		void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVsync(bool enabled) override;
		bool IsVSync() const override;
		inline void* GetNativeWindow() const override { return m_Window; }

	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown();

		GLFWwindow* m_Window;

		struct WindowData {
			unsigned int Height;
			unsigned int Width;
			EventCallbackFn EventCallback;
			std::string Title;
			bool VSync;
		};

		WindowData m_Data;
	};

}