#pragma once
#include "Camera.h"
#include "RageV/Core/Timestep.h"
#include "RageV/Events/Event.h"
#include "RageV/Events/MouseEvent.h"
#include <glm/gtx/quaternion.hpp>

namespace RageV
{
	// The viewport's own camera, independent of any entity.
	//
	// The editor used to render through whichever entity happened to hold the
	// primary CameraComponent, which meant a scene without one drew nothing at
	// all and inspecting anything required moving the game camera.
	//
	// Orbit and fly share a single state -- a focal point, a distance from it,
	// and a yaw/pitch pair. Position is *derived* from those rather than stored
	// alongside them, so switching navigation style mid-motion cannot leave the
	// two representations disagreeing.
	class EditorCamera : public Camera
	{
	public:
		EditorCamera();
		EditorCamera(float fovDegrees, float aspectRatio, float nearClip, float farClip);

		void OnUpdate(Timestep ts);
		void OnEvent(Event& e);

		void SetViewportSize(float width, float height);

		// True while the pointer is over the viewport. Navigation is ignored
		// otherwise, so dragging inside a panel does not also fly the camera.
		void SetActive(bool active) { m_Active = active; }
		bool IsActive() const { return m_Active; }

		const glm::mat4& GetView() const { return m_View; }
		glm::mat4 GetViewProjection() const { return m_Projection * m_View; }
		// Renderer2D/3D take a camera *transform*, not a view matrix.
		glm::mat4 GetTransform() const;

		glm::vec3 GetPosition() const { return m_Position; }
		glm::vec3 GetForward() const;
		glm::vec3 GetRight() const;
		glm::vec3 GetUp() const;

		// Frames a point at a distance that fits the given radius. Bound to F.
		void Focus(const glm::vec3& point, float radius = 1.0f);

		float GetFOV() const { return m_FOV; }
		void  SetFOV(float degrees);
		float GetNearClip() const { return m_Near; }
		float GetFarClip() const { return m_Far; }
		float GetMoveSpeed() const { return m_MoveSpeed; }
		void  SetMoveSpeed(float speed);
		float GetDistance() const { return m_Distance; }

	private:
		bool OnMouseScrolled(MouseScrolledEvent& e);

		void RecalculateProjection();
		void RecalculateView();

		glm::quat Orientation() const;

		void Orbit(const glm::vec2& delta);
		void Pan(const glm::vec2& delta);
		void Zoom(float delta);

		float ZoomSpeed() const;
		glm::vec2 PanSpeed() const;

	private:
		float m_FOV = 45.0f;
		float m_AspectRatio = 16.0f / 9.0f;
		float m_Near = 0.1f;
		float m_Far = 1000.0f;
		float m_ViewportWidth = 1280.0f, m_ViewportHeight = 720.0f;

		glm::mat4 m_View{ 1.0f };
		glm::vec3 m_Position{ 0.0f, 0.0f, 0.0f };

		// The orbit pivot. Fly mode translates this and lets position follow, so
		// a subsequent orbit turns around wherever flying finished.
		glm::vec3 m_FocalPoint{ 0.0f, 0.0f, 0.0f };
		float m_Distance = 10.0f;
		float m_Yaw = 0.0f, m_Pitch = 0.0f;   // radians

		glm::vec2 m_LastMousePosition{ 0.0f };
		float m_MoveSpeed = 6.0f;
		bool  m_Active = false;
	};
}
