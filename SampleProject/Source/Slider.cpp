#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

namespace RageV
{
	// **A camera dolly for measuring temporal filters under parallax.** Moves
	// its entity along world X at `Speed` metres a second for `StopAfter`
	// seconds, then holds still. A yaw (Rotator) turns the whole picture
	// together and shows nothing of a reprojection that finds the wrong
	// surface -- every point on a line of sight lands on the same texel under
	// a pure rotation. A translation slides near things across far ones,
	// which is where a reflection's history goes wrong, and stopping lets the
	// settle afterwards be counted in frames. Attach it to the camera in a
	// copy of the scene, burst-capture, delete the copy.
	class Slider : public ScriptableEntity
	{
	public:
		float Speed = 0.5f;
		float StopAfter = 2.0f;

		void OnTick(Timestep dt) override
		{
			const float seconds = dt.GetSeconds();
			if (m_Elapsed >= StopAfter)
				return;
			const float step = Math::Min(seconds, StopAfter - m_Elapsed);
			m_Elapsed += seconds;
			Translate({ Speed * step, 0.0f, 0.0f });
		}

	private:
		float m_Elapsed = 0.0f;
	};

	RV_REGISTER_SCRIPT(Slider)
		.Field<&Slider::Speed>("Speed")
		.Field<&Slider::StopAfter>("StopAfter");
}
