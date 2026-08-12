#pragma once
#include "RageV/Renderer/RHI/RHIDevice.h"
#include "RageV/Asset/Font.h"
#include "RageV/UI/TextLayout.h"
#include "RageV/Math/Math.h"

#include <string>

namespace RageV
{
	// A rectangle in the UI's own space: pixels, origin top left, y down.
	//
	// Top left and y down because that is what a screen, an image and every UI
	// toolkit use, and because an artist given a mock-up measures from the top
	// left corner. The renderer converts once, at the projection.
	struct UIRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;

		float Right() const { return X + Width; }
		float Bottom() const { return Y + Height; }

		bool Contains(float x, float y) const
		{
			return x >= X && x < Right() && y >= Y && y < Bottom();
		}
	};

	// Draws the screen-space layer: panels, images and text, batched together.
	//
	// Deliberately not Renderer2D. That one is lit, world-space, and built
	// around scene quads; sharing it would mean bending a lit scene renderer
	// around an unlit screen-space one and getting neither.
	//
	// Deliberately not ImGui either. ImGui is the *editor's* UI. A game's UI is
	// authored in a scene, serialized, driven by scripts and has to run with no
	// editor present -- see ENGINE-NOTES 7d.
	//
	// **Sprites and glyphs share one pipeline.** A UI interleaves them
	// constantly and z order decides what comes first, so two pipelines would
	// mean a batch break at every label. The quad carries a mode instead; see
	// ui.rvshader.
	class UIRenderer
	{
	public:
		static void Init(RHI::RHIDevice& device);
		static void Shutdown();

		// False when the shader did not compile.
		static bool IsReady();

		static void SetTargetFormats(RHI::Format color, RHI::Format depth);

		// Resets the per-frame batch pool. Called by Renderer::BeginFrame.
		static void BeginFrame();

		// Opens a layer of the given size in pixels. Everything until End is
		// drawn in that space.
		static void Begin(uint32_t width, uint32_t height);
		static void End();

		// A solid rectangle -- a panel, a bar, a letterbox.
		static void DrawRect(const UIRect& rect, const Vec4& color);

		// An image, tinted. `uv` selects a region of it, which is how a sprite
		// sheet and a nine-slice will both work.
		static void DrawImage(const UIRect& rect, const RHI::Ref<RHI::RHITexture>& texture,
							  const Vec4& tint = Vec4(1.0f, 1.0f, 1.0f, 1.0f),
							  const UIRect& uv = { 0.0f, 0.0f, 1.0f, 1.0f });

		// Text, with the **top left of the block** at `position` -- not the
		// baseline. A UI positions text in a box, so a box-relative origin is
		// what every caller would otherwise work out for itself.
		//
		// Takes the font and its atlas rather than a handle: the renderer does
		// not reach into the asset manager, so it stays usable from a test with
		// a font assembled by hand.
		//
		// `Size` is the em size in pixels. Below the atlas's own
		// SmallestSharpSize the antialiasing degrades -- that is a property of
		// the baked atlas rather than of this call, and rvfont prints the
		// number when it bakes.
		static void DrawText(const std::string& text, const Font& font,
							 const RHI::Ref<RHI::RHITexture>& atlas,
							 const Vec2& position, const UI::TextStyle& style,
							 const Vec4& color);

		// One line, left aligned, unwrapped -- the common case.
		static void DrawText(const std::string& text, const Font& font,
							 const RHI::Ref<RHI::RHITexture>& atlas,
							 const Vec2& position, float size, const Vec4& color);

		// --- text in the world -----------------------------------------------
		//
		// A nameplate over a character, damage numbers, a number painted on a
		// door. **The same shader, the same atlas and the same layout** -- what
		// differs is the matrix and a depth test.
		//
		// It reuses the shader because `screenPxRange` is measured from
		// screen-space derivatives rather than passed in. A CPU-side value
		// would be right for a HUD and wrong for every pixel of a sign under a
		// perspective divide; see ui.rvshader.

		// The formats of the HDR target this draws into. Separate from
		// SetTargetFormats because the two layers land in different places: the
		// screen layer writes the finished LDR image, this one writes the scene
		// before tone mapping, so a glowing label is possible here and not
		// there.
		static void SetWorldTargetFormats(RHI::Format color, RHI::Format depth);

		// Opens a world-space layer. Quads are depth-*tested* against the scene
		// and do not write depth, so geometry occludes the text and the glyphs
		// of one string do not occlude each other.
		static void BeginWorld(const Mat4& viewProjection);

		// Text on the plane `right` x `up`, with the **top left of the block**
		// at `origin` -- the same corner the screen version positions from.
		//
		// The axes are given rather than derived, and that is the whole
		// billboarding policy: pass the camera's right and up for text that
		// faces the viewer, the entity's for text lying in its own plane. The
		// renderer does not need to know which was meant, and a caller that
		// wants an upright billboard passes the camera's right with world up.
		//
		// `Size` in TextStyle is the em height in **world units**, not pixels.
		static void DrawWorldText(const std::string& text, const Font& font,
								  const RHI::Ref<RHI::RHITexture>& atlas,
								  const Vec3& origin, const Vec3& right, const Vec3& up,
								  const UI::TextStyle& style, const Vec4& color);

		static uint32_t GetDrawCallCount();
		static uint32_t GetQuadCount();

		// Pixels to clip space, for the layer size given to Begin.
		//
		// Exposed because it is the one piece of this with a backend
		// difference in it, and a matrix that is upside down on one API is
		// exactly the kind of thing that is easier to assert than to notice.
		static Mat4 BuildProjection(uint32_t width, uint32_t height);
	};
}
