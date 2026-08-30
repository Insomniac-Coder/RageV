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

		// ---- dark: the mark, at editor scale ----------------------------
		//
		// A panel *is* the mark's field; the dockspace goes darker still so
		// panels read as raised. Ratios against BgSurface, measured: primary
		// text 17.2:1, secondary 6.9:1, accent 4.3:1, and black on the accent
		// 4.6:1. See tools/scripts/check_theme_contrast.py.
		constexpr Palette kDark = {
			// **Six levels apart was not a raised panel, it was one black
			// field.** The loading card reads as a card because it sits
			// visibly above its ground; the editor's panels sat six levels
			// above theirs, which at this end of the curve is nothing, so
			// every panel edge depended entirely on its hairline. Widened to
			// eighteen: still quiet, and now the panel is a surface.
			.BgBase = Hex(0x050508),
			.BgSurface = Hex(0x131319),
			.BgControl = Hex(0x191920),
			.BgHover = Hex(0x23232C),
			.BgActive = Hex(0x2D2D38),
			.Line = Hex(0x26262F),
			.LineStrong = Hex(0x454553),
			.TextPrimary = Hex(0xF2F2F6),
			.TextSecondary = Hex(0x8E8E9C),
			.TextDisabled = Hex(0x64646F),
			.Accent = Hex(0xE03030),
			.AccentHover = Hex(0xF04A4A),
			.AccentPressed = Hex(0xB82525),
			.OnAccent = Hex(0x000000),
			.AccentMuted = Hex(0xE03030, 0.30f),
			.AccentFaint = Hex(0xE03030, 0.14f),
			.Success = Hex(0x48BB78),
			.Warning = Hex(0xE8A33D),
			// Lighter than the accent, not another shade of it: see the note
			// on Danger in the header.
			.Danger = Hex(0xFF7A7A),
			.AxisX = Hex(0xE05656),
			.AxisY = Hex(0x5FBF6A),
			.AxisZ = Hex(0x5B8DEF),
		};

		// ---- light: the same red, retuned so it still means the same thing
		//
		// The two ends swap: the mark's light is the panel, the mark's field
		// is the ink. Ratios against BgSurface: primary text 17.2:1,
		// secondary 7.0:1, accent 5.1:1, and white on the accent 5.7:1.
		constexpr Palette kLight = {
			// The same widening as the dark palette, in the other direction:
			// the ground drops away from the panel rather than the panel
			// rising off the ground, because on a light theme the panel is
			// the paler of the two.
			.BgBase = Hex(0xCFCFDA),
			.BgSurface = Hex(0xF2F2F6),
			.BgControl = Hex(0xE6E6EE),
			.BgHover = Hex(0xDADAE4),
			.BgActive = Hex(0xCBCBD8),
			.Line = Hex(0xD2D2DC),
			.LineStrong = Hex(0x9E9EAE),
			.TextPrimary = Hex(0x0E0E12),
			.TextSecondary = Hex(0x51515B),
			.TextDisabled = Hex(0x84848F),
			.Accent = Hex(0xC81E1E),
			.AccentHover = Hex(0xA81818),
			.AccentPressed = Hex(0x8C1212),
			.OnAccent = Hex(0xFFFFFF),
			.AccentMuted = Hex(0xC81E1E, 0.22f),
			.AccentFaint = Hex(0xC81E1E, 0.10f),
			.Success = Hex(0x1F7A45),
			.Warning = Hex(0x8A5A00),
			// Darker than the accent here, for the same reason it is lighter
			// in the dark theme: away from the accent, toward the ink.
			.Danger = Hex(0x7E0E0E),
			.AxisX = Hex(0xB02020),
			// Four percent darker than the green it was, and the reason is the
			// axis badge rather than the gizmo. The badge used to be a filled
			// block with a white glyph on it; now the *letter* carries the
			// colour, so it is read as text and owes 4.5:1 against the field
			// it sits on. #1F7A3A managed 4.33 and check_theme_contrast.py
			// said so.
			.AxisY = Hex(0x1E7538),
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
		// **The menu bar is chrome and chrome is raised.** On the ground
		// colour it read as a hole above the panels; on the surface colour it
		// is the same band the toolbar under it already is, and the two now
		// form one header rather than two unrelated strips.
		colors[ImGuiCol_MenuBarBg]            = c.BgSurface;
		// The hairline every panel and popup is drawn with -- the card's own
		// border, and now that the surface sits further off the ground it has
		// something to separate rather than everything to do on its own.
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

		// **A checked box is not a different surface from an unchecked one.**
		// ImGui grew a separate colour for it, and a theme that does not know
		// the name gets ImGui's default -- which is blue, and which is why
		// every ticked checkbox in this editor was blue while every empty one
		// was correctly grey. The box stays structural either way; the check
		// mark is the state, and the check mark is the accent.
		colors[ImGuiCol_CheckboxSelectedBg]   = c.BgControl;

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
		// 75%, not 35%.
		//
		// This is the overline on a panel whose dock node does not have focus,
		// which is *most panels most of the time* -- an editor has one focused
		// node and six that are not. At 35% over a near-black surface the mark
		// that says which tab you are looking at had all but gone, so the
		// thing carrying the accent's meaning was invisible in the common
		// case and full strength in the rare one. Focus is still worth
		// distinguishing; it is not worth that much.
		colors[ImGuiCol_TabDimmedSelectedOverline] = Fade(c.Accent, 0.75f);
		colors[ImGuiCol_TextSelectedBg]       = c.AccentMuted;
		colors[ImGuiCol_NavCursor]            = c.Accent;
		colors[ImGuiCol_DragDropTarget]       = c.Accent;
		colors[ImGuiCol_PlotHistogram]        = c.Accent;
		colors[ImGuiCol_PlotHistogramHovered] = c.AccentHover;
		colors[ImGuiCol_PlotLines]            = c.Accent;
		colors[ImGuiCol_PlotLinesHovered]     = c.AccentHover;

		// The rest of what ImGui has grown since this palette was written.
		// Every one of these was sitting at an ImGui default, and several of
		// those defaults are blue -- the checkbox above is simply the one
		// somebody noticed. A theme is only complete against the version of
		// ImGui it was written for, so this list is worth re-deriving after
		// an upgrade rather than assumed to still be exhaustive.
		colors[ImGuiCol_InputTextCursor]      = c.TextPrimary;   // the caret is ink
		colors[ImGuiCol_TextLink]             = c.Accent;        // a link is an action
		colors[ImGuiCol_TreeLines]            = c.Line;          // structure
		colors[ImGuiCol_DragDropTargetBg]     = c.AccentFaint;   // quieter than its outline
		colors[ImGuiCol_NavWindowingHighlight] = c.Accent;
		colors[ImGuiCol_NavWindowingDimBg]    = ImVec4{ 0.02f, 0.02f, 0.03f, 0.60f };
		// Deliberately *not* red. An unsaved document is a state wanting
		// attention, not something you are acting on, and the accent means the
		// second of those.
		colors[ImGuiCol_UnsavedMarker]        = c.Warning;

		// A modal has to dim what it covers, and the veil is darker in a light
		// theme than a dark one -- against near-white, 60% black is a hole.
		colors[ImGuiCol_ModalWindowDimBg] =
			s_Current == Theme::Light ? ImVec4{ 0.10f, 0.10f, 0.13f, 0.35f }
									  : ImVec4{ 0.02f, 0.02f, 0.03f, 0.60f };

		// ---- geometry ------------------------------------------------------
		//
		// **One family, applied consistently** -- which is what the old
		// square-everywhere rule was actually protecting, and it survives the
		// change of radius. See Corner in the header for why the radius moved.
		style.WindowRounding    = Corner::Panel;
		style.ChildRounding     = Corner::Panel;
		style.PopupRounding     = Corner::Panel;
		style.FrameRounding     = Corner::Control;
		style.GrabRounding      = Corner::Control;
		style.TabRounding       = Corner::Control;
		style.ScrollbarRounding = Corner::Control;

		// Borders do real work at the window edge and nowhere else. A border
		// on every frame is noise: the fill already says where the control is,
		// and two signals for one fact is what makes dense UI feel busy.
		// ImGui defaults this to a single pixel, which is a hairline rather
		// than a mark -- and it is the one place the accent says "this is the
		// panel you are in".
		style.TabBarOverlineSize = 3.0f;

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
		loading.Surface    = packed(c.BgSurface);
		loading.Line       = packed(c.Line);

		// Measured by eye against both palettes rather than shared: over the
		// dark theme's near-black the accent itself at 0x26 is a glow at the
		// bottom of the frame; over the light theme the same thing is a pink
		// cast across half the window, so the light one uses the pressed
		// accent -- a deeper red -- and less of it.
		const bool light = s_Current == Theme::Light;
		loading.Wash      = packed(light ? c.AccentPressed : c.Accent);
		loading.WashAlpha = light ? 0x62u : 0x26u;
		// Under a whole editor rather than a card: quieter than the screen
		// wash in both themes, and quieter still in light, where the same
		// alpha covers a far paler ground and reads as a stain.
		loading.WashAlphaChrome = light ? 0x22u : 0x1Cu;
		LoadingScreen::SetPalette(loading);

	}
}
