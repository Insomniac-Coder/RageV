#include <rvpch.h>
#include "Application.h"
#include "Events/ApplicationEvent.h"
#include "RageV/Log.h"

namespace RageV {
	Application::Application() {

	}

	Application::~Application() {

	}

	void Application::Run() {

		WindowResizeEvent e(1280, 720);
		RV_TRACE(e);
		while (true);
	}
}