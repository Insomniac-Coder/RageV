#include <rvpch.h>
#include "WindowsInput.h"
#include "GLFW/glfw3.h"
#include "RageV/Application.h"

namespace RageV {
	Input* Input::m_Instance = new WindowsInput();

	bool WindowsInput::IsKeyPressedImpl(int keycode)
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetKey(windowPtr, keycode);

		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool WindowsInput::IsMouseButtonPressedImpl(int button)
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetMouseButton(windowPtr, button);

		return state == GLFW_PRESS;
	}

	float WindowsInput::GetMouseXImpl()
	{
		auto [x, y] = GetMousePositionImpl();

		return (float)x;
	}

	float WindowsInput::GetMouseYImpl()
	{
		auto [x,y] = GetMousePositionImpl();

		return (float)y;
	}

	std::pair<float, float> WindowsInput::GetMousePositionImpl()
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xPos, yPos;
		glfwGetCursorPos(windowPtr, &xPos, &yPos);

		return { xPos, yPos };
	}

}