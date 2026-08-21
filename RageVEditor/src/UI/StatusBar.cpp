#include "StatusBar.h"

#include "EditorTheme.h"
#include "imgui.h"

namespace RageV::EditorUI
{
	namespace
	{
		// How long a message stays at full strength, and how long it takes to
		// fade to the resting colour afterwards. It never disappears: a line
		// that clears itself takes the answer to "what did it just say" with
		// it, and that question is usually asked a few seconds late.
		constexpr float kFullSeconds = 6.0f;
		constexpr float kFadeSeconds = 3.0f;

		ImVec4 ColorFor(StatusBar::Kind kind)
		{
			const EditorTheme::Palette& palette = EditorTheme::Colors();
			switch (kind)
			{
				case StatusBar::Kind::Working: return palette.Accent;
				case StatusBar::Kind::Warning: return palette.Warning;
				case StatusBar::Kind::Error:   return palette.Danger;
				case StatusBar::Kind::Info:
				default:                       return palette.TextPrimary;
			}
		}

		const char* GlyphFor(StatusBar::Kind kind)
		{
			// Text rather than an icon font: the bar has to read the same in
			// a screenshot pasted into a bug report as it does on screen, and
			// half the people who will see one do not have the atlas.
			switch (kind)
			{
				case StatusBar::Kind::Working: return "...";
				case StatusBar::Kind::Warning: return "!";
				case StatusBar::Kind::Error:   return "x";
				case StatusBar::Kind::Info:
				default:                       return "-";
			}
		}
	}

	float StatusBar::Height()
	{
		// One line of text and a little air. Asked for rather than assumed by
		// the caller, so reserving the space and drawing into it cannot drift
		// apart -- which shows as a bar that clips its own descenders.
		return ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f + 2.0f;
	}

	void StatusBar::Post(Kind kind, std::string message, std::string detail)
	{
		// Identical consecutive messages collapse rather than stacking. A
		// watcher that sees the same file twice in a second is common and
		// saying so twice tells nobody anything.
		if (!m_History.empty() && m_History.back().What == kind &&
			m_History.back().Message == message && m_History.back().Detail == detail)
		{
			m_Age = 0.0f;
			return;
		}

		m_History.push_back({ kind, std::move(message), std::move(detail) });
		while (m_History.size() > kHistory)
			m_History.pop_front();

		m_Age = 0.0f;
	}

	void StatusBar::Draw()
	{
		const EditorTheme::Palette& palette = EditorTheme::Colors();
		const float height = Height();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x;

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
							ImGui::GetColorU32(palette.BgSurface));
		// A hairline along the top, so the bar reads as a different surface
		// from the panel above rather than as part of it.
		draw->AddLine(origin, ImVec2(origin.x + width, origin.y),
					  ImGui::GetColorU32(palette.Line));

		ImGui::SetCursorScreenPos(ImVec2(origin.x + ImGui::GetStyle().FramePadding.x * 2.0f,
										 origin.y + ImGui::GetStyle().FramePadding.y + 1.0f));

		if (!m_History.empty())
		{
			const Entry& entry = m_History.back();

			// Faded toward the resting text colour rather than toward nothing.
			// Fading to transparent would leave the newest line invisible over
			// a surface it is supposed to be legible on.
			const float over = m_Age - kFullSeconds;
			const float t = over <= 0.0f ? 0.0f
										 : (over > kFadeSeconds ? 1.0f : over / kFadeSeconds);
			const ImVec4 hot = ColorFor(entry.What);
			const ImVec4 rest = palette.TextSecondary;
			const ImVec4 color(hot.x + (rest.x - hot.x) * t,
							   hot.y + (rest.y - hot.y) * t,
							   hot.z + (rest.z - hot.z) * t,
							   hot.w + (rest.w - hot.w) * t);

			ImGui::TextColored(color, "%s  %s", GlyphFor(entry.What), entry.Message.c_str());

			if (!entry.Detail.empty())
			{
				ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
				ImGui::TextColored(palette.TextSecondary, "%s", entry.Detail.c_str());
			}

			// The last few, for the case the bar is the only place something
			// was said and it has already been replaced.
			if (ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + width, origin.y + height)) &&
				m_History.size() > 1)
			{
				ImGui::BeginTooltip();
				for (auto it = m_History.rbegin(); it != m_History.rend(); ++it)
				{
					ImGui::TextColored(ColorFor(it->What), "%s  %s",
									   GlyphFor(it->What), it->Message.c_str());
					if (!it->Detail.empty())
					{
						ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
						ImGui::TextColored(palette.TextSecondary, "%s", it->Detail.c_str());
					}
				}
				ImGui::EndTooltip();
			}
		}
		else
		{
			ImGui::TextColored(palette.TextDisabled, "Ready");
		}

		if (!m_Standing.empty())
		{
			const float text = ImGui::CalcTextSize(m_Standing.c_str()).x;
			const float right = origin.x + width - text -
								ImGui::GetStyle().FramePadding.x * 2.0f;
			// Only when there is room. Overlapping the message with the
			// standing text would lose the half that is actually news.
			if (right > ImGui::GetCursorScreenPos().x + ImGui::GetStyle().ItemSpacing.x)
			{
				ImGui::SetCursorScreenPos(
					ImVec2(right, origin.y + ImGui::GetStyle().FramePadding.y + 1.0f));
				ImGui::TextColored(palette.TextDisabled, "%s", m_Standing.c_str());
			}
		}

		ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
	}
}
