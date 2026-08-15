#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

// The file "New Script..." writes, kept in the tree so every engine build
// compiles the template's exact shape.
//
// Since declaration-site markers, what this proves has split in two. Here:
// that the markers compile away to nothing -- the engine build runs no
// generator over its own sources, so this file must build untouched, and
// **must register nothing** (scenetest asserts that too: markers without the
// generator are inert, never half-working). The other half -- that the same
// shape *registers* once rvgen has seen it -- is proved by scenetest's
// fixture, which is run through the real generator at build time.

namespace RageV
{
	class TemplateProbe : public ScriptableEntity
	{
	public:
		// In the inspector and stored in the scene -- the marker is the
		// whole registration; the build generates the rest. Public,
		// because the generated code names it from outside the class.
		RVShowInEditor
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

		// Every frame, with the real elapsed time, which varies. For things
		// nothing else has to agree about: a camera, a fade, a number
		// counting up on the screen. Delete it if this script has none.
		void OnFrame(Timestep dt) override
		{
		}
	};
}
