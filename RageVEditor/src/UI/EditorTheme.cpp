#include "EditorTheme.h"
#include "RageV/ImGui/LoadingScreen.h"
#include <cstring>

namespace RageV::EditorTheme
{
	namespace
	{
		// The palettes are authored as hex because that is what every colour
		// picker and every designer speaks, and because the contrast script
		// that validates them reads the same literals.
		constexpr ImVec4 Hex(uint32_t rgb, float alpha = 1.0f)
		{
			return { ((rgb >> 16) & 0xFF) / 255.0f,
					 ((rgb >> 8) & 0xFF) / 255.0f,
					 (rgb & 0xFF) / 255.0f,
					 alpha };
		}

		ImVec4 Fade(const ImVec4& color, float alpha)
		{
			return { color.x, color.y, color.z, alpha };
		}

		// ---- dark: red on charcoal --------------------------------------
		//
		// Ratios against BgSurface, measured: primary text 14.6:1, secondary
		// 7.1:1, accent 4.6:1. See tools/scripts/check_theme_contrast.py.
		constexpr Palette kDark = {
			.BgBase = Hex(0x111116),
			.BgSurface = Hex(0x17171E),
			.BgControl = Hex(0x22222B),
			.BgHover = Hex(0x2C2C36),
			.BgActive = Hex(0x363642),
			.Line = Hex(0x30303B),
			.LineStrong = Hex(0x4E4E5C),
			.TextPrimary = Hex(0xE8E8ED),
			.TextSecondary = Hex(0xA2A2B0),
			.TextDisabled = Hex(0x70707E),
			.Accent = Hex(0xE5484D),
			.AccentHover = Hex(0xF26669),
			.AccentPressed = Hex(0xC13438),
			.OnAccent = Hex(0x12070A),
			.AccentMuted = Hex(0xE5484D, 0.30f),
			.AccentFaint = Hex(0xE5484D, 0.14f),
			.Success = Hex(0x48BB78),
			.Warning = Hex(0xE8A33D),
			.Danger = Hex(0xF2555A),
			.AxisX = Hex(0xE5686D),
			.AxisY = Hex(0x5FBF6A),
			.AxisZ = Hex(0x5B8DEF),
		};

		// ---- light: the same red, retuned so it still means the same thing
		//
		// Ratios against BgSurface: primary text 17.4:1, secondary 7.1:1,
		// accent 5.6:1, and white on the accent 5.7:1.
		constexpr Palette kLight = {
			.BgBase = Hex(0xE7E7EE),
			.BgSurface = Hex(0xFBFBFD),
			.BgControl = Hex(0xECECF3),
			.BgHover = Hex(0xE1E1EB),
			.BgActive = Hex(0xD4D4E0),
			.Line = Hex(0xDBDBE4),
			.LineStrong = Hex(0xA8A8B8),
			.TextPrimary = Hex(0x16161C),
			.TextSecondary = Hex(0x55555F),
			.TextDisabled = Hex(0x8B8B98),
			.Accent = Hex(0xC4262C),
			.AccentHover = Hex(0xA81E24),
			.AccentPressed = Hex(0x8C1319),
			.OnAccent = Hex(0xFFFFFF),
			.AccentMuted = Hex(0xC4262C, 0.22f),
			.AccentFaint = Hex(0xC4262C, 0.10f),
			.Success = Hex(0x1F7A45),
			.Warning = Hex(0x8A5A00),
			.Danger = Hex(0xB3161C),
			.AxisX = Hex(0xB3262C),
			.AxisY = Hex(0x1F7A3A),
			.AxisZ = Hex(0x2B5BC4),
		};

		Theme s_Current = Theme::Dark;
	}

	const Palette& Colors()
	{
		return s_Current == Theme::Light ? kLight : kDark;
	}

	Theme Current()
	{
		return s_Current;
	}

	const char* Name(Theme theme)
	{
		return theme == Theme::Light ? "light" : "dark";
	}

	Theme Parse(const char* name)
	{
		return (name && std::strcmp(name, "light") == 0) ? Theme::Light : Theme::Dark;
	}

	void Apply(Theme theme)
	{
		s_Current = theme;

		const Palette& c = Colors();
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		// ---- structure: greyscale only -----------------------------------
		colors[ImGuiCol_WindowBg]             = c.BgSurface;
		colors[ImGuiCol_ChildBg]              = { 0, 0, 0, 0 };   // inherit; see below
		colors[ImGuiCol_PopupBg]              = c.BgSurface;
		colors[ImGuiCol_MenuBarBg]            = c.BgBase;
		colors[ImGuiCol_Border]               = c.Line;
		colors[ImGuiCol_BorderShadow]         = { 0, 0, 0, 0 };

		colors[ImGuiCol_Text]                 = c.TextPrimary;
		colors[ImGuiCol_TextDisabled]         = c.TextDisabled;

		colors[ImGuiCol_FrameBg]              = c.BgControl;
		colors[ImGuiCol_FrameBgHovered]       = c.BgHover;
		colors[ImGuiCol_FrameBgActive]        = c.BgActive;

		colors[ImGuiCol_TitleBg]              = c.BgBase;
		colors[ImGuiCol_TitleBgActive]        = c.BgBase;
		colors[ImGuiCol_TitleBgCollapsed]     = c.BgBase;

		colors[ImGuiCol_ScrollbarBg]          = { 0, 0, 0, 0 };
		colors[ImGuiCol_ScrollbarGrab]        = c.LineStrong;
		colors[ImGuiCol_Separator]            = c.Line;
		colors[ImGuiCol_SeparatorHovered]     = c.LineStrong;
		colors[ImGuiCol_SeparatorActive]      = c.Accent;

		// Collapsing headers are structure, not selection, so they stay grey.
		colors[ImGuiCol_Header]               = c.BgControl;
		colors[ImGuiCol_HeaderHovered]        = c.BgHover;
		colors[ImGuiCol_HeaderActive]         = c.BgActive;

		colors[ImGuiCol_Button]               = c.BgControl;
		colors[ImGuiCol_ButtonHovered]        = c.BgHover;
		colors[ImGuiCol_ButtonActive]         = c.BgActive;

		colors[ImGuiCol_Tab]                  = c.BgBase;
		colors[ImGuiCol_TabHovered]           = c.BgHover;
		colors[ImGuiCol_TabDimmed]            = c.BgBase;
		colors[ImGuiCol_TabDimmedSelected]    = c.BgSurface;
		colors[ImGuiCol_TabSelected]          = c.BgSurface;

		colors[ImGuiCol_TableHeaderBg]        = c.BgBase;
		colors[ImGuiCol_TableBorderStrong]    = c.Line;
		colors[ImGuiCol_TableBorderLight]     = c.Line;
		colors[ImGuiCol_TableRowBg]           = { 0, 0, 0, 0 };
		colors[ImGuiCol_TableRowBgAlt]        = Fade(c.LineStrong, 0.16f);

		colors[ImGuiCol_DockingPreview]       = c.AccentMuted;
		colors[ImGuiCol_DockingEmptyBg]       = c.BgBase;

		// ---- the accent: interaction and state, nothing else --------------
		colors[ImGuiCol_CheckMark]            = c.Accent;
		// Muted at rest, full accent once touched.
		//
		// A thin fully saturated bar sitting in the middle of an otherwise
		// empty field reads as a stray mark or an error rather than as a
		// handle -- there is no track behind it to say it is a position on a
		// range. Quieter at rest and a wider grab make it a control again.
		colors[ImGuiCol_SliderGrab]           = c.AccentMuted;
		colors[ImGuiCol_SliderGrabActive]     = c.Accent;
		colors[ImGuiCol_ScrollbarGrabHovered] = c.Accent;
		colors[ImGuiCol_ScrollbarGrabActive]  = c.AccentHover;
		colors[ImGuiCol_ResizeGrip]           = { 0, 0, 0, 0 };   // invisible until touched
		colors[ImGuiCol_ResizeGripHovered]    = c.AccentMuted;
		colors[ImGuiCol_ResizeGripActive]     = c.Accent;
		colors[ImGuiCol_TabSelectedOverline]  = c.Accent;
		colors[ImGuiCol_TabDimmedSelectedOverline] = Fade(c.Accent, 0.35f);
		colors[ImGuiCol_TextSelectedBg]       = c.AccentMuted;
		colors[ImGuiCol_NavCursor]            = c.Accent;
		colors[ImGuiCol_DragDropTarget]       = c.Accent;
		colors[ImGuiCol_PlotHistogram]        = c.Accent;
		colors[ImGuiCol_PlotHistogramHovered] = c.AccentHover;
		colors[ImGuiCol_PlotLines]            = c.Accent;
		colors[ImGuiCol_PlotLinesHovered]     = c.AccentHover;

		// A modal has to dim what it covers, and the veil is darker in a light
		// theme than a dark one -- against near-white, 60% black is a hole.
		colors[ImGuiCol_ModalWindowDimBg] =
			s_Current == Theme::Light ? ImVec4{ 0.10f, 0.10f, 0.13f, 0.35f }
									  : ImVec4{ 0.02f, 0.02f, 0.03f, 0.60f };

		// ---- geometry ------------------------------------------------------
		// One radius family, applied everywhere. A mix of rounded and square
		// elements is most of what makes an editor look assembled rather than
		// designed.
		style.WindowRounding    = Radius::Panel;
		style.ChildRounding     = Radius::Panel;
		style.PopupRounding     = Radius::Panel;
		style.FrameRounding     = Radius::Control;
		style.GrabRounding      = Radius::Control;
		style.TabRounding       = Radius::Control;
		style.ScrollbarRounding = Radius::Pill;

		// Borders do real work at the window edge and nowhere else. A border
		// on every frame is noise: the fill already says where the control is,
		// and two signals for one fact is what makes dense UI feel busy.
		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize  = 1.0f;
		style.PopupBorderSize  = 1.0f;
		style.FrameBorderSize  = 0.0f;
		style.TabBarBorderSize = 1.0f;
		style.SeparatorTextBorderSize = 1.0f;

		style.WindowPadding    = { Space::Roomy, Space::Roomy };
		style.FramePadding     = { Space::Base,  Space::Snug };
		style.CellPadding      = { Space::Snug,  Space::Tight };
		style.ItemSpacing      = { Space::Base,  Space::Snug };
		style.ItemInnerSpacing = { Space::Snug,  Space::Tight };
		style.IndentSpacing    = 18.0f;
		style.ScrollbarSize    = 11.0f;
		style.GrabMinSize      = 14.0f;

		style.WindowTitleAlign = { 0.0f, 0.5f };
		style.WindowMenuButtonPosition = ImGuiDir_None;   // no collapse arrow
		style.SeparatorTextAlign = { 0.0f, 0.5f };
		style.SeparatorTextPadding = { 0.0f, Space::Snug };

		// A disabled control should read as unavailable, not as absent.
		style.DisabledAlpha = 0.45f;

		// The loading screen draws before any panel exists and lives in the
		// engine, where this palette is not reachable -- so every Apply pushes
		// the handful of colours it needs. This is the only place a theme
		// changes, which is what keeps the two permanently in step: a
		// light-theme editor must not open on a dark flash.
		const auto packed = [](const ImVec4& colour)
		{
			return ((uint32_t)(colour.x * 255.0f + 0.5f) << 16)
				 | ((uint32_t)(colour.y * 255.0f + 0.5f) << 8)
				 |  (uint32_t)(colour.z * 255.0f + 0.5f);
		};

		LoadingScreen::Palette loading;
		loading.Background = packed(c.BgBase);
		loading.Title      = packed(c.TextPrimary);
		loading.Phase      = packed(c.TextSecondary);
		loading.Detail     = packed(c.TextDisabled);
		// BgActive, not BgHover: on the light palette BgHover is six levels
		// off the background, and a track nobody can see makes the bar read
		// as a floating red sliver with no destination.
		loading.Track      = packed(c.BgActive);
		loading.Fill       = packed(c.Accent);
		LoadingScreen::SetPalette(loading);
	}
}
