#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace RageV
{
	// Named intents over raw device state.
	//
	// Game code asks "is Jump down", never "is space down". That indirection is
	// what lets a keyboard and a gamepad drive the same behaviour, lets the
	// player rebind, and lets a menu and gameplay interpret the same key
	// differently -- all of which are rewrites if the game reads keycodes.
	//
	// Shaped after what Unity's Input System and Unreal's Enhanced Input
	// converged on: actions, bindings, axes, contexts. See
	// docs/ENGINE-NOTES.md section 7.

	enum class MouseAxis
	{
		X,      // horizontal movement since the last frame, in pixels
		Y,
		Wheel,
	};

	class InputMap
	{
	public:
		static void Init();
		static void Shutdown();

		// Samples devices and works out this frame's edges. Called once per
		// frame by Application, before any simulation step -- and after
		// FrameClock::BeginFrame, because that is what the edges are stamped
		// with.
		static void Update();

		// The wheel arrives as an event and has no queryable state, unlike
		// every other input, so it has to be fed in.
		static void OnScroll(float delta);

		// --- contexts --------------------------------------------------------
		// A context is a named set of bindings that can be switched off as a
		// unit: gameplay versus menu versus vehicle. Bindings in a disabled
		// context are not sampled at all.
		static void SetContextEnabled(const std::string& context, bool enabled);
		static bool IsContextEnabled(const std::string& context);

		// --- bindings --------------------------------------------------------
		// Several bindings may drive one action. The action is down if any of
		// them is, so adding gamepad support later is additive.
		static void BindKey(const std::string& context, const std::string& action, int keycode);
		static void BindMouseButton(const std::string& context, const std::string& action, int button);

		// Two keys forming a -1..+1 axis, the way WASD works.
		static void BindKeyAxis(const std::string& context, const std::string& axis,
								int positiveKey, int negativeKey, float scale = 1.0f);
		static void BindMouseAxis(const std::string& context, const std::string& axis,
								  MouseAxis which, float scale = 1.0f);

		static void ClearBindings();
		// Movement, look and a few verbs, so a new project can be driven
		// without configuring anything first.
		static void LoadDefaults();

		// --- queries ---------------------------------------------------------
		static bool IsActionDown(const std::string& action);

		// The edges, answered against the caller's own clock: inside a fixed
		// step, whether the edge belongs to *this* step; outside one, whether
		// it belongs to this frame. So a frame running three steps fires Jump
		// once, a frame running none does not lose the press, and OnFrame and
		// OnTick each get their own answer instead of racing for one flag.
		// Nothing is consumed. See Core/FrameClock.h and ENGINE-NOTES 7co.
		static bool WasActionPressed(const std::string& action);
		static bool WasActionReleased(const std::string& action);

		// Summed across every binding, so a stick and WASD can both drive one
		// axis. Key axes are -1..1; mouse axes are in pixels or wheel notches.
		static float GetAxis(const std::string& axis);

		// For an input-settings panel, and for round-tripping to project
		// settings once those exist.
		static std::vector<std::string> GetActionNames();
		static std::vector<std::string> GetAxisNames();
	};
}
