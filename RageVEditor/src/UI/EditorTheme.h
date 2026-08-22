#pragma once
#include "imgui.h"
#include <cstdint>

namespace RageV::EditorTheme
{
	// The editor's design system: two themes, one set of names.
	//
	// ---------------------------------------------------------------------
	// Where the values come from
	// ---------------------------------------------------------------------
	//
	// The application mark -- see tools/scripts/make_icon.py -- is three
	// colours and no fourth: a field at #0E0E12, a red at #E03030, a light at
	// #F2F2F6. Those three are not *inspired by* the palette below, they are
	// literally in it: the accent is the mark's red, and the two ends of each
	// theme's grey ramp are the mark's other two values. Which of them is the
	// ground and which is the ink is the whole difference between the themes.
	//
	// This is a correction as much as a design. Three surfaces claim to be
	// RageV -- the icon, the generated manual, and this editor -- and they
	// carried three different reds. The icon and the manual already agreed on
	// #E03030; the editor was the one that did not.
	//
	// The mark's red is a **pure** red: its green and blue channels are equal,
	// so the hue is exactly 0 and there is no blue in it. The accent this
	// replaces had more blue than green, which is a lot of why it read as soft
	// -- it was drifting toward pink. Every red here now holds G == B.
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
	//  - **The accent.** The mark's #E03030 on white reads pink and washed
	//    out, and white on it misses 4.5:1. The light theme gets the same hue
	//    darkened -- which is the value the manual's light stylesheet already
	//    uses. Same red, different luminance, same meaning.
	//  - **BgControl**, the surface of an input. In dark it sits *above* the
	//    panel; in light it sits *below* it. That is not an inconsistency --
	//    it is what a control you can type into looks like in each. The token
	//    is named for the role, never for being lighter.
	//  - **Danger.** It has to stay apart from the accent, because the script
	//    graph outlines a selected node in one and a broken node in the other
	//    and an error must win. Two reds cannot be told apart by hue, so it
	//    separates by luminance, **moving away from the accent toward the ink
	//    end of the ramp** -- lighter in dark, darker in light.
	//
	// **Near-black, and not black.** A panel is the mark's own field, and the
	// dockspace goes darker still so panels read as raised off it rather than
	// cut into it. Neither is #000, and that is deliberate: reading speed
	// drops measurably on pure-black themes, and a saturated red against #000
	// fringes badly enough to look out of focus. #0E0E12 is dark enough to be
	// the mark and far enough off black to avoid both.
	//
	// Every foreground/background pair the editor actually puts together is
	// measured against WCAG 2.2 AA -- 4.5:1 for body text, 3:1 for large text
	// and for the boundary or state of a control. `tools/scripts/
	// check_theme_contrast.py` prints the table, and falsify.py breaks a
	// colour to prove the table would catch it.

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
		// A label drawn *on* an accent fill. The mark's red is saturated
		// enough to be dark, so in the dark theme this is **pure black** --
		// #0E0E12 against it manages only 4.25:1, and a label needs 4.5.
		ImVec4 OnAccent;
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

	// Corners.
	//
	// There is no curve anywhere in the mark -- a chamfered tile, and a V of
	// straight-sided quads with mitred ends and a real point. A radius is the
	// one thing that would contradict it, so `Sharp` is what every framed
	// thing in the editor gets, and it is zero.
	//
	// `Chamfer` is the tile's cut, and it is a separate number because it is a
	// separate idea: a radius is a corner that curves, a chamfer is a corner
	// that is *gone*. **ImGui's style has only the first**, so this one cannot
	// be set globally -- it applies where the editor draws the shape itself.
	// See UI::ChamferedRect.
	//
	// **A fraction of the shorter side, which is the mark's own logic** -- the
	// tile's cut is 26% of a square. One number then gives the same shape at
	// every size: three pixels off a toolbar button, seven off a graph node,
	// and both of them larger by the same factor when the UI is scaled up. A
	// pixel count cannot do that, because the UI scale reaches the font and
	// the paddings and would never reach a constant in a header.
	//
	// A tenth rather than a quarter because a logo is looked at once at 512px
	// and a button is 30px and looked at all day: 26% of 30px is a corner
	// removed, not a corner cut.
	namespace Corner
	{
		constexpr float Sharp   = 0.0f;
		constexpr float Chamfer = 0.10f;
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
