#include <rvpch.h>
#include "Canvas.h"
#include "TextLayout.h"

#include "RageV/Scene/Scene.h"
#include "RageV/Scene/Entity.h"
#include "RageV/Scene/Components.h"
#include "RageV/Asset/AssetManager.h"

#include <algorithm>
#include <cmath>

namespace RageV::UI
{
	float CanvasScale(const UICanvasComponent& canvas, float screenWidth, float screenHeight)
	{
		if (canvas.ScaleMode == CanvasScaleMode::ConstantPixels)
			return 1.0f;

		const float referenceWidth = Math::Max(canvas.ReferenceResolution.x, 1.0f);
		const float referenceHeight = Math::Max(canvas.ReferenceResolution.y, 1.0f);

		const float byWidth = Math::Max(screenWidth, 1.0f) / referenceWidth;
		const float byHeight = Math::Max(screenHeight, 1.0f) / referenceHeight;

		const float match = Math::Clamp(canvas.MatchWidthOrHeight, 0.0f, 1.0f);

		// Blended in log space -- a geometric mean rather than an arithmetic
		// one. That is what makes the halfway setting behave symmetrically: a
		// window twice as wide and a window half as wide should scale by
		// reciprocal amounts, and averaging the ratios directly does not.
		return std::exp(std::log(byWidth) * (1.0f - match) + std::log(byHeight) * match);
	}

	UIRect CanvasRect(const UICanvasComponent& canvas, float screenWidth, float screenHeight)
	{
		const float scale = Math::Max(CanvasScale(canvas, screenWidth, screenHeight), 1e-6f);
		return UIRect{ 0.0f, 0.0f, screenWidth / scale, screenHeight / scale };
	}

	UIRect ResolveRect(const RectAnchors& anchors, const UIRect& parent)
	{
		const float left   = parent.X + anchors.AnchorMin.x * parent.Width  + anchors.OffsetMin.x;
		const float top    = parent.Y + anchors.AnchorMin.y * parent.Height + anchors.OffsetMin.y;
		const float right  = parent.X + anchors.AnchorMax.x * parent.Width  + anchors.OffsetMax.x;
		const float bottom = parent.Y + anchors.AnchorMax.y * parent.Height + anchors.OffsetMax.y;

		// Negative extents are allowed through rather than clamped. A rectangle
		// whose right edge is left of its left edge is somebody's offsets being
		// wrong, and it should look wrong -- clamping to zero would make it
		// vanish instead, which is much harder to trace back to the number that
		// caused it.
		return UIRect{ left, top, right - left, bottom - top };
	}

	namespace
	{
		RectAnchors AnchorsOf(const UIRectComponent& rect)
		{
			RectAnchors anchors;
			anchors.AnchorMin = rect.AnchorMin;
			anchors.AnchorMax = rect.AnchorMax;
			anchors.OffsetMin = rect.OffsetMin;
			anchors.OffsetMax = rect.OffsetMax;
			return anchors;
		}

		// Depth first, parents before children, which is both the order a
		// rectangle needs its parent resolved in and the order that makes
		// hierarchy position a sensible tiebreak for equal sort orders.
		void Walk(Scene& scene, Entity entity, const UIRect& parentRect, float scale,
				  uint32_t& sequence, std::vector<ResolvedElement>& out)
		{
			if (!entity || !entity.HasComponent<UIRectComponent>())
				return;

			const UIRectComponent& rect = entity.GetComponent<UIRectComponent>();
			const UIRect resolved = ResolveRect(AnchorsOf(rect), parentRect);

			// An invisible element still resolves, because its children are
			// positioned against it. It is simply not emitted -- hiding a panel
			// must not move the label on it.
			if (rect.Visible)
			{
				ResolvedElement element;
				element.Entity = (uint64_t)entity.GetUUID();
				element.Rect = resolved;
				element.Scale = scale;
				element.SortOrder = rect.SortOrder;
				element.Sequence = sequence++;
				out.push_back(element);
			}
			else
			{
				sequence++;
			}

			// A copy, not a reference: resolving a child can touch the scene's
			// storage, and the list would be the thing that moved.
			const std::vector<UUID> children = scene.GetChildren(entity);
			for (UUID child : children)
				Walk(scene, scene.GetEntityByUUID(child), resolved, scale, sequence, out);
		}
	}

	std::vector<ResolvedElement> ResolveScene(Scene& scene, float screenWidth, float screenHeight)
	{
		std::vector<ResolvedElement> out;

		struct CanvasEntry { Entity Root; int32_t SortOrder; };
		std::vector<CanvasEntry> canvases;

		auto view = scene.GetRegistry().view<UICanvasComponent>();
		for (auto handle : view)
		{
			Entity entity{ handle, &scene };
			canvases.push_back({ entity, entity.GetComponent<UICanvasComponent>().SortOrder });
		}

		// Between canvases, sort order decides; within one, the walk does.
		std::stable_sort(canvases.begin(), canvases.end(),
						 [](const CanvasEntry& a, const CanvasEntry& b)
						 { return a.SortOrder < b.SortOrder; });

		for (const CanvasEntry& entry : canvases)
		{
			const UICanvasComponent& canvas = entry.Root.GetComponent<UICanvasComponent>();
			const UIRect rect = CanvasRect(canvas, screenWidth, screenHeight);
			const float scale = CanvasScale(canvas, screenWidth, screenHeight);

			const size_t first = out.size();
			uint32_t sequence = 0;

			// The canvas entity is the *space*, not an element: its children are
			// laid out against the screen. Giving it a rectangle of its own
			// would mean every UI needed two entities before it could show
			// anything.
			const std::vector<UUID> children = scene.GetChildren(entry.Root);
			for (UUID child : children)
				Walk(scene, scene.GetEntityByUUID(child), rect, scale, sequence, out);

			// Within one canvas only. Sorting across canvases here would undo
			// the ordering above.
			std::stable_sort(out.begin() + first, out.end(),
							 [](const ResolvedElement& a, const ResolvedElement& b)
							 {
								 if (a.SortOrder != b.SortOrder)
									 return a.SortOrder < b.SortOrder;
								 return a.Sequence < b.Sequence;
							 });
		}

		return out;
	}

	void DrawScene(Scene& scene, uint32_t screenWidth, uint32_t screenHeight)
	{
		if (!UIRenderer::IsReady() || screenWidth == 0 || screenHeight == 0)
			return;

		const std::vector<ResolvedElement> elements =
			ResolveScene(scene, (float)screenWidth, (float)screenHeight);

		if (elements.empty())
			return;

		UIRenderer::Begin(screenWidth, screenHeight);

		for (const ResolvedElement& element : elements)
		{
			Entity entity = scene.GetEntityByUUID(UUID(element.Entity));
			if (!entity)
				continue;

			// Canvas units to pixels, once, here. Everything above this line is
			// resolution independent and testable; everything below is drawing.
			const float scale = element.Scale;
			const UIRect pixels{ element.Rect.X * scale, element.Rect.Y * scale,
								 element.Rect.Width * scale, element.Rect.Height * scale };

			if (entity.HasComponent<UIImageComponent>())
			{
				const UIImageComponent& image = entity.GetComponent<UIImageComponent>();
				RHI::Ref<RHI::RHITexture> texture =
					image.Texture.IsValid() ? Assets::Manager::GetTexture(image.Texture) : nullptr;

				if (texture)
					UIRenderer::DrawImage(pixels, texture, image.Color);
				else
					UIRenderer::DrawRect(pixels, image.Color);
			}

			if (entity.HasComponent<UITextComponent>())
			{
				const UITextComponent& text = entity.GetComponent<UITextComponent>();

				const Font* font = Assets::Manager::GetFont(text.Font);
				RHI::Ref<RHI::RHITexture> atlas = Assets::Manager::GetFontAtlas(text.Font);

				if (font && atlas)
				{
					TextStyle style;
					style.Size = text.Size * scale;
					style.LineSpacing = text.LineSpacing;
					style.WrapWidth = text.Wrap ? pixels.Width : 0.0f;

					switch (text.Align)
					{
					case UITextAlign::Center: style.Align = TextAlign::Center; break;
					case UITextAlign::Right:  style.Align = TextAlign::Right; break;
					default:                  style.Align = TextAlign::Left; break;
					}

					// Aligned inside the rectangle rather than against the
					// block's own width, which is what Build returns. Without
					// this, centring would centre the text on its longest line
					// and leave the block itself hard against the left edge.
					float x = pixels.X;
					if (text.Align != UITextAlign::Left)
					{
						const TextLayout measured = Build(text.Text, *font, style);
						const float slack = pixels.Width - measured.Width;
						x += text.Align == UITextAlign::Center ? slack * 0.5f : slack;
					}

					UIRenderer::DrawText(text.Text, *font, atlas, Vec2(x, pixels.Y),
										 style, text.Color);
				}
			}
		}

		UIRenderer::End();
	}
}
