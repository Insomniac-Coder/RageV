#pragma once
#include "RageV/GraphicsContext.h"

struct GLFWwindow;
namespace RageV
{

	class OpenGLContext : public GraphicsContext
	{
	public:
		OpenGLContext(GLFWwindow* windowHandle);
		virtual void SwapBuffers() override;
		virtual void Init() override;
		
	private:
		GLFWwindow* m_WindowHandle;

	};

}