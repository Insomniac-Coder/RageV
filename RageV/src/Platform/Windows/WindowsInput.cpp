#include <rvpch.h>
#include "RageV/Core/Input.h"
#include "GLFW/glfw3.h"
#include "RageV/Core/Application.h"

namespace RageV {

	namespace
	{
		// Null when there is no application -- a headless tool, or anything
		// running before Application's constructor finishes. Application::Get()
		// dereferences its instance pointer unconditionally, so calling any of
		// these without one used to be a null dereference rather than a
		// diagnosable failure.
		GLFWwindow* NativeWindow()
		{
			if (!Application::Exists())
				return nullptr;

			return static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		}
	}

	bool Input::IsKeyPressed(int keycode)
	{
		GLFWwindow* window = NativeWindow();
		if (!window)
			return false;

		const auto state = glfwGetKey(window, keycode);
		return state == GLFW_PRESS || state == GLFW_REPEAT;
	}

	bool Input::IsMouseButtonPressed(int button)
	{
		GLFWwindow* window = NativeWindow();
		if (!window)
			return false;

		return glfwGetMouseButton(window, button) == GLFW_PRESS;
	}

	float Input::GetMouseX()
	{
		return GetMousePosition().first;
	}

	float Input::GetMouseY()
	{
		return GetMousePosition().second;
	}

	std::pair<float, float> Input::GetMousePosition()
	{
		GLFWwindow* window = NativeWindow();
		if (!window)
			return { 0.0f, 0.0f };

		double xPos = 0.0, yPos = 0.0;
		glfwGetCursorPos(window, &xPos, &yPos);
		return { (float)xPos, (float)yPos };
	}

}
