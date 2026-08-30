#include <rvpch.h>
#include "LoadingScreen.h"
#include "RageV/Core/Application.h"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace RageV
{
	namespace
	{
		// The defaults in the header are the dark look; the editor replaces
		// them with its theme's colours through SetPalette.
		LoadingScreen::Palette s_Palette;

		ImU32 ToImColor(uint32_t rgb)
		{
			return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
		}

		// The bar is a fixed fraction of the window rather than a fixed pixel
		// width, so it looks deliberate at 1280x720 and at 4K alike, and
		// bounded so it neither vanishes on a small window nor stretches into
		// a hairline across a wide one.
		constexpr float kBarWidthFraction = 0.34f;
		constexpr float kBarMinWidth = 240.0f;
		constexpr float kBarMaxWidth = 560.0f;

		void CenteredText(ImDrawList* draw, float centreX, float y, ImU32 colour,
						  float scale, const char* text)
		{
			if (!text || !*text)
				return;

			const float size = ImGui::GetFontSize() * scale;
			const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(
				size, FLT_MAX, 0.0f, text);

			draw->AddText(ImGui::GetFont(), size,
						  ImVec2(centreX - extent.x * 0.5f, y), colour, text);
		}

		// A name that would overrun the bar is cut rather than allowed to run
		// off the window. Ellipsised from the *front*, because these are
		// paths: "materials/soil_normal.png" says what is loading and
		// ".../assets/materials/soil" does not.
		std::string Fit(const std::string& text, float maxWidth, float scale)
		{
			if (text.empty())
				return text;

			const float size = ImGui::GetFontSize() * scale;
			ImFont* font = ImGui::GetFont();

			if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x <= maxWidth)
				return text;

			std::string clipped = text;
			while (clipped.size() > 1)
			{
				clipped.erase(0, 1);
				const std::string candidate = "..." + clipped;
				if (font->CalcTextSizeA(size, FLT_MAX, 0.0f, candidate.c_str()).x <= maxWidth)
					return candidate;
			}

			return clipped;
		}
	}

	void LoadingScreen::SetPalette(const Palette& palette)
	{
		s_Palette = palette;
	}

	const LoadingScreen::Palette& LoadingScreen::CurrentPalette()
	{
		return s_Palette;
	}

	namespace
	{
		void RightText(ImDrawList* draw, float rightX, float y, ImU32 colour,
					   float scale, const char* text)
		{
			if (!text || !*text)
				return;
			const float size = ImGui::GetFontSize() * scale;
			const ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
			draw->AddText(ImGui::GetFont(), size, ImVec2(rightX - extent.x, y), colour, text);
		}

		void LeftText(ImDrawList* draw, float x, float y, ImU32 colour,
					  float scale, const char* text)
		{
			if (!text || !*text)
				return;
			const float size = ImGui::GetFontSize() * scale;
			draw->AddText(ImGui::GetFont(), size, ImVec2(x, y), colour, text);
		}

		std::string Percent(float fraction)
		{
			const int whole = (int)(Math::Clamp(fraction, 0.0f, 1.0f) * 100.0f + 0.5f);
			return std::to_string(whole) + "%";
		}
	}

	void LoadingScreen::DrawWash(float topY)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 origin = viewport->Pos;
		const ImVec2 size = viewport->Size;
		if (size.x <= 0.0f || size.y <= 0.0f)
			return;

		// Transparent at the top, `WashAlpha` at the bottom edge of the
		// window. The colour is carried in both stops so the fade is in alpha
		// alone -- fading a colour toward transparent black instead puts a
		// grey band through the middle of it on every backend that blends in
		// straight alpha, which is all of them.
		const uint32_t rgb = s_Palette.Wash;
		const ImU32 clear = IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0);
		const ImU32 solid = IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF,
									 (int)(s_Palette.WashAlpha & 0xFF));

		ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(
			ImVec2(origin.x, topY),
			ImVec2(origin.x + size.x, origin.y + size.y),
			clear, clear, solid, solid);
	}

	// The card, on the field.
	//
	// **The editor's own surface language, not a bar on a void.** A raised
	// panel, the theme's hairline around it and the accent as a single rule
	// along its top edge -- the same three moves every panel in the editor
	// makes, so the screen a project opens on and the window it opens into
	// are recognisably one product. The wash underneath is the mark's idea:
	// a red field behind a dark stage.
	void LoadingScreen::Draw(const std::string& title, const Boot::Status& status)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 origin = viewport->Pos;
		const ImVec2 size = viewport->Size;
		if (size.x <= 0.0f || size.y <= 0.0f)
			return;

		const ImU32 background = ToImColor(s_Palette.Background);
		const ImU32 titleInk   = ToImColor(s_Palette.Title);
		const ImU32 phaseInk   = ToImColor(s_Palette.Phase);
		const ImU32 detailInk  = ToImColor(s_Palette.Detail);
		const ImU32 track      = ToImColor(s_Palette.Track);
		const ImU32 fill       = ToImColor(s_Palette.Fill);
		const ImU32 surface    = ToImColor(s_Palette.Surface);
		const ImU32 line       = ToImColor(s_Palette.Line);

		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
							background);

		// Bounded so the card looks deliberate at 1280x720 and at 4K alike,
		// which is the same reason the bar inside it is a fraction rather than
		// a pixel count.
		const float cardW = Math::Clamp(size.x * 0.42f, 420.0f, 620.0f);
		const float cardH = 168.0f;
		const ImVec2 tl(origin.x + (size.x - cardW) * 0.5f,
						origin.y + (size.y - cardH) * 0.5f);
		const ImVec2 br(tl.x + cardW, tl.y + cardH);

		// The wash starts level with the card's middle, so the card is the
		// thing standing in the field rather than floating above it.
		DrawWash(tl.y + cardH * 0.5f);

		draw->AddRectFilled(tl, br, surface, 10.0f);
		draw->AddRect(tl, br, line, 10.0f, 0, 1.0f);

		// Inset from the corners, so the rule reads as part of the card
		// rather than as a lid on it.
		draw->AddRectFilled(ImVec2(tl.x + 10.0f, tl.y),
							ImVec2(br.x - 10.0f, tl.y + 2.0f), fill, 1.0f);

		const float pad = 26.0f;
		LeftText(draw, tl.x + pad, tl.y + 30.0f, titleInk, 1.5f, title.c_str());
		RightText(draw, br.x - pad, tl.y + 36.0f, detailInk, 1.0f,
				  Percent(status.Fraction).c_str());

		const float barY = tl.y + 92.0f;
		const float barW = cardW - pad * 2.0f;
		draw->AddRectFilled(ImVec2(tl.x + pad, barY),
							ImVec2(tl.x + pad + barW, barY + 4.0f), track, 2.0f);

		const float filled = barW * Math::Clamp(status.Fraction, 0.0f, 1.0f);
		if (filled > 0.0f)
		{
			draw->AddRectFilled(ImVec2(tl.x + pad, barY),
								ImVec2(tl.x + pad + filled, barY + 4.0f), fill, 2.0f);
		}

		// The phase, then what it is doing. Two lines rather than one: the
		// phase holds still long enough to read, the detail changes per file,
		// and putting them together makes the whole line flicker.
		LeftText(draw, tl.x + pad, barY + 18.0f, phaseInk, 1.0f, status.Phase.c_str());
		const std::string detail = Fit(status.Detail, barW, 0.85f);
		LeftText(draw, tl.x + pad, barY + 40.0f, detailInk, 0.85f, detail.c_str());
	}
}
