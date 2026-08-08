#include "RuntimeLayer.h"
#include "RageV/Project/Project.h"
#include "imgui.h"

using namespace RageV;

RuntimeLayer::RuntimeLayer()
	: Layer("RuntimeLayer")
{
}

void RuntimeLayer::OnAttach()
{
	if (!Project::GetActive())
	{
		RV_ERROR("No project. Pass --project=<folder>, or put a .rvproject "
				 "beside the executable.");
		return;
	}

	const std::string& startScene = Project::Config().StartScene;
	if (startScene.empty())
	{
		RV_ERROR("Project '{0}' has no start scene. Set one in the editor: "
				 "File > Set Start Scene.", Project::Config().Name);
		return;
	}

	const std::filesystem::path scenePath = Project::AssetPath(startScene);

	m_Scene = std::make_shared<Scene>();
	SceneSerializer serializer(m_Scene);
	if (!serializer.Deserialize(scenePath.string()))
	{
		RV_ERROR("Could not load the start scene {0}", scenePath.string());
		m_Scene.reset();
		return;
	}

	auto& device = Renderer::GetDevice();

	// The scene draws into a linear HDR target, not the swapchain: bloom needs
	// values from before the tone curve compresses them, and there are none
	// left once an 8-bit backbuffer has been written. The tonemap pass is what
	// reaches the swapchain.
	Renderer::SetTargetFormats(RHI::Format::R16G16B16A16_SFLOAT, RHI::Format::D32_SFLOAT);

	m_Graph = std::make_unique<RenderGraph>(device);

	// So the UI pass does not wipe the frame this layer just drew.
	Application::Get().GetImGuiLayer()->SetClearsBackbuffer(false);

	m_Width = device.GetSwapchainWidth();
	m_Height = device.GetSwapchainHeight();
	m_Scene->OnViewportResize((float)m_Width, (float)m_Height);

	// A game starts running. There is no Play button to press, and that
	// difference is most of what separates this from the editor.
	m_Scene->OnRuntimeStart();
	m_Ready = true;

	RV_INFO("Running '{0}' -- {1}", Project::Config().Name, startScene);
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

	m_Scene->OnUpdateRuntime(ts);

	RHI::RHICommandList* cmd = Renderer::GetCommandList();
	if (!cmd || m_Height == 0)
		return;

	// Before the frame graph: both of these open render passes of their own,
	// and nothing may do that inside another one.
	m_Scene->PrepareEnvironment();
	m_Scene->CaptureReflectionProbes();

	if (Entity camera = m_Scene->GetPrimaryCameraEntity())
	{
		m_Scene->RenderShadows(camera.GetComponent<CameraComponent>().Camera,
							   camera.GetComponent<TransformComponent>().World);
	}

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
	frame.DrawScene = [this](RGPassContext& context)
	{
		m_Scene->OnRenderRuntime((float)context.Width / (float)context.Height);
	};

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
