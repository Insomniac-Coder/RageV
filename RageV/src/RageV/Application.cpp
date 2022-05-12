#include <rvpch.h>
#include "Application.h"
#include "RageV/Log.h"
#include "Input.h"
#include "Renderer/Renderer.h"
#include "Core/Timestep.h"
#include "Platform/Windows/WindowsPlatform.h"

namespace RageV {
#define RV_BIND_FUNCTION(x) std::bind(&x, this, std::placeholders::_1)

	Application* Application::m_Instance = nullptr;

	Application::Application() {
		RV_CORE_ASSERT(!m_Instance, "Application instance already present present");
		m_Instance = this;
		m_Window = std::unique_ptr<Window>(Window::Create());
		m_Window->SetEventCallback(RV_BIND_FUNCTION(Application::OnEvent));
		m_Window->SetVsync(false);

		if (Platform::GetPlatformType() == PlatformType::Windows)
		{
			m_Platform.reset(new WindowsPlatform);
		}
		else
		{
			RV_CORE_ASSERT(false, "Currently only Windows platform is supported!");
		}

		GetTime = m_Platform->GetTimeFn();

		m_ImGuiLayer = new ImGuiLayer();
 		PushOverlay(m_ImGuiLayer);
	}

	Application::~Application() {

	}

	void  Application::PushOverlay(Layer* layer)
	{
		m_LayerStack.PushOverlay(layer);
		layer->OnAttach();
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::OnEvent(Event& e) {
		//RV_CORE_TRACE("{0}", e);

		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(RV_BIND_FUNCTION(Application::OnWindowClose));

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);

			if (e.m_Handled)
				break;
		}

	}

	void Application::Run() {

		//WindowResizeEvent e(1280, 720);
		//RV_TRACE(e);
		while (m_Running) {

			float time = GetTime();
			Timestep ts = time - m_LastTime;
			m_LastTime = time;

			m_Window->OnUpdate();

			for (Layer* layer : m_LayerStack)
				layer->OnUpdate(ts);
			//auto [x, y] = Input::GetMousePosition();
			//RV_CORE_TRACE("{0}, {1}", x, y); 
			m_ImGuiLayer->Begin();
			
			for (Layer* layer : m_LayerStack)
				layer->OnImGuiRender();
			
			m_ImGuiLayer->End();

		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;

		return true;
	}
}