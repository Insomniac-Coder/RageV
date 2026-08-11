#include "Widgets.h"
#include "EditorTheme.h"
#include <algorithm>

namespace RageV::UI
{
	namespace
	{
		// A control narrower than this is not a control, it is a hint that
		// something is wrong. Rows overflow their cell and get clipped by the
		// table rather than being asked for a negative width -- clipping is
		// ugly, a negative width is an assertion failure.
		constexpr float kMinControlWidth = 22.0f;
	}

	bool BeginProperties(const char* id, float labelFraction, bool resizable)
	{
		labelFraction = std::clamp(labelFraction, 0.15f, 0.75f);

		// NoBordersInBody because a grid drawn around every property is noise:
		// the alignment already does the grouping that lines would do, and
		// lines cost a pixel of contrast on every row to say something the
		// reader already knows.
		ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp
									| ImGuiTableFlags_NoBordersInBody
									| ImGuiTableFlags_PadOuterX;

		if (resizable)
			flags |= ImGuiTableFlags_Resizable;

		if (!ImGui::BeginTable(id, 2, flags))
			return false;

		ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch, labelFraction);
		ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelFraction);
		return true;
	}

	void EndProperties()
	{
		ImGui::EndTable();
	}

	void PropertyRow(const char* label, const char* tooltip)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);

		// Without this the label sits at the top of a row whose control is
		// taller than a line of text, and every row looks a pixel off.
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);

		// A label too long for its column is clipped by the table. Saying so
		// on hover is the difference between "this UI is broken" and "this
		// column is narrow" -- and it is why the old fixed column truncating
		// mid-word was worse than it looked: there was no way to read the
		// rest.
		const bool clipped = ImGui::GetItemRectMax().x >= ImGui::GetCursorScreenPos().x
											 + ImGui::GetContentRegionAvail().x;
		if (ImGui::IsItemHovered())
		{
			if (tooltip)
				ImGui::SetTooltip("%s", tooltip);
			else if (clipped)
				ImGui::SetTooltip("%s", label);
		}

		ImGui::TableSetColumnIndex(1);

		// -FLT_MIN is ImGui's "fill the cell". Never a negative literal: a
		// computed negative width is what produces an inverted clip rectangle.
		ImGui::SetNextItemWidth(-FLT_MIN);
	}

	void SectionHeader(const char* label)
	{
		const auto& colors = EditorTheme::Colors();

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, colors.TextSecondary);
		ImGui::SeparatorText(label);
		ImGui::PopStyleColor();
	}

	void HelpMarker(const char* text)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			// Wrapped, because an explanation long enough to need a tooltip is
			// long enough to run off a monitor if it is left on one line.
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
			ImGui::TextUnformatted(text);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	bool DragVec3(const char* id, Vec3& values, float resetValue, float speed)
	{
		bool changed = false;
		const auto& colors = EditorTheme::Colors();

		ImGui::PushID(id);

		const float frameHeight = ImGui::GetFrameHeight();

		// Narrower than it is tall. A square badge is the obvious choice and
		// the wrong one: it holds a single letter, and at 1280x720 the three
		// squares plus their fields do not fit the inspector -- which showed
		// up as the Z field being clipped off the panel. Full height keeps the
		// row aligned; two thirds of the width is all the glyph needs.
		const ImVec2 buttonSize = { std::max(frameHeight * 0.68f, 16.0f), frameHeight };

		// One pixel between a badge and its field so they read as one control,
		// and a real gap between the three so they read as three.
		const float hairline = 1.0f;
		const float betweenAxes = EditorTheme::Space::Tight;

		const float available = ImGui::GetContentRegionAvail().x;
		const float furniture = 3.0f * (buttonSize.x + hairline) + 2.0f * betweenAxes;

		// The whole point of this function. `available` shrinks with the panel
		// and `furniture` does not, so the subtraction goes negative on a
		// narrow inspector -- which the previous version passed straight to
		// ImGui.
		const float fieldWidth = std::max((available - furniture) / 3.0f, kMinControlWidth);

		const ImVec4 axisColors[3] = { colors.AxisX, colors.AxisY, colors.AxisZ };
		const char* axisLabels[3] = { "X", "Y", "Z" };
		const char* fieldIds[3]   = { "##x", "##y", "##z" };
		float* components[3] = { &values.x, &values.y, &values.z };

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f });
		// A square badge wants its own radius, or the "X" sits in a lozenge.
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, EditorTheme::Radius::Control);

		for (int axis = 0; axis < 3; axis++)
		{
			ImGui::PushID(axis);

			// The badge is a button because it resets the axis, and it is
			// coloured because "which one is Y" is the question being asked
			// dozens of times a minute. Hover brightens rather than switching
			// to the accent: the accent means interaction, and all three of
			// these are interactive, so it would say nothing.
			ImGui::PushStyleColor(ImGuiCol_Button, axisColors[axis]);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.AccentHover);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, axisColors[axis]);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.OnAccent);

			if (ImGui::Button(axisLabels[axis], buttonSize))
			{
				*components[axis] = resetValue;
				changed = true;
			}
			ImGui::PopStyleColor(4);

			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Reset %s to %.3g", axisLabels[axis], resetValue);

			ImGui::SameLine(0.0f, hairline);
			ImGui::SetNextItemWidth(fieldWidth);
			changed |= ImGui::DragFloat(fieldIds[axis], components[axis], speed,
										0.0f, 0.0f, "%.3g");

			if (axis < 2)
				ImGui::SameLine(0.0f, betweenAxes);

			ImGui::PopID();
		}

		ImGui::PopStyleVar(2);
		ImGui::PopID();
		return changed;
	}


	// -------------------------------------------------------------------------
	// One-line rows
	// -------------------------------------------------------------------------
	namespace
	{
		// Every row is its own one-row table. Several of them with the same
		// proportions line up with each other, which is what a reader sees as
		// alignment; what is given up is dragging the divider, and a drag that
		// moved one row and not its neighbours would read as a fault.
		//
		// BeginTable answers false when the row is entirely culled, and
		// EndTable must not be called then -- so the open/close state travels
		// back to the caller rather than being assumed.
		bool OpenRow(const char* label, const char* tooltip)
		{
			ImGui::PushID(label);
			if (!BeginProperties("##row", 0.42f, false))
			{
				ImGui::PopID();
				return false;
			}
			PropertyRow(label, tooltip);
			return true;
		}

		void CloseRow()
		{
			EndProperties();
			ImGui::PopID();
		}
	}

	bool RowCheckbox(const char* label, bool* value, const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;

		// A checkbox is the one control that must NOT stretch: a tick box the
		// width of the panel is a click target with nothing in most of it.
		const bool changed = ImGui::Checkbox("##v", value);
		CloseRow();
		return changed;
	}

	bool RowDragFloat(const char* label, float* value, float speed, float min, float max,
					  const char* format, const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;
		const bool changed = ImGui::DragFloat("##v", value, speed, min, max, format);
		CloseRow();
		return changed;
	}

	bool RowSliderFloat(const char* label, float* value, float min, float max,
						const char* format, const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;
		const bool changed = ImGui::SliderFloat("##v", value, min, max, format);
		CloseRow();
		return changed;
	}

	bool RowDragInt(const char* label, int* value, float speed, int min, int max,
					const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;
		const bool changed = ImGui::DragInt("##v", value, speed, min, max);
		CloseRow();
		return changed;
	}

	bool RowColor3(const char* label, float* rgb, const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;

		// NoInputs on purpose. The three numeric boxes ImGui shows by default
		// eat the whole row to express something nobody reads as numbers --
		// and they were most of why this panel looked like a spreadsheet. The
		// picker still has them, one click away.
		const bool changed = ImGui::ColorEdit3("##v", rgb,
			ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
		CloseRow();
		return changed;
	}

	bool RowColor4(const char* label, float* rgba, const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;
		const bool changed = ImGui::ColorEdit4("##v", rgba,
			ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar
			| ImGuiColorEditFlags_AlphaPreviewHalf);
		CloseRow();
		return changed;
	}

	bool RowCombo(const char* label, int* current, const char* const items[], int count,
				  const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return false;
		const bool changed = ImGui::Combo("##v", current, items, count);
		CloseRow();
		return changed;
	}

	void RowText(const char* label, const char* value, const char* tooltip)
	{
		if (!OpenRow(label, tooltip))
			return;

		// Secondary, because in a row of label-and-value the label is the
		// question and the value is the answer -- and making both the same
		// weight is what turns a readout into a wall.
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextSecondary);
		ImGui::TextUnformatted(value);
		ImGui::PopStyleColor();

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", value);

		CloseRow();
	}

	int SegmentedControl(const char* id, const char* const* labels, int count, int current)
	{
		if (count <= 0)
			return current;

		const auto& colors = EditorTheme::Colors();
		int chosen = current;

		ImGui::PushID(id);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 1.0f, 0.0f });

		const float available = ImGui::GetContentRegionAvail().x;
		const float each = std::max((available - (count - 1)) / (float)count, kMinControlWidth);

		for (int i = 0; i < count; i++)
		{
			ImGui::PushID(i);
			const bool selected = i == current;

			// The selected segment is the one place a fill of the accent is
			// right: it is state, and it is the state of a control.
			if (selected)
			{
				ImGui::PushStyleColor(ImGuiCol_Button, colors.Accent);
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.AccentHover);
				ImGui::PushStyleColor(ImGuiCol_ButtonActive, colors.AccentPressed);
				ImGui::PushStyleColor(ImGuiCol_Text, colors.OnAccent);
			}

			if (ImGui::Button(labels[i], ImVec2(each, 0.0f)))
				chosen = i;

			if (selected)
				ImGui::PopStyleColor(4);

			if (i < count - 1)
				ImGui::SameLine();
			ImGui::PopID();
		}

		ImGui::PopStyleVar();
		ImGui::PopID();
		return chosen;
	}
}
