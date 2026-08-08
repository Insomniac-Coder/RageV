#include "EditorTheme.h"

namespace RageV::EditorTheme
{
	namespace
	{
		ImVec4 Mix(const ImVec4& color, float alpha)
		{
			return { color.x, color.y, color.z, alpha };
		}
	}

	void Apply()
	{
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;

		using namespace Color;

		// ---- structure: greyscale only -----------------------------------
		colors[ImGuiCol_WindowBg]             = Black;
		colors[ImGuiCol_ChildBg]              = Surface;
		colors[ImGuiCol_PopupBg]              = Surface;
		colors[ImGuiCol_MenuBarBg]            = Surface;
		colors[ImGuiCol_Border]               = Line;
		colors[ImGuiCol_BorderShadow]         = { 0, 0, 0, 0 };

		colors[ImGuiCol_Text]                 = Text;
		colors[ImGuiCol_TextDisabled]         = TextDim;

		colors[ImGuiCol_FrameBg]              = Raised;
		colors[ImGuiCol_FrameBgHovered]       = RaisedHigh;
		colors[ImGuiCol_FrameBgActive]        = RaisedHigh;

		colors[ImGuiCol_TitleBg]              = Surface;
		colors[ImGuiCol_TitleBgActive]        = Raised;
		colors[ImGuiCol_TitleBgCollapsed]     = Surface;

		colors[ImGuiCol_ScrollbarBg]          = Black;
		colors[ImGuiCol_ScrollbarGrab]        = Line;
		colors[ImGuiCol_Separator]            = Line;
		colors[ImGuiCol_SeparatorHovered]     = Line;
		colors[ImGuiCol_SeparatorActive]      = Line;

		// Collapsing headers are structure, not selection, so they stay grey.
		colors[ImGuiCol_Header]               = Raised;
		colors[ImGuiCol_HeaderHovered]        = RaisedHigh;
		colors[ImGuiCol_HeaderActive]         = RaisedHigh;

		colors[ImGuiCol_Button]               = Raised;
		colors[ImGuiCol_ButtonHovered]        = RaisedHigh;

		colors[ImGuiCol_Tab]                  = Surface;
		colors[ImGuiCol_TabHovered]           = Raised;
		colors[ImGuiCol_TabDimmed]            = Surface;
		colors[ImGuiCol_TabDimmedSelected]    = Raised;

		colors[ImGuiCol_TableHeaderBg]        = Raised;
		colors[ImGuiCol_TableBorderStrong]    = Line;
		colors[ImGuiCol_TableBorderLight]     = Line;
		colors[ImGuiCol_TableRowBg]           = { 0, 0, 0, 0 };
		colors[ImGuiCol_TableRowBgAlt]        = Mix(RaisedHigh, 0.35f);

		colors[ImGuiCol_DockingPreview]       = AccentMuted;
		colors[ImGuiCol_DockingEmptyBg]       = Black;

		// ---- the accent: interaction and state, nothing else --------------
		colors[ImGuiCol_CheckMark]            = Accent;
		colors[ImGuiCol_SliderGrab]           = Accent;
		colors[ImGuiCol_SliderGrabActive]     = AccentHover;
		colors[ImGuiCol_ButtonActive]         = Accent;
		colors[ImGuiCol_ScrollbarGrabHovered] = Accent;
		colors[ImGuiCol_ScrollbarGrabActive]  = AccentHover;
		colors[ImGuiCol_ResizeGrip]           = Mix(Accent, 0.0f);   // invisible until touched
		colors[ImGuiCol_ResizeGripHovered]    = AccentMuted;
		colors[ImGuiCol_ResizeGripActive]     = Accent;
		colors[ImGuiCol_TabSelected]          = Raised;
		colors[ImGuiCol_TabSelectedOverline]  = Accent;   // the active tab's underline
		colors[ImGuiCol_TextSelectedBg]       = AccentMuted;
		colors[ImGuiCol_NavCursor]            = Accent;
		colors[ImGuiCol_DragDropTarget]       = Accent;
		colors[ImGuiCol_PlotHistogram]        = Accent;
		colors[ImGuiCol_PlotHistogramHovered] = AccentHover;
		colors[ImGuiCol_PlotLines]            = Accent;
		colors[ImGuiCol_PlotLinesHovered]     = AccentHover;

		colors[ImGuiCol_ModalWindowDimBg]     = { 0.0f, 0.0f, 0.0f, 0.60f };

		// ---- geometry ------------------------------------------------------
		// Small consistent radius everywhere; a mix of rounded and square
		// elements is what makes an editor look assembled rather than designed.
		style.WindowRounding    = 4.0f;
		style.ChildRounding     = 4.0f;
		style.FrameRounding     = 3.0f;
		style.PopupRounding     = 4.0f;
		style.ScrollbarRounding = 3.0f;
		style.GrabRounding      = 3.0f;
		style.TabRounding       = 3.0f;

		style.WindowBorderSize = 1.0f;
		style.ChildBorderSize  = 1.0f;
		style.PopupBorderSize  = 1.0f;
		style.FrameBorderSize  = 0.0f;
		style.TabBarBorderSize = 1.0f;

		style.WindowPadding    = { 10.0f, 10.0f };
		style.FramePadding     = { 8.0f, 4.0f };
		style.ItemSpacing      = { 8.0f, 6.0f };
		style.ItemInnerSpacing = { 6.0f, 4.0f };
		style.IndentSpacing    = 18.0f;
		style.ScrollbarSize    = 12.0f;
		style.GrabMinSize      = 8.0f;

		style.WindowTitleAlign = { 0.0f, 0.5f };
		style.WindowMenuButtonPosition = ImGuiDir_None;   // no collapse arrow
		style.SeparatorTextBorderSize = 1.0f;
		style.SeparatorTextAlign = { 0.0f, 0.5f };
	}
}
