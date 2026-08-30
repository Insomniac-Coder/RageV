#pragma once

// The first screen the editor shows, before any project exists.
//
// **In the engine, next to LoadingScreen, and for a stronger version of the
// same reason.** The loading screen is here because a packaged game boots the
// same way; this is here because the alternative places it in the editor's
// layer -- and the editor's layer does not exist yet. Choosing a project is
// what happens *before* there is anything to attach a layer to: the asset
// registry has no root, the content browser has nothing to browse, and the
// scene has no assets to resolve. So Application drives it, between creating
// the window and booting into a project.
//
// It borrows LoadingScreen's palette rather than owning one. The two screens
// appear back to back -- pick a project, watch it load -- and a second set of
// colours would be a second thing to keep in step with the editor's theme.

#include "RageV/Core/Core.h"
#include "LoadingScreen.h"

namespace RageV
{
	class RV_API StartupScreen
	{
	public:
		enum class Choice
		{
			None,
			OpenProject,
			CreateProject,
		};

		// One frame of it, between ImGuiLayer::Begin and End like any other UI
		// pass. Returns what was clicked *this frame*, and None on every other
		// frame -- so the caller acts once and does not need to track edges.
		static Choice Draw();
	};
}
