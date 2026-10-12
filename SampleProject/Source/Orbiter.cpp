#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

namespace RageV
{
	// **A camera spin for measuring temporal filters under rotation (RT-24).** Holds
	// still for `Delay` seconds so every history settles, turns round the target at
	// `Speed` degrees a second for `SpinFor` seconds, then holds still again -- the
	// owner's fast drag round the car, made repeatable. The frame at the stop against
	// the same pose once settled is the smear; a Slider cannot show it, since a
	// translation is not what the drag does.
	//
	// The pose is ShowroomCamera's (Scripts/ShowroomCamera.cs) without the smoothing:
	// the same target, distance and pitch, and the same two limits in the same order --
	// the ceiling flattens the pitch, then the walls shorten the arm -- so the camera
	// stays in the room at every yaw exactly as the drag keeps it.
	class Orbiter : public ScriptableEntity
	{
	public:
		float TargetX = -4.3f;
		float TargetY = 0.72f;
		float TargetZ = -2.0f;
		float Yaw = 0.0f;          // degrees at the start
		float Pitch = 4.0f;        // degrees
		float Distance = 11.0f;    // metres
		float Speed = 240.0f;      // degrees a second while turning
		float Delay = 2.0f;        // seconds held still first
		float SpinFor = 1.0f;      // seconds of turning
		// **Or a swing about the start** (owner, 2026-09-23): `Swing` degrees either side
		// of `Yaw`, `Rate` times a second, for `SpinFor` seconds -- a fast drag left and
		// right at the front. It passes the centre at full speed every half period, so
		// those frames sit exactly on the pose the camera settles at. Zero turns instead.
		float Swing = 0.0f;
		float Rate = 2.0f;
		// **Or, with Pan non-zero, the swing turns the view about the camera** (RT-24): the
		// camera stays at the `Yaw` pose, so nothing in frame moves by its depth -- the
		// control that says whether a reflection's history is misplaced by parallax.
		float Pan = 0.0f;

		void OnCreate() override { Apply(); }

		void OnTick(Timestep dt) override
		{
			m_Elapsed += dt.GetSeconds();
			Apply();
		}

	private:
		float m_Elapsed = 0.0f;

		static float WallDistance(float component, float origin, float low, float high)
		{
			if (std::abs(component) < 1e-4f)
				return 3.0e38f;
			const float wall = component > 0.0f ? high : low;
			return Math::Max((wall - origin) / component, 0.1f);
		}

		void Apply()
		{
			const float turning = Math::Clamp(m_Elapsed - Delay, 0.0f, SpinFor);
			const float swung = Swing > 0.0f ? Swing * std::sin(2.0f * 3.14159265f * Rate * turning) : 0.0f;
			const float degrees = Swing > 0.0f ? Yaw + (Pan != 0.0f ? 0.0f : swung) : Yaw + Speed * turning;
			const float yaw = degrees * 3.14159265f / 180.0f;
			float pitch = Pitch * 3.14159265f / 180.0f;
			const float sinYaw = std::sin(yaw), cosYaw = std::cos(yaw);
			// ShowroomCamera's room: the same four walls and ceiling.
			const float reach = Math::Min(WallDistance(sinYaw, TargetX, -5.4f, 5.4f),
										 WallDistance(cosYaw, TargetZ, -7.4f, 9.2f));
			const float headroom = Math::Max(3.30f - TargetY, 0.05f);
			float distance = Distance;
			if (distance * std::sin(pitch) > headroom)
				pitch = std::asin(Math::Clamp(headroom / Math::Max(distance, 0.01f), -1.0f, 1.0f));
			float ground = distance * std::cos(pitch);
			if (ground > reach)
			{
				distance = reach / Math::Max(std::cos(pitch), 1e-3f);
				ground = reach;
			}
			const Vec3 target(TargetX, TargetY, TargetZ);
			GetPosition() = target + Vec3(ground * sinYaw, distance * std::sin(pitch), ground * cosYaw);
			if (Pan == 0.0f)
			{
				LookAt(target);
				return;
			}
			const float turn = swung * 3.14159265f / 180.0f;
			const Vec3 ahead = target - GetPosition();
			LookAt(GetPosition() + Vec3(ahead.x * std::cos(turn) + ahead.z * std::sin(turn), ahead.y,
										-ahead.x * std::sin(turn) + ahead.z * std::cos(turn)));
		}
	};

	RV_REGISTER_SCRIPT(Orbiter)
		.Field<&Orbiter::TargetX>("TargetX")
		.Field<&Orbiter::TargetY>("TargetY")
		.Field<&Orbiter::TargetZ>("TargetZ")
		.Field<&Orbiter::Yaw>("Yaw")
		.Field<&Orbiter::Pitch>("Pitch")
		.Field<&Orbiter::Distance>("Distance")
		.Field<&Orbiter::Speed>("Speed")
		.Field<&Orbiter::Delay>("Delay")
		.Field<&Orbiter::SpinFor>("SpinFor")
		.Field<&Orbiter::Swing>("Swing")
		.Field<&Orbiter::Rate>("Rate")
		.Field<&Orbiter::Pan>("Pan");
}
