#include <rvpch.h>
#include "WindowsWindow.h"
#include "RageV/Events/ApplicationEvent.h"
#include "RageV/Events/KeyEvent.h"
#include "RageV/Events/MouseEvent.h"
#include "Platform/OpenGL/OpenGLContext.h"

namespace RageV

{
	static bool s_GLFWInitialized = false;

	Window* Window::Create(const WindowProps& props)
	{
		return new WindowsWindow(props);
	}

	static void GLFWErrorCallback(int error, const char* description) {
		RV_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
	}

}

RageV::WindowsWindow::WindowsWindow(const WindowProps& props)
{
	Init(props);
}

RageV::WindowsWindow::~WindowsWindow()
{
	Shutdown();
}

void RageV::WindowsWindow::OnUpdate()
{
	glfwPollEvents();
	m_Context->SwapBuffers();
}

void RageV::WindowsWindow::SetVsync(bool enabled)
{
	if (enabled)
		glfwSwapInterval(1);
	else
		glfwSwapInterval(0);
	m_Data.VSync = enabled;
}

bool RageV::WindowsWindow::IsVSync() const
{
	return m_Data.VSync;
}

void RageV::WindowsWindow::Init(const WindowProps& props)
{
	m_Data.Title = props.Title;
	m_Data.Height = props.Height;
	m_Data.Width = props.Width;

	RV_CORE_INFO("Creating window {0} of size {1} x {2}", m_Data.Title, m_Data.Width, m_Data.Height);

	if (!s_GLFWInitialized)
	{
		int success = glfwInit();
		RV_CORE_ASSERT(sucess, "Could not initialize GLFW!");
		glfwSetErrorCallback(GLFWErrorCallback);
		s_GLFWInitialized = true;
	}

	m_Window = glfwCreateWindow(m_Data.Width, m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);
	m_Context = new OpenGLContext(m_Window);
	m_Context->Init();

	glfwSetWindowUserPointer(m_Window, &m_Data);
	SetVsync(true);

	//Set GLFW callbacks
	glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height) {
		WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);

		winData.Width = width;
		winData.Height = height;

		WindowResizeEvent e(width, height);

		winData.EventCallback(e);
	});

	glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window) {
		WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);

		WindowCloseEvent e;
		winData.EventCallback(e);
	});

	glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int modes) {
		WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);

		switch (action)
		{
			case GLFW_PRESS: {
				KeyPressedEvent e(key, 0);
				winData.EventCallback(e);
				break;
			}
			case GLFW_REPEAT: {
				KeyPressedEvent e(key, 1);
				winData.EventCallback(e);
				break;
			}
			case GLFW_RELEASE: {
				KeyReleasedEvent e(key);
				winData.EventCallback(e);
				break;
			}
		}
	});

	glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
		{
			WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);

			switch (action)
			{
				case GLFW_PRESS: {
					MouseButtonPressedEvent e(button);
					winData.EventCallback(e);
					break;
				}
				case GLFW_RELEASE: {
					MouseButtonReleasedEvent e(button);
					winData.EventCallback(e);
					break;
				}
			}
		}
	);

	glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int character)
		{
			WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);
			KeyTypedEvent e(character);

			winData.EventCallback(e);
		}
	);

	glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xOffset, double yOffset)
		{
			WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseScrolledEvent e((float)xOffset, (float)yOffset);
			winData.EventCallback(e);
		}
	);

	glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xPos, double yPos)
		{
			WindowData& winData = *(WindowData*)glfwGetWindowUserPointer(window);

			MouseMovedEvent e((float )xPos, (float)yPos);
			winData.EventCallback(e);
		}
	);
}

void RageV::WindowsWindow::Shutdown()
{
	glfwDestroyWindow(m_Window);
}
