#pragma once

// The screen shown while a project loads.
//
// In the engine rather than the editor, because a packaged game boots with
// the same delay for the same reasons -- it opens the same project, scans the
// same registry and uploads the same textures -- and the runtime is not a
// lesser citizen here.
//
// That placement costs one thing: `EditorTheme` lives in the editor and
// cannot be reached from here, so this carries its own handful of colours.
// They are deliberately not a second theme system. Loading is over in
// seconds, nothing on it is interactive, and the alternative -- hoisting the
// whole palette into the engine so that two windows can agree about a
// progress bar -- would be a much larger change for a much smaller reason.

#include "RageV/Core/Boot.h"
#include "RageV/Core/Core.h"

#include <string>

namespace RageV
{
	class RV_API LoadingScreen
	{
	public:
		// One frame of it. Called between ImGuiLayer::Begin and End, so it is
		// an ordinary UI pass and needs nothing of its own.
		//
		// `title` is what is being loaded -- the project's name -- and is
		// shown above the bar. The status supplies everything else.
		static void Draw(const std::string& title, const Boot::Status& status);
	};
}
