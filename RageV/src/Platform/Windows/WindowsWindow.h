#pragma once
#include "vulkan.h"
#include "GLFW/glfw3.h"
#include <RageV/Core/Window.h>
#include "RageV/Renderer/GraphicsContext.h"

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
		virtual GraphicsInfo GetGraphicsInfo() const override { return m_GraphicsInfo; }

	private:
		virtual void Init(const WindowProps& props);
		virtual void Shutdown();
		

		GLFWwindow* m_Window;

		GraphicsContext* m_Context;

		struct WindowData {
			unsigned int Height;
			unsigned int Width;
			EventCallbackFn EventCallback;
			std::string Title;
			bool VSync;
		};

		GraphicsInfo m_GraphicsInfo;

		WindowData m_Data;
	};

}