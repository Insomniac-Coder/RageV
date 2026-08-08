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

	// Straight to the swapchain, not through an offscreen target. The editor
	// needs one because a panel samples it; a game has nothing to sample it
	// with, and the copy would be a full-screen blit per frame for nothing.
	Renderer::SetTargetFormats(device.GetSwapchainFormat(), device.GetSwapchainDepthFormat());

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

	RHI::RenderPassBeginInfo pass;
	pass.Target = nullptr;   // the swapchain
	pass.Clear.Color[0] = 0.05f;
	pass.Clear.Color[1] = 0.05f;
	pass.Clear.Color[2] = 0.06f;
	pass.Clear.Color[3] = 1.0f;

	cmd->PushDebugGroup("Game");
	cmd->BeginRenderPass(pass);
	m_Scene->OnRenderRuntime((float)m_Width / (float)m_Height);
	cmd->EndRenderPass();
	cmd->PopDebugGroup();
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
