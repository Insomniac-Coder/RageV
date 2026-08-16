#include <rvpch.h>
#include "RageV/Scene/ScriptRegistry.h"
#include "RageV/Scene/Components.h"

// The courtyard's two buttons, and the things they act on.
//
// They used to increment a counter, which is a button that demonstrates a
// click rather than a button that does anything -- and a label reading "ring
// the bell" beside a courtyard containing no bell is the same emptiness the
// old demo scene had, moved into the UI.
//
// So there is a bell and there is an anvil, and the buttons hit them.
//
// **These live in the project, not the engine.** They are game code: the
// engine has no opinion about bells. That is also what makes them the demo's
// only worked example of the C++ game module beyond the one-line Rotator.

namespace RageV
{
	// Rung. Swings, and sounds.
	//
	// The swing is a decaying oscillation rather than a tween to a pose,
	// because a bell that stops dead reads as an animation being cut off. It
	// is arithmetic rather than an animation clip on purpose: a script moving
	// a transform is the thing being demonstrated.
	class Bell : public ScriptableEntity
	{
	public:
		RVShowInEditor
		float Swing = 0.34f;      // radians at the first pass
		RVShowInEditor
		float Decay = 1.9f;       // per second
		RVShowInEditor
		float Rate = 7.0f;        // radians per second of the oscillation

		// Named by the button's OnClick, via the RVCallable marker -- and the
		// whole point of the bound path: the handler lives on the thing being
		// acted on rather than on the control.
		RVCallable
		void Ring()
		{
			m_Age = 0.0f;
			m_Ringing = true;

			// The clip comes from this entity's own AudioSourceComponent
			// rather than from a field.
			//
			// Not a preference: a script field can be a bool, an int, a float,
			// a Vec3 or a string, and an asset handle is none of those. Naming
			// the clip on the component is also where a person would look for
			// it, and it means the bell can be re-voiced in the inspector
			// without touching code.
			PlaySource();
		}

		void OnTick(Timestep dt) override
		{
			if (!m_Ringing)
				return;

			m_Age += dt.GetSeconds();

			const float amplitude = Swing * Math::Exp(-Decay * m_Age);
			if (amplitude < 0.002f)
			{
				// Put it back exactly rather than leaving it wherever the
				// decay ran out. A prop that drifts a fraction of a degree
				// every time it is used is a prop that is visibly crooked
				// after a minute of somebody playing with it.
				GetComponent<TransformComponent>().Rotation.z = m_Rest;
				m_Ringing = false;
				return;
			}

			GetComponent<TransformComponent>().Rotation.z =
				m_Rest + amplitude * Math::Sin(Rate * m_Age);
		}

		void OnCreate() override
		{
			m_Rest = GetComponent<TransformComponent>().Rotation.z;
		}

	private:
		float m_Rest = 0.0f;
		float m_Age = 0.0f;
		bool m_Ringing = false;
	};

	// Struck. Sparks, and sounds.
	//
	// The sparks are a burst emitter that is otherwise off: `Emit` false with
	// a `Burst` count is the shape the component was built for, and asking for
	// one is a single write rather than a coroutine that turns emission on and
	// remembers to turn it off.
	class Anvil : public ScriptableEntity
	{
	public:
		RVCallable
		void Strike()
		{
			PlaySource();

			// The sparks are a child, so the anvil does not have to be an
			// emitter itself -- and so the burst is positioned at the face
			// being struck rather than at the object's origin.
			for (Entity child : GetChildren())
			{
				if (child.HasComponent<ParticleEmitterComponent>())
				{
					auto& sparks = child.GetComponent<ParticleEmitterComponent>();
					sparks.Emit = false;
					sparks.Burst = 34;
				}
			}
		}
	};

	// No registration block: the RVShowInEditor / RVCallable markers above are
	// the registration. rvgen reads them at build time and generates exactly
	// the RV_REGISTER_SCRIPT chain that used to end this file. Rotator.cpp
	// still uses the block, deliberately -- both styles work, so a project
	// migrates one file at a time or not at all.
}
