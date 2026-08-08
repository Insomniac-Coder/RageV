#pragma once

namespace RageV
{
	// Turns real elapsed time into a whole number of fixed simulation steps,
	// plus how far the renderer sits between the last one and the next.
	//
	// Extracted from the main loop so it can be tested. The interesting failure
	// modes are all off-by-one-ish and invisible at a glance: computing the
	// blend factor before consuming the accumulator instead of after, forgetting
	// the clamp so a stall spirals, or letting the factor reach 1.0 and render a
	// step that has not happened.
	//
	// See docs/ENGINE-NOTES.md section 1.
	struct FixedStep
	{
		float Timestep = 1.0f / 60.0f;

		// A frame longer than this is a stall, not time to simulate. Without
		// dropping the excess, a three-second hitch queues 180 steps, each
		// taking longer than a frame, and the process never catches up.
		float MaxFrameTime = 0.25f;

		float Accumulator = 0.0f;

		// [0, 1). Never reaches 1: at exactly one step's worth of accumulated
		// time the loop runs another step and the remainder drops back to zero.
		float Alpha = 0.0f;

		// How many steps to run this frame. Zero is normal above the simulation
		// rate, which is precisely why rendering has to interpolate rather than
		// read the simulation directly.
		int Advance(float frameTime)
		{
			if (frameTime < 0.0f)
				frameTime = 0.0f;
			if (frameTime > MaxFrameTime)
				frameTime = MaxFrameTime;

			Accumulator += frameTime;

			int steps = 0;
			while (Accumulator >= Timestep)
			{
				Accumulator -= Timestep;
				steps++;
			}

			// After consuming, not before: the leftover is the distance past
			// the last completed step.
			Alpha = Timestep > 0.0f ? Accumulator / Timestep : 0.0f;
			return steps;
		}

		// Discards pending time. For a window that was minimised, or a scene
		// that has just been loaded -- otherwise the first frame after spends
		// the whole gap in one burst.
		void Reset()
		{
			Accumulator = 0.0f;
			Alpha = 0.0f;
		}
	};
}
