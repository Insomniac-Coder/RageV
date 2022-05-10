#include <rvpch.h>
#include "OpenGLContext.h"
#include "glad/glad.h"
#include "GLFW/glfw3.h"

namespace RageV
{



	OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
		:m_WindowHandle(windowHandle) 
	{
		RV_CORE_ASSERT(windowHandle, "Window handle is null!");
	}

	void OpenGLContext::SwapBuffers()
	{
		glfwSwapBuffers(m_WindowHandle);
	}

	void OpenGLContext::Init()
	{
		glfwMakeContextCurrent(m_WindowHandle);
		int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
		RV_CORE_ASSERT(status, "Failed to initialise GLAD!");

		RV_CORE_INFO("OpenGL Vendor: {0}", glGetString(GL_VENDOR));
		RV_CORE_INFO("OpenGL Renderer: {0}", glGetString(GL_RENDERER));
		RV_CORE_INFO("OpenGL Version: {0}", glGetString(GL_VERSION));
	}


}