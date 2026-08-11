#pragma once
#include "Camera.h"
#include "RageV/Core/Timestep.h"
#include "RageV/Events/Event.h"
#include "RageV/Events/MouseEvent.h"
#include "RageV/Math/Math.h"

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

		const Mat4& GetView() const { return m_View; }
		Mat4 GetViewProjection() const { return m_Projection * m_View; }
		// Renderer2D/3D take a camera *transform*, not a view matrix.
		Mat4 GetTransform() const;

		Vec3 GetPosition() const { return m_Position; }
		Vec3 GetForward() const;
		Vec3 GetRight() const;
		Vec3 GetUp() const;

		// Frames a point at a distance that fits the given radius. Bound to F.
		void Focus(const Vec3& point, float radius = 1.0f);

		// The camera's whole state at once, in degrees. For --camera, and for
		// anything else that needs a repeatable viewpoint: the four numbers
		// below *are* the state, so setting them cannot leave a derived value
		// disagreeing the way setting a position would.
		void SetOrbit(const Vec3& focalPoint, float distance, float yawDegrees,
					  float pitchDegrees);

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

		Quat Orientation() const;

		void Orbit(const Vec2& delta);
		void Pan(const Vec2& delta);
		void Zoom(float delta);

		float ZoomSpeed() const;
		Vec2 PanSpeed() const;

	private:
		float m_FOV = 45.0f;
		float m_AspectRatio = 16.0f / 9.0f;
		float m_Near = 0.1f;
		float m_Far = 1000.0f;
		float m_ViewportWidth = 1280.0f, m_ViewportHeight = 720.0f;

		Mat4 m_View{ 1.0f };
		Vec3 m_Position{ 0.0f, 0.0f, 0.0f };

		// The orbit pivot. Fly mode translates this and lets position follow, so
		// a subsequent orbit turns around wherever flying finished.
		Vec3 m_FocalPoint{ 0.0f, 0.0f, 0.0f };
		float m_Distance = 10.0f;
		float m_Yaw = 0.0f, m_Pitch = 0.0f;   // radians

		Vec2 m_LastMousePosition{ 0.0f };
		float m_MoveSpeed = 6.0f;
		bool  m_Active = false;
	};
}
