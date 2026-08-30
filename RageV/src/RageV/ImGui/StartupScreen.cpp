#include <rvpch.h>
#include "StartupScreen.h"

#include "imgui.h"

namespace RageV
{
	namespace
	{
		ImU32 ToImColor(uint32_t rgb, float alpha = 1.0f)
		{
			return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF,
							(int)(alpha * 255.0f));
		}

		ImU32 Mix(uint32_t a, uint32_t b, float t)
		{
			const auto lerp = [&](int shift)
			{
				const float x = (float)((a >> shift) & 0xFF);
				const float y = (float)((b >> shift) & 0xFF);
				return (int)(x + (y - x) * t);
			};
			return IM_COL32(lerp(16), lerp(8), lerp(0), 255);
		}

		// Square, because the brief said square and because two equal squares
		// read as two equal choices -- which these are. A wide button beside a
		// narrow one implies a default before anything has been said.
		constexpr float kTileSize = 168.0f;
		constexpr float kTileGap = 28.0f;

		void CenteredText(ImDrawList* draw, float centreX, float y, ImU32 colour,
						  float scale, const char* text)
		{
			if (!text || !*text)
				return;

			const float size = ImGui::GetFontSize() * scale;
			const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
			draw->AddText(ImGui::GetFont(), size,
						  ImVec2(centreX - extent.x * 0.5f, y), colour, text);
		}

		// --- the icons ------------------------------------------------------
		//
		// Drawn rather than loaded. An icon here would have to come from
		// somewhere, and the only somewhere available before a project exists
		// is the engine's own asset folder -- which would make the startup
		// screen the one screen that fails when a staged build is incomplete.
		// Two shapes in a draw list cannot fail, scale to any DPI, and take
		// their colour from the theme like everything else on the screen.

		// A folder: a body with a raised tab on the left.
		void DrawFolder(ImDrawList* draw, ImVec2 centre, float extent, ImU32 colour)
		{
			const float w = extent;
			const float h = extent * 0.76f;
			const ImVec2 topLeft(centre.x - w * 0.5f, centre.y - h * 0.5f);

			// The tab, above the body's top edge on the left third.
			draw->AddRectFilled(ImVec2(topLeft.x, topLeft.y),
								ImVec2(topLeft.x + w * 0.42f, topLeft.y + h * 0.18f),
								colour, 3.0f);

			draw->AddRectFilled(ImVec2(topLeft.x, topLeft.y + h * 0.12f),
								ImVec2(topLeft.x + w, topLeft.y + h),
								colour, 5.0f);
		}

		// A plus, in the same weight as the folder's tab.
		void DrawPlus(ImDrawList* draw, ImVec2 centre, float extent, ImU32 colour)
		{
			const float arm = extent * 0.5f;
			const float thickness = extent * 0.16f;

			draw->AddRectFilled(ImVec2(centre.x - thickness * 0.5f, centre.y - arm),
								ImVec2(centre.x + thickness * 0.5f, centre.y + arm),
								colour, thickness * 0.5f);
			draw->AddRectFilled(ImVec2(centre.x - arm, centre.y - thickness * 0.5f),
								ImVec2(centre.x + arm, centre.y + thickness * 0.5f),
								colour, thickness * 0.5f);
		}

		// One tile. Returns true on the frame it is clicked.
		//
		// Hit-tested against the mouse directly rather than through an ImGui
		// button, because everything else on this screen is drawn into the
		// background list: mixing the two would put an invisible window over
		// the drawing and leave the hover states disagreeing about where the
		// tiles are.
		bool Tile(ImDrawList* draw, ImVec2 topLeft, const char* label,
				  void (*icon)(ImDrawList*, ImVec2, float, ImU32),
				  const LoadingScreen::Palette& palette)
		{
			const ImVec2 bottomRight(topLeft.x + kTileSize, topLeft.y + kTileSize);
			const ImVec2 mouse = ImGui::GetIO().MousePos;

			const bool hovered = mouse.x >= topLeft.x && mouse.x <= bottomRight.x
							  && mouse.y >= topLeft.y && mouse.y <= bottomRight.y;

			// **The loading card's three moves, at tile size.** Raised
			// surface, the theme's hairline, and the accent as a rule along
			// the top edge -- so the screen that asks for a project and the
			// screen that loads one are visibly the same object, and the
			// transition between them is a card replacing two cards rather
			// than one design replacing another.
			//
			// Hovering lifts the surface toward the accent and turns the rule
			// and the ink up with it, rather than merely brightening the
			// fill: on a panel this dark a brightness step alone is a couple
			// of levels and reads as nothing happening.
			const ImU32 fill = hovered ? Mix(palette.Surface, palette.Fill, 0.16f)
									   : ToImColor(palette.Surface);
			const ImU32 border = hovered ? ToImColor(palette.Fill)
										 : ToImColor(palette.Line);
			const ImU32 ink = hovered ? ToImColor(palette.Title)
									  : ToImColor(palette.Phase);

			draw->AddRectFilled(topLeft, bottomRight, fill, 10.0f);
			draw->AddRect(topLeft, bottomRight, border, 10.0f, 0, hovered ? 2.0f : 1.0f);

			// Inset from the corners, exactly as the card's is.
			draw->AddRectFilled(ImVec2(topLeft.x + 10.0f, topLeft.y),
								ImVec2(bottomRight.x - 10.0f, topLeft.y + 2.0f),
								ToImColor(palette.Fill), 1.0f);

			const ImVec2 centre(topLeft.x + kTileSize * 0.5f,
								topLeft.y + kTileSize * 0.42f);
			icon(draw, centre, kTileSize * 0.30f, ink);

			CenteredText(draw, topLeft.x + kTileSize * 0.5f,
						 topLeft.y + kTileSize * 0.70f, ink, 1.0f, label);

			return hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
		}
	}

	StartupScreen::Choice StartupScreen::Draw()
	{
		const LoadingScreen::Palette palette = LoadingScreen::CurrentPalette();

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 origin = viewport->Pos;
		const ImVec2 size = viewport->Size;
		if (size.x <= 0.0f || size.y <= 0.0f)
			return Choice::None;

		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
							ToImColor(palette.Background));

		const float centreX = origin.x + size.x * 0.5f;

		// The pair sits on the optical centre and the question above it, which
		// is the same arrangement the loading screen uses for its title and
		// bar -- the two screens follow one another and should not appear to
		// jump.
		const float tilesTop = origin.y + size.y * 0.5f - kTileSize * 0.5f + 18.0f;
		const float leftTile = centreX - kTileGap * 0.5f - kTileSize;

		CenteredText(draw, centreX, tilesTop - 62.0f, ToImColor(palette.Title),
					 1.6f, "What would you like to do");

		// The same field the loading screen stands in, starting level with the
		// middle of the tiles. These two screens run one after the other and
		// the transition between them should be the tiles being replaced by a
		// card, not the whole picture changing.
		LoadingScreen::DrawWash(tilesTop + kTileSize * 0.5f);

		Choice choice = Choice::None;

		if (Tile(draw, ImVec2(leftTile, tilesTop), "Open a Project",
				 &DrawFolder, palette))
		{
			choice = Choice::OpenProject;
		}

		if (Tile(draw, ImVec2(centreX + kTileGap * 0.5f, tilesTop), "Create New",
				 &DrawPlus, palette))
		{
			choice = Choice::CreateProject;
		}

		return choice;
	}
}
