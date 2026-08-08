#pragma once
#include "imgui.h"

namespace RageV::EditorTheme
{
	// Red on black, with one rule the whole palette follows:
	//
	//   Red means "you can act on this, or it is acting now".
	//
	// Selection, hover, active state, focus, checkmarks, drag grabs, the active
	// tab. Nothing else. Structure -- panels, frames, separators, headers,
	// backgrounds -- stays greyscale so the accent keeps its meaning. An accent
	// applied to decoration stops reading as a signal.

	namespace Color
	{
		// Greyscale structure, darkest to lightest.
		constexpr ImVec4 Black      = { 0.043f, 0.043f, 0.051f, 1.00f };  // window
		constexpr ImVec4 Surface    = { 0.071f, 0.071f, 0.082f, 1.00f };  // child / popup
		constexpr ImVec4 Raised     = { 0.102f, 0.102f, 0.118f, 1.00f };  // frame
		constexpr ImVec4 RaisedHigh = { 0.141f, 0.141f, 0.161f, 1.00f };  // frame hover
		constexpr ImVec4 Line       = { 0.180f, 0.180f, 0.204f, 1.00f };  // border / separator

		constexpr ImVec4 Text       = { 0.902f, 0.902f, 0.918f, 1.00f };
		constexpr ImVec4 TextDim    = { 0.478f, 0.478f, 0.522f, 1.00f };

		// The accent, and only where interaction lives.
		constexpr ImVec4 Accent       = { 0.855f, 0.169f, 0.192f, 1.00f };
		constexpr ImVec4 AccentHover  = { 0.949f, 0.267f, 0.290f, 1.00f };
		constexpr ImVec4 AccentActive = { 0.678f, 0.106f, 0.129f, 1.00f };
		// Same hue at low alpha, for selection fills that must not shout.
		constexpr ImVec4 AccentMuted  = { 0.855f, 0.169f, 0.192f, 0.28f };
		constexpr ImVec4 AccentFaint  = { 0.855f, 0.169f, 0.192f, 0.14f };

		// Axis colours for the transform widget. Red is spoken for by the
		// accent, so X borrows it deliberately -- it is the one place a
		// non-interactive red is unambiguous.
		constexpr ImVec4 AxisX = { 0.855f, 0.243f, 0.267f, 1.00f };
		constexpr ImVec4 AxisY = { 0.353f, 0.741f, 0.376f, 1.00f };
		constexpr ImVec4 AxisZ = { 0.290f, 0.522f, 0.898f, 1.00f };
	}

	void Apply();
}
