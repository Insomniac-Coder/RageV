#include <rvpch.h>
#include "RageV/Core/Input.h"
#include "GLFW/glfw3.h"
#include "RageV/Core/Application.h"

namespace RageV {

	bool Input::IsKeyPressed(int keycode)
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetKey(windowPtr, keycode);

		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(int button)
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		auto state = glfwGetMouseButton(windowPtr, button);

		return state == GLFW_PRESS;
	}

	float Input::GetMouseX()
	{
		auto [x, y] = GetMousePosition();

		return (float)x;
	}

	float Input::GetMouseY()
	{
		auto [x,y] = GetMousePosition();

		return (float)y;
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		auto windowPtr = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		double xPos, yPos;
		glfwGetCursorPos(windowPtr, &xPos, &yPos);

		return { (float)xPos, (float)yPos };
	}

}