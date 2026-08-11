#pragma once

#include "RageV/Asset/Curve.h"
#include "RageV/Asset/Asset.h"

namespace RageV::UI
{
	// The draggable editor for a curve asset, drawn inline under the field
	// that points at it.
	//
	// Inline rather than a panel of its own, because a ramp is only meaningful
	// beside the emitter it belongs to -- an editor in a separate window makes
	// you hold "which curve was I editing" in your head, which is exactly the
	// thing an inspector exists to avoid.
	//
	// Two shapes from one type, chosen by the curve's channel count: a graph
	// with draggable points for one channel, a gradient strip with draggable
	// stops for three. They are different pictures of the same keys, and the
	// editing rules are identical.
	class CurveEditor
	{
	public:
		// Returns true on the frame a key was moved, added or removed, which
		// is the caller's cue to write the file back. `handle` is used only to
		// scope ImGui ids, so two curves on one component do not share drag
		// state.
		static bool Draw(const char* id, Curve& curve, AssetHandle handle);

		// Height of the drawing area, so the caller can lay out around it.
		static float GetHeight(const Curve& curve);
	};
}
