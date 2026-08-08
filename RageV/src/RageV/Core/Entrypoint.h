#pragma once

#include "RageV/Core/EngineConfig.h"

#ifdef RV_PLATFORM_WINDOWS

extern RageV::Application* RageV::CreateApplication();

int main(int argc, char** argv)
{
	RageV::Log::Init();

	// Must run before anything creates a window: the window is created
	// differently depending on the graphics backend.
	RageV::EngineConfig::Init(argc, argv);

	RageV::Application* app = RageV::CreateApplication();
	app->Run();
	delete app;

	return 0;
}

#else
#error RageV only supports Windows!
#endif