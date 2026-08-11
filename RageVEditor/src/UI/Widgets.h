#pragma once
#include "imgui.h"
#include "RageV/Math/Math.h"
#include "AssetIcons.h"

// The editor's layout primitives.
//
// Everything that draws a labelled control goes through here, for one reason:
// a panel that lays itself out is a panel that lays itself out *differently*,
// and an inspector where three components each invented their own label column
// is the thing that reads as unfinished no matter how good the palette is.
//
// ---------------------------------------------------------------------------
// Why a table and not two columns
// ---------------------------------------------------------------------------
//
// The version this replaces used `ImGui::Columns` with `SetColumnWidth(0, 140)`
// -- a *fixed* label column. Two things follow from a fixed column, and both
// were visible in the editor:
//
//  - Narrow the panel and the control column keeps shrinking until controls
//    are unusable, because the label column never gives anything back.
//  - Any widget that draws its own label after itself (`ColorEdit4`,
//    `SliderFloat` with a visible name) had no room for it, so labels were
//    cut off mid-word: "Base Colo", "Roughnes", "Clear colour".
//
// A table shares the width proportionally, lets the user drag the split, and
// clips a long label with a tooltip instead of truncating it silently.
//
// ---------------------------------------------------------------------------
// Why label-left rather than label-above
// ---------------------------------------------------------------------------
//
// Form research is clear that top-aligned labels are scanned fastest, and it
// is measuring a *form*: a dozen fields, filled once, top to bottom. An
// inspector is the opposite -- a hundred fields, read repeatedly, where
// vertical space is the scarce resource and doubling the row count means half
// as many properties visible at once.
//
// The finding underneath the research still applies, though. Left-aligned
// labels scan badly because a *ragged* gutter forces the eye to hunt for where
// the control starts. A fixed column removes the raggedness, which is why
// every professional inspector uses one and why this is a table rather than
// text-then-widget.

namespace RageV::UI
{
	// Opens a two-column property table. Returns false if it could not open,
	// in which case draw nothing and do not call EndProperties -- same
	// contract as ImGui's own Begin/End pairs.
	//
	// `labelFraction` is the share of the width the label column starts with.
	// The user can drag it; it is a starting point, not a rule.
	// `resizable` off is for a table holding a single row: several one-row
	// tables with the same proportions line up with each other, but a drag on
	// one of them would move only that row's divider, which reads as a bug.
	bool BeginProperties(const char* id, float labelFraction = 0.42f,
						 bool resizable = true);
	void EndProperties();

	// Starts a row and leaves the cursor in the control cell, with the next
	// item stretched to fill it. Everything after this call and before the
	// next PropertyRow is the control.
	//
	//     if (UI::BeginProperties("mat"))
	//     {
	//         UI::PropertyRow("Base Colour", "The albedo, in sRGB.");
	//         changed |= ImGui::ColorEdit4("##base", ...);
	//         UI::EndProperties();
	//     }
	//
	// Note the `##` on the control: the row already drew the label, and a
	// widget that draws a second one is the bug this whole file exists to stop.
	void PropertyRow(const char* label, const char* tooltip = nullptr);

	// --- type ------------------------------------------------------------
	//
	// ImGui 1.92 can rebake a font at a new size on demand, so a scale costs a
	// push and a pop rather than several preloaded fonts. Sizes are given as
	// multipliers from EditorTheme::Type and applied against the *base* size,
	// because GetFontSize() has the global DPI factor in it already and
	// feeding that back in applies it twice.
	void PushTextScale(float multiplier);
	void PopTextScale();

	// Small and dim: a unit, a hint, a table header, the count beside a name.
	void TextCaption(const char* fmt, ...) IM_FMTARGS(1);
	// The one number a panel exists to show.
	void TextDisplay(const char* fmt, ...) IM_FMTARGS(1);

	// A heading inside a panel. Quieter than a collapsing header -- this is for
	// grouping rows that are already inside one.
	void SectionHeader(const char* label);

	// A dimmed "(?)" that explains something on hover. Uses the disabled text
	// colour, so it reads as available-but-secondary rather than as a warning.
	void HelpMarker(const char* text);

	// Three drag floats with X/Y/Z reset buttons, sized so they always fit the
	// space they are given.
	//
	// The version this replaces asserted, and finding out which part of it was
	// at fault took reintroducing the old code deliberately.
	//
	// It drew into `ImGui::Columns` with a *fixed* 140px label column, then
	// called `PushMultiItemsWidths(3, CalcItemWidth())`. Once the padding grew,
	// the fixed column left `CalcItemWidth()` too small, the split produced
	// negative widths, and ImGui asserted on the inverted clip rectangle -- so
	// the editor did not merely look wrong on a narrow panel, it stopped.
	// Restoring that function on top of today's padding reproduces it on
	// demand, at 1600x900 and at 1280x720.
	//
	// What fixes it is the proportional column plus widths computed here: the
	// label column now gives space back instead of holding 140px whatever
	// happens. The std::max below is belt and braces on top of that -- removing
	// it does *not* bring the assertion back at any window size reachable
	// here, which is worth knowing before trusting it as the guard.
	bool DragVec3(const char* id, Vec3& values, float resetValue = 0.0f,
				  float speed = 0.1f);

	// ---------------------------------------------------------------------
	// One-line rows
	// ---------------------------------------------------------------------
	//
	// Each opens its own single-row table, draws the label in the left column
	// and the control -- with a hidden label -- stretched across the right.
	// A panel written with these cannot produce the truncation the fixed
	// column produced, because no control is ever competing with a label for
	// the same pixels.
	//
	// They exist as wrappers rather than as a convention because a convention
	// is a thing to remember: `ImGui::Checkbox("VSync", &v)` compiles, looks
	// fine at the width it was written at, and clips at every other one.

	bool RowCheckbox(const char* label, bool* value, const char* tooltip = nullptr);
	bool RowDragFloat(const char* label, float* value, float speed, float min, float max,
					  const char* format = "%.3f", const char* tooltip = nullptr);
	bool RowSliderFloat(const char* label, float* value, float min, float max,
						const char* format = "%.3f", const char* tooltip = nullptr);
	bool RowDragInt(const char* label, int* value, float speed, int min, int max,
					const char* tooltip = nullptr);
	bool RowColor3(const char* label, float* rgb, const char* tooltip = nullptr);
	bool RowColor4(const char* label, float* rgba, const char* tooltip = nullptr);
	bool RowCombo(const char* label, int* current, const char* const items[], int count,
				  const char* tooltip = nullptr);
	// A read-only value, for a panel that reports rather than edits.
	void RowText(const char* label, const char* value, const char* tooltip = nullptr);

	// A button filled with the accent, for the one control in a group that is
	// currently on: the active gizmo mode, Play while a scene is running.
	//
	// It exists because filling a button with the accent and forgetting the
	// text colour is a contrast failure that looks fine to whoever wrote it.
	// Measured on the palette this replaces: default text on an accent fill is
	// **3.1:1 in light and 3.2:1 in dark**, against the 4.5:1 a label needs.
	// Both themes, so it was not a light-theme oversight -- it was there before
	// there was a light theme, and nobody could see it because nobody had put
	// the two numbers next to each other.
	//
	// The palette has always had a token for this: OnAccent, whose whole job is
	// to be legible on Accent. Note what the contrast script can and cannot do
	// here -- it proved OnAccent/Accent passes, and could not tell that no call
	// site was using it. A palette being correct and a palette being *used*
	// correctly are separate claims.
	bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0));

	// A square button with an icon in it and no text.
	//
	// `tooltip` is not optional. An icon-only control is a guess until someone
	// hovers it, and the tooltip is the label -- leaving it out is how a
	// toolbar becomes a memory test.
	bool IconButton(const char* id, IconKind kind, const char* tooltip, bool active = false);

	// A row of buttons that share the full width evenly. For a segmented
	// control -- two or three mutually exclusive choices where a combo box
	// would hide the options behind a click.
	int SegmentedControl(const char* id, const char* const* labels, int count, int current);
}
