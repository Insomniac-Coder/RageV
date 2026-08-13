#include <rvpch.h>
#include "LoadingScreen.h"
#include "RageV/Core/Application.h"

#include "imgui.h"

#include <algorithm>

namespace RageV
{
	namespace
	{
		// Near-black with a red accent: the editor's look, restated here
		// because the theme it comes from is not reachable from the engine.
		constexpr ImU32 kBackground = IM_COL32(18, 18, 20, 255);
		constexpr ImU32 kTitle      = IM_COL32(236, 236, 240, 255);
		constexpr ImU32 kPhase      = IM_COL32(176, 176, 184, 255);
		constexpr ImU32 kDetail     = IM_COL32(120, 120, 128, 255);
		constexpr ImU32 kTrack      = IM_COL32(44, 44, 50, 255);
		constexpr ImU32 kFill       = IM_COL32(226, 74, 74, 255);

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

	void LoadingScreen::Draw(const std::string& title, const Boot::Status& status)
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const ImVec2 origin = viewport->Pos;
		const ImVec2 size = viewport->Size;
		if (size.x <= 0.0f || size.y <= 0.0f)
			return;

		// The background draw list, not a window: there is nothing to
		// interact with, nothing to dock and nothing to focus, and a window
		// would bring a title bar, padding and a focus rule to suppress.
		ImDrawList* draw = ImGui::GetBackgroundDrawList();

		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
							kBackground);

		const float centreX = origin.x + size.x * 0.5f;
		const float barWidth = std::clamp(size.x * kBarWidthFraction,
										  kBarMinWidth, kBarMaxWidth);
		const float barLeft = centreX - barWidth * 0.5f;
		// Slightly above centre. Text hung under a bar that sits dead centre
		// reads as low; this puts the *group* on the optical centre.
		const float barTop = origin.y + size.y * 0.5f - 2.0f;
		const float barHeight = 3.0f;

		CenteredText(draw, centreX, barTop - 64.0f, kTitle, 1.6f, title.c_str());

		draw->AddRectFilled(ImVec2(barLeft, barTop),
							ImVec2(barLeft + barWidth, barTop + barHeight),
							kTrack, barHeight * 0.5f);

		const float filled = barWidth * std::clamp(status.Fraction, 0.0f, 1.0f);
		if (filled > 0.0f)
		{
			draw->AddRectFilled(ImVec2(barLeft, barTop),
								ImVec2(barLeft + filled, barTop + barHeight),
								kFill, barHeight * 0.5f);
		}

		// The phase, then what it is doing. Two lines rather than one: the
		// phase holds still long enough to read, the detail changes per file,
		// and putting them together makes the whole line flicker.
		CenteredText(draw, centreX, barTop + 20.0f, kPhase, 1.0f,
					 status.Phase.c_str());

		const std::string detail = Fit(status.Detail, barWidth, 0.85f);
		CenteredText(draw, centreX, barTop + 42.0f, kDetail, 0.85f, detail.c_str());
	}
}
