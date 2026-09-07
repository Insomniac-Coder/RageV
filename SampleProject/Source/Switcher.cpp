#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

namespace RageV
{
	// A change test for the temporal reconstruction (WR-16 R4, 2026-09-06):
	// after `AtSeconds` of play this entity's light, if it has one, is set to
	// `Intensity`, and its mesh, if it has one, to a flat emissive of
	// `Emissive` (0 = off). Attached by tools/scripts/garage/burst.py to the
	// garage's "Tube" lights and their "Bottom light bars" lenses when
	// BURST_SWITCH is set, so a burst can measure how many frames the
	// reflection on the floor takes to forget a tube that went out.
	class Switcher : public ScriptableEntity
	{
	public:
		float AtSeconds = 1.0f;
		float Intensity = 0.0f;
		float Emissive = 0.0f;

		void OnTick(Timestep dt) override
		{
			if (m_Done)
				return;
			m_Elapsed += dt.GetSeconds();
			if (m_Elapsed < AtSeconds)
				return;
			m_Done = true;
			if (HasComponent<LightComponent>())
				GetComponent<LightComponent>().Light.Intensity = Intensity;
			if (HasComponent<MeshComponent>())
			{
				auto& mesh = GetComponent<MeshComponent>();
				mesh.OverrideEmissive = true;
				mesh.EmissiveColor = { Emissive, Emissive, Emissive, 1.0f };
			}
		}

	private:
		float m_Elapsed = 0.0f;
		bool m_Done = false;
	};

	RV_REGISTER_SCRIPT(Switcher)
		.Field<&Switcher::AtSeconds>("AtSeconds")
		.Field<&Switcher::Intensity>("Intensity")
		.Field<&Switcher::Emissive>("Emissive");
}
