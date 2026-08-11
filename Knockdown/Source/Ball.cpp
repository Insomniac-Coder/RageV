#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

namespace RageV
{
	// A fired ball. The launcher spawns one, places it at the muzzle and
	// writes Velocity into its fields; all the ball does is act on that once
	// its body exists, and take itself out of the scene before the floor
	// fills up with spent shots.
	class Ball : public ScriptableEntity
	{
	public:
		Vec3 Velocity{ 0.0f, 0.0f, 0.0f };
		float Life = 6.0f;

		void OnCreate() override
		{
			SetLinearVelocity(Velocity);
		}

		void OnTick(Timestep dt) override
		{
			m_Age += dt.GetSeconds();
			if (m_Age >= Life)
				Destroy();
		}

	private:
		float m_Age = 0.0f;
	};

	RV_REGISTER_SCRIPT(Ball)
		.Field<&Ball::Velocity>("Velocity")
		.Field<&Ball::Life>("Life");
}
