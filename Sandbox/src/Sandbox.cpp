#include <RageV.h>

class ExampleLayer : public RageV::Layer
{
public:
	ExampleLayer() : Layer("Example") {}

	void  OnUpdate() override
	{
		//RV_INFO("ExampleLayer::Update");
		if (RageV::Input::IsKeyPressed(RV_KEY_TAB))
		{
			RV_TRACE("Tab key was pressed");
		}

		if (RageV::Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_LEFT))
		{
			auto [x, y] = RageV::Input::GetMousePosition();
			RV_TRACE("Mouse position is: {0}, {1}", x, y);
		}
	}

	void OnEvent(RageV::Event& e) override
	{
		//RV_INFO("{0}", e);
		if (e.GetEventType() == RageV::EventType::KeyPressed)
		{
			RageV::KeyPressedEvent& ke = (RageV::KeyPressedEvent&)e;
			RV_INFO("{0}", (char)ke.GetKeyCode());
		}
	}
};

class Sandbox : public RageV::Application {
public:
	Sandbox()
	{
		PushLayer(new ExampleLayer());
	}
	~Sandbox()
	{

	}
};

RageV::Application* RageV::CreateApplication() {
	Sandbox* sandbox = new Sandbox();

	return sandbox;
}