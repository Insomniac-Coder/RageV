#include <rvpch.h>
#include "Application.h"
#include "Log.h"
#include "Input.h"
#include "EngineConfig.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Project/Project.h"
#include "RageV/Core/InputMap.h"
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

		m_Window = std::unique_ptr<Window>(Window::Create(
			WindowProps(appname, config.WindowWidth, config.WindowHeight)));
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

		// The registry scans for source files and mints handles; the manager
		// turns those handles into GPU resources. Both before any layer runs,
		// since a layer's OnAttach may already want to load something.
		InputMap::Init();

		// The project decides where assets live, so it is opened first. Without
		// one the registry has no root and every handle resolves to nothing,
		// which the editor reports rather than papering over.
		//
		// Only if none is open: an application may need the project before its
		// base constructor runs -- the runtime titles its window after it --
		// and re-reading the file would be wasted work, not a correction.
		if (!Project::GetActive())
			Project::OpenConfigured();

		if (Project::GetActive())
			AssetRegistry::Init(Project::AssetRoot());

		AssetManager::Init(*m_Device);

		// After the registry, because a clip is resolved through it. Never
		// fatal: a machine with no output device still runs the editor.
		AudioEngine::Init(config.EnableAudio ? AudioMode::Device : AudioMode::Silent);

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

		// After the layers, so any scene still playing has already stopped what
		// it started, and before anything else, because a sound outliving the
		// mixer is a use-after-free on the audio thread.
		AudioEngine::Shutdown();

		InputMap::Shutdown();
		AssetManager::Shutdown();
		AssetRegistry::Shutdown();

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

		// Fed rather than sampled: a wheel has no queryable state, so it is the
		// one input that has to arrive as an event. Not marked handled -- the
		// editor camera and ImGui both want it too.
		dispatcher.Dispatch<MouseScrolledEvent>([](MouseScrolledEvent& scrolled)
			{
				InputMap::OnScroll(scrolled.GetYOffset());
				return false;
			});

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

	// Fixed-step simulation, variable-rate rendering.
	//
	// Simulation cannot run on the frame's elapsed time: integrator behaviour
	// depends on dt, so the same scene would settle at 300 fps and explode at
	// 40. Rendering cannot run on the fixed step either, or the frame rate is
	// pinned to it. So: accumulate real time, spend it in fixed steps, and hand
	// the renderer how far through the next step it is.
	//
	// See docs/ENGINE-NOTES.md section 1.
	float Application::GetInterpolationAlpha()
	{
		return m_Instance ? m_Instance->m_FixedStep.Alpha : 0.0f;
	}

	float Application::GetElapsedTime()
	{
		return (m_Instance && m_Instance->GetTime) ? (float)m_Instance->GetTime() : 0.0f;
	}

	uint32_t Application::GetFixedHz()
	{
		// The project wins over the default, because the rate a game was tuned
		// at travels with the game. An explicit --fixed-hz still overrides both:
		// it is a diagnostic, and being able to run a game at 240 Hz to see what
		// breaks is the reason it exists.
		const EngineConfig& config = EngineConfig::Get();
		if (config.FixedHzExplicit)
			return config.FixedHz;

		if (Project::GetActive())
			return Project::Config().FixedHz;

		return config.FixedHz;
	}

	float Application::GetFixedTimestep()
	{
		return 1.0f / (float)GetFixedHz();
	}

	void Application::Run() {

		m_FixedStep.Timestep = 1.0f / (float)GetFixedHz();

		while (m_Running) {
			const float time = (float)GetTime();
			const float frameTime = time - m_LastTime;
			m_LastTime = time;

			m_Window->OnUpdate();

			if (m_Minimised)
			{
				// Nothing is being shown, so nothing accumulates. Otherwise
				// restoring the window would spend the whole minimised
				// duration in one burst of steps.
				m_FixedStep.Reset();
				continue;
			}

			// Sampled once per frame, not per step: several steps in one frame
			// must see one press, not one each.
			InputMap::Update();

			const int steps = m_FixedStep.Advance(frameTime);
			for (int i = 0; i < steps; i++)
			{
				for (Layer* layer : m_LayerStack)
					layer->OnFixedUpdate(m_FixedStep.Timestep);

				// Edges are consumed by the first step that runs; a frame with
				// no steps carries them forward rather than losing them.
				InputMap::EndFixedStep();
			}

			// Null means the frame must be skipped -- a resized or minimised
			// window. EndFrame must not be called in that case.
			RHI::RHICommandList* cmd = m_Device->BeginFrame();
			if (!cmd)
				continue;

			Renderer::BeginFrame(cmd);

			const Timestep ts = frameTime;
			for (Layer* layer : m_LayerStack)
				layer->OnUpdate(ts);

			m_ImGuiLayer->Begin();

			for (Layer* layer : m_LayerStack)
				layer->OnImGuiRender();

			m_ImGuiLayer->End();

			// Retires sounds that have played out. Once a frame, unconditional:
			// a one-shot fired from a script belongs to nothing that would
			// otherwise clean it up.
			AudioEngine::Update();

			Renderer::EndFrame();
			m_Device->EndFrame();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;

		return true;
	}
}
