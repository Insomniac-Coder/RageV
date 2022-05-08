#pragma once
#include "Core.h"
#include "RageV/Window.h"
#include "Events/ApplicationEvent.h"
#include "LayerStack.h"

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
			bool m_Running = true;
			LayerStack m_LayerStack;
			static Application* m_Instance;
	};

	Application* CreateApplication();
}