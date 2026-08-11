#pragma once

#include "RageV/Renderer/UIRenderer.h"
#include "RageV/Math/Math.h"

#include <cstdint>
#include <vector>

namespace RageV
{
	class Scene;
	struct UICanvasComponent;
}

namespace RageV::UI
{
	// Resolving a canvas: anchors and offsets into rectangles.
	//
	// **Pure arithmetic, and kept that way deliberately.** Resolution
	// independence is the whole point of the anchor model, and it is exactly
	// what cannot be checked by looking at one screenshot -- a layout is either
	// right at every window size or it is a bug somebody hits on a display you
	// do not own. Everything down to ResolveScene runs with no GPU and no
	// window, so the suite can ask about 4K and an ultrawide in the same
	// millisecond.

	// What one rectangle needs to resolve itself, without dragging the
	// component header into every caller.
	struct RectAnchors
	{
		Vec2 AnchorMin{ 0.0f, 0.0f };
		Vec2 AnchorMax{ 1.0f, 1.0f };
		Vec2 OffsetMin{ 0.0f, 0.0f };
		Vec2 OffsetMax{ 0.0f, 0.0f };
	};

	// How many screen pixels one canvas unit is worth.
	//
	// Blended in log space between the two axes, which is what stops a layout
	// jumping as a window crosses the reference's aspect ratio. Matching one
	// axis alone is the classic mistake: a HUD that follows the width grows
	// when a window gets wider *and* shorter, and walks off the bottom edge.
	float CanvasScale(const UICanvasComponent& canvas, float screenWidth, float screenHeight);

	// The canvas's own rectangle, in canvas units: origin at its top left, and
	// the screen size divided by the scale above -- so a layout authored
	// against the reference resolution lands where it was drawn.
	UIRect CanvasRect(const UICanvasComponent& canvas, float screenWidth, float screenHeight);

	// One rectangle against its parent's. The formula is on UIRectComponent.
	UIRect ResolveRect(const RectAnchors& anchors, const UIRect& parent);

	// One element, resolved and ready to draw.
	struct ResolvedElement
	{
		uint64_t Entity = 0;      // the entity's UUID
		UIRect Rect;
		float Scale = 1.0f;       // canvas units to screen pixels
		int32_t SortOrder = 0;

		// Where it came out of the walk, so equal sort orders keep hierarchy
		// order rather than whatever the sort felt like doing.
		uint32_t Sequence = 0;
	};

	// Walks every canvas in the scene and resolves its tree.
	//
	// Rectangles come back in **canvas units**, with the scale that converts
	// them to pixels alongside. Keeping those apart is what lets a layout be
	// asserted without a window.
	//
	// Sorted by canvas, then SortOrder, then hierarchy order.
	std::vector<ResolvedElement> ResolveScene(Scene& scene, float screenWidth,
											  float screenHeight);

	// Resolves and draws. The half that needs the renderer and the assets.
	void DrawScene(Scene& scene, uint32_t screenWidth, uint32_t screenHeight);
}
