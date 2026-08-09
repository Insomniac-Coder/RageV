#include <rvpch.h>
#include "WindowsWindow.h"
#include "RageV/Events/ApplicationEvent.h"
#include "RageV/Events/KeyEvent.h"
#include "RageV/Events/MouseEvent.h"
#include "RageV/Core/EngineConfig.h"

// For the window icon, which GLFW wants as raw pixels rather than an .ico.
#include "stb_image.h"

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
	// Presentation belongs to the RHI device now; this only pumps input.
	glfwPollEvents();
}

void RageV::WindowsWindow::SetVsync(bool enabled)
{
	// Swap interval is a GL-context concept; on Vulkan the present mode is a
	// swapchain property. Either way the device owns it.
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
		RV_CORE_ASSERT(success, "Could not initialize GLFW!");
		glfwSetErrorCallback(GLFWErrorCallback);
		s_GLFWInitialized = true;
	}
	// Vulkan drives presentation itself, so GLFW must not create a GL context.
	// OpenGL needs one, and it has to be requested before the window exists --
	// which is why the backend is a restart-time choice.
	if (EngineConfig::Get().Backend == RHI::Backend::Vulkan)
	{
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	}
	else
	{
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	}

	m_Window = glfwCreateWindow(m_Data.Width, m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);
	RV_CORE_ASSERT(m_Window, "Failed to create GLFW window!");

	SetWindowIcon();

	glfwSetWindowUserPointer(m_Window, &m_Data);

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

void RageV::WindowsWindow::SetWindowIcon()
{
	// The .rc resource compiled into the executable is what Explorer shows on
	// the file and what a pinned taskbar entry inherits. This is what the title
	// bar and alt-tab use, and they are genuinely separate mechanisms -- setting
	// only the first gives a correct icon in Explorer and a blank default in
	// alt-tab, which reads as unfinished in a way that is hard to place.
	//
	// Two sizes because GLFW picks the nearest and scales it, and a 32 blown up
	// to 48 on a high-DPI taskbar looks like exactly what it is.
	static const char* const kIcons[] = { "assets/icon-32.png", "assets/icon-48.png" };

	GLFWimage images[2]{};
	int loaded = 0;

	for (const char* path : kIcons)
	{
		int width = 0, height = 0, channels = 0;
		stbi_uc* pixels = stbi_load(path, &width, &height, &channels, 4);
		if (!pixels)
			continue;

		images[loaded].width = width;
		images[loaded].height = height;
		images[loaded].pixels = pixels;
		loaded++;
	}

	// Not an error. A packaged game may legitimately ship without them and the
	// window still works -- it just wears the default icon.
	if (loaded == 0)
	{
		RV_CORE_WARN("No window icon beside the executable; using the default");
		return;
	}

	// GLFW copies the pixels, so they are ours to free immediately.
	glfwSetWindowIcon(m_Window, loaded, images);

	for (int i = 0; i < loaded; i++)
		stbi_image_free(images[i].pixels);
}
