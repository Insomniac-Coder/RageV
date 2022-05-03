#include <RageV.h>

class Sandbox : public RageV::Application {
public:
	Sandbox()
	{

	}
	~Sandbox()
	{

	}
};

RageV::Application* RageV::CreateApplication() {
	Sandbox* sandbox = new Sandbox();

	return sandbox;
}