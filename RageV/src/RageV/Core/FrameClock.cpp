#include <rvpch.h>
#include "FrameClock.h"

namespace RageV
{
	namespace
	{
		// One is the first of everything, because zero is the stamp an edge
		// that has never been raised carries. Starting either clock at zero
		// would make every unraised edge read as having happened on the first
		// frame of the process.
		uint64_t s_Frame = 1;
		uint64_t s_Step = 1;
		bool s_InFixedStep = false;
	}

	// Main thread only, and unguarded on purpose. Both clocks are written by
	// the loop and read by whatever the loop is calling, all on the one thread;
	// a lock here would be a lock in every edge query in the engine.

	uint64_t FrameClock::Frame()
	{
		return s_Frame;
	}

	uint64_t FrameClock::Step()
	{
		return s_Step;
	}

	bool FrameClock::InFixedStep()
	{
		return s_InFixedStep;
	}

	void FrameClock::BeginFrame()
	{
		s_Frame++;
	}

	FrameClock::StepScope::StepScope()
	{
		// A step already open means this is the scene's scope inside the
		// loop's. The inner one borrows the number; it does not start a step.
		if (s_InFixedStep)
			return;

		m_Owns = true;
		s_InFixedStep = true;
	}

	FrameClock::StepScope::~StepScope()
	{
		if (!m_Owns)
			return;

		s_InFixedStep = false;

		// After, not before. A step's own number has to be stable for as long
		// as it runs, or an edge stamped for it would stop matching partway
		// through the step it belongs to.
		s_Step++;
	}
}
