#pragma once

#ifdef RV_PLATFORM_WINDOWS

extern RageV::Application* RageV::CreateApplication();

int main(int argc, char** argv)
{
	RageV::Log::Init();

	RV_CORE_INFO("Hello from Core");
	RV_TRACE("Hello from App!");

	RageV::Application* app = RageV::CreateApplication();
	app->Run();
	delete app;
}

#else
#error RageV only supports Windows!
#endif