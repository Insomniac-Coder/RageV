#include "RageV/Renderer/UIRenderer.h"
#include "RuntimeLayer.h"
#include "RageV/Asset/AssetManager.h"
#include "RageV/Project/Project.h"
#include "RageV/Core/FrameProfiler.h"
#include "RageV/Particles/ParticleSystem.h"
#include "RageV/Renderer/ParticleRenderer.h"
#include "RageV/UI/Canvas.h"
#include "RageV/UI/Interaction.h"
#include "RageV/Core/Input.h"
#include "RageV/Core/MouseButtonCodes.h"
#include "imgui.h"
#include "RageV/ImGui/ImGuiBinding.h"

using namespace RageV;

RuntimeLayer::RuntimeLayer()
	: Layer("RuntimeLayer")
{
}

void RuntimeLayer::OnAttach()
{
	// The engine is a DLL and ImGui's state is a global, so this executable
	// starts with its own empty one. See ImGuiBinding.h.
	ImGuiBinding::Bind();

	// The scene draws into a linear HDR target, not the swapchain: bloom needs
	// values from before the tone curve compresses them, and there are none
	// left once an 8-bit backbuffer has been written. The tonemap pass is what
	// reaches the swapchain.
	// The velocity format is named here as well as in BuildFrame, and that is
	// not redundancy. A reflection probe captures the scene *before* the frame
	// graph is built on the very first frame, so pipelines that learn the
	// target's shape only from BuildFrame are bound into a probe face that
	// already has the extra attachment. ENGINE-NOTES 7r.
	Renderer::SetTargetFormats(RHI::Format::R16G16B16A16_SFLOAT, RHI::Format::D32_SFLOAT,
							   1, RHI::Format::R16G16_SFLOAT);
	// The UI's world layer draws in the scene pass too, and learns the shape
	// from the same two places for the same reason.
	UIRenderer::SetWorldTargetFormats(RHI::Format::R16G16B16A16_SFLOAT,
									  RHI::Format::D32_SFLOAT, 1, RHI::Format::R16G16_SFLOAT);

	m_Graph = std::make_unique<RenderGraph>(Renderer::GetDevice());

	// So the UI pass does not wipe the frame this layer just drew.
	//
	// Set here rather than after the scene loads, because the loading screen
	// is drawn through that same pass and needs the opposite: it *is* the
	// whole frame while it runs, and the boot loop draws nothing underneath
	// it. Turning the clear off before there is anything to preserve would
	// leave the bar compositing over whatever the swapchain last held.
	Application::Get().GetImGuiLayer()->SetClearsBackbuffer(true);
}

// On the boot worker. Files and CPU only -- see Layer::OnLoad.
void RuntimeLayer::OnLoad(Boot::Progress& progress)
{
	if (!Project::GetActive())
	{
		RV_ERROR("No project. Pass --project=<folder>, or put a .rvproject "
				 "beside the executable.");
		return;
	}

	// --scene wins over the project's own, so a benchmark or a bug report can
	// name a scene without changing what everyone else opens.
	const std::string& override = EngineConfig::Get().ScenePath;
	const std::string startScene = override.empty() ? Project::Config().StartScene : override;

	if (startScene.empty())
	{
		RV_ERROR("Project '{0}' has no start scene. Set one in the editor: "
				 "File > Set Start Scene.", Project::Config().Name);
		return;
	}

	const std::filesystem::path scenePath = Project::AssetPath(startScene);

	progress.BeginPhase("Opening scene", 0.0f, 0.15f);
	progress.SetDetail(scenePath.filename().string());

	auto scene = std::make_shared<Scene>();
	SceneSerializer serializer(scene);
	if (!serializer.Deserialize(scenePath.string()))
	{
		RV_ERROR("Could not load the start scene {0}", scenePath.string());
		return;
	}

	m_Scene = std::move(scene);

	if (progress.Cancelled())
		return;

	progress.BeginPhase("Loading assets", 0.15f, 0.95f);
	Assets::Manager::PrepareScene(*m_Scene, progress);

	m_SceneName = startScene;
}

// Main thread, one slice per loading-screen frame.
bool RuntimeLayer::OnLoadStep(Boot::Progress& progress)
{
	if (!m_Scene)
		return false;

	progress.BeginPhase("Uploading assets", 0.95f, 1.0f);
	return Assets::Manager::UploadPrepared(progress, 1.0f / 12.0f);
}

// Back on the main thread, after the uploads.
void RuntimeLayer::OnLoaded()
{
	// A game with nothing to run says so and exits, rather than presenting an
	// empty window that is indistinguishable from a broken one. Decided here
	// because OnLoad is where it becomes knowable, and acted on here because
	// this is the last point before the first frame.
	if (!m_Scene)
	{
		Application::Get().Close();
		return;
	}

	auto& device = Renderer::GetDevice();

	m_Width = device.GetSwapchainWidth();
	m_Height = device.GetSwapchainHeight();
	m_Scene->OnViewportResize((float)m_Width, (float)m_Height);

	// The scene owns the frame from here, so the UI pass must stop clearing.
	Application::Get().GetImGuiLayer()->SetClearsBackbuffer(false);

	// A game starts running. There is no Play button to press, and that
	// difference is most of what separates this from the editor.
	m_Scene->OnRuntimeStart();
	m_Ready = true;

	RV_INFO("Running '{0}' -- {1}", Project::Config().Name, m_SceneName);
}

void RuntimeLayer::OnFixedUpdate(Timestep dt)
{
	if (m_Ready)
		m_Scene->OnFixedUpdateRuntime(dt);
}

void RuntimeLayer::OnUpdate(Timestep ts)
{
	if (!m_Ready)
		return;

	// A rolling average rather than the instantaneous value, which is
	// unreadable at any frame rate worth having.
	m_FrameTimeAccum += ts.GetSeconds() * 1000.0f;
	if (++m_FrameTimeSamples >= 30)
	{
		m_FrameTimeMs = m_FrameTimeAccum / (float)m_FrameTimeSamples;
		m_FrameTimeAccum = 0.0f;
		m_FrameTimeSamples = 0;
	}

	auto& device = Renderer::GetDevice();

	// The window drives the aspect ratio here, where in the editor a panel
	// does. Aspect is a property of the surface being drawn into, which is why
	// it is passed per pass rather than stored on the camera.
	const uint32_t width = device.GetSwapchainWidth();
	const uint32_t height = device.GetSwapchainHeight();
	if (width != m_Width || height != m_Height)
	{
		m_Width = width;
		m_Height = height;
		m_Scene->OnViewportResize((float)width, (float)height);
	}

	// The pointer, before the scripts that read it.
	//
	// In a shipped game the UI layer *is* the window, so a cursor position
	// needs no mapping at all -- which is the whole reason this is two lines
	// here and a dozen in the editor, where the same layer is an image inside
	// a panel.
	{
		const std::pair<float, float> cursor = Input::GetMousePosition();

		UI::PointerInput pointer;
		pointer.X = cursor.first;
		pointer.Y = cursor.second;
		pointer.Down = Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_LEFT);
		pointer.Inside = cursor.first >= 0.0f && cursor.second >= 0.0f
					  && cursor.first < (float)m_Width && cursor.second < (float)m_Height;

		UI::UpdatePointer(*m_Scene, (float)m_Width, (float)m_Height, pointer);
	}

	m_Scene->OnUpdateRuntime(ts);

	RHI::RHICommandList* cmd = Renderer::GetCommandList();
	if (!cmd || m_Height == 0)
		return;

	// Before the frame graph: both of these open render passes of their own,
	// and nothing may do that inside another one.
	{
		RV_PROFILE_PHASE(FramePhase::EnvironmentPrefilter);
		m_Scene->PrepareEnvironment();
	}

	{
		RV_PROFILE_PHASE(FramePhase::Probes);
		m_Scene->CaptureReflectionProbes();
	}

	if (Entity camera = m_Scene->GetPrimaryCameraEntity())
	{
		RV_PROFILE_PHASE(FramePhase::Shadows);
		m_Scene->RenderShadows(camera.GetComponent<CameraComponent>().Camera,
							   camera.GetComponent<TransformComponent>().World);
	}

	RV_PROFILE_PHASE(FramePhase::Graph);

	m_Graph->Begin(m_Width, m_Height);

	// The scene, bloom, tone mapping and anti-aliasing, described in one place
	// and shared with the editor -- the two differ only in where the finished
	// image goes.
	FrameDesc frame;
	frame.Output = m_Graph->Backbuffer();
	frame.Width = m_Width;
	frame.Height = m_Height;
	frame.Environment = m_Scene->GetEnvironment();
	frame.OutputFormat = device.GetSwapchainFormat();
	// One frame chain, so one history. See TemporalHistory for why the graph
	// cannot own this.
	frame.History = &m_History;
	frame.DrawScene = [this](RGPassContext& context)
	{
		m_Scene->OnRenderRuntime((float)context.Width / (float)context.Height);
	};

	// The game's own UI, over the finished image. The whole reason the UI layer
	// exists is that a shipped game can say something, so this is not
	// conditional on anything -- a scene with no canvas resolves to nothing and
	// the pass costs a compare.
	frame.DrawUI = [this](RGPassContext& context)
	{
		UI::DrawScene(*m_Scene, context.Width, context.Height);
	};

	// Asked of the scene rather than of the renderer: the graph is described
	// before anything draws, so the renderer would answer for last frame.
	if (Particles::System::HasWeightedEmitters(*m_Scene))
	{
		frame.DrawTransparent = [](RGPassContext&) { ParticleRenderer::FlushWeighted(); };
		frame.ResolveTransparent = [](RGPassContext&, const RHI::Ref<RHI::RHITexture>& accumulate,
									  const RHI::Ref<RHI::RHITexture>& revealage)
		{
			ParticleRenderer::ResolveWeighted(accumulate, revealage);
		};
	}

	BuildFrame(*m_Graph, frame);

	if (!m_Graph->Compile())
	{
		// Loudly and once. A frame that cannot be described is a bug in the
		// description, and it will be the same bug every frame.
		for (const std::string& error : m_Graph->Errors())
			RV_ERROR("Render graph: {0}", error);
		return;
	}

	m_Graph->Execute(*cmd);
}

void RuntimeLayer::OnImGuiRender()
{
	if (!m_ShowStats)
		return;

	ImGui::SetNextWindowBgAlpha(0.6f);
	ImGui::SetNextWindowPos({ 12.0f, 12.0f }, ImGuiCond_Always);

	if (ImGui::Begin("##runtimestats", nullptr,
					 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
					 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
					 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
	{
		ImGui::Text("%.2f ms  (%.0f fps)", m_FrameTimeMs,
					m_FrameTimeMs > 0.0f ? 1000.0f / m_FrameTimeMs : 0.0f);
		ImGui::Text("%u x %u", m_Width, m_Height);
		ImGui::Text("%u Hz simulation", Application::GetFixedHz());
	}
	ImGui::End();
}

void RuntimeLayer::OnEvent(Event& e)
{
	EventDispatcher dispatcher(e);
	dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& key) { return OnKeyPressed(key); });
}

bool RuntimeLayer::OnKeyPressed(KeyPressedEvent& e)
{
	if (e.GetRepeatCount() > 0)
		return false;

	switch (e.GetKeyCode())
	{
		case RV_KEY_F1:
			m_ShowStats = !m_ShowStats;
			return true;

		// Escape quits. A game with no way out but the window's close button is
		// a game that traps anyone who launches it full-screen.
		case RV_KEY_ESCAPE:
			Application::Get().Close();
			return true;
	}

	return false;
}

void RuntimeLayer::ResizeTarget(uint32_t width, uint32_t height)
{
	m_Width = width;
	m_Height = height;
}
