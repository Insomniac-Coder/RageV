#pragma once
#include <RageV.h>
#include "imgui.h"

struct ProfileData
{
	const char* name;
	double time;
};

class ExampleLayer : public RageV::Layer
{
public:
	ExampleLayer();

	void OnAttach() override;
	void  OnUpdate(RageV::Timestep ts) override;
	void OnImGuiRender() override;
	void OnEvent(RageV::Event& e) override;

private:
	glm::vec3 m_Color;
	RageV::OrthographicCameraController m_CameraController;
	std::shared_ptr<RageV::Texture2D> m_Texture;
	std::vector<ProfileData> m_ProfileDataList;
};

class Sandbox2D : public RageV::Application {
public:
	Sandbox2D()
	{
		PushLayer(new ExampleLayer());
	}
	~Sandbox2D()
	{

	}
};

RageV::Application* RageV::CreateApplication() {
	Sandbox2D* sandbox2d = new Sandbox2D();

	return sandbox2d;
}