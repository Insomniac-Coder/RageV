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
		};

		// The editor calls this from EditorTheme::Apply -- the one place a
		// theme change happens -- so the loading screen can never drift from
		// the chosen theme. Nothing else needs to call it: the defaults are
		// what the runtime and a packaged game show.
		static void SetPalette(const Palette& palette);

		// One frame of it. Called between ImGuiLayer::Begin and End, so it is
		// an ordinary UI pass and needs nothing of its own.
		//
		// `title` is what is being loaded -- the project's name -- and is
		// shown above the bar. The status supplies everything else.
		static void Draw(const std::string& title, const Boot::Status& status);
	};
}
