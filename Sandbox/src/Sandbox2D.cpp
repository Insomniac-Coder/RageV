#include "Sandbox2D.h"
#include <chrono>

template<typename Fn>
class Timer
{
public:
	Timer(const char* name, Fn&& func) : m_Name(name), m_Func(func)
	{
		m_Start = std::chrono::high_resolution_clock::now();
	}

	~Timer()
	{
		std::chrono::steady_clock::time_point m_End = Stop();
		std::chrono::duration<double, std::micro> difference = m_End - m_Start;

		m_Func({ m_Name, difference.count() });
	}
private:
	std::chrono::steady_clock::time_point Stop()
	{
		return std::chrono::high_resolution_clock::now();
	}
	const char* m_Name;
	std::chrono::steady_clock::time_point m_Start;
	Fn m_Func;
};

#define PROFILER(name) Timer time##__LINE__(name, [&](ProfileData profileData) { m_ProfileDataList.push_back(profileData); })

ExampleLayer::ExampleLayer() : Layer("Renderer2D"), m_CameraController(1270.f/ 720.f, true), m_Color(1.0f, 0.0f, 0.0f) {

}

void ExampleLayer::OnAttach()
{
	m_Texture = RageV::Texture2D::Create("assets/textures/square.png");
}

void ExampleLayer::OnUpdate(RageV::Timestep ts)
{
	PROFILER("On Update");
	//RV_TRACE("Delta time: {0} s ({1} ms)", ts, ts.GetMilliSeconds());
	m_CameraController.OnUpdate(ts);

	RageV::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
	RageV::RenderCommand::Clear();
	RageV::Transform2D t1, t2;
	t1.position = { 0.0f, 0.0f, -0.1f };
	t1.scale = { 5.0f, 5.0f };
	t1.rotation = 45.0f;
	t2.position = { 0.5f, 0.0f, 0.0f };
	t2.scale = { 1.0f, 1.0f };

	{
		PROFILER("Render Scene");
		RageV::Renderer2D::BeginScene(m_CameraController.GetCamera());
		{
			PROFILER("Draw textured quad");
			RageV::Renderer2D::DrawQuad(t1, m_Texture, 10.0f);
		}
		RageV::Renderer2D::DrawQuad(t2, glm::vec4(m_Color, 1.0f));
		RageV::Renderer2D::EndScene();
	}
}

void ExampleLayer::OnImGuiRender()
{
	ImGui::Begin("Color Picker");
	ImGui::ColorEdit3("Color,", &m_Color[0]);

	for (auto item : m_ProfileDataList)
	{
		char name[50];
		strcpy(name, item.name);
		strcat(name, ": %.3f ms");
		ImGui::Text(name, item.time);
	}

	ImGui::End();
	m_ProfileDataList.clear();
}

void ExampleLayer::OnEvent(RageV::Event& e)
{
	m_CameraController.OnEvent(e);
}
