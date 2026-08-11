#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

namespace RageV
{
	class TemplateProbe : public ScriptableEntity
	{
	public:
		// Public so the registration below can name it. C++ has no
		// reflection, so an editable field has to be declared explicitly --
		// unlike C#, where the inspector finds private fields on its own.
		float Speed = 1.0f;

		void OnCreate() override
		{
		}

		// Every fixed simulation step, not every frame. A frame may run zero
		// steps, one, or several -- so multiply rates by dt and the behaviour
		// stays the same at any simulation frequency.
		void OnTick(Timestep dt) override
		{
			Translate({ 0.0f, Speed * dt.GetSeconds(), 0.0f });
		}
	};

	// The name here is what scene files store, so renaming it breaks every
	// scene that used it. Fields declared on the same line show up in the
	// inspector.
	RV_REGISTER_SCRIPT(TemplateProbe).Field<&TemplateProbe::Speed>("Speed");
}

