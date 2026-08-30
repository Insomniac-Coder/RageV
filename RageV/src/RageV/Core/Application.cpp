#include <rvpch.h>
#include "Application.h"
#include "Log.h"
#include "Input.h"
#include "EngineConfig.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Audio/AudioEngine.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/ScriptGraphGenerator.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Project/Project.h"
#include "RageV/Core/InputMap.h"
#include "RageV/Core/FrameClock.h"
#include "RageV/Core/FrameProfiler.h"
#include "RageV/Renderer/Renderer2D.h"
#include "RageV/Renderer/GpuCull.h"
#include "Timestep.h"
#include "RageV/ImGui/LoadingScreen.h"
#include "RageV/ImGui/StartupScreen.h"
#include "RageV/Project/ProjectTemplate.h"
#include "RageV/Utils/PlatformUtils.h"
#include "Platform/Windows/WindowsPlatform.h"

#include <stb_write_image.h>
#include <filesystem>
#include <thread>

namespace
{
	// **The phases, printed beside the passes.** A graph pass is only part of a
	// frame: probe capture, shadow rendering, the swapchain wait and the present
	// all happen outside it. A capture on 2026-08-28 had slow frames whose named
	// passes summed to 20 ms out of 56, and one 313 ms frame with no pass time at
	// all -- so the thing being hunted was in a phase the log did not print.
	void LogFrameBreakdown(bool warn)
	{
		struct Row { const char* Name; RageV::FramePhase Phase; };
		static const Row kPhases[] = {
			{ "wait (gpu/vsync)", RageV::FramePhase::Wait },
			{ "environment",      RageV::FramePhase::EnvironmentPrefilter },
			{ "shadows",          RageV::FramePhase::Shadows },
			{ "probes",           RageV::FramePhase::Probes },
			{ "graph",            RageV::FramePhase::Graph },
			{ "imgui",            RageV::FramePhase::ImGui },
			{ "present",          RageV::FramePhase::Present },
		};

		for (const Row& row : kPhases)
		{
			const float cpu = RageV::FrameProfiler::LivePhaseMs(row.Phase);
			const float gpu = RageV::FrameProfiler::LiveGpuPhaseMs(row.Phase);
			// Silent when a phase did nothing, so the interesting rows are not
			// buried under five zeroes every time.
			if (cpu < 0.05f && gpu < 0.05f)
				continue;
			if (warn) RV_CORE_WARN("    [phase] {0}  cpu {1} ms  gpu {2} ms", row.Name, cpu, gpu);
			else      RV_CORE_INFO("    [phase] {0}  cpu {1} ms  gpu {2} ms", row.Name, cpu, gpu);
		}

		auto passes = RageV::FrameProfiler::PassTimings();
		std::sort(passes.begin(), passes.end(),
				  [](const auto& a, const auto& b) { return a.GpuMs > b.GpuMs; });
		const size_t show = passes.size() < 6 ? passes.size() : 6;
		for (size_t i = 0; i < show; i++)
		{
			if (warn) RV_CORE_WARN("    {0}  cpu {1} ms  gpu {2} ms",
								   passes[i].Name, passes[i].CpuMs, passes[i].GpuMs);
			else      RV_CORE_INFO("    {0}  cpu {1} ms  gpu {2} ms",
								   passes[i].Name, passes[i].CpuMs, passes[i].GpuMs);
		}
	}
}

namespace RageV {


#define RV_BIND_FUNCTION(x) std::bind(&x, this, std::placeholders::_1)

	Application* Application::m_Instance = nullptr;

	// See the declarations: these are calls rather than inline reads of
	// m_Instance so that a module outside the engine DLL sees the one instance.
	Application& Application::Get() { return *m_Instance; }
	bool Application::Exists() { return m_Instance != nullptr; }

	Application::Application(const std::string& appname, bool choosesProject) {
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
		deviceDesc.GpuAssistedValidation = config.ValidationGpuAssisted;
		deviceDesc.FramesInFlight = config.FramesInFlight;

		// **The chosen backend first, then whatever else the build allows.**
		//
		// A packaged game's `ragev.ini` names every backend it was built for,
		// and this is the whole of what that means: `RHIDevice::Create` returns
		// null rather than falling back, so without this a game built for both
		// would still die on a machine whose Vulkan driver is missing. Nothing
		// but a package sets the list, so the editor and every command line
		// still fail on the backend they asked for -- a developer who says
		// `--api=vulkan` wants to hear that Vulkan failed, not to be moved
		// quietly to OpenGL and left measuring the wrong renderer.
		std::vector<RHI::Backend> order{ config.Backend };
		for (RHI::Backend backend : config.Backends)
		{
			if (std::find(order.begin(), order.end(), backend) == order.end())
				order.push_back(backend);
		}

		for (RHI::Backend backend : order)
		{
			deviceDesc.Backend = backend;
			m_Device = RHI::RHIDevice::Create(deviceDesc);
			if (m_Device)
			{
				if (backend != config.Backend)
				{
					// Said, not written back: EngineConfig is read-only once
					// parsed, and anything that needs the backend actually in
					// use has the device to ask -- GetCaps().APIName is what
					// the title bar and the statistics panel already read.
					RV_CORE_WARN("No {0} device; running on {1} instead, which this "
								 "build also supports",
								 EngineConfig::BackendName(config.Backend),
								 EngineConfig::BackendName(backend));
				}
				break;
			}
		}

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
		// one the registry has no root and every handle resolves to nothing.
		//
		// Only if none is open: an application may need the project before its
		// base constructor runs -- the runtime titles its window after it --
		// and re-reading the file would be wasted work, not a correction.
		if (!Project::GetActive())
			Project::OpenConfigured();

		if (Project::GetActive())
		{
			AdoptProject();
		}
		else if (choosesProject)
		{
			// **Nothing found, and that is not an error here.** The engine
			// used to finish this line by opening the project CMake baked into
			// it, which made one particular sample the state the editor was
			// always in. An application that can ask instead defers to
			// RunStartupPhase, which happens once there is a window to ask in.
			m_NeedsProject = true;
		}

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

		// Closing is dispatched *after* the layers, and only if none of them
		// objected. It is the one event a layer may need to stop: the editor
		// asks about an unsaved scene before letting the window go.
		//
		// It used to be dispatched first, which made that impossible in a way
		// that looked like the handler never running. `Dispatch` marks the
		// event handled from the callback's return value, and OnWindowClose
		// returns true -- so the loop above broke after the topmost overlay and
		// the editor layer was never asked. Anything a layer wants to veto has
		// to be offered to the layers before it is acted on.
		if (!e.m_Handled)
			dispatcher.Dispatch<WindowCloseEvent>(RV_BIND_FUNCTION(Application::OnWindowClose));
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
		LoadingScreen::Draw(m_LoadingTitle.empty() ? GetLoadingTitle() : m_LoadingTitle,
							status);
		m_ImGuiLayer->End();

		Renderer::EndFrame();
		m_Device->EndFrame();
	}

	// The half of startup that needs a project. Two callers: the constructor,
	// when --project or a .rvproject beside the executable answered; and the
	// startup screen, when a person did.
	void Application::AdoptProject()
	{
		if (!Project::GetActive())
			return;

		Assets::Registry::Init(Project::AssetRoot());

		// --generate-graphs, for both executables (8.10, ENGINE-NOTES 7bh).
		// Here rather than in the editor's layer because a build server and
		// every check in tools/scripts run the *runtime*, and a generator only
		// the editor can drive is one a check cannot.
		if (EngineConfig::Get().GenerateGraphs)
		{
			const bool generated = Assets::ScriptGraphGenerator::GenerateAll(
				Project::AssetRoot(), Project::Root() / "Scripts");
			RV_CORE_INFO("--generate-graphs: {0}",
						 generated ? "every graph generated"
								   : "at least one graph did not generate");
		}
	}

	// --- the startup screen ---------------------------------------------------
	//
	// **Before the loading screen, and it is the reason the engine no longer
	// has a project baked into it.** The sequence is now: window, ask, load,
	// edit. It used to be window, open the sample, edit -- which worked
	// perfectly for the one person whose engine was built from this repository
	// and not at all for anybody else.
	//
	// This runs its own pump-and-present loop for the same reason the boot
	// phase does: there are no layers to drive yet, the window must keep
	// repainting, and a frame here goes through BeginFrame/EndFrame like any
	// other rather than down a private present path.
	bool Application::RunStartupPhase()
	{
		m_Window->Show();

		while (m_Running && !Project::GetActive())
		{
			m_Window->OnUpdate();

			if (m_Minimised)
				continue;

			StartupScreen::Choice choice = StartupScreen::Choice::None;

			if (RHI::RHICommandList* cmd = m_Device->BeginFrame())
			{
				Renderer::BeginFrame(cmd);
				m_ImGuiLayer->Begin();
				choice = StartupScreen::Draw();
				m_ImGuiLayer->End();
				Renderer::EndFrame();
				m_Device->EndFrame();
			}

			// **Acted on after the frame is presented, not during it.** Both
			// branches open a modal file dialog, which blocks this thread for
			// as long as it is up -- inside the frame that would leave a
			// half-recorded command list open across it.
			if (choice == StartupScreen::Choice::OpenProject)
			{
				const std::string picked =
					FileDialogs::OpenFile("RageV Project (*.rvproject)\0*.rvproject\0");

				if (!picked.empty() && !Project::Load(picked))
					RV_CORE_ERROR("Could not open {0}", picked);
			}
			else if (choice == StartupScreen::Choice::CreateProject)
			{
				// A file dialog because the platform layer has no folder one;
				// what gets made is a folder named after the file, which is
				// the same compromise File > New Project already makes.
				const std::string picked =
					FileDialogs::SaveFile("RageV Project (*.rvproject)\0*.rvproject\0");

				if (!picked.empty())
				{
					const std::filesystem::path chosen(picked);
					const std::string name = chosen.stem().string();
					RunCreateProjectPhase(chosen.parent_path() / name, name);
				}
			}
		}

		if (Project::GetActive())
			AdoptProject();

		return Project::GetActive();
	}

	bool Application::RunCreateProjectPhase(const std::filesystem::path& directory,
											const std::string& name)
	{
		Boot::Progress progress;
		std::filesystem::path scene;
		bool created = false;

		// The card names what is being made. Without this it would say "RageV
		// Editor" -- the fallback for having no project -- through the one
		// screen whose whole subject is a project that does not exist yet.
		m_LoadingTitle = name;

		// The same shape as RunBootPhase, and deliberately so: the two screens
		// appear back to back and a person watching cannot tell where one ends
		// -- which is the point. Creating a project and loading one are two
		// halves of the same wait.
		std::thread worker([&]()
		{
			created = ProjectTemplate::Create(directory, name, progress, scene);
			progress.Finish();
		});

		while (!progress.IsDone() && m_Running)
		{
			m_Window->OnUpdate();

			Boot::Status status = progress.Get();
			// The title above the bar names what is being made, where the
			// loading screen names what is being loaded.
			DrawLoadingFrame(status);
		}

		worker.join();
		m_LoadingTitle.clear();

		if (!created)
			RV_CORE_ERROR("Could not create a project at {0}", directory.string());

		return created;
	}

	std::string Application::GetLoadingTitle() const
	{
		if (Project::GetActive() && !Project::Config().Name.empty())
			return Project::Config().Name;

		return m_Name;
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

		// **Nothing was named and this application can ask.** Everything below
		// assumes a project; the startup screen is what produces one. Closing
		// the window there is a legitimate way to leave, so it ends the run
		// rather than falling through into a boot with no assets.
		if (m_NeedsProject && !RunStartupPhase())
			return;

		// The fixed step is re-read here because the project decides it, and
		// until the line above there may not have been one.
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
		// A benchmark that cannot say which pass spent the time is a number
		// without a lead, so it turns this on for itself.
		// An explicit flag wins; otherwise a benchmark implies it. The OR this
		// replaces meant --pass-timings=off was silently ignored under
		// --benchmark, so an un-instrumented frame could not be measured.
		// --slow-frames needs the per-pass numbers it is going to print.
		FrameProfiler::EnablePassTimings(config.SlowFrameMs > 0.0f ? true
									   : config.HasPassTimingsOverride
											 ? config.PassTimings
											 : benchmarkFrames > 0);
		// Before the first frame and after Renderer3D::Init, which is what the
		// flag has to sit between: the pipelines are already built, and
		// switching this off makes IsAvailable answer no so every depth view
		// takes the walk instead.
		GpuCull::SetEnabled(config.GpuCull);
		GpuCull::SetLitEnabled(config.GpuLit);
		const uint32_t benchmarkWarmup = benchmarkFrames > 0
			? Math::Max(10u, benchmarkFrames / 5u) : 0u;

		while (m_Running) {
			const float time = (float)GetTime();
			// A fixed frame time when one was asked for, so a capture is
			// reproducible. Everything downstream of this -- particles,
			// OnFrame, the interpolation alpha -- becomes a function of the
			// frame *number* rather than of how busy this machine was.
			//
			// **Measured and simulated are two different numbers and the
			// profiler must never be handed the simulated one.** Under
			// --frame-time the step is a constant, and reporting it as the
			// frame's duration made the Statistics panel claim a 16.600 ms
			// frame while the phases inside it summed to 26.822 -- parts
			// larger than the whole, on a run where vsync was off. The step
			// paces the simulation; the wall clock is how long the frame took.
			const float measured = time - m_LastTime;
			const float frameTime = config.FrameTime > 0.0f
								  ? config.FrameTime
								  : measured;
			m_LastTime = time;
			// The span just closed, which is the previous frame's -- the
			// current one is not over yet. Filed against this frame's phases,
			// off by one frame, which is what any single-clock loop can do.
			m_MeasuredFrameMs = measured * 1000.0f;

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

			// Before input is sampled, because this is the number the frame's
			// edges are stamped with.
			FrameClock::BeginFrame();

			// Sampled once per frame, not per step: several steps in one frame
			// must see one press, not one each.
			InputMap::Update();

			const int steps = m_FixedStep.Advance(frameTime);
			for (int i = 0; i < steps; i++)
			{
				// The engine's step, and the authoritative one -- opened here
				// rather than inside the scene so that it advances even when
				// the scene is paused, which is what keeps a press made during
				// a pause from firing the moment play resumes.
				FrameClock::StepScope step;

				for (Layer* layer : m_LayerStack)
					layer->OnFixedUpdate(m_FixedStep.Timestep);
			}

			// Null means the frame must be skipped -- a resized or minimised
			// window. EndFrame must not be called in that case.
			//
			// Wrapped as its own phase because this is where Vulkan blocks
			// under vsync -- the in-flight fence and the swapchain acquire --
			// and unattributed it was three milliseconds of "unaccounted"
			// that made one backend look mysteriously slower than the other.
			RHI::RHICommandList* cmd = nullptr;
			{
				RV_PROFILE_PHASE(FramePhase::Wait);
				cmd = m_Device->BeginFrame();
			}
			if (!cmd)
				continue;

			Renderer::BeginFrame(cmd);

			// The device recycled a query pool inside BeginFrame, so this is
			// where a couple of frames' worth of GPU results become readable.
			FrameProfiler::CollectGpu();

			// **The ray budget, driven by what the GPU actually took.** Here
			// rather than inside the frame graph because the graph runs once
			// per view -- the editor builds it twice, for two viewports -- and
			// a controller stepped twice a frame moves at twice the rate it
			// was tuned for.
			Renderer::UpdateRayBudget(FrameProfiler::LiveRayGpuMs(),
									  FrameProfiler::LiveGpuFrameMs());

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
			const uint64_t lastScreenshotFrame =
				(uint64_t)config.ScreenshotFrame + Math::Max(config.ScreenshotCount, 1u) - 1;
			if (!config.ScreenshotPath.empty() && frameNumber >= config.ScreenshotFrame
				&& frameNumber <= lastScreenshotFrame)
			{
				// One frame: the file as named. A run of them: numbered, so a
				// flicker can be looked at as the sequence it is.
				std::string path = config.ScreenshotPath;
				if (config.ScreenshotCount > 1)
				{
					const std::filesystem::path given(path);
					std::filesystem::path numbered = given.parent_path() /
						(given.stem().string() + "_" + std::to_string(frameNumber) + given.extension().string());
					path = numbered.string();
				}
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
			if (!config.ScreenshotPath.empty() && frameNumber >= lastScreenshotFrame)
				m_Running = false;

			// **A lost device ends the run.** It cannot be recovered from
			// without recreating the device and every resource on it, and the
			// alternative to stopping is what a report of this actually looked
			// like: a window that keeps its last frame and writes two errors
			// per frame until somebody kills it. Stopping loses nothing that
			// was not already lost, and it leaves the *first* error at the
			// bottom of the log where it can be read.
			if (m_Device && m_Device->IsDeviceLost())
			{
				RV_CORE_ERROR("Stopping: the graphics device is gone.");
				m_Running = false;
			}

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
			FrameProfiler::EndFrame(m_MeasuredFrameMs);

			// **--slow-frames: say what a hitch was made of, while it is still
			// this frame.**
			//
			// A mean over a run cannot answer "why did it drop when I turned
			// toward the car" -- the frames being complained about are a handful
			// among hundreds, and the average is dominated by the ones that were
			// fine. This prints the frame's own passes, longest first, for every
			// frame over the threshold. Ten of those lines from an interactive
			// session name the subsystem outright.
			//
			// Rate-limited rather than unconditional: a threshold set too low
			// turns every frame into eight lines of log, which is slow enough to
			// cause the very hitches it is reporting.
			// **A baseline every two seconds, so the slow frames have something
			// to be different from.** A log of only the bad frames says what the
			// renderer was doing when it hurt and nothing about whether that is
			// unusual -- and "the scene pass was 9 ms" means opposite things
			// depending on whether it is 3 ms or 9 ms the rest of the time.
			if (config.SlowFrameMs > 0.0f)
			{
				static int since = 0;
				if (++since >= 120)
				{
					since = 0;
					RV_CORE_INFO("Baseline frame: {0} ms", m_MeasuredFrameMs);
					LogFrameBreakdown(false);
				}
			}

			if (config.SlowFrameMs > 0.0f && m_MeasuredFrameMs > config.SlowFrameMs)
			{
				static int reported = 0;
				static int skipped = 0;
				if (reported < 400)
				{
					reported++;
					RV_CORE_WARN("Slow frame: {0} ms (over {1})",
						m_MeasuredFrameMs, config.SlowFrameMs);
					LogFrameBreakdown(true);
				}
				else
				{
					// Counted so the log says how much it stopped saying.
					if (++skipped % 200 == 0)
						RV_CORE_WARN("Slow frame: {0} more not listed", skipped);
				}
			}

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
