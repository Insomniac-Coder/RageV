#pragma once

// The screen shown while a project loads.
//
// In the engine rather than the editor, because a packaged game boots with
// the same delay for the same reasons -- it opens the same project, scans the
// same registry and uploads the same textures -- and the runtime is not a
// lesser citizen here.
//
// That placement costs one thing: `EditorTheme` lives in the editor and
// cannot be reached from here, so this carries a handful of colours of its
// own -- the defaults below, which are the dark look and are what a packaged
// game shows. They are deliberately not a second theme system: the *editor*
// pushes its current palette through SetPalette whenever its theme is
// applied, so a light-theme editor loads on a light screen, and the engine
// still needs to know nothing about themes.

#include "RageV/Core/Boot.h"
#include "RageV/Core/Core.h"

#include <cstdint>
#include <string>

namespace RageV
{
	class RV_API LoadingScreen
	{
	public:
		// Colours as 0xRRGGBB -- plain integers rather than an ImGui type,
		// because this header is public API and the third-party-types rule
		// applies to it like any other. Defaults are the dark look.
		struct Palette
		{
			uint32_t Background = 0x121214;
			uint32_t Title      = 0xECECF0;
			uint32_t Phase      = 0xB0B0B8;
			uint32_t Detail     = 0x787880;
			uint32_t Track      = 0x2C2C32;
			uint32_t Fill       = 0xE24A4A;
			// The editor's raised-panel colour and its hairline. Added when
			// the loading screen stopped being a bar on a void and started
			// using the same surface language as the panels it opens into.
			uint32_t Surface    = 0x18181C;
			uint32_t Line       = 0x2A2A32;

			// **The field the card stands on**, and the one part of these
			// screens that is decoration rather than information: a wash of
			// the accent rising out of the bottom edge of the window.
			//
			// Two values rather than one because the two themes need
			// genuinely different amounts of it. Red at a given alpha over
			// near-black darkens toward the accent and reads as a glow; the
			// same alpha over the light theme's pale grey lightens toward
			// pink and reads as a stain, so the light palette asks for less
			// of a deeper red. `WashAlpha` is 0..255.
			uint32_t Wash       = 0xE24A4A;
			uint32_t WashAlpha  = 0x26;

			// The same field over a *working* surface -- the editor, which
			// carries the wash so the handover from the loading screen is not
			// where the design stops.
			//
			// Its own number rather than a fraction of the one above, because
			// the two themes need different fractions: the light theme's
			// screen wash is deliberately heavy (a pale ground swallows red),
			// and that same weight across an editor full of panels is a pink
			// page rather than a warm bottom edge.
			uint32_t WashAlphaChrome = 0x18;
		};

		// The editor calls this from EditorTheme::Apply -- the one place a
		// theme change happens -- so the loading screen can never drift from
		// the chosen theme. Nothing else needs to call it: the defaults are
		// what the runtime and a packaged game show.
		static void SetPalette(const Palette& palette);

		// What SetPalette last set, so a screen shown beside this one -- the
		// startup screen is the only one -- follows the editor's theme without
		// a second copy of it.
		static const Palette& CurrentPalette();

		// One frame of it. Called between ImGuiLayer::Begin and End, so it is
		// an ordinary UI pass and needs nothing of its own.
		//
		// `title` is what is being loaded -- the project's name -- and is
		// shown above the bar. The status supplies everything else.
		static void Draw(const std::string& title, const Boot::Status& status);

		// The accent field alone, from `topY` to the bottom of the window.
		// Public because the startup screen stands in the same one -- the two
		// appear back to back and a second gradient would be a second thing to
		// keep in step.
		static void DrawWash(float topY);
	};
}
