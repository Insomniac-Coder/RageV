#include <rvpch.h>
#include "WindowsInput.h"
#include "GLFW/glfw3.h"
#include "RageV/Application.h"

namespace RageV {
	Input* Input::m_Instance = nullptr;

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
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xPos, yPos;
		glfwGetCursorPos(windowPtr, &xPos, &yPos);
		return (float)xPos;
	}

	float WindowsInput::GetMouseYImpl()
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xPos, yPos;
		glfwGetCursorPos(windowPtr, &xPos, &yPos);
		return (float)yPos;
	}

}