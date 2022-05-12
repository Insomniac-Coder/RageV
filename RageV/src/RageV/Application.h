#pragma once
#include "Core.h"
#include "RageV/Window.h"
#include "Events/ApplicationEvent.h"
#include "LayerStack.h"
#include "RageV/Core/Platform.h"

#include "RageV/ImGui/ImGuiLayer.h"

namespace RageV {
	class RV_API Application
	{
	public:
		Application();
		virtual ~Application();
		void Run();
		void OnEvent(Event& e);
		bool OnWindowClose(WindowCloseEvent& e);

		void PushOverlay(Layer* layer);
		void PushLayer(Layer* layer);
		inline Window& GetWindow() { return *m_Window; }
		inline static Application& Get() { return *m_Instance; }
	private:
			std::unique_ptr<Window> m_Window;
			ImGuiLayer* m_ImGuiLayer;
			bool m_Running = true;
			LayerStack m_LayerStack;
			static Application* m_Instance;
			float m_LastTime = 0.0f;
			std::unique_ptr<Platform> m_Platform;
			std::function<double()> GetTime;

	};

	Application* CreateApplication();
}