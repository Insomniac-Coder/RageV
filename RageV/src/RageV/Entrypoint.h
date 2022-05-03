#pragma once

#ifdef RV_PLATFORM_WINDOWS

extern RageV::Application* RageV::CreateApplication();

int main(int argc, char** argv)
{
	RageV::Application* app = RageV::CreateApplication();
	app->Run();
	delete app;
}

#else
#error RageV only supports Windows!
#endif