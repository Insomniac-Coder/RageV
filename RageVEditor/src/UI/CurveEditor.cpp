#include "CurveEditor.h"
#include "EditorTheme.h"
#include "RageV/Math/Math.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace RageV::UI
{
	namespace
	{
		constexpr float kGraphHeight = 120.0f;
		constexpr float kStripHeight = 46.0f;
		constexpr float kGrabRadius = 7.0f;

		// The value range a scalar curve is drawn against.
		//
		// Not fixed at 0..1: a size curve is in world units and routinely goes
		// past one, and a graph that clips its own curve is worse than useless.
		// Grown to fit what is there, with a floor so a flat zero curve is not
		// divided by nothing.
		void ValueRange(const Curve& curve, float& low, float& high)
		{
			low = 0.0f;
			high = 1.0f;

			for (const Curve::Key& key : curve.GetKeys())
			{
				low = Math::Min(low, key.Value[0]);
				high = Math::Max(high, key.Value[0]);
			}

			if (high - low < 1e-3f)
				high = low + 1.0f;

			// A margin, so a point at the maximum is not drawn half outside the
			// box and impossible to grab.
			const float margin = (high - low) * 0.08f;
			low -= margin;
			high += margin;
		}

		ImU32 KeyColor(const Curve& curve, size_t index, bool hovered)
		{
			if (curve.GetChannels() >= 3)
			{
				const Curve::Key& key = curve.GetKeys()[index];
				return ImGui::GetColorU32(ImVec4(key.Value[0], key.Value[1], key.Value[2], 1.0f));
			}
			return ImGui::GetColorU32(hovered ? ImVec4(1.0f, 0.85f, 0.4f, 1.0f)
											  : EditorTheme::Colors().Accent);
		}
	}

	float CurveEditor::GetHeight(const Curve& curve)
	{
		return curve.GetChannels() >= 3 ? kStripHeight : kGraphHeight;
	}

	bool CurveEditor::Draw(const char* id, Curve& curve, AssetHandle handle)
	{
		ImGui::PushID(id);
		ImGui::PushID((int)(uint64_t)handle);

		const bool gradient = curve.GetChannels() >= 3;
		const float height = GetHeight(curve);
		const float width = Math::Max(120.0f, ImGui::GetContentRegionAvail().x);

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(width, height);
		ImDrawList* draw = ImGui::GetWindowDrawList();

		// One invisible button over the whole area claims the mouse, so
		// dragging a point does not also drag the panel behind it.
		ImGui::InvisibleButton("##area", size);
		const bool areaHovered = ImGui::IsItemHovered();

		draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
							ImGui::GetColorU32(EditorTheme::Colors().BgBase), 3.0f);

		float low = 0.0f;
		float high = 1.0f;
		if (!gradient)
			ValueRange(curve, low, high);

		auto toScreen = [&](float time, float value)
		{
			const float x = origin.x + Math::Clamp(time, 0.0f, 1.0f) * size.x;
			const float normalised = (value - low) / (high - low);
			const float y = origin.y + (1.0f - Math::Clamp(normalised, 0.0f, 1.0f)) * size.y;
			return ImVec2(x, y);
		};

		auto toCurve = [&](const ImVec2& screen, float& time, float& value)
		{
			time = Math::Clamp((screen.x - origin.x) / size.x, 0.0f, 1.0f);
			const float normalised = 1.0f - Math::Clamp((screen.y - origin.y) / size.y, 0.0f, 1.0f);
			value = low + normalised * (high - low);
		};

		bool changed = false;

		if (gradient)
		{
			// The ramp itself, sampled across the strip. Drawn as thin
			// rectangles rather than a gradient primitive because the curve is
			// piecewise linear over an arbitrary number of stops, which no
			// two-colour gradient call can express.
			const int steps = (int)Math::Min(size.x, 256.0f);
			for (int i = 0; i < steps; i++)
			{
				const float t0 = (float)i / (float)steps;
				const float t1 = (float)(i + 1) / (float)steps;
				const Vec4 c = curve.Evaluate(t0);

				draw->AddRectFilled(ImVec2(origin.x + t0 * size.x, origin.y),
									ImVec2(origin.x + t1 * size.x + 1.0f, origin.y + size.y - 14.0f),
									ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, 1.0f)));
			}
		}
		else
		{
			// Gridlines at the quarters, so a shape can be read rather than
			// just seen.
			for (int i = 1; i < 4; i++)
			{
				const float x = origin.x + size.x * (float)i / 4.0f;
				const float y = origin.y + size.y * (float)i / 4.0f;
				const ImU32 grid = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.06f));
				draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), grid);
				draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), grid);
			}

			// The curve, sampled rather than drawn key to key: what is shown
			// has to be what Evaluate answers, including its clamped ends,
			// or the picture and the particle disagree.
			const int steps = (int)Math::Min(size.x, 192.0f);
			ImVec2 previous = toScreen(0.0f, curve.EvaluateScalar(0.0f));
			for (int i = 1; i <= steps; i++)
			{
				const float t = (float)i / (float)steps;
				const ImVec2 point = toScreen(t, curve.EvaluateScalar(t));
				draw->AddLine(previous, point,
							  ImGui::GetColorU32(ImVec4(1.0f, 0.55f, 0.2f, 0.95f)), 2.0f);
				previous = point;
			}
		}

		// --- the keys ---------------------------------------------------------
		const ImVec2 mouse = ImGui::GetIO().MousePos;

		// Which key the pointer is over. Nearest rather than first, so
		// overlapping stops can both be reached.
		size_t hovered = (size_t)-1;
		float best = kGrabRadius * kGrabRadius;
		for (size_t i = 0; i < curve.GetKeyCount(); i++)
		{
			const Curve::Key& key = curve.GetKeys()[i];
			const ImVec2 at = gradient
							? ImVec2(origin.x + key.Time * size.x, origin.y + size.y - 7.0f)
							: toScreen(key.Time, key.Value[0]);

			const float dx = mouse.x - at.x;
			const float dy = mouse.y - at.y;
			const float distance = dx * dx + dy * dy;
			if (distance < best)
			{
				best = distance;
				hovered = i;
			}
		}

		static size_t s_Dragging = (size_t)-1;
		static ImGuiID s_DraggingIn = 0;
		const ImGuiID self = ImGui::GetID("##area");

		// Drag state is keyed on which editor owns it, so releasing over a
		// different curve cannot move a key in this one.
		if (areaHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered != (size_t)-1)
		{
			s_Dragging = hovered;
			s_DraggingIn = self;
		}

		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && s_DraggingIn == self)
		{
			s_Dragging = (size_t)-1;
			s_DraggingIn = 0;
		}

		if (s_DraggingIn == self && s_Dragging < curve.GetKeyCount())
		{
			float time = 0.0f;
			float value = 0.0f;
			toCurve(mouse, time, value);

			const Curve::Key& key = curve.GetKeys()[s_Dragging];
			Vec4 moved(key.Value[0], key.Value[1], key.Value[2], key.Value[3]);

			// A gradient stop moves in time only. Its colour is edited by the
			// swatch below, because dragging in two dimensions cannot say
			// which of three channels was meant.
			if (!gradient)
				moved.x = value;

			// MoveKey re-sorts and answers the new index, so a point dragged
			// past its neighbour keeps being the one under the cursor instead
			// of the drag jumping to whichever key inherited the old index.
			s_Dragging = curve.MoveKey(s_Dragging, time, moved);
			changed = true;
		}

		for (size_t i = 0; i < curve.GetKeyCount(); i++)
		{
			const Curve::Key& key = curve.GetKeys()[i];
			const ImVec2 at = gradient
							? ImVec2(origin.x + key.Time * size.x, origin.y + size.y - 7.0f)
							: toScreen(key.Time, key.Value[0]);

			const bool active = i == hovered || (s_DraggingIn == self && i == s_Dragging);
			draw->AddCircleFilled(at, active ? 6.0f : 4.5f, KeyColor(curve, i, active));
			draw->AddCircle(at, active ? 6.0f : 4.5f,
							ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.9f)), 0, 1.5f);
		}

		// --- adding and removing ----------------------------------------------
		// Double click on empty space adds a key where the pointer is, which is
		// the gesture every curve editor uses. On a key it removes it, so both
		// live on one button and neither needs a menu.
		if (areaHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			if (hovered != (size_t)-1)
			{
				// Never below one: an empty curve evaluates to its fallback,
				// which reads as the effect vanishing rather than as an edit.
				if (curve.GetKeyCount() > 1)
				{
					curve.RemoveKey(hovered);
					changed = true;
				}
			}
			else
			{
				float time = 0.0f;
				float value = 0.0f;
				toCurve(mouse, time, value);

				// A new stop takes the colour already there, so adding one does
				// not change the ramp until it is dragged -- an add that alters
				// the picture is indistinguishable from a misclick.
				curve.AddKey(time, gradient ? curve.Evaluate(time)
											: Vec4(value, 0.0f, 0.0f, 0.0f));
				changed = true;
			}
		}

		if (areaHovered && hovered == (size_t)-1)
			ImGui::SetTooltip("Double click to add a point.\nDrag a point to move it, "
							  "double click it to remove it.");

		// --- the selected stop's colour ---------------------------------------
		// A gradient needs a colour picker somewhere, and the hovered stop is
		// the one the pointer is already on.
		if (gradient && hovered != (size_t)-1)
		{
			const Curve::Key& key = curve.GetKeys()[hovered];
			Vec4 color(key.Value[0], key.Value[1], key.Value[2], 1.0f);

			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::ColorEdit3("##stop", Math::ValuePtr(color),
								  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
			{
				curve.MoveKey(hovered, key.Time, color);
				changed = true;
			}
		}

		ImGui::PopID();
		ImGui::PopID();
		return changed;
	}
}
