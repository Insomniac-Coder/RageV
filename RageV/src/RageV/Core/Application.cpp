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
#include "RageV/Core/FrameProfiler.h"
#include "RageV/Renderer/Renderer2D.h"
#include "Timestep.h"
#include "RageV/ImGui/LoadingScreen.h"
#include "Platform/Windows/WindowsPlatform.h"

#include <stb_write_image.h>
#include <filesystem>
#include <thread>

namespace RageV {


#define RV_BIND_FUNCTION(x) std::bind(&x, this, std::placeholders::_1)

	Application* Application::m_Instance = nullptr;

	// See the declarations: these are calls rather than inline reads of
	// m_Instance so that a module outside the engine DLL sees the one instance.
	Application& Application::Get() { return *m_Instance; }
	bool Application::Exists() { return m_Instance != nullptr; }

	Application::Application(const std::string& appname) {
		RV_CORE_ASSERT(!m_Instance, "Application instance already present present");
		m_Instance = this;
		m_Name = appname;

		const EngineConfig& config = EngineConfig::Get();

		// Created hidden, and shown in Run() once the renderer can put a
		// loading screen in it.
		//
		// Everything below -- device, shaders, every pipeline -- is over a
		// second, and nothing pumps a message loop during it. A window that
		// exists through that is a white rectangle the compositor greys out
		// and labels "Not Responding", which is the startup complaint this
		// whole path exists to answer. No window at all for that second is
		// simply a program starting.
		WindowProps props(appname, config.WindowWidth, config.WindowHeight);
		props.Visible = false;

		m_Window = std::unique_ptr<Window>(Window::Create(props));
		m_Window->SetEventCallback(RV_BIND_FUNCTION(Application::OnEvent));

		RHI::DeviceDesc deviceDesc;
		deviceDesc.Backend = config.Backend;
		deviceDesc.Window = m_Window->GetNativeWindow();
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
			Assets::Registry::Init(Project::AssetRoot());

		Assets::Manager::Init(*m_Device);

		// After the registry, because a clip is resolved through it. Never
		// fatal: a machine with no output device still runs the editor.
		Audio::Engine::Init(config.EnableAudio ? AudioMode::Device : AudioMode::Silent);

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
		// Idle before anything is destroyed.
		//
		// The loop can exit with frames still executing -- --benchmark and
		// --screenshot both stop it in the same iteration that submitted one,
		// and a window close does the same -- and the layers below destroy the
		// buffers, samplers, descriptor sets and pipelines those frames are
		// still reading. Seven validation errors on every close, all of them
		// "currently in use by VkCommandBuffer".
		if (m_Device)
			m_Device->WaitIdle();

		// Layers hold GPU resources, so they must be torn down while the device
		// is still alive.
		m_LayerStack.Clear();

		// After the layers -- no script instance survives them -- and
		// deliberately before process teardown gets a chance. At exit the game
		// module DLL unmaps before the engine (reverse dependency order), and
		// the registry's static maps would then destroy std::functions whose
		// code lived in it: a crash on the way out, in destructors, with no
		// useful stack. Closing the project unloads the module while
		// everything it needs is still mapped.
		Project::Close();

		// After the layers, so any scene still playing has already stopped what
		// it started, and before anything else, because a sound outliving the
		// mixer is a use-after-free on the audio thread.
		Audio::Engine::Shutdown();

		InputMap::Shutdown();
		Assets::Manager::Shutdown();
		Assets::Registry::Shutdown();

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

	bool Application::WriteScreenshot(const std::string& path, const uint8_t* rgba,
									  uint32_t width, uint32_t height)
	{
		if (!rgba || width == 0 || height == 0)
			return false;

		// So --screenshot=shots/frame.png works without the caller having had to
		// create the folder first.
		std::error_code error;
		const std::filesystem::path file(path);
		if (file.has_parent_path())
			std::filesystem::create_directories(file.parent_path(), error);

		if (stbi_write_png(path.c_str(), (int)width, (int)height, 4, rgba, (int)width * 4) == 0)
		{
			RV_CORE_ERROR("Could not write {0}", path);
			return false;
		}

		RV_CORE_INFO("Wrote {0} ({1}x{2})", path, width, height);
		return true;
	}

	// One frame whose only content is the loading screen.
	//
	// Through BeginFrame/EndFrame like any other, rather than a private
	// present path: a second way to get a picture onto the swapchain is a
	// second thing to keep working on two backends, and this one would only
	// ever be exercised for the couple of seconds nobody is looking closely.
	void Application::DrawLoadingFrame(const Boot::Status& status)
	{
		if (m_Minimised)
			return;

		RHI::RHICommandList* cmd = m_Device->BeginFrame();
		if (!cmd)
			return;

		Renderer::BeginFrame(cmd);

		m_ImGuiLayer->Begin();
		LoadingScreen::Draw(GetLoadingTitle(), status);
		m_ImGuiLayer->End();

		Renderer::EndFrame();
		m_Device->EndFrame();
	}

	bool Application::RunBootPhase()
	{
		Boot::Progress progress;
		const double started = GetTime();

		// The window appears here: the first thing drawn in it is the bar.
		m_Window->Show();

		std::thread worker([this, &progress]()
		{
			for (Layer* layer : m_LayerStack)
			{
				if (progress.Cancelled())
					break;

				layer->OnLoad(progress);
			}

			progress.Finish();
		});

		uint32_t bootFrame = 0;
		bool captured = false;
		std::string loadingShot = EngineConfig::Get().LoadingScreenshotPath;

		while (!progress.IsDone() && m_Running)
		{
			m_Window->OnUpdate();

			// Counted here, unconditionally, and not inside the capture test
			// below -- where it started, behind a `--loading-screenshot`
			// short-circuit that made every ordinary run report zero frames
			// and look exactly like a boot loop that never spun.
			bootFrame++;

			const Boot::Status status = progress.Get();

			// Captured on the first frame that has something on every line.
			//
			// Not simply "frame 2": ImGui has no display size until it has
			// drawn once, and the frames right after that land in the gap
			// where a phase has begun and named nothing yet -- so a fixed
			// frame number reliably photographed a screen with its detail
			// line empty, which is the half most worth checking. Frame 2 is
			// the fallback for a project that loads no assets at all, so the
			// flag always produces a file.
			if (!loadingShot.empty() && !captured && bootFrame >= 2 &&
				(!status.Detail.empty() || bootFrame > 8))
			{
				captured = true;
				const std::string path = std::move(loadingShot);
				m_Device->RequestCapture([path](const uint8_t* rgba, uint32_t w, uint32_t h)
				{
					WriteScreenshot(path, rgba, w, h);
				});
			}

			DrawLoadingFrame(status);
		}

		// A close during loading. Told to stop, then waited for: the worker
		// holds references to members that are about to be destroyed, so
		// there is no version of this that skips the join.
		if (!m_Running)
			progress.Cancel();

		worker.join();

		if (!m_Running)
		{
			RV_CORE_INFO("Loading cancelled after {0:.2f}s", GetTime() - started);
			return false;
		}

		// The second half, on this thread, where the device may be touched.
		// Still inside the loading screen: the bar keeps moving and the
		// window keeps pumping while the uploads happen.
		//
		// A twelfth of a second per slice. Long enough that the frames
		// between slices are not most of the cost, short enough that the
		// window still answers -- and the *reason* it is a budget rather
		// than an asset count is that assets differ by a factor of sixty.
		constexpr float kUploadBudgetSeconds = 1.0f / 12.0f;

		for (bool more = true; more && m_Running; )
		{
			more = false;
			for (Layer* layer : m_LayerStack)
				more |= layer->OnLoadStep(progress);

			m_Window->OnUpdate();
			bootFrame++;
			DrawLoadingFrame(progress.Get());
		}

		if (!m_Running)
			return false;

		for (Layer* layer : m_LayerStack)
			layer->OnLoaded();

		// The frame count is the point, not decoration. "Loading took four
		// seconds" says nothing about whether the window was usable during
		// them; "four seconds, 243 frames" says the message loop ran
		// throughout, which is the entire difference between this and the
		// frozen white rectangle it replaced. A boot reporting single-digit
		// frames means something is blocking the main thread again.
		RV_CORE_INFO("Loaded in {0:.2f}s ({1} frames drawn while loading)",
					 GetTime() - started, bootFrame);

		return m_Running;
	}

	void Application::Run() {

		m_FixedStep.Timestep = 1.0f / (float)GetFixedHz();

		// Before the first frame and before the clock starts: loading is not
		// a frame that took four seconds, and letting it become one would
		// hand the fixed step four seconds of accumulated time to spend in
		// one burst of physics.
		if (!RunBootPhase())
			return;

		m_LastTime = (float)GetTime();
		m_FixedStep.Reset();

		const EngineConfig& config = EngineConfig::Get();

		// Frame one is the first frame of the loop, not the first frame of the
		// process. Loading drew some too, and how many depends on whether the
		// import cache was warm -- so counting those would make
		// --screenshot-frame=30 mean a different frame on a cold run, and
		// would move the temporal jitter with it. The fixed step is reset
		// beside this for the same reason: the clock starts here.
		Renderer::ResetFrameCount();

		// Warm-up, discarded: the first frames pay for shader compilation, the
		// first environment prefilter and every first-touch allocation, and
		// averaging those in makes a run look worse the shorter it is. A fifth
		// of the run, and never fewer than ten frames.
		const uint32_t benchmarkFrames = config.BenchmarkFrames;
		const uint32_t benchmarkWarmup = benchmarkFrames > 0
			? std::max(10u, benchmarkFrames / 5u) : 0u;

		while (m_Running) {
			const float time = (float)GetTime();
			// A fixed frame time when one was asked for, so a capture is
			// reproducible. Everything downstream of this -- particles,
			// OnFrame, the interpolation alpha -- becomes a function of the
			// frame *number* rather than of how busy this machine was.
			const float frameTime = config.FrameTime > 0.0f
								  ? config.FrameTime
								  : time - m_LastTime;
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

			FrameProfiler::BeginFrame();

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

			// The device recycled a query pool inside BeginFrame, so this is
			// where a couple of frames' worth of GPU results become readable.
			FrameProfiler::CollectGpu();

			// The whole frame's GPU span, either side of everything recorded.
			cmd->WriteTimestamp(kWholeFrameBeginSlot);

			const Timestep ts = frameTime;
			for (Layer* layer : m_LayerStack)
				layer->OnUpdate(ts);

			{
				RV_PROFILE_PHASE(FramePhase::ImGui);

				m_ImGuiLayer->Begin();

				for (Layer* layer : m_LayerStack)
					layer->OnImGuiRender();

				m_ImGuiLayer->End();
			}

			// Retires sounds that have played out. Once a frame, unconditional:
			// a one-shot fired from a script belongs to nothing that would
			// otherwise clean it up.
			Audio::Engine::Update();

			// Armed before EndFrame, which is the call that consumes it.
			//
			// Against the renderer's count rather than a local one, so "frame
			// 30" means the same frame to the screenshot and to the temporal
			// jitter. Two counters that were meant to agree would be one more
			// thing that can drift.
			const uint64_t frameNumber = Renderer::GetFrameCount();
			if (!config.ScreenshotPath.empty() && frameNumber == config.ScreenshotFrame)
			{
				const std::string path = config.ScreenshotPath;
				m_Device->RequestCapture([path](const uint8_t* rgba, uint32_t w, uint32_t h)
				{
					WriteScreenshot(path, rgba, w, h);
				});
			}

			cmd->WriteTimestamp(kWholeFrameEndSlot);

			{
				RV_PROFILE_PHASE(FramePhase::Present);

				Renderer::EndFrame();
				m_Device->EndFrame();
			}

			// After the capture, not before: the point of the flag is the file.
			if (!config.ScreenshotPath.empty() && frameNumber >= config.ScreenshotFrame)
				m_Running = false;

			if (benchmarkFrames > 0)
			{
				// Counted here rather than beside the screenshot's counter,
				// because that one only advances when a screenshot is armed.
				m_BenchmarkFrame++;

				if (m_BenchmarkFrame == benchmarkWarmup)
					FrameProfiler::StartCollecting();
			}

			// Every frame, not only under --benchmark.
			//
			// This also updates the rolling averages the editor's Statistics
			// panel reads, and having it inside the benchmark branch meant that
			// panel showed 0.000 for every phase in normal use -- a profiler
			// that only works when nobody is looking at it.
			FrameProfiler::EndFrame(frameTime * 1000.0f);

			if (benchmarkFrames > 0 && m_BenchmarkFrame >= benchmarkWarmup + benchmarkFrames)
			{
				FrameProfiler::LogReport(m_Name.c_str());
				m_Running = false;
			}
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e) {
		m_Running = false;

		return true;
	}
}
