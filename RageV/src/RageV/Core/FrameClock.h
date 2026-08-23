#pragma once
#include <cstdint>

namespace RageV
{
	// Which frame, and which simulation step, the engine is in right now.
	//
	// **This exists so an input edge can be stamped rather than consumed.**
	//
	// An edge -- a key going down, a button completing a click -- is raised at
	// one point in the frame and read at several others. The obvious way to
	// make it last exactly long enough is for the reader to clear it, and that
	// is what RageV did: the fixed-step loop cleared the action edges, and
	// `Scene::OnFixedUpdateRuntime` cleared the click edges. It works for
	// whoever reads first and silently starves everyone after. A script asking
	// `WasActionPressed` from `OnFrame` saw a press only on a frame that
	// happened to run no step, which is a feature whose reliability is the
	// frame rate divided by sixty. See ENGINE-NOTES 7cn, and 7co for this.
	//
	// Godot's answer, and now this one: nobody clears anything. The edge
	// records *when* it happened on both clocks, and each reader asks whether
	// that was during its own current moment. Two readers cannot take it from
	// each other, and "a frame running three steps fires jump once" stops being
	// a rule someone has to defend and becomes what the comparison says.
	class FrameClock
	{
	public:
		// The frame being processed. Advanced once by `Application`, at the
		// top of the frame and before input is sampled -- so an edge raised by
		// `InputMap::Update` names the frame that is about to run, which is the
		// frame whose `OnFrame` should see it.
		static uint64_t Frame();

		// The simulation step running now or, outside one, the next that will
		// run. **Those are deliberately the same number.** It is what lets an
		// edge raised before the step loop name the step that should see it,
		// without knowing whether this frame will run three steps or none.
		static uint64_t Step();

		// Whether the caller is inside a fixed step, and therefore which of the
		// two clocks an edge query answers from. Script code does not ask this;
		// the edge queries ask it.
		static bool InFixedStep();

		// Once per frame, before input is sampled.
		static void BeginFrame();

		// One fixed step, for as long as it is in scope.
		//
		// **Nested scopes count once.** Two things run steps: `Application`'s
		// loop, which is the engine, and anything driving a scene directly --
		// scenetest, and any tool that steps a scene without a window.
		// `Scene::OnFixedUpdateRuntime` opens one of these too, so that a step
		// is a step however it was reached; when the loop already has one open,
		// the scene's is a no-op rather than a second step. Requiring the
		// caller to remember a separate call is how the old design failed, and
		// it would fail the same way again.
		class StepScope
		{
		public:
			StepScope();
			~StepScope();

			StepScope(const StepScope&) = delete;
			StepScope& operator=(const StepScope&) = delete;

		private:
			// False when a step was already open, which means this one neither
			// closes it nor advances anything.
			bool m_Owns = false;
		};

		// **There is deliberately no Reset.** A clock that can be wound back is
		// a clock an old stamp can collide with, and one put here for the tests
		// would be one the engine could call too. The tests reason relatively
		// instead -- this frame against the next, this step against the one
		// after -- which is all any of the claims actually need.
	};

	// When an edge happened, on both clocks, and whether that was now.
	//
	// Zero on both is an edge that has never been raised, which is why the
	// clocks start at one.
	struct InputEdge
	{
		uint64_t Frame = 0;
		uint64_t Step = 0;

		// Stamp it with the present moment. Called where the edge is detected
		// -- `InputMap::Update` for actions, `UI::UpdatePointer` for clicks --
		// which for both is once a frame, before anything reads it.
		void Raise()
		{
			Frame = FrameClock::Frame();
			Step = FrameClock::Step();
		}

		// Did this edge happen in the caller's current moment?
		//
		// Inside a fixed step that means *this* step and no other, so a frame
		// running three steps answers true to exactly one of them. Outside one
		// it means this frame, so `OnFrame` sees the press on the frame it was
		// made and not on the next. **Both readers get their own answer** --
		// neither can spend the other's.
		bool IsNow() const
		{
			return FrameClock::InFixedStep()
				 ? Step == FrameClock::Step()
				 : Frame == FrameClock::Frame();
		}

		// Never happened. For state that is being torn down rather than aged
		// out -- play mode stopping, a scene closing.
		void Clear()
		{
			Frame = 0;
			Step = 0;
		}
	};
}
