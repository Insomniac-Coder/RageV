#pragma once
#include "Core.h"
#include "Window.h"
#include "RageV/Events/ApplicationEvent.h"
#include "LayerStack.h"
#include "RageV/Core/Platform.h"
#include "RageV/Core/GraphicsInformation.h"
#include "RageV/ImGui/ImGuiLayer.h"
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "FixedStep.h"


namespace RageV {
	class RV_API Application
	{
	public:
		// [0, 1): how far the frame being rendered sits between the last
		// completed simulation step and the next. Anything simulated should
		// draw at lerp(previous, current, alpha) or it will stutter.
		static float GetInterpolationAlpha();
		static float GetFixedTimestep();
		// The rate actually in use: the project's, unless --fixed-hz overrode it.
		static uint32_t GetFixedHz();
		// Seconds since startup. Scripts want this without reaching for the
		// platform layer.
		static float GetElapsedTime();

		Application(const std::string& appname);
		virtual ~Application();
		void Run();
		void OnEvent(Event& e);
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
		void Close() { m_Running = false; }

		// Writes RGBA8 pixels, top row first, as a PNG. Public because the
		// screenshot flag is not the only reason to want one.
		static bool WriteScreenshot(const std::string& path, const uint8_t* rgba,
									uint32_t width, uint32_t height);
		void PushOverlay(Layer* layer);
		void PushLayer(Layer* layer);
		inline Window& GetWindow() { return *m_Window; }
		inline static Application& Get() { return *m_Instance; }
		// Get() dereferences unconditionally, so anything that may run without
		// an application -- a headless tool, or code during construction --
		// has to ask first.
		inline static bool Exists() { return m_Instance != nullptr; }
		ImGuiLayer* GetImGuiLayer() const { return m_ImGuiLayer; }
		RHI::RHIDevice& GetDevice() { return *m_Device; }
	private:
		std::unique_ptr<Window> m_Window;
		// Declared after the window: the device is built from it and must be
		// destroyed before it.
		RHI::Scope<RHI::RHIDevice> m_Device;
		ImGuiLayer* m_ImGuiLayer;
		bool m_Running = true;
		bool m_Minimised = false;
		LayerStack m_LayerStack;
		static Application* m_Instance;
		float m_LastTime = 0.0f;

		// Real time not yet spent in fixed steps, plus where the renderer sits
		// between the last completed step and the next one.
		FixedStep m_FixedStep;
		std::unique_ptr<Platform> m_Platform;
		std::function<double()> GetTime;

	};

	Application* CreateApplication();
}