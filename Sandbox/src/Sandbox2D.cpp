#include "Sandbox2D.h"


ExampleLayer::ExampleLayer() : Layer("Renderer2D"), m_CameraController(1270.f/ 720.f, true), m_Color(1.0f) {

}

void ExampleLayer::OnAttach()
{
	m_Texture = RageV::Texture2D::Create("assets/textures/square.png");
}

void ExampleLayer::OnUpdate(RageV::Timestep ts)
{
	//RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
	m_CameraController.OnUpdate(ts);

	RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	RageV::RenderCommand::Clear();

	RageV::Renderer2D::BeginScene(m_CameraController.GetCamera());
	RageV::Renderer2D::DrawQuad(glm::vec3(0.0f, 0.0f, -0.1f), glm::vec2(5.0f, 5.0f), m_Texture);
	RageV::Renderer2D::DrawQuad(glm::vec2(0.5f, 0.0f), glm::vec2(1.0f, 1.0f), glm::vec4(m_Color, 1.0f));
	RageV::Renderer2D::EndScene();
}

void ExampleLayer::OnImGuiRender()
{
	ImGui::Begin("Color Picker");
	ImGui::ColorEdit3("Color,", &m_Color[0]);
	ImGui::End();
}

void ExampleLayer::OnEvent(RageV::Event& e)
{
	m_CameraController.OnEvent(e);
}
