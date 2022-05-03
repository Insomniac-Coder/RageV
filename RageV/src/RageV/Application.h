#pragma once
#include "Core.h"

namespace RageV {
	class RV_API Application
	{
	public:
		Application();
		virtual ~Application();
		void Run();
	};

	Application* CreateApplication();
}