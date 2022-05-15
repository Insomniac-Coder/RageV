#include <rvpch.h>
#include "OrthographicCameraController.h"
#include "RageV/Core/Input.h"
#include "RageV/Core/KeyCodes.h"

namespace RageV
{



	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool isRotationEnabled)
		: m_AspectRatio(aspectRatio), m_IsRotationEnabled(isRotationEnabled), m_Camera(-m_AspectRatio * m_Zoom, m_AspectRatio* m_Zoom, -m_Zoom, m_Zoom)
	{}

	void OrthographicCameraController::OnUpdate(Timestep ts)
	{
		if (Input::IsKeyPressed(RV_KEY_A))
			m_Position.x += 1.0f * ts;
		else if (Input::IsKeyPressed(RV_KEY_D))
			m_Position.x -= 1.0f * ts;
		if (Input::IsKeyPressed(RV_KEY_W))
			m_Position.y -= 1.0f * ts;
		else if (Input::IsKeyPressed(RV_KEY_S))
			m_Position.y += 1.0f * ts;

		if (m_IsRotationEnabled)
		{
			if (Input::IsKeyPressed(RV_KEY_Q))
				m_Rotation -= 90.0f * ts;
			else if (Input::IsKeyPressed(RV_KEY_E))
				m_Rotation += 90.0f * ts;
			m_Camera.SetRotation(m_Rotation);
		}
		m_Camera.SetPosition(m_Position);
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);

		dispatcher.Dispatch<MouseScrolledEvent>(RV_BIND_EVENT_FN(OrthographicCameraController::OnMouseScrolled));
		dispatcher.Dispatch<WindowResizeEvent>(RV_BIND_EVENT_FN(OrthographicCameraController::OnWindowResized));

	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		m_Zoom -= e.GetYOffset() * m_CameraZoomSpeed;
		m_Zoom = std::max(m_Zoom, 0.25f);
		m_Camera.SetProjectionMatrix(-m_AspectRatio * m_Zoom, m_AspectRatio * m_Zoom, -m_Zoom, m_Zoom);
		return false;
	}

	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		m_AspectRatio = (float)e.GetWidth() / (float)e.GetHeight();
		m_Camera.SetProjectionMatrix(-m_AspectRatio * m_Zoom, m_AspectRatio * m_Zoom, -m_Zoom, m_Zoom);
		return false;
	}


}