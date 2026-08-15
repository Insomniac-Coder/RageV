#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

// The declaration-site fixture: scenetest's build runs the real rvgen over
// this file and compiles what it emits, so the whole chain -- marker, scan,
// wrapper TU, static registrar -- is proven on every engine build, without
// building a game module. TemplateProbe.cpp in the engine is the other half
// of the pair: the same markers with *no* generator must register nothing.
//
// All three markers on purpose. A field, a callable, and a class with
// nothing editable at all -- each registers through a different path in the
// emitter, and a fixture that only used one would prove a third of the tool.

namespace RageV
{
	class MarkerProbe : public ScriptableEntity
	{
	public:
		RVShowInEditor
		float Speed = 1.5f;

		RVShowInEditor
		bool Enabled = true;

		RVShowInEditor
		Vec3 Direction = Vec3(0.0f, 1.0f, 0.0f);

		RVShowInEditor
		std::string Label = "probe";

		RVCallable
		void Poke()
		{
			Translate({ 0.0f, Speed, 0.0f });
		}

		void OnTick(Timestep dt) override
		{
			Translate({ 0.0f, Speed * dt.GetSeconds(), 0.0f });
		}
	};

	// Registered with no fields: most scripts have nothing worth tuning, and
	// the class-level marker is how they say "in the dropdown anyway".
	RVScript
	class MarkerProbeBare : public ScriptableEntity
	{
	public:
		void OnTick(Timestep dt) override
		{
			Rotate({ 0.0f, dt.GetSeconds(), 0.0f });
		}
	};
}
