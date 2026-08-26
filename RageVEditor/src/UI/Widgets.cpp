#include "Widgets.h"
#include "EditorTheme.h"
#include "RageV/Scene/Scene.h"
#include <algorithm>
#include <cstdarg>

namespace RageV::UI
{
	namespace
	{
		// A control narrower than this is not a control, it is a hint that
		// something is wrong. Rows overflow their cell and get clipped by the
		// table rather than being asked for a negative width -- clipping is
		// ugly, a negative width is an assertion failure.
		constexpr float kMinControlWidth = 22.0f;

		// The tile as points, clockwise, which is what AddConvexPolyFilled
		// wants. Returns 4 for a cut of nothing, so a caller that turns the
		// chamfer off gets a plain rectangle rather than a degenerate hexagon
		// with two zero-length edges -- those show up as dark pixels at the
		// corners once the polygon is antialiased.
		int ChamferPoints(const ImVec2& min, const ImVec2& max, float cut, ImVec2 out[6])
		{
			if (cut <= 0.0f)
			{
				out[0] = min;
				out[1] = { max.x, min.y };
				out[2] = max;
				out[3] = { min.x, max.y };
				return 4;
			}

			out[0] = { min.x + cut, min.y };
			out[1] = { max.x, min.y };
			out[2] = { max.x, max.y - cut };
			out[3] = { max.x - cut, max.y };
			out[4] = { min.x, max.y };
			out[5] = { min.x, min.y + cut };
			return 6;
		}

		// A button filled with the accent and cut like the mark.
		//
		// **The fill is drawn, not styled**, because ImGui emits a button's
		// frame and its label in one call and the chamfer has to go *under*
		// the label. Two draw channels put it there. The obvious alternative
		// -- let ImGui draw it square, then paint the background back over the
		// two corners -- needs to know what is behind the button, which a
		// widget has no business assuming and which is wrong the moment one
		// sits on anything but a flat panel.
		//
		// Drawing the fill also means picking it, so the three states are
		// read back off the item rather than handed to ImGui as style colours.
		bool AccentFilledButton(const char* label, const ImVec2& size)
		{
			const auto& colors = EditorTheme::Colors();
			const ImVec4 clear{ 0.0f, 0.0f, 0.0f, 0.0f };

			ImGui::PushStyleColor(ImGuiCol_Button, clear);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, clear);
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, clear);
			ImGui::PushStyleColor(ImGuiCol_Text, colors.OnAccent);

			ImDrawList* draw = ImGui::GetWindowDrawList();
			ImDrawListSplitter splitter;
			splitter.Split(draw, 2);
			splitter.SetCurrentChannel(draw, 1);

			const bool pressed = ImGui::Button(label, size);
			const bool held = ImGui::IsItemActive();
			const bool hovered = ImGui::IsItemHovered();

			splitter.SetCurrentChannel(draw, 0);
			ChamferedRect(draw, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
						  ImGui::GetColorU32(held ? colors.AccentPressed
												  : (hovered ? colors.AccentHover
															 : colors.Accent)),
						  ChamferCut(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
									 EditorTheme::Corner::Chamfer));
			splitter.Merge(draw);

			ImGui::PopStyleColor(4);
			return pressed;
		}
	}

	float ChamferCut(const ImVec2& min, const ImVec2& max, float fraction)
	{
		const float shorter = Math::Min(max.x - min.x, max.y - min.y);
		return Math::Clamp(shorter * fraction, 0.0f, shorter * 0.5f);
	}

	void ChamferedRect(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
					   ImU32 fill, float cut)
	{
		ImVec2 points[6];
		const int count = ChamferPoints(min, max, cut, points);
		draw->AddConvexPolyFilled(points, count, fill);
	}

	void ChamferedRectOutline(ImDrawList* draw, const ImVec2& min, const ImVec2& max,
							  ImU32 colour, float cut, float thickness)
	{
		ImVec2 points[6];
		const int count = ChamferPoints(min, max, cut, points);
		draw->AddPolyline(points, count, colour, ImDrawFlags_Closed, thickness);
	}

	bool BeginProperties(const char* id, float labelFraction, bool resizable)
	{
		labelFraction = Math::Clamp(labelFraction, 0.15f, 0.75f);

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
		// Secondary on purpose. In a label/value row the label is the question
		// and the control is the answer, and setting both at full strength is
		// most of why a dense inspector reads as a wall of text.
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextSecondary);
		ImGui::TextUnformatted(label);
		ImGui::PopStyleColor();

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

	void PushTextScale(float multiplier)
	{
		// Against FontSizeBase, not GetFontSize(). GetFontSize() is the size
		// *after* the global DPI and UI-scale factors; feeding it back into
		// PushFont applies them a second time, which ImGui's own header warns
		// about in capitals.
		ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * multiplier);
	}

	void PopTextScale()
	{
		ImGui::PopFont();
	}

	void TextCaption(const char* fmt, ...)
	{
		PushTextScale(EditorTheme::Type::Caption);
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextSecondary);

		va_list args;
		va_start(args, fmt);
		ImGui::TextV(fmt, args);
		va_end(args);

		ImGui::PopStyleColor();
		PopTextScale();
	}

	void TextDisplay(const char* fmt, ...)
	{
		PushTextScale(EditorTheme::Type::Display);
		ImGui::PushStyleColor(ImGuiCol_Text, EditorTheme::Colors().TextPrimary);

		va_list args;
		va_start(args, fmt);
		ImGui::TextV(fmt, args);
		va_end(args);

		ImGui::PopStyleColor();
		PopTextScale();
	}

	void SectionHeader(const char* label)
	{
		const auto& colors = EditorTheme::Colors();

		// Smaller than body text, not larger. It looks backwards written down
		// and is what every dense tool does: the separator line already says
		// "a new group starts here", so the words only have to name it. A
		// heading set larger than the content competes with the content.
		ImGui::Spacing();
		PushTextScale(EditorTheme::Type::Caption);
		ImGui::PushStyleColor(ImGuiCol_Text, colors.TextSecondary);
		ImGui::SeparatorText(label);
		ImGui::PopStyleColor();
		PopTextScale();
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

		// Narrower than it is tall, and narrower again than the first attempt.
		//
		// A square badge is the obvious choice and the wrong one: it holds a
		// single letter, and three squares plus their fields do not fit the
		// inspector. Two thirds was still too generous -- with every row on the
		// same label column, a 0.75 was rendering as "0.7", and a value that
		// silently drops a digit is worse than one that is slightly cramped.
		// Just over half the height is all one glyph needs.
		const ImVec2 buttonSize = { Math::Max(frameHeight * 0.54f, 15.0f), frameHeight };

		// One pixel between a badge and its field so they read as one control,
		// and a real gap between the three so they read as three.
		const float hairline = 1.0f;
		const float betweenAxes = EditorTheme::Space::Tight;

		const float available = ImGui::GetContentRegionAvail().x;
		const float furniture = 3.0f * (buttonSize.x + hairline) + 2.0f * betweenAxes;

		// A floor, not the fix. `available` shrinks with the panel and
		// `furniture` does not, so in principle this can go negative -- but it
		// does not at any size reachable here, and measuring that mattered:
		// the assertion the rewrite cured came from the old fixed 140px label
		// column starving CalcItemWidth(), not from this subtraction. Keep the
		// clamp because a negative width is an assert rather than a glitch;
		// do not mistake it for the reason the panel works.
		const float fieldWidth = Math::Max((available - furniture) / 3.0f, kMinControlWidth);

		const ImVec4 axisColors[3] = { colors.AxisX, colors.AxisY, colors.AxisZ };
		const char* axisLabels[3] = { "X", "Y", "Z" };
		const char* fieldIds[3]   = { "##x", "##y", "##z" };
		float* components[3] = { &values.x, &values.y, &values.z };

		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0.0f, 0.0f });

		// The badge is about 20px wide and FramePadding.x is 8, which leaves an
		// inner box of ~4px for a glyph that needs ~9. ImGui centres a label
		// inside the *inner* rectangle, so the letter was being centred in a
		// space narrower than itself and clipped hard against the left edge --
		// which is why X, Y and Z looked shoved into a corner rather than sat
		// in the middle of their colour.
		//
		// Zero horizontal padding gives the whole badge back to the glyph;
		// ButtonTextAlign then centres it in the space it can actually use.
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
							ImVec2{ 0.0f, ImGui::GetStyle().FramePadding.y });
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2{ 0.5f, 0.5f });

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

		ImGui::PopStyleVar(3);
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
			if (!BeginProperties("##row", 0.38f, false))
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

	bool AccentButton(const char* label, const ImVec2& size)
	{
		return AccentFilledButton(label, size);
	}

	bool IconButton(const char* id, IconKind kind, const char* tooltip, bool active)
	{
		const auto& colors = EditorTheme::Colors();
		const float side = ImGui::GetFrameHeight();

		const ImVec2 at = ImGui::GetCursorScreenPos();

		// The active one is the mark's tile; the rest are ordinary buttons.
		// Which is the point of the cut being reserved: in a row of seven, the
		// one that is on is the only one shaped differently.
		bool pressed = false;
		bool hovered = false;
		if (active)
		{
			pressed = AccentFilledButton(id, { side, side });
			hovered = ImGui::IsItemHovered();
		}
		else
		{
			pressed = ImGui::Button(id, { side, side });
			hovered = ImGui::IsItemHovered();
		}

		// OnAccent while filled, for the same reason AccentButton exists: the
		// glyph is the label here, and a label on an accent fill has to clear
		// 4.5:1 against it.
		const ImU32 tint = ImGui::GetColorU32(
			active ? colors.OnAccent : (hovered ? colors.TextPrimary : colors.TextSecondary));

		// 14%, not 24%. A 30px button inset by a quarter leaves ~14px of icon,
		// and at 14px a 1px stroke with arrowheads a pixel and a half across
		// collapses into a smudge -- which is what made the gizmo glyphs look
		// like marks rather than symbols. The icons are drawn inside 0.16-0.84
		// of their canvas already, so they carry their own breathing room.
		const float inset = side * 0.14f;
		DrawIcon(ImGui::GetWindowDrawList(), { at.x + inset, at.y + inset },
				 side - inset * 2.0f, kind, tint);

		if (hovered && tooltip)
			ImGui::SetTooltip("%s", tooltip);

		return pressed;
	}

	int SegmentedControl(const char* id, const char* const* labels, int count, int current)
	{
		if (count <= 0)
			return current;

		int chosen = current;

		ImGui::PushID(id);
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 1.0f, 0.0f });

		const float available = ImGui::GetContentRegionAvail().x;
		const float each = Math::Max((available - (count - 1)) / (float)count, kMinControlWidth);

		for (int i = 0; i < count; i++)
		{
			ImGui::PushID(i);
			const bool selected = i == current;

			// The selected segment is the one place a fill of the accent is
			// right: it is state, and it is the state of a control.
			const bool clicked = selected
				? AccentFilledButton(labels[i], ImVec2(each, 0.0f))
				: ImGui::Button(labels[i], ImVec2(each, 0.0f));

			if (clicked)
				chosen = i;

			if (i < count - 1)
				ImGui::SameLine();
			ImGui::PopID();
		}

		ImGui::PopStyleVar();
		ImGui::PopID();
		return chosen;
	}

	namespace
	{
		// Installed by the editor; empty in any host that has no better idea.
		std::function<bool()> s_LaunchBake;
		std::function<bool()> s_BakeRunning;
	}

	void SetBakeLauncher(std::function<bool()> launch, std::function<bool()> running)
	{
		s_LaunchBake = std::move(launch);
		s_BakeRunning = std::move(running);
	}

	bool LaunchBake(Scene* scene)
	{
		if (s_LaunchBake)
			return s_LaunchBake();
		if (scene)
			scene->RequestLightingBake();
		return scene != nullptr;
	}

	bool BakeRunning(Scene* scene)
	{
		if (s_BakeRunning && s_BakeRunning())
			return true;
		return scene && scene->LightingBakePending();
	}

	bool BakedGiNotice(Scene* scene)
	{
		if (!scene)
			return false;

		const Scene::BakedGi baked = scene->GetBakedGiState();
		if (baked != Scene::BakedGi::NoVolume && baked != Scene::BakedGi::NoBake)
			return false;

		ImGui::Spacing();
		ImGui::TextColored(EditorTheme::Colors().Warning,
						   "Baked GI is not available: rendering Realtime");

		if (baked == Scene::BakedGi::NoVolume)
		{
			ImGui::TextDisabled("This scene has no Irradiance Volume, so there is "
								"nothing stored to read.");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Add an entity with an Irradiance Volume component, "
								  "scale it to cover the space that needs indirect "
								  "light, and bake it from that component.");
			}
			return true;
		}

		const Scene::FieldStatus status = scene->GetFieldStatus();
		ImGui::TextDisabled("No bake matches this scene's current lighting.");
		if (ImGui::IsItemHovered() && !status.File.empty())
		{
			// **Named, so it can be acted on.** The same argument the log line
			// makes: the files are hash-named, so "no bake" without a name is
			// true and useless.
			ImGui::SetTooltip("Looked for %s\nin %s\n\nA bake is one file per "
							  "lighting. Change a light -- or let a script change one "
							  "-- and the scene asks for a different file, so each "
							  "lighting a scene can be in needs its own bake.",
							  status.File.filename().string().c_str(),
							  status.Directory.string().c_str());
		}

		const bool pending = BakeRunning(scene);
		ImGui::BeginDisabled(pending);
		if (ImGui::Button(pending ? "Baking..." : "Bake lighting now"))
			LaunchBake(scene);
		ImGui::EndDisabled();

		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Bakes the scene as saved, in the background -- the "
							  "editor stays free and the Build Log shows the "
							  "baker's progress. The files land beside the scene "
							  "and load the moment they do.");
		}

		return true;
	}
}
