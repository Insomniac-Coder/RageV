#include <rvpch.h>
#include "EditorCamera.h"
#include "RageV/Core/Input.h"
#include "RageV/Core/KeyCodes.h"
#include "RageV/Core/MouseButtonCodes.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	namespace
	{
		// Radians per pixel of mouse travel. Applies to both orbit and fly look,
		// so the two feel like the same camera.
		constexpr float kLookSensitivity = 0.003f;

		// Just short of straight up/down. At exactly +/- 90 degrees the forward
		// axis is parallel to world up and the orientation loses its yaw.
		constexpr float kPitchLimit = Math::HalfPi - 0.01f;
	}

	EditorCamera::EditorCamera()
		: EditorCamera(45.0f, 16.0f / 9.0f, 0.1f, 1000.0f)
	{
	}

	EditorCamera::EditorCamera(float fovDegrees, float aspectRatio, float nearClip, float farClip)
		: m_FOV(fovDegrees), m_AspectRatio(aspectRatio), m_Near(nearClip), m_Far(farClip)
	{
		// Looking slightly down at the origin from a few units back: the same
		// framing every editor opens on, and it makes a ground plane visible.
		m_Yaw = 0.0f;
		m_Pitch = Math::Radians(15.0f);
		m_Distance = 12.0f;

		RecalculateProjection();
		RecalculateView();
	}

	void EditorCamera::SetFOV(float degrees)
	{
		m_FOV = Math::Clamp(degrees, 10.0f, 120.0f);
		RecalculateProjection();
	}

	void EditorCamera::SetMoveSpeed(float speed)
	{
		// **The floor is 0.5 m/s, not 0.05.** The wheel is the throttle while
		// flying and each notch divides by 1.15, so a couple of flicks take
		// 6 m/s down to the old floor -- at which W for a whole second moved
		// the camera five centimetres. Nothing showed the throttle, nothing
		// reset it, and the symptom is that W and S both appear to do nothing
		// and are therefore indistinguishable from each other.
		//
		// Half a metre a second is still a crawl, and it is visibly a crawl
		// rather than visibly nothing, which is the difference that matters.
		m_MoveSpeed = Math::Clamp(speed, 0.5f, 200.0f);
	}

	void EditorCamera::SetViewportSize(float width, float height)
	{
		if (width <= 0.0f || height <= 0.0f)
			return;
		if (m_ViewportWidth == width && m_ViewportHeight == height)
			return;

		m_ViewportWidth = width;
		m_ViewportHeight = height;
		m_AspectRatio = width / height;
		RecalculateProjection();
	}

	Quat EditorCamera::Orientation() const
	{
		// Negated because pitch/yaw describe where the camera looks, while the
		// quaternion rotates the camera's own axes.
		return Math::FromEuler(Vec3(-m_Pitch, -m_Yaw, 0.0f));
	}

	Vec3 EditorCamera::GetForward() const { return Math::Rotate(Orientation(), Vec3(0.0f, 0.0f, -1.0f)); }
	Vec3 EditorCamera::GetRight()   const { return Math::Rotate(Orientation(), Vec3(1.0f, 0.0f,  0.0f)); }
	Vec3 EditorCamera::GetUp()      const { return Math::Rotate(Orientation(), Vec3(0.0f, 1.0f,  0.0f)); }

	Mat4 EditorCamera::GetTransform() const
	{
		return Math::Translate(Mat4(1.0f), m_Position) * Math::ToMat4(Orientation());
	}

	void EditorCamera::RecalculateProjection()
	{
		// Math::Perspective emits depth in [0, 1] rather than [-1, 1],
		// so this already produces Vulkan's [0,1] depth range and the OpenGL
		// backend compensates once at the swapchain. Nothing to adjust here.
		m_Projection = Math::Perspective(Math::Radians(m_FOV), m_AspectRatio, m_Near, m_Far);
	}

	void EditorCamera::RecalculateView()
	{
		m_Position = m_FocalPoint - GetForward() * m_Distance;
		m_View = Math::Inverse(GetTransform());
	}

	void EditorCamera::Focus(const Vec3& point, float radius)
	{
		m_FocalPoint = point;

		// Distance at which a sphere of `radius` fills most of the vertical FOV.
		const float safeRadius = Math::Max(radius, 0.1f);
		m_Distance = Math::Max(safeRadius / Math::Tan(Math::Radians(m_FOV) * 0.5f) * 1.4f, 0.5f);

		RecalculateView();
	}

	void EditorCamera::SetOrbit(const Vec3& focalPoint, float distance,
								float yawDegrees, float pitchDegrees)
	{
		m_FocalPoint = focalPoint;

		// The same floor Focus uses. A distance of zero puts the eye on the
		// pivot, where yaw and pitch stop meaning anything.
		m_Distance = Math::Max(distance, 0.01f);
		m_Yaw = Math::Radians(yawDegrees);
		m_Pitch = Math::Radians(pitchDegrees);

		RecalculateView();
	}

	// How much closer one notch of the wheel gets you, as a *ratio*.
	//
	// **Multiplicative, and that is the whole fix.** The step used to be
	// `min((distance * 0.25)^2, 100)` metres -- proportional to distance, which
	// is the right instinct and the wrong power. Squared, it collapses: at the
	// one-metre floor a notch moved the camera six millimetres, so getting
	// close to something meant arriving in a well you then could not scroll
	// out of, because climbing out was equally slow. It read as the wheel
	// simply stopping working.
	//
	// A ratio has no such well. Fifteen per cent of the distance is fifteen per
	// cent whether that distance is a centimetre or a kilometre, so the same
	// flick of the wheel always covers the same fraction of the way in or out
	// and the control behaves identically at every scale.
	constexpr float kZoomPerNotch = 1.15f;

	// Close enough to inspect a windscreen, far enough not to sit inside the
	// near clip. Below it the pivot is pushed ahead instead -- see Zoom.
	constexpr float kMinDistance = 0.5f;
	constexpr float kMaxDistance = 5000.0f;

	Vec2 EditorCamera::PanSpeed() const
	{
		// Quadratic falloff against viewport size, so panning covers a similar
		// fraction of the screen regardless of how large the panel is.
		const float x = Math::Min(m_ViewportWidth / 1000.0f, 2.4f);
		const float y = Math::Min(m_ViewportHeight / 1000.0f, 2.4f);
		return { 0.0366f * (x * x) - 0.1778f * x + 0.3021f,
				 0.0366f * (y * y) - 0.1778f * y + 0.3021f };
	}

	void EditorCamera::Orbit(const Vec2& delta)
	{
		// Orbiting past vertical would flip the horizon; clamping instead is
		// what every DCC tool does and what people expect.
		m_Yaw += delta.x * kLookSensitivity;
		m_Pitch = Math::Clamp(m_Pitch - delta.y * kLookSensitivity, -kPitchLimit, kPitchLimit);
		m_Yaw = Math::Mod(m_Yaw + Math::Pi, Math::TwoPi) - Math::Pi;
	}

	void EditorCamera::Pan(const Vec2& delta)
	{
		const Vec2 speed = PanSpeed();
		m_FocalPoint += -GetRight() * delta.x * speed.x * m_Distance * 0.1f;
		m_FocalPoint += GetUp() * delta.y * speed.y * m_Distance * 0.1f;
	}

	void EditorCamera::Zoom(float notches)
	{
		const float wanted = m_Distance * Math::Pow(kZoomPerNotch, -notches);

		// Once the pivot is reached, keep going by pushing the pivot ahead
		// instead of stopping dead or inverting through it. The push is what
		// the zoom would have travelled, so scrolling in past the floor keeps
		// moving at the rate it was moving at rather than grinding to a halt.
		if (wanted < kMinDistance)
		{
			m_FocalPoint += GetForward() * (kMinDistance - wanted);
			m_Distance = kMinDistance;
			return;
		}

		m_Distance = Math::Min(wanted, kMaxDistance);
	}

	void EditorCamera::OnUpdate(Timestep ts)
	{
		const Vec2 mouse = [] {
			const auto [x, y] = Input::GetMousePosition();
			return Vec2(x, y);
		}();

		const Vec2 delta = mouse - m_LastMousePosition;
		// Refreshed every frame, not only while dragging: otherwise the first
		// frame of a drag reports the travel since the last drag ended and the
		// camera jumps.
		m_LastMousePosition = mouse;

		if (!m_Active)
			return;

		const bool alt = Input::IsKeyPressed(RV_KEY_LEFT_ALT) || Input::IsKeyPressed(RV_KEY_RIGHT_ALT);
		const bool shift = Input::IsKeyPressed(RV_KEY_LEFT_SHIFT) || Input::IsKeyPressed(RV_KEY_RIGHT_SHIFT);
		const bool left = Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_LEFT);
		const bool right = Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_RIGHT);
		const bool middle = Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_MIDDLE);

		if (alt && left)
		{
			Orbit(delta);
		}
		else if (middle || (alt && middle))
		{
			Pan(delta);
		}
		else if (alt && right)
		{
			// Horizontal travel too, so the gesture works without a wheel.
			Zoom((delta.x + delta.y) * 0.01f);
		}
		else if (right)
		{
			// Fly: look with the mouse, move with the keyboard. Translating the
			// focal point rather than the position means a later orbit pivots
			// around wherever the flight ended.
			m_Yaw += delta.x * kLookSensitivity;
			m_Pitch = Math::Clamp(m_Pitch - delta.y * kLookSensitivity, -kPitchLimit, kPitchLimit);

			Vec3 movement(0.0f);
			if (Input::IsKeyPressed(RV_KEY_W)) movement += GetForward();
			if (Input::IsKeyPressed(RV_KEY_S)) movement -= GetForward();
			if (Input::IsKeyPressed(RV_KEY_D)) movement += GetRight();
			if (Input::IsKeyPressed(RV_KEY_A)) movement -= GetRight();
			if (Input::IsKeyPressed(RV_KEY_E)) movement += Vec3(0.0f, 1.0f, 0.0f);
			if (Input::IsKeyPressed(RV_KEY_Q)) movement -= Vec3(0.0f, 1.0f, 0.0f);

			if (Math::Dot(movement, movement) > 0.0f)
				m_FocalPoint += Math::Normalize(movement) * m_MoveSpeed * (shift ? 3.0f : 1.0f) * ts.GetSeconds();
		}

		RecalculateView();
	}

	void EditorCamera::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(RV_BIND_EVENT_FN(EditorCamera::OnMouseScrolled));
	}

	bool EditorCamera::OnMouseScrolled(MouseScrolledEvent& e)
	{
		if (!m_Active)
			return false;

		// While flying, the wheel is the throttle rather than a zoom -- zooming
		// mid-flight moves the pivot you are not looking at.
		if (Input::IsMouseButtonPressed(RV_MOUSE_BUTTON_RIGHT))
		{
			SetMoveSpeed(m_MoveSpeed * (e.GetYOffset() > 0.0f ? 1.15f : 1.0f / 1.15f));
			return true;
		}

		// One notch, one step of the ratio. The wheel reports whole notches, so
		// this is deliberately unscaled.
		Zoom(e.GetYOffset());
		RecalculateView();
		return true;
	}
}
