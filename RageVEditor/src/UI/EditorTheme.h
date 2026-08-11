#pragma once
#include "imgui.h"
#include <cstdint>

namespace RageV::EditorTheme
{
	// The editor's design system: two themes, one set of names.
	//
	// ---------------------------------------------------------------------
	// The one rule the palette follows
	// ---------------------------------------------------------------------
	//
	//   Red means "you can act on this, or it is acting now".
	//
	// Selection, hover, active state, focus, check marks, drag grabs, the
	// active tab. Nothing else. Structure -- panels, frames, separators,
	// headers, backgrounds -- stays greyscale so the accent keeps its meaning.
	// An accent applied to decoration stops reading as a signal.
	//
	// ---------------------------------------------------------------------
	// Why tokens rather than colours
	// ---------------------------------------------------------------------
	//
	// Every name below is a *role*, not a value: `TextSecondary` means "the
	// less important of two labels", and what it resolves to depends on the
	// theme. Call sites therefore never mention a colour, which is what makes
	// a second theme a data change instead of an audit of every panel.
	//
	// ---------------------------------------------------------------------
	// Why the light theme is not the dark one inverted
	// ---------------------------------------------------------------------
	//
	// Inverting is the classic mistake in both directions. Two places it shows:
	//
	//  - **The accent.** A red tuned to sit on charcoal reads pink and washed
	//    out on white, and white text on it fails contrast. The light theme
	//    gets its own darker, more saturated red. Same meaning, different value.
	//  - **BgControl**, the surface of an input. In dark it sits *above* the
	//    panel; in light it sits *below* it. That is not an inconsistency --
	//    it is what a control you can type into looks like in each. The token
	//    is named for the role, never for being lighter.
	//
	// The dark theme's deepest surface is deliberately **not** near-black.
	// Reading speed drops measurably on pure-black themes, and a saturated red
	// against #000 fringes badly enough to look out of focus.
	//
	// Every foreground/background pair the editor actually puts together is
	// measured against WCAG 2.2 AA -- 4.5:1 for body text, 3:1 for large text
	// and for the boundary or state of a control. `tools/scripts/
	// check_theme_contrast.py` prints the table; scenetest asserts the same
	// pairs so the header and the check cannot drift apart.

	enum class Theme : int32_t
	{
		Dark = 0,
		Light = 1,
	};

	// One theme's worth of values. Named by role; see the note above.
	struct Palette
	{
		// --- surfaces, in the order things stack -------------------------
		ImVec4 BgBase;       // the dock space, behind every panel
		ImVec4 BgSurface;    // a panel, a popup, a menu
		ImVec4 BgControl;    // an input, a frame, a button at rest
		ImVec4 BgHover;
		ImVec4 BgActive;

		ImVec4 Line;         // separators and quiet borders
		ImVec4 LineStrong;   // a border that has to be seen

		// --- type ---------------------------------------------------------
		ImVec4 TextPrimary;
		ImVec4 TextSecondary;
		ImVec4 TextDisabled;

		// --- the accent, and only where interaction lives -----------------
		ImVec4 Accent;
		ImVec4 AccentHover;
		ImVec4 AccentPressed;
		ImVec4 OnAccent;      // a label drawn *on* an accent fill
		ImVec4 AccentMuted;   // the accent at low alpha, for selection fills
		ImVec4 AccentFaint;   // fainter still, for a hovered row

		// --- meaning, not decoration --------------------------------------
		ImVec4 Success;
		ImVec4 Warning;
		ImVec4 Danger;

		// --- the transform widget ------------------------------------------
		// X borrows the accent hue deliberately: it is the one place a
		// non-interactive red is unambiguous, because it is next to a green
		// and a blue that mean the same kind of thing.
		ImVec4 AxisX;
		ImVec4 AxisY;
		ImVec4 AxisZ;
	};

	// Spacing, on a 4px grid, and radii.
	//
	// A grid is not neatness for its own sake: it is what stops a panel built
	// on Tuesday from being 3px out from one built on Monday, which is most of
	// what "assembled rather than designed" actually looks like.
	//
	// These are the *unscaled* values. Everything the editor draws is
	// multiplied by the UI scale, so read them through Metrics(), never as
	// literals.
	namespace Space
	{
		constexpr float Hair  = 2.0f;
		constexpr float Tight = 4.0f;
		constexpr float Snug  = 6.0f;
		constexpr float Base  = 8.0f;
		constexpr float Roomy = 12.0f;
		constexpr float Wide  = 16.0f;
		constexpr float Bay   = 24.0f;
	}

	// The type scale, as multipliers on the base font size.
	//
	// Multipliers rather than pixel sizes because the editor has a UI scale and
	// a DPI factor already folded into the base; a scale in pixels would be
	// right at 100% and wrong everywhere else.
	//
	// Four steps is deliberate. One size -- which is what this was -- means the
	// only tools left for hierarchy are colour and boxes, and a panel where a
	// title, a section heading, a label and a value all render identically is
	// read by position rather than by weight. Ten steps is the other failure:
	// nobody can tell 1.05x from 1.0x, so the extra steps are noise that has to
	// be maintained.
	namespace Type
	{
		// Hints, units, table headers, the small print under a control.
		constexpr float Caption = 0.84f;
		// Everything else.
		constexpr float Body    = 1.00f;
		// A panel's name, a modal's question.
		constexpr float Title   = 1.22f;
		// The one number a panel exists to show. Used sparingly -- if two
		// things on a page are Display, neither of them is.
		constexpr float Display = 1.55f;
	}

	namespace Radius
	{
		constexpr float Control = 5.0f;   // inputs, buttons, tabs
		constexpr float Panel   = 7.0f;   // windows, popups, child frames
		constexpr float Pill    = 99.0f;  // scrollbar grabs and other capsules
	}

	// The active theme's values. Call this; never name a colour at a call site.
	const Palette& Colors();

	Theme Current();

	// Applies a theme to the live ImGui style. Safe to call every frame,
	// though there is no reason to.
	void Apply(Theme theme);

	// Parses "dark" / "light"; anything else answers Dark. For the command
	// line and the settings file, which both speak text.
	Theme Parse(const char* name);
	const char* Name(Theme theme);
}
