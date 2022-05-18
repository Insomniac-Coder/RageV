#pragma once
#include "RageV/Renderer/OrthographicCamera.h"
#include "RageV/Events/Event.h"
#include "RageV/Core/Timestep.h"
#include "RageV/Events/ApplicationEvent.h"
#include "RageV/Events/MouseEvent.h"

namespace RageV
{

	class OrthographicCameraController
	{
	public:
		OrthographicCameraController(float aspectRatio, bool isRotationEnabled = false);

		void OnUpdate(Timestep ts);
		void OnEvent(Event& e);

		OrthographicCamera& GetCamera()  { return m_Camera; }
		const OrthographicCamera& GetCamera() const { return m_Camera; }
		void OnResize(unsigned int width, unsigned int height) { m_AspectRatio = (float)width / (float)height; m_Camera.SetProjectionMatrix(-m_AspectRatio * m_Zoom, m_AspectRatio * m_Zoom, -m_Zoom, m_Zoom); }

	private:
		bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);
		
		float m_AspectRatio;
		float m_Zoom = 1.0f;
		OrthographicCamera m_Camera;
		
		bool m_IsRotationEnabled;
		float m_Rotation = 0.0f;
		glm::vec3 m_Position = {0.0f, 0.0f, 0.0f};
		float m_CameraRotationSpeed = 90.f;
		float m_CameraZoomSpeed = 0.25f;

	};

}