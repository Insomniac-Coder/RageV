#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"
#include "RageV/Scene/Scene.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Math/Math.h"

namespace RageV
{
	// The player's half of the game: A/D swing the whole launcher (the camera
	// rides on it), W/S tilt the muzzle, Fire or Jump lobs a ball. A raycast
	// out of the muzzle parks the aim dot on whatever would be hit, which is
	// the game's only sight.
	class Launcher : public ScriptableEntity
	{
	public:
		float TurnSpeed = 1.6f;    // radians per second at full deflection
		float PitchSpeed = 1.1f;
		float BallSpeed = 16.0f;
		float Cooldown = 0.45f;

		void OnCreate() override
		{
			m_Muzzle = FindEntityByName("Muzzle");
			m_AimDot = FindEntityByName("AimDot");
			m_BallPrefab = Assets::Registry::GetHandle("prefabs/Ball.rprefab");
			m_ShotClip = Assets::Registry::GetHandle("audio/shot.wav");

			if (!m_Muzzle)
				RV_WARN("Launcher: no entity named 'Muzzle' to pitch or fire from");
			if (!m_BallPrefab.IsValid())
				RV_WARN("Launcher: prefabs/Ball.rprefab is not in the asset registry");
		}

		void OnUpdate(Timestep dt) override
		{
			// Positive yaw turns left, so full right deflection must turn the
			// other way.
			Rotate({ 0.0f, -GetAxis("MoveRight") * TurnSpeed * dt.GetSeconds(), 0.0f });

			if (m_Muzzle)
			{
				auto& rotation = m_Muzzle.GetComponent<TransformComponent>().Rotation;
				rotation.x = Math::Clamp(
					rotation.x + GetAxis("MoveForward") * PitchSpeed * dt.GetSeconds(),
					-0.1f, 0.8f);
			}

			AimAndFire(dt);
		}

	private:
		void AimAndFire(Timestep dt)
		{
			if (!m_Muzzle)
				return;

			const Mat4 muzzle = GetScene().GetWorldTransform(m_Muzzle);
			const Vec3 origin = Vec3(muzzle[3]);
			const Vec3 forward = Math::Normalize(Vec3(muzzle * Vec4(0.0f, 0.0f, -1.0f, 0.0f)));

			// The sight: wherever the ray lands, the dot sits, a little off the
			// surface so it does not z-fight with it. No hit parks it out of
			// sight rather than leaving it on whatever it marked last.
			if (m_AimDot)
			{
				auto& dot = m_AimDot.GetComponent<TransformComponent>().Position;
				if (RayHit hit = Raycast(origin + forward * 0.5f, forward * 60.0f))
					dot = hit.Position + hit.Normal * 0.06f;
				else
					dot = Vec3(0.0f, -10.0f, 0.0f);
			}

			m_SinceShot += dt.GetSeconds();
			const bool wantsFire = WasActionPressed("Fire") || WasActionPressed("Jump");
			if (!wantsFire || m_SinceShot < Cooldown || !m_BallPrefab.IsValid())
				return;

			m_SinceShot = 0.0f;

			Entity ball = SpawnPrefab(m_BallPrefab);
			if (!ball)
				return;

			// Spawned clear of the barrel, and told its velocity as a field
			// override -- the ball's body does not exist until the next step,
			// so the launch has to be the ball's own first act.
			ball.GetComponent<TransformComponent>().Position = origin + forward * 1.2f;

			ball.GetComponent<NativeScriptComponent>().Set(
				"Velocity", Detail::ScriptFieldToText(forward * BallSpeed));

			if (m_ShotClip.IsValid())
				PlayOneShot(m_ShotClip, 0.9f);
		}

		Entity m_Muzzle;
		Entity m_AimDot;
		AssetHandle m_BallPrefab;
		AssetHandle m_ShotClip;
		float m_SinceShot = 1000.0f;
	};

	RV_REGISTER_SCRIPT(Launcher)
		.Field<&Launcher::TurnSpeed>("TurnSpeed")
		.Field<&Launcher::PitchSpeed>("PitchSpeed")
		.Field<&Launcher::BallSpeed>("BallSpeed")
		.Field<&Launcher::Cooldown>("Cooldown");
}
