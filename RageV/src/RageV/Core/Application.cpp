#include <rvpch.h>
#include "Application.h"
#include "Log.h"
#include "Input.h"
#include "EngineConfig.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/Renderer2D.h"
#include "Timestep.h"
#include "Platform/Windows/WindowsPlatform.h"

#include <GLFW/glfw3.h>

namespace RageV {
#define RV_BIND_FUNCTION(x) std::bind(&x, this, std::placeholders::_1)

	Application* Application::m_Instance = nullptr;

	Application::Application(const std::string& appname) {
		RV_CORE_ASSERT(!m_Instance, "Application instance already present present");
		m_Instance = this;

		const EngineConfig& config = EngineConfig::Get();

		m_Window = std::unique_ptr<Window>(Window::Create(WindowProps(appname)));
		m_Window->SetEventCallback(RV_BIND_FUNCTION(Application::OnEvent));

		RHI::DeviceDesc deviceDesc;
		deviceDesc.Backend = config.Backend;
		deviceDesc.Window = static_cast<GLFWwindow*>(m_Window->GetNativeWindow());
		deviceDesc.Width = m_Window->GetWidth();
		deviceDesc.Height = m_Window->GetHeight();
		deviceDesc.VSync = config.VSync;
		deviceDesc.EnableValidation = config.EnableValidation;
		deviceDesc.FramesInFlight = config.FramesInFlight;

		m_Device = RHI::RHIDevice::Create(deviceDesc);
		if (!m_Device)
		{
			RV_CORE_ERROR("Could not create a {0} device; the application cannot start",
						  EngineConfig::BackendName(config.Backend));
			m_Running = false;
			return;
		}

		const auto& caps = m_Device->GetCaps();
		GraphicsInformation::SetGraphicsInfo({ caps.DeviceName, caps.APIName });

		Renderer::Init(*m_Device);

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
		// Layers hold GPU resources, so they must be torn down while the device
		// is still alive.
		m_LayerStack.Clear();

		if (m_Device)
		{
			m_Device->WaitIdle();
			Renderer::Shutdown();
		}
		m_Device.reset();
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
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(RV_BIND_FUNCTION(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(RV_BIND_FUNCTION(Application::OnWindowResize));

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin();)
		{
			(*--it)->OnEvent(e);

			if (e.m_Handled)
				break;
		}
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		if (e.GetHeight() == 0 || e.GetWidth() == 0)
		{
			m_Minimised = true;
			return false;
		}
		m_Minimised = false;
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
		return false;
	}

	void Application::Run() {

		while (m_Running) {
			float time = (float)GetTime();
			Timestep ts = time - m_LastTime;
			m_LastTime = time;

			m_Window->OnUpdate();

			if (m_Minimised)
				continue;

			// Null means the frame must be skipped -- a resized or minimised
			// window. EndFrame must not be called in that case.
			RHI::RHICommandList* cmd = m_Device->BeginFrame();
			if (!cmd)
				continue;

			Renderer::BeginFrame(cmd);

			for (Layer* layer : m_LayerStack)
				layer->OnUpdate(ts);

			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
				layer->OnImGuiRender();

			m_ImGuiLayer->End();

			Renderer::EndFrame();
			m_Device->EndFrame();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;

		return true;
	}
}
