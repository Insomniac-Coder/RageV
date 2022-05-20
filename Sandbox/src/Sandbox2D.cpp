#include "Sandbox2D.h"

ExampleLayer::ExampleLayer() : Layer("Renderer2D"), m_CameraController(1270.f/ 720.f, true), m_Color(1.0f, 0.0f, 0.0f) {

}

void ExampleLayer::OnAttach()
{
	m_Texture = RageV::Texture2D::Create("assets/textures/square.png");

	RageV::FrameBufferData fbdata;
	fbdata.Width = 1920;
	fbdata.Height = 1080;

	m_FrameBuffer = RageV::FrameBuffer::Create(fbdata);
}

void ExampleLayer::OnUpdate(RageV::Timestep ts)
{
	PROFILER("On Update");
	//RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
	m_CameraController.OnUpdate(ts);

	m_FrameBuffer->Bind();

	RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	RageV::RenderCommand::Clear();
	glm::mat4 t1, t2;
	t1 = glm::translate(t1, { 0.0f, 0.0f, -0.1f });
	t1 = glm::rotate(t1, m_Rotation, glm::vec3(0.0f, 0.0f, 1.0f));
	t1 = glm::scale(t1, glm::vec3(5.0f, 5.0f, 1.0f));
	m_Rotation += (40.0f * ts);

	{
		RageV::Renderer2D::BeginScene(m_CameraController.GetCamera());
		{
			RageV::Renderer2D::DrawQuad(t1, m_Texture, 10.0f);
		}
		float factor = 25.0f;
		for (float x = -factor; x <= factor; x += 0.1f)
		{
			for (float y = -factor; y <= factor; y += 0.1f)
			{
				t2 = glm::translate(t1, { x, y, 0.0f });
				t2 = glm::scale(t1, glm::vec3(0.08f, 0.08f, 1.0f));
				float r = (x + factor) / factor;
				float b = (y + factor) / factor;
				RageV::Renderer2D::DrawQuad(t2, glm::vec4(x, 0.4f, y, 0.7f));
			}
		}

		//RageV::Renderer2D::DrawQuad(t1, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
		RageV::Renderer2D::EndScene();
	}
	m_FrameBuffer->UnBind();
}

void ExampleLayer::OnImGuiRender()
{
    static bool p_open = true;
    static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
	
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
 	
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
 	
    if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
        window_flags |= ImGuiWindowFlags_NoBackground;
	
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace Demo", &p_open, window_flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar(2);
	
    // Submit the DockSpace
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    }
	
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Menu"))
        {
			if (ImGui::MenuItem("Close", NULL, false, p_open != NULL))
				RageV::Application::Get().Close();
            ImGui::EndMenu();
        }
	
        ImGui::EndMenuBar();
    }
	
    ImGui::End();

	ImGui::Begin("Info Box");
	//ImGui::ColorEdit3("Color,", &m_Color[0]);
	ImGui::Text("DrawCalls: %d", RageV::Renderer2D::GetDrawCallCount());
	ImGui::Text("Quad Count: %d", RageV::Renderer2D::GetQuadCount());
	ImGui::Text("Vertices Count: %d", RageV::Renderer2D::GetVerticesCount());
	ImGui::Text("Indices Count: %d", RageV::Renderer2D::GetIndiciesCount());
	ImGui::Text("Graphics Card: %s", RageV::GraphicsInformation::GetGraphicsInfo().GPUName.c_str());
	ImGui::Text("API Name: %s", RageV::GraphicsInformation::GetGraphicsInfo().APIName.c_str());
	ImGui::Image((void*)m_FrameBuffer->GetColorAttachment(), ImVec2{1280.0f, 720.0f});
	ImGui::End();
	m_ProfileDataList.clear();
}

void ExampleLayer::OnEvent(RageV::Event& e)
{
	m_CameraController.OnEvent(e);
}
