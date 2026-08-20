#include "ScriptGraphPanel.h"
#include "EditorTheme.h"

#include "RageV/Asset/AssetManager.h"
#include "RageV/Asset/ScriptGraphGenerator.h"
#include "RageV/Project/Project.h"
#include "RageV/Asset/AssetRegistry.h"
#include "RageV/Core/Log.h"

#include <algorithm>

namespace RageV::UI
{
	namespace
	{
		// ImGui's ImVec2 operators are behind IMGUI_DEFINE_MATH_OPERATORS,
		// which this translation unit does not turn on -- defining it here and
		// not elsewhere is how one header starts meaning two things.
		ImVec2 Add(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
		ImVec2 Sub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
		ImVec2 Mul(const ImVec2& a, float s) { return ImVec2(a.x * s, a.y * s); }

		float LengthSq(const ImVec2& v) { return v.x * v.x + v.y * v.y; }

		// A floor and a ceiling rather than a fixed width. The floor keeps a
		// row of tiny maths nodes looking like a row; the ceiling stops one
		// long field name stretching a node across the canvas, and that is the
		// case the ellipsis and the tooltip are still for.
		// The zoom the wheel and --graph-zoom are both held to. A quarter is
		// where a node is a coloured block and the shape of the graph is all
		// there is to read; two and a half is where one node fills a third of
		// the canvas and there is nothing further to see.
		constexpr float kMinZoom = 0.25f;
		constexpr float kMaxZoom = 2.5f;

		constexpr float kMinNodeWidth = 168.0f;
		constexpr float kMaxNodeWidth = 340.0f;
		constexpr float kHeaderHeight = 26.0f;
		constexpr float kPinRow = 20.0f;
		constexpr float kPinRadius = 5.0f;
		constexpr float kPinHitRadius = 9.0f;
		constexpr float kNodePad = 8.0f;

		// A colour per category, so a graph can be read at a glance before any
		// of the labels are legible -- which at low zoom is the only thing
		// that is.
		ImVec4 CategoryColor(const char* category)
		{
			const std::string name = category ? category : "";
			if (name == "Events")    return ImVec4(0.72f, 0.24f, 0.24f, 1.0f);
			if (name == "Flow")      return ImVec4(0.38f, 0.38f, 0.42f, 1.0f);
			if (name == "Values")    return ImVec4(0.24f, 0.46f, 0.62f, 1.0f);
			if (name == "Component") return ImVec4(0.30f, 0.50f, 0.34f, 1.0f);
			if (name == "Maths")     return ImVec4(0.46f, 0.36f, 0.58f, 1.0f);
			if (name == "Output")    return ImVec4(0.58f, 0.44f, 0.20f, 1.0f);
			return ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
		}

		ImVec4 PinColor(GraphPinType type)
		{
			switch (type)
			{
				case GraphPinType::Exec:   return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
				// Teal, not red. Red is what an error outline is, and a Bool wire
				// in the same colour reads as a broken link on a clean graph --
				// which is the one thing this panel must never say by accident.
				case GraphPinType::Bool:   return ImVec4(0.35f, 0.76f, 0.80f, 1.0f);
				case GraphPinType::Float:  return ImVec4(0.45f, 0.78f, 0.55f, 1.0f);
				case GraphPinType::Vec3:   return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
				case GraphPinType::String: return ImVec4(0.85f, 0.45f, 0.75f, 1.0f);
				case GraphPinType::Entity: return ImVec4(0.40f, 0.65f, 0.90f, 1.0f);
				// Containers share a family: a warm grey-blue for lists, the
				// same hue lighter for maps, so "this wire carries many" reads
				// before the label does.
				case GraphPinType::NumberList: return ImVec4(0.55f, 0.62f, 0.72f, 1.0f);
				case GraphPinType::EntityList: return ImVec4(0.45f, 0.58f, 0.78f, 1.0f);
				case GraphPinType::NumberMap:  return ImVec4(0.70f, 0.72f, 0.60f, 1.0f);
				case GraphPinType::EntityMap:  return ImVec4(0.62f, 0.68f, 0.52f, 1.0f);
			}
			return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
		}

		// Text that stays inside the node, at the canvas's scale.
		//
		// Two bugs in one helper, because they share a cause -- ImGui's
		// AddText draws at the *font's* size and takes no width. So a long
		// field name ran straight out of the node and over its neighbour, and
		// zooming out left the labels at full size on shrinking boxes.
		//
		// `maxWidth` is in graph units, so callers reason in the node's own
		// coordinates and the zoom is applied here once.
		void DrawFitted(ImDrawList* draw, const ImVec2& at, ImU32 color,
						const std::string& text, float maxWidth, float zoom)
		{
			if (text.empty())
				return;

			const float size = ImGui::GetFontSize() * zoom;
			const float limit = maxWidth * zoom;

			auto width = [&](const char* begin, const char* end)
			{
				return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, begin, end).x;
			};

			const char* begin = text.c_str();
			const char* end = begin + text.size();
			if (width(begin, end) <= limit)
			{
				draw->AddText(ImGui::GetFont(), size, at, color, begin, end);
				return;
			}

			// Cut to fit and mark the cut, rather than clipping mid-glyph: a
			// name that merely stops looks like the name, and a wrong name
			// read confidently is worse than a visibly shortened one.
			static const char* kEllipsis = "...";
			const float tail = width(kEllipsis, kEllipsis + 3);
			size_t keep = text.size();
			while (keep > 0 && width(begin, begin + keep) + tail > limit)
				keep--;

			const std::string cut = text.substr(0, keep) + kEllipsis;
			draw->AddText(ImGui::GetFont(), size, at, color,
						  cut.c_str(), cut.c_str() + cut.size());
		}

		// Width of a string at the canvas's scale, in graph units.
		float TextWidth(const std::string& text, float zoom)
		{
			if (text.empty() || zoom <= 0.0f)
				return 0.0f;
			const float size = ImGui::GetFontSize() * zoom;
			return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f,
												   text.c_str()).x / zoom;
		}

		// What a node shows under its header: the literal it carries, or the
		// field it names. Values are edited in the sidebar rather than inline,
		// because an ImGui widget does not scale with the canvas and a text
		// box that is right only at 100% zoom is worse than none.
		std::string NodeSubtitle(const GraphNode& node)
		{
			char buffer[64];
			switch (node.Type)
			{
				case GraphNodeType::LiteralBool:
					return node.Value.x != 0.0f ? "true" : "false";
				case GraphNodeType::LiteralFloat:
					std::snprintf(buffer, sizeof(buffer), "%.3g", node.Value.x);
					return buffer;
				case GraphNodeType::LiteralVec3:
					std::snprintf(buffer, sizeof(buffer), "%.3g, %.3g, %.3g",
								  node.Value.x, node.Value.y, node.Value.z);
					return buffer;
				case GraphNodeType::LiteralString:
				case GraphNodeType::GetField:
				case GraphNodeType::SetField:
				case GraphNodeType::Log:
				case GraphNodeType::LogWarning:
					return node.Text;
				case GraphNodeType::Compare:
				{
					const int mode = (int)node.Value.x;
					const char* names[] = { "<", "<=", "==", ">=", ">" };
					return names[Math::Clamp(mode, 0, 4)];
				}
				default:
					break;
			}

			// Every variable node carries its name in Text, and a canvas full
			// of boxes labelled "Get Number" with no name on them is a canvas
			// nobody can read.
			const GraphEmit emit = GraphNodeDescOf(node.Type).Emit;
			if (emit == GraphEmit::GetVariable || emit == GraphEmit::SetVariable
				|| node.Type == GraphNodeType::FunctionEntry
				|| node.Type == GraphNodeType::CallFunction)
				return node.Text.empty() ? std::string("(unnamed)") : node.Text;

			return std::string();
		}
	}

	void ScriptGraphPanel::Open(AssetHandle handle)
	{
		if (!handle.IsValid())
			return;

		m_Handle = handle;
		m_Name = Assets::Registry::GetAbsolutePath(handle).stem().string();
		m_Dirty = false;

		// Null is "will not load" since 10.10, and the panel has to say so
		// rather than return -- a double-click that opens nothing looks like
		// the editor being broken, which is the wrong thing to have learnt.
		const ScriptGraph* graph = Assets::Manager::GetScriptGraph(handle);
		m_LoadError = graph ? std::string()
							: Assets::Manager::GetScriptGraphError(handle);
		m_Graph = graph ? *graph : ScriptGraph();
		m_DroppedNotice.clear();

		// Everything transient is cleared with the asset, not carried over:
		// a drag half-finished in the last graph must not continue into this
		// one, which is CurveEditor's handle-scoping rule stated as state.
		m_Selection.clear();
		m_Undo.clear();
		m_Redo.clear();
		m_DraggingNodes = false;
		m_DraggingLink = false;
		m_BoxSelecting = false;
		m_LinkRefusal.clear();
		FrameAll();
	}

	void ScriptGraphPanel::FrameAll()
	{
		if (m_Graph.GetNodes().empty())
		{
			m_Pan = Vec2(-60.0f, -60.0f);
			return;
		}

		Vec2 min(FLT_MAX, FLT_MAX);
		for (const GraphNode& node : m_Graph.GetNodes())
		{
			min.x = Math::Min(min.x, node.Position.x);
			min.y = Math::Min(min.y, node.Position.y);
		}

		// The top-left of the content, a margin in from the corner. Not a fit
		// to the canvas: zooming out to swallow a sprawling graph makes every
		// label unreadable, and the first thing anyone does is zoom back in.
		m_Pan = Vec2(min.x - 40.0f, min.y - 40.0f);
		m_Zoom = 1.0f;
	}

	void ScriptGraphPanel::RequestOpen(AssetHandle handle)
	{
		if (!handle.IsValid() || handle == m_Handle)
			return;

		if (!m_Dirty || !m_Handle.IsValid())
		{
			Open(handle);
			return;
		}

		m_PendingOpen = handle;
	}

	void ScriptGraphPanel::SetZoom(float zoom)
	{
		m_Zoom = Math::Clamp(zoom, kMinZoom, kMaxZoom);
	}

	bool ScriptGraphPanel::Save()
	{
		if (!m_Handle.IsValid())
			return false;

		// Nothing on the refusal page offers this, and it still says no: the
		// canvas behind a refusal is empty because the file could not be read,
		// not because the graph is, and writing it back is exactly the loss
		// 10.10 exists to stop.
		if (!m_LoadError.empty())
		{
			RV_ERROR("Refusing to save '{0}' over a graph that would not load",
					 m_Name);
			return false;
		}

		if (!Assets::Manager::SaveScriptGraph(m_Handle, m_Graph))
		{
			RV_ERROR("Could not save script graph '{0}'", m_Name);
			return false;
		}

		m_Dirty = false;
		m_DroppedNotice.clear();

		// The graph and its C# are written together, so what the canvas shows
		// and what the project compiles cannot disagree (7bh). A graph with
		// errors writes no file and removes a stale one -- the panel is
		// already showing why, so this does not say it twice.
		std::vector<GraphIssue> issues;
		Assets::ScriptGraphGenerator::GenerateToFile(m_Graph, m_Name,
													 Project::Root() / "Scripts", issues);
		return true;
	}

	ImVec2 ScriptGraphPanel::ToScreen(const Vec2& graph) const
	{
		// The origin is added by the caller; this is the view transform alone.
		return ImVec2((graph.x - m_Pan.x) * m_Zoom, (graph.y - m_Pan.y) * m_Zoom);
	}

	Vec2 ScriptGraphPanel::ToGraph(const ImVec2& screen) const
	{
		return Vec2(screen.x / m_Zoom + m_Pan.x, screen.y / m_Zoom + m_Pan.y);
	}

	float ScriptGraphPanel::NodeWidth(const GraphNode& node) const
	{
		const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);

		// The title and the subtitle each need the node minus their margins.
		float wanted = TextWidth(desc.Name, m_Zoom) + 16.0f;
		wanted = Math::Max(wanted, TextWidth(NodeSubtitle(node), m_Zoom) + 16.0f);

		// And every row needs its input label, its output label, and the two
		// pin gutters -- measured per row, because it is the widest *pair*
		// that sets the width, not the widest label.
		const size_t rows = Math::Max(desc.Inputs.size(), desc.Outputs.size());
		for (size_t row = 0; row < rows; row++)
		{
			float used = 24.0f;
			if (row < desc.Inputs.size())
				used += TextWidth(desc.Inputs[row].Name, m_Zoom);
			if (row < desc.Outputs.size())
				used += TextWidth(desc.Outputs[row].Name, m_Zoom);
			wanted = Math::Max(wanted, used + 16.0f);
		}

		return Math::Clamp(wanted, kMinNodeWidth, kMaxNodeWidth);
	}

	ImVec2 ScriptGraphPanel::NodeSize(const GraphNode& node) const
	{
		const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
		const size_t rows = Math::Max(desc.Inputs.size(), desc.Outputs.size());
		const float subtitle = NodeSubtitle(node).empty() ? 0.0f : kPinRow;
		return ImVec2(NodeWidth(node),
					  kHeaderHeight + subtitle + (float)rows * kPinRow + kNodePad);
	}

	ImVec2 ScriptGraphPanel::PinPos(const GraphNode& node, uint32_t pin, bool input) const
	{
		const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
		const float subtitle = NodeSubtitle(node).empty() ? 0.0f : kPinRow;
		const float y = kHeaderHeight + subtitle + ((float)pin + 0.5f) * kPinRow;
		const float x = input ? 0.0f : NodeWidth(node);
		(void)desc;
		return ImVec2((node.Position.x - m_Pan.x) * m_Zoom + x * m_Zoom,
					  (node.Position.y - m_Pan.y) * m_Zoom + y * m_Zoom);
	}

	bool ScriptGraphPanel::IsSelected(uint32_t id) const
	{
		return std::find(m_Selection.begin(), m_Selection.end(), id) != m_Selection.end();
	}

	void ScriptGraphPanel::Select(uint32_t id, bool add)
	{
		if (!add)
			m_Selection.clear();
		if (id != 0 && !IsSelected(id))
			m_Selection.push_back(id);
	}

	bool ScriptGraphPanel::WouldAccept(const GraphNode& node, uint32_t pin, bool input) const
	{
		if (!m_DraggingLink)
			return true;

		// A wire runs output -> input, so a pin on the same side as the one it
		// came from is never a candidate.
		if (input == m_LinkFromInput)
			return false;

		const uint32_t fromNode = m_LinkFromInput ? node.Id : m_LinkFromNode;
		const uint32_t fromPin  = m_LinkFromInput ? pin : m_LinkFromPin;
		const uint32_t toNode   = m_LinkFromInput ? m_LinkFromNode : node.Id;
		const uint32_t toPin    = m_LinkFromInput ? m_LinkFromPin : pin;

		std::string reason;
		return m_Graph.CanLink(fromNode, fromPin, toNode, toPin, reason);
	}

	int ScriptGraphPanel::IssueLevel(uint32_t node) const
	{
		int level = -1;
		for (const GraphIssue& issue : m_Issues)
		{
			if (issue.Node == node)
				level = Math::Max(level, (int)issue.Severity);
		}
		return level;
	}

	void ScriptGraphPanel::PushUndo()
	{
		m_Undo.push_back(m_Graph);
		if (m_Undo.size() > 64)
			m_Undo.erase(m_Undo.begin());
		m_Redo.clear();
		m_Dirty = true;
	}

	void ScriptGraphPanel::Undo()
	{
		if (m_Undo.empty())
			return;
		m_Redo.push_back(m_Graph);
		m_Graph = m_Undo.back();
		m_Undo.pop_back();
		m_Selection.clear();
		m_Dirty = true;
	}

	void ScriptGraphPanel::Redo()
	{
		if (m_Redo.empty())
			return;
		m_Undo.push_back(m_Graph);
		m_Graph = m_Redo.back();
		m_Redo.pop_back();
		m_Selection.clear();
		m_Dirty = true;
	}

	void ScriptGraphPanel::DrawGrid(ImDrawList* draw, const ImVec2& origin,
									const ImVec2& size) const
	{
		const EditorTheme::Palette& colors = EditorTheme::Colors();
		draw->AddRectFilled(origin, Add(origin, size),
							ImGui::GetColorU32(colors.BgBase));

		// Two spacings, so panning reads as movement at any zoom: at 25% the
		// fine grid would be a grey wash and only the coarse one is legible.
		const float steps[] = { 24.0f, 24.0f * 5.0f };
		const ImU32 shades[] = { ImGui::GetColorU32(ImVec4(1, 1, 1, 0.035f)),
								 ImGui::GetColorU32(ImVec4(1, 1, 1, 0.075f)) };

		for (int level = 0; level < 2; level++)
		{
			const float step = steps[level] * m_Zoom;
			if (step < 6.0f)
				continue;

			const float startX = -std::fmod(m_Pan.x * m_Zoom, step);
			for (float x = startX; x < size.x; x += step)
			{
				draw->AddLine(ImVec2(origin.x + x, origin.y),
							  ImVec2(origin.x + x, origin.y + size.y), shades[level]);
			}

			const float startY = -std::fmod(m_Pan.y * m_Zoom, step);
			for (float y = startY; y < size.y; y += step)
			{
				draw->AddLine(ImVec2(origin.x, origin.y + y),
							  ImVec2(origin.x + size.x, origin.y + y), shades[level]);
			}
		}
	}

	void ScriptGraphPanel::DrawLinks(ImDrawList* draw)
	{
		const ImVec2 origin = ImGui::GetItemRectMin();

		for (const GraphLink& link : m_Graph.GetLinks())
		{
			const GraphNode* from = m_Graph.FindNode(link.FromNode);
			const GraphNode* to = m_Graph.FindNode(link.ToNode);
			if (!from || !to)
				continue;

			const GraphNodeDesc& fromDesc = GraphNodeDescOf(from->Type);
			if (link.FromPin >= fromDesc.Outputs.size())
				continue;

			const ImVec2 a = Add(origin, PinPos(*from, link.FromPin, false));
			const ImVec2 b = Add(origin, PinPos(*to, link.ToPin, true));

			// Control points pushed out horizontally by a fraction of the gap,
			// so a wire leaves a pin sideways however the two nodes are
			// arranged -- including right to left, where a straight line would
			// cross both nodes.
			const float reach = Math::Max(40.0f, std::fabs(b.x - a.x) * 0.5f) * m_Zoom;
			const ImVec4 color = PinColor(fromDesc.Outputs[link.FromPin].Type);

			draw->AddBezierCubic(a, ImVec2(a.x + reach, a.y), ImVec2(b.x - reach, b.y), b,
								 ImGui::GetColorU32(color),
								 Math::Max(1.5f, 2.4f * m_Zoom));
		}

		// The wire being dragged, to the cursor.
		if (m_DraggingLink)
		{
			const GraphNode* from = m_Graph.FindNode(m_LinkFromNode);
			if (from)
			{
				const ImVec2 a = Add(origin, PinPos(*from, m_LinkFromPin, m_LinkFromInput));
				const ImVec2 b = ImGui::GetIO().MousePos;
				const float reach = Math::Max(40.0f, std::fabs(b.x - a.x) * 0.5f) * m_Zoom;
				const float sign = m_LinkFromInput ? -1.0f : 1.0f;

				// Red the moment the cursor is over a pin that will refuse it,
				// so the answer arrives while the mouse is still down.
				const ImVec4 color = m_DragOverBad
					? ImVec4(0.92f, 0.35f, 0.30f, 0.95f)
					: ImVec4(1.0f, 1.0f, 1.0f, 0.75f);

				draw->AddBezierCubic(a, ImVec2(a.x + reach * sign, a.y),
									 ImVec2(b.x - reach * sign, b.y), b,
									 ImGui::GetColorU32(color),
									 Math::Max(1.5f, 2.0f * m_Zoom));
			}
		}
	}

	void ScriptGraphPanel::DrawNodes(ImDrawList* draw)
	{
		const ImVec2 origin = ImGui::GetItemRectMin();
		const EditorTheme::Palette& colors = EditorTheme::Colors();
		// Level of detail, densest first. A pin label is the most text per
		// pixel on a node and is the first thing to go; the title is the last,
		// because "which node is this" survives being small in a way that
		// "which pin is this" does not.
		const bool showPins = m_Zoom > 0.70f;
		const bool showSubtitle = m_Zoom > 0.50f;
		const bool showTitle = m_Zoom > 0.30f;

		for (const GraphNode& node : m_Graph.GetNodes())
		{
			const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
			const ImVec2 size = Mul(NodeSize(node), m_Zoom);
			const ImVec2 min = Add(origin, ToScreen(node.Position));
			const ImVec2 max = Add(min, size);

			const float rounding = 4.0f * m_Zoom;

			draw->AddRectFilled(min, max, ImGui::GetColorU32(colors.BgSurface), rounding);

			// The header, in the category's colour, clipped to the top corners
			// by drawing the body over it below.
			const ImVec2 headerMax(max.x, min.y + kHeaderHeight * m_Zoom);
			draw->AddRectFilled(min, headerMax,
								ImGui::GetColorU32(CategoryColor(desc.Category)),
								rounding, ImDrawFlags_RoundCornersTop);

			// The outline carries the verdict: selection is the accent, an
			// error is danger, a warning is warning. Errors win over selection
			// -- a node that stops the file being written must look wrong even
			// while you have it picked up.
			const bool selected = IsSelected(node.Id);
			const int level = IssueLevel(node.Id);
			ImVec4 outline = selected ? colors.Accent : colors.Line;
			float thickness = selected ? 2.5f : 1.0f;
			if (level == (int)GraphIssueSeverity::Error)
			{
				outline = colors.Danger;
				thickness = 2.5f;
			}
			else if (level == (int)GraphIssueSeverity::Warning)
			{
				outline = colors.Warning;
				thickness = Math::Max(thickness, 2.0f);
			}
			draw->AddRect(min, max, ImGui::GetColorU32(outline), rounding, 0, thickness);

			if (showTitle)
			{
				const float width = NodeWidth(node);
				DrawFitted(draw, ImVec2(min.x + 8.0f * m_Zoom, min.y + 5.0f * m_Zoom),
						   ImGui::GetColorU32(ImVec4(1, 1, 1, 0.95f)),
						   desc.Name, width - 16.0f, m_Zoom);

				if (showSubtitle)
				{
					DrawFitted(draw, ImVec2(min.x + 8.0f * m_Zoom,
											min.y + (kHeaderHeight + 2.0f) * m_Zoom),
							   ImGui::GetColorU32(colors.TextSecondary),
							   NodeSubtitle(node), width - 16.0f, m_Zoom);
				}
			}

			// Pins. Exec pins are triangles and data pins circles, so control
			// flow can be followed without reading a single label -- which is
			// what makes a graph legible zoomed out.
			auto pin = [&](uint32_t index, const GraphPin& info, bool input)
			{
				const ImVec2 at = Add(origin, PinPos(node, index, input));

				// While a wire is in the air, anything it cannot land on fades
				// out. Teaching the rule before the mistake beats explaining it
				// after -- and the pin that *can* take it stays at full
				// strength, so the eye is led to it.
				ImVec4 tint = PinColor(info.Type);
				if (m_DraggingLink && !WouldAccept(node, index, input))
					tint.w = 0.18f;

				const ImU32 color = ImGui::GetColorU32(tint);
				const float r = kPinRadius * m_Zoom;

				if (info.Type == GraphPinType::Exec)
				{
					draw->AddTriangleFilled(ImVec2(at.x - r, at.y - r),
											ImVec2(at.x - r, at.y + r),
											ImVec2(at.x + r, at.y), color);
				}
				else
				{
					const bool filled = input ? m_Graph.IsInputLinked(node.Id, index) : true;
					if (filled)
						draw->AddCircleFilled(at, r, color, 12);
					else
						draw->AddCircle(at, r, color, 12, 1.6f * m_Zoom);
				}

				if (showPins && info.Name && info.Name[0])
				{
					// Half the node only when the *other* side of this row has a
					// label to protect. On a row where one side is empty --
					// which is most of them, and every output-only row on a
					// loop -- the label gets the whole node, and halving it
					// was what turned "Completed" into "Compl...".
					const GraphNodeDesc& rowDesc = GraphNodeDescOf(node.Type);
					const std::vector<GraphPin>& opposite = input ? rowDesc.Outputs
																  : rowDesc.Inputs;
					const bool shared = index < opposite.size()
									 && opposite[index].Name && opposite[index].Name[0];
					const float room = shared ? NodeWidth(node) * 0.5f - 16.0f
											  : NodeWidth(node) - 28.0f;
					const float size = ImGui::GetFontSize() * m_Zoom;
					const float w = ImGui::GetFont()->CalcTextSizeA(
						size, FLT_MAX, 0.0f, info.Name).x;
					const float x = input ? at.x + 10.0f * m_Zoom
										  : at.x - 10.0f * m_Zoom - Math::Min(w, room * m_Zoom);
					DrawFitted(draw, ImVec2(x, at.y - size * 0.5f),
							   ImGui::GetColorU32(colors.TextSecondary),
							   info.Name, room, m_Zoom);
				}
			};

			for (uint32_t i = 0; i < desc.Inputs.size(); i++)
				pin(i, desc.Inputs[i], true);
			for (uint32_t i = 0; i < desc.Outputs.size(); i++)
				pin(i, desc.Outputs[i], false);
		}
	}

	void ScriptGraphPanel::HandleInput(const ImVec2& origin, const ImVec2& size)
	{
		ImGuiIO& io = ImGui::GetIO();
		const bool hovered = ImGui::IsItemHovered();
		const ImVec2 mouse = io.MousePos;
		const ImVec2 local = Sub(mouse, origin);

		// --- zoom, about the cursor so the point under it stays put ---------
		if (hovered && io.MouseWheel != 0.0f)
		{
			const Vec2 before = ToGraph(local);
			m_Zoom = Math::Clamp(m_Zoom * (1.0f + io.MouseWheel * 0.12f),
								 kMinZoom, kMaxZoom);
			const Vec2 after = ToGraph(local);
			m_Pan.x += before.x - after.x;
			m_Pan.y += before.y - after.y;
		}

		// --- pan on the middle button ---------------------------------------
		if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
		{
			const ImVec2 delta = io.MouseDelta;
			m_Pan.x -= delta.x / m_Zoom;
			m_Pan.y -= delta.y / m_Zoom;
		}

		// --- what is under the cursor ---------------------------------------
		uint32_t hitNode = 0;
		uint32_t hitPin = 0;
		bool hitPinInput = false;
		bool overPin = false;

		// Back to front, so the topmost node wins -- the same order they are
		// drawn in, reversed.
		const std::vector<GraphNode>& nodes = m_Graph.GetNodes();
		for (size_t i = nodes.size(); i-- > 0;)
		{
			const GraphNode& node = nodes[i];
			const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);

			for (uint32_t p = 0; p < desc.Inputs.size() && !overPin; p++)
			{
				if (LengthSq(Sub(local, PinPos(node, p, true))) <= kPinHitRadius * kPinHitRadius)
				{
					overPin = true; hitNode = node.Id; hitPin = p; hitPinInput = true;
				}
			}
			for (uint32_t p = 0; p < desc.Outputs.size() && !overPin; p++)
			{
				if (LengthSq(Sub(local, PinPos(node, p, false))) <= kPinHitRadius * kPinHitRadius)
				{
					overPin = true; hitNode = node.Id; hitPin = p; hitPinInput = false;
				}
			}
			if (overPin)
				break;

			const ImVec2 min = ToScreen(node.Position);
			const ImVec2 max = Add(min, Mul(NodeSize(node), m_Zoom));
			if (local.x >= min.x && local.x <= max.x && local.y >= min.y && local.y <= max.y)
			{
				hitNode = node.Id;
				break;
			}
		}

		// --- what the cursor is over, said in words ---------------------------
		//
		// A pin's name can still be clipped -- at low zoom, or on a field name
		// longer than the node's ceiling -- and its *type* is worth saying even
		// when the name fits, because the colour is a hint and this is the
		// answer. Not while dragging a wire: the canvas is already telling that
		// story with the dimming and the red.
		if (hovered && !m_DraggingLink && !m_DraggingNodes && !m_BoxSelecting)
		{
			if (overPin)
			{
				const GraphNode* node = m_Graph.FindNode(hitNode);
				if (node)
				{
					const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);
					const std::vector<GraphPin>& pins = hitPinInput ? desc.Inputs
																   : desc.Outputs;
					if (hitPin < pins.size())
					{
						const char* name = pins[hitPin].Name;
						ImGui::SetTooltip("%s%s%s",
										  name && name[0] ? name : "(flow)",
										  name && name[0] ? "  -  " : "  -  ",
										  GraphPinTypeName(pins[hitPin].Type));
					}
				}
			}
			else if (hitNode != 0)
			{
				// Only when something was actually cut: a tooltip that repeats
				// a label already on screen is noise on every node.
				const GraphNode* node = m_Graph.FindNode(hitNode);
				if (node)
				{
					const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);
					const std::string subtitle = NodeSubtitle(*node);
					const float room = NodeWidth(*node) - 16.0f;
					const bool clipped =
						TextWidth(desc.Name, m_Zoom) > room
						|| TextWidth(subtitle, m_Zoom) > room;
					if (clipped)
					{
						if (subtitle.empty())
							ImGui::SetTooltip("%s", desc.Name);
						else
							ImGui::SetTooltip("%s\n%s", desc.Name, subtitle.c_str());
					}
				}
			}
		}

		m_DragOverBad = false;
		if (m_DraggingLink && overPin)
		{
			const GraphNode* over = m_Graph.FindNode(hitNode);
			m_DragOverBad = over && !WouldAccept(*over, hitPin, hitPinInput);
		}

		// --- press ------------------------------------------------------------
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (overPin)
			{
				m_DraggingLink = true;
				m_LinkFromNode = hitNode;
				m_LinkFromPin = hitPin;
				m_LinkFromInput = hitPinInput;
			}
			else if (hitNode != 0)
			{
				if (!IsSelected(hitNode))
					Select(hitNode, io.KeyCtrl || io.KeyShift);
				m_DraggingNodes = true;
				PushUndo();
			}
			else
			{
				if (!io.KeyCtrl && !io.KeyShift)
					m_Selection.clear();
				m_BoxSelecting = true;
				m_BoxStart = local;
			}
		}

		// --- drag -------------------------------------------------------------
		if (m_DraggingNodes && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
		{
			const ImVec2 delta = Mul(io.MouseDelta, 1.0f / m_Zoom);
			for (uint32_t id : m_Selection)
			{
				if (GraphNode* node = m_Graph.FindNode(id))
				{
					node->Position.x += delta.x;
					node->Position.y += delta.y;
				}
			}
		}

		// --- release ----------------------------------------------------------
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (m_DraggingLink)
			{
				if (overPin && hitPinInput != m_LinkFromInput)
				{
					// Whichever end was the output is the source. Dragging
					// backwards from an input is the same link, and refusing
					// it would be an editor with a preferred direction.
					const uint32_t fromNode = m_LinkFromInput ? hitNode : m_LinkFromNode;
					const uint32_t fromPin  = m_LinkFromInput ? hitPin  : m_LinkFromPin;
					const uint32_t toNode   = m_LinkFromInput ? m_LinkFromNode : hitNode;
					const uint32_t toPin    = m_LinkFromInput ? m_LinkFromPin  : hitPin;

					std::string reason;
					if (m_Graph.CanLink(fromNode, fromPin, toNode, toPin, reason))
					{
						PushUndo();
						m_Graph.AddLink(fromNode, fromPin, toNode, toPin);
						m_LinkRefusal.clear();
					}
					else
					{
						// Said, not swallowed: a wire that will not connect
						// and gives no reason reads as a broken editor.
						m_LinkRefusal = reason;
						m_LinkRefusalAge = 0.0f;
					}
				}
				m_DraggingLink = false;
			}

			if (m_BoxSelecting)
			{
				const ImVec2 a(Math::Min(m_BoxStart.x, local.x), Math::Min(m_BoxStart.y, local.y));
				const ImVec2 b(Math::Max(m_BoxStart.x, local.x), Math::Max(m_BoxStart.y, local.y));
				if (LengthSq(Sub(b, a)) > 16.0f)
				{
					for (const GraphNode& node : m_Graph.GetNodes())
					{
						const ImVec2 min = ToScreen(node.Position);
						const ImVec2 max = Add(min, Mul(NodeSize(node), m_Zoom));
						if (max.x >= a.x && min.x <= b.x && max.y >= a.y && min.y <= b.y)
							Select(node.Id, true);
					}
				}
				m_BoxSelecting = false;
			}

			m_DraggingNodes = false;
		}

		// --- keys -------------------------------------------------------------
		const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		if (focused)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Selection.empty())
			{
				PushUndo();
				for (uint32_t id : m_Selection)
					m_Graph.RemoveNode(id);
				m_Selection.clear();
			}
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
				io.KeyShift ? Redo() : Undo();
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y))
				Redo();
			if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
				Save();
			if (ImGui::IsKeyPressed(ImGuiKey_F))
				FrameAll();
		}

		// --- the add menu ------------------------------------------------------
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			m_MenuPosition = ToGraph(local);
			ImGui::OpenPopup("##addnode");
		}

		(void)size;
	}

	void ScriptGraphPanel::DrawAddMenu()
	{
		if (!ImGui::BeginPopup("##addnode"))
			return;

		ImGui::TextDisabled("Add node");
		ImGui::Separator();

		// In the table's own order, so the menu and the file agree about how
		// the set is grouped.
		const char* categories[] = { "Events", "Flow", "Functions", "Values", "Variables", "Containers",
									 "Maths", "Logic", "Vector", "Entity",
									 "Transform", "Physics", "Input", "Time",
									 "Audio", "Component", "UI", "Output" };
		for (const char* category : categories)
		{
			if (!ImGui::BeginMenu(category))
				continue;

			for (const GraphNodeDesc& desc : GraphNodeDescs())
			{
				if (desc.Type == GraphNodeType::None
					|| std::string(desc.Category) != category)
					continue;

				if (ImGui::MenuItem(desc.Name))
				{
					PushUndo();
					const uint32_t id = m_Graph.AddNode(desc.Type, m_MenuPosition);
					Select(id, false);
				}
			}
			ImGui::EndMenu();
		}

		ImGui::EndPopup();
	}

	void ScriptGraphPanel::DrawSidebar()
	{
		ImGui::BeginChild("##graphside", ImVec2(300.0f, 0.0f), true);

		// Up until the graph is saved. Somebody who chose "open without it" ten
		// minutes ago is no longer thinking about it, and the next Save is the
		// moment the file stops holding what they dropped.
		if (!m_DroppedNotice.empty())
		{
			const EditorTheme::Palette& warn = EditorTheme::Colors();
			ImGui::PushStyleColor(ImGuiCol_Text, warn.Warning);
			ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
			ImGui::TextUnformatted(m_DroppedNotice.c_str());
			ImGui::TextUnformatted("Saving makes this permanent.");
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::Separator();
			ImGui::Spacing();
		}

		ImGui::TextDisabled("Graph");
		ImGui::Separator();
		ImGui::Text("%s", m_Name.c_str());
		ImGui::Text("%d nodes, %d links",
					(int)m_Graph.GetNodes().size(), (int)m_Graph.GetLinks().size());

		if (ImGui::Button(m_Dirty ? "Save*" : "Save", ImVec2(-1.0f, 0.0f)))
			Save();

		DrawDeclarations();

		ImGui::Spacing();
		ImGui::TextDisabled("Selected");
		ImGui::Separator();

		// Values are edited here rather than inside the node, because an ImGui
		// widget does not scale with the canvas: an inline text box is correct
		// only at 100% zoom and misplaced at every other, and a field that
		// lands next to the box it belongs to is worse than one in a panel.
		if (m_Selection.size() != 1)
		{
			ImGui::TextWrapped(m_Selection.empty()
				? "Nothing selected. Right-click the canvas to add a node."
				: "Several nodes selected.");
		}
		else if (GraphNode* node = m_Graph.FindNode(m_Selection.front()))
		{
			const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);
			ImGui::Text("%s", desc.Name);
			ImGui::TextDisabled("%s", desc.Category);
			ImGui::Spacing();

			char text[128];
			std::snprintf(text, sizeof(text), "%s", node->Text.c_str());

			switch (node->Type)
			{
				case GraphNodeType::LiteralBool:
				{
					bool value = node->Value.x != 0.0f;
					if (ImGui::Checkbox("Value", &value))
					{
						PushUndo();
						node = m_Graph.FindNode(m_Selection.front());
						node->Value.x = value ? 1.0f : 0.0f;
					}
					break;
				}
				case GraphNodeType::LiteralFloat:
					if (ImGui::DragFloat("Value", &node->Value.x, 0.01f))
						m_Dirty = true;
					break;
				case GraphNodeType::LiteralVec3:
					if (ImGui::DragFloat3("Value", &node->Value.x, 0.01f))
						m_Dirty = true;
					break;
				case GraphNodeType::Compare:
				{
					const char* modes[] = { "<", "<=", "==", ">=", ">" };
					int mode = Math::Clamp((int)node->Value.x, 0, 4);
					if (ImGui::Combo("Test", &mode, modes, 5))
					{
						PushUndo();
						node = m_Graph.FindNode(m_Selection.front());
						node->Value.x = (float)mode;
					}
					break;
				}
				case GraphNodeType::LiteralString:
				case GraphNodeType::Log:
				case GraphNodeType::LogWarning:
					if (ImGui::InputText("Text", text, sizeof(text)))
					{
						node->Text = text;
						m_Dirty = true;
					}
					break;
				case GraphNodeType::GetField:
				case GraphNodeType::SetField:
					// "Component.Field", by registry name -- the same named
					// access C# has, which is what stops a graph reaching
					// anything a script could not.
					if (ImGui::InputText("Field", text, sizeof(text)))
					{
						node->Text = text;
						m_Dirty = true;
					}
					ImGui::TextDisabled("Component.Field");
					break;
				default:
					if (desc.Emit == GraphEmit::GetVariable
						|| desc.Emit == GraphEmit::SetVariable
						|| node->Type == GraphNodeType::FunctionEntry
						|| node->Type == GraphNodeType::CallFunction)
					{
						if (ImGui::InputText("Name", text, sizeof(text)))
						{
							node->Text = text;
							m_Dirty = true;
						}
						ImGui::TextWrapped("A field on the generated class, so it survives between events.");
					}
					else
					{
						ImGui::TextDisabled("No settings.");
					}
					break;
			}
		}

		DrawProblems();

		ImGui::Spacing();
		ImGui::TextDisabled("Canvas");
		ImGui::Separator();
		ImGui::Text("Zoom %.0f%%%s", m_Zoom * 100.0f,
					m_Zoom <= kMinZoom + 0.001f ? "  (min)"
					: m_Zoom >= kMaxZoom - 0.001f ? "  (max)" : "");
		ImGui::BulletText("Right-click: add");
		ImGui::BulletText("Middle-drag: pan");
		ImGui::BulletText("Wheel: zoom");
		ImGui::BulletText("Delete: remove");
		ImGui::BulletText("Ctrl+Z / Ctrl+S");

		ImGui::EndChild();
	}

	void ScriptGraphPanel::DrawDeclarations()
	{
		const std::vector<GraphVariable> used = m_Graph.UsedVariables();

		// Functions come from their entry nodes, the same way variables come
		// from their Get and Set nodes.
		std::vector<std::string> functions;
		for (const GraphNode& node : m_Graph.GetNodes())
		{
			if (node.Type == GraphNodeType::FunctionEntry && !node.Text.empty())
				functions.push_back(node.Text);
		}
		std::sort(functions.begin(), functions.end());
		functions.erase(std::unique(functions.begin(), functions.end()),
						functions.end());

		if (used.empty() && functions.empty())
			return;

		ImGui::Spacing();
		ImGui::TextDisabled("Variables and functions");
		ImGui::Separator();

		// Two passes over one list, Public first: what the rest of the project
		// can reach is what a reader should see first. Both headings are drawn
		// whether or not anything is under them, so the two sections read as a
		// structure rather than appearing when something lands in one.
		for (int pass = 0; pass < 2; pass++)
		{
			const bool wantPublic = pass == 0;

			ImGui::Spacing();
			ImGui::TextDisabled(wantPublic ? "  Public" : "  Private");

			bool any = false;

			for (const GraphVariable& variable : used)
			{
				if (variable.Public != wantPublic)
					continue;

				any = true;
				ImGui::PushID(variable.Name.c_str());

				ImGui::Text("%s", variable.Name.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s", GraphPinTypeName(variable.Type));

				// ShowInEditor stays a checkbox: it is a property of the
				// variable, not of which section it is in, and both
				// combinations are things somebody wants.
				bool shown = variable.ShowInEditor;
				if (ImGui::Checkbox("ShowInEditor", &shown))
				{
					PushUndo();
					m_Graph.DeclareVariable(variable.Name, variable.Type).ShowInEditor = shown;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("A row in the Script component, per entity.\n"
									  "Each entity keeps its own value.");

				// And the move. A button rather than a checkbox, because the
				// heading above already says which section this is in and a
				// second control saying the same thing can only disagree with
				// it.
				ImGui::SameLine();
				if (ImGui::SmallButton(wantPublic ? "Make private" : "Make public"))
				{
					PushUndo();
					m_Graph.DeclareVariable(variable.Name, variable.Type).Public = !wantPublic;
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip(wantPublic
						? "Moves it to Private: a private field, which no other\n"
						  "script can reach."
						: "Moves it to Public: a public field, which another\n"
						  "script can read or write.");

				ImGui::PopID();
			}

			for (const std::string& name : functions)
			{
				const GraphFunction* declared = m_Graph.FindFunction(name);
				const bool isPublicNow = declared && declared->Public;
				if (isPublicNow != wantPublic)
					continue;

				any = true;
				ImGui::PushID(("fn" + name).c_str());

				ImGui::Text("%s()", name.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton(wantPublic ? "Make private" : "Make public"))
				{
					PushUndo();
					m_Graph.DeclareFunction(name).Public = !wantPublic;
				}

				ImGui::PopID();
			}

			if (!any)
				ImGui::TextDisabled("    (none)");
		}
	}

	void ScriptGraphPanel::DrawProblems()
	{
		ImGui::Spacing();

		size_t errors = 0;
		for (const GraphIssue& issue : m_Issues)
			errors += issue.Severity == GraphIssueSeverity::Error ? 1 : 0;

		const EditorTheme::Palette& colors = EditorTheme::Colors();

		if (m_Issues.empty())
		{
			ImGui::TextDisabled("Problems");
			ImGui::Separator();
			ImGui::TextColored(colors.Success, "None. This graph will generate.");
			return;
		}

		ImGui::TextDisabled("Problems (%d)", (int)m_Issues.size());
		ImGui::Separator();

		// Said once, at the top, because it is the only line here that decides
		// whether anything is written at all (7bh, trap 2).
		if (errors > 0)
			ImGui::TextColored(colors.Danger,
							   "%d error%s: no C# will be generated.",
							   (int)errors, errors == 1 ? "" : "s");

		ImGui::BeginChild("##problems", ImVec2(0.0f, 190.0f), true);
		for (size_t i = 0; i < m_Issues.size(); i++)
		{
			const GraphIssue& issue = m_Issues[i];
			const bool error = issue.Severity == GraphIssueSeverity::Error;

			ImGui::PushID((int)i);
			ImGui::PushStyleColor(ImGuiCol_Text, error ? colors.Danger : colors.Warning);
			ImGui::TextUnformatted(error ? "[error]" : "[warn]");
			ImGui::PopStyleColor();
			ImGui::SameLine();

			// Clicking takes you to the node. A problem list that names a node
			// you then have to find on a canvas you can pan is half a feature.
			// Wrapped, and the whole message on hover. A list that clips the
			// half of the sentence naming the type that did not fit is a list
			// that reports the problem without saying what it is.
			const float wrap = ImGui::GetContentRegionAvail().x;
			const ImVec2 extent = ImGui::CalcTextSize(issue.Message.c_str(), nullptr,
													  false, wrap);
			if (ImGui::Selectable("##issue", false, 0, ImVec2(0.0f, extent.y))
				&& issue.Node != 0)
			{
				Select(issue.Node, false);
				if (const GraphNode* node = m_Graph.FindNode(issue.Node))
					m_Pan = Vec2(node->Position.x - 120.0f, node->Position.y - 90.0f);
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", issue.Message.c_str());

			ImGui::SameLine(0.0f, 0.0f);
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
			ImGui::TextUnformatted(issue.Message.c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	void ScriptGraphPanel::DrawLoadError()
	{
		const EditorTheme::Palette& colors = EditorTheme::Colors();

		ImGui::Dummy(ImVec2(0.0f, 12.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, colors.Danger);
		ImGui::TextUnformatted("This graph was not opened.");
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::TextDisabled("%s.rvgraph", m_Name.c_str());
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Wrapped rather than truncated: the sentence names the node type and
		// the node's id, which is the whole of what a person needs to go and
		// look, and an ellipsis would take exactly that off the end.
		ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
		ImGui::TextUnformatted(m_LoadError.c_str());
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		ImGui::Spacing();

		// **Why this is a page and not a silent drop.** The editor *can* open
		// the graph without what it cannot read -- that is the second button
		// -- and it does not do so on its own, because dropping a node is a
		// decision about somebody's work and the two ways out are not
		// equivalent. ENGINE-NOTES 7bi, 10.10.
		ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
		ImGui::TextDisabled(
			"Nothing has been changed on disk, and nothing will be until you "
			"save. Open the project on a build that still has this node type to "
			"get the graph back whole -- or open it without that node here, and "
			"rewire what it was connected to.");
		ImGui::PopTextWrapPos();

		ImGui::Spacing();
		if (ImGui::Button("Try again"))
		{
			// For the case where somebody has just fixed the file in another
			// window. Drops the cached refusal so the loader is asked again.
			Assets::Manager::ReloadScriptGraph(m_Handle);
			Open(m_Handle);
		}

		ImGui::SameLine();

		// **The way forward, and why it is a button rather than the default.**
		// Refusing keeps the file safe and leaves the user nowhere: the graph
		// cannot be opened, so it cannot be repaired in the tool that made it.
		// This is the same operation the loader used to perform silently at
		// generate time -- with the difference that somebody has now read what
		// it costs and pressed it, the canvas opens *dirty*, and the file is
		// untouched until they save.
		if (ImGui::Button("Open without it"))
			OpenWithoutUnknown();

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Opens the graph with the parts this build cannot "
							  "read removed.\nNothing is written until you save.");
	}

	void ScriptGraphPanel::OpenWithoutUnknown()
	{
		if (!m_Handle.IsValid())
			return;

		ScriptGraph partial;
		std::string dropped;
		if (!Assets::Manager::LoadScriptGraphWithoutUnknown(m_Handle, partial,
														   &dropped))
			return;

		m_Graph = std::move(partial);
		m_LoadError.clear();
		m_DroppedNotice = dropped;

		// Dirty on purpose: the canvas is showing something the file does not
		// contain, and the title's asterisk plus the unsaved-changes guard are
		// how this editor already says exactly that.
		m_Dirty = true;
		m_Selection.clear();
		m_Undo.clear();
		m_Redo.clear();
		FrameAll();
	}

	void ScriptGraphPanel::OnImGuiRender(bool* open)
	{
		if (!m_Handle.IsValid())
			return;

		char title[160];
		std::snprintf(title, sizeof(title), "Script Graph - %s%s###scriptgraph",
					  m_Name.c_str(), m_Dirty ? " *" : "");

		// A canvas needs room. Without this ImGui opens it at the default
		// couple of hundred pixels, the sidebar takes all of it, and the
		// canvas early-outs on being too small -- which looks like the panel
		// failing to draw rather than being cramped.
		ImGui::SetNextWindowSize(ImVec2(1040.0f, 640.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(520.0f, 300.0f), ImVec2(FLT_MAX, FLT_MAX));

		if (!ImGui::Begin(title, open))
		{
			ImGui::End();
			return;
		}

		if (!m_LoadError.empty())
		{
			DrawLoadError();
			ImGui::End();
			return;
		}

		// Before anything is drawn, so the node outlines and the list agree
		// about the same frame's graph.
		m_Issues = m_Graph.Validate();

		DrawSidebar();
		ImGui::SameLine();

		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size = ImGui::GetContentRegionAvail();
		if (size.x < 32.0f || size.y < 32.0f)
		{
			ImGui::End();
			return;
		}

		// One invisible button over the whole canvas claims the mouse, so
		// dragging a node does not also drag the window behind it -- the same
		// move CurveEditor makes for the same reason.
		ImGui::InvisibleButton("##canvas", size,
							   ImGuiButtonFlags_MouseButtonLeft
							   | ImGuiButtonFlags_MouseButtonRight
							   | ImGuiButtonFlags_MouseButtonMiddle);

		ImDrawList* draw = ImGui::GetWindowDrawList();
		draw->PushClipRect(origin, Add(origin, size), true);

		DrawGrid(draw, origin, size);
		HandleInput(origin, size);
		DrawLinks(draw);
		DrawNodes(draw);

		if (m_BoxSelecting)
		{
			const ImVec2 a = Add(origin, m_BoxStart);
			const ImVec2 b = ImGui::GetIO().MousePos;
			draw->AddRectFilled(a, b, ImGui::GetColorU32(EditorTheme::Colors().AccentMuted));
			draw->AddRect(a, b, ImGui::GetColorU32(EditorTheme::Colors().Accent));
		}

		draw->PopClipRect();

		// The refusal, over the canvas and fading, so it is read without
		// having to be dismissed.
		if (!m_LinkRefusal.empty())
		{
			m_LinkRefusalAge += ImGui::GetIO().DeltaTime;
			if (m_LinkRefusalAge > 2.5f)
			{
				m_LinkRefusal.clear();
			}
			else
			{
				const float alpha = Math::Min(1.0f, (2.5f - m_LinkRefusalAge) / 0.6f);
				draw->AddText(ImVec2(origin.x + 12.0f, origin.y + size.y - 24.0f),
							  ImGui::GetColorU32(ImVec4(0.95f, 0.5f, 0.4f, alpha)),
							  m_LinkRefusal.c_str());
			}
		}

		DrawAddMenu();

		// Asked here rather than at the call site, because the panel is what
		// knows whether anything would be lost.
		if (m_PendingOpen.IsValid())
			ImGui::OpenPopup("Unsaved graph##graphswitch");

		if (ImGui::BeginPopupModal("Unsaved graph##graphswitch", nullptr,
								   ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("'%s' has unsaved changes.", m_Name.c_str());
			ImGui::Spacing();

			if (ImGui::Button("Save and open"))
			{
				Save();
				const AssetHandle next = m_PendingOpen;
				m_PendingOpen = AssetHandle::Invalid();
				Open(next);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard"))
			{
				const AssetHandle next = m_PendingOpen;
				m_PendingOpen = AssetHandle::Invalid();
				Open(next);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_PendingOpen = AssetHandle::Invalid();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		ImGui::End();
	}
}
