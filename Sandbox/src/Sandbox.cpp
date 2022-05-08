#include <RageV.h>

class ExampleLayer : public RageV::Layer
{
public:
	ExampleLayer() : Layer("Example") {}

	void  OnUpdate() override
	{
		RV_INFO("ExampleLayer::Update");
	}

	void OnEvent(RageV::Event& e) override
	{
		RV_INFO("{0}", e);
	}
};

class Sandbox : public RageV::Application {
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer());
		PushOverlay(new RageV::ImGuiLayer());
	}
	~Sandbox()
	{

	}
};

RageV::Application* RageV::CreateApplication() {
	Sandbox* sandbox = new Sandbox();

	return sandbox;
}