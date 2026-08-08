#pragma once
#include "Core.h"
#include "Window.h"
#include "RageV/Events/ApplicationEvent.h"
#include "LayerStack.h"
#include "RageV/Core/Platform.h"
#include "RageV/Core/GraphicsInformation.h"
#include "RageV/ImGui/ImGuiLayer.h"
#include "RageV/Renderer/RHI/RHIDevice.h"


namespace RageV {
	class RV_API Application
	{
	public:
		Application(const std::string& appname);
		virtual ~Application();
		void Run();
		void OnEvent(Event& e);
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);
		void Close() { m_Running = false; }
		void PushOverlay(Layer* layer);
		void PushLayer(Layer* layer);
		inline Window& GetWindow() { return *m_Window; }
		inline static Application& Get() { return *m_Instance; }
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
		std::unique_ptr<Platform> m_Platform;
		std::function<double()> GetTime;

	};

	Application* CreateApplication();
}