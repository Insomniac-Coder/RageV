#pragma once
#include "RageV/Core/Platform.h"
#include "GLFW/glfw3.h"

namespace RageV
{
	class WindowsPlatform : public Platform 
	{
	public:
		virtual std::function<double()> GetTimeFn() override { return glfwGetTime; }
	};

}