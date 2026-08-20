#pragma once

#include "RageV/Asset/ScriptGraph.h"
#include "RageV/Asset/Asset.h"

#include <imgui.h>

#include <string>
#include <vector>

namespace RageV::UI
{
	// The canvas for a `.rvgraph` (8.10, ENGINE-NOTES 7bh).
	//
	// Hand-rolled on ImGui draw lists rather than on a node-editor library,
	// for the reason `CurveEditor` is: the shape of the interaction is the
	// point, and 300 lines that are understood beat a dependency whose id
	// model has to be worked around. **`CurveEditor`'s rule carries over** --
	// drag state is scoped by asset handle, so two graphs open in sequence
	// cannot inherit one another's half-finished drag.
	//
	// A panel of its own rather than inline in the inspector, unlike the curve:
	// a graph is the thing being worked on for minutes at a time, not a detail
	// of the entity beside it.
	//
	// **Nothing here executes a graph**, and nothing here should ever start to.
	// A graph generates C# and the managed pipeline runs that; if this file
	// ever grows an `Evaluate`, the design in 7bh has been lost.
	class ScriptGraphPanel
	{
	public:
		// Loads a copy of the asset's graph. A copy and not a pointer into the
		// cache: the panel edits freely and only a Save puts it back, which is
		// what makes closing without saving mean something.
		void Open(AssetHandle handle);

		bool IsOpen() const { return m_Handle.IsValid(); }
		AssetHandle GetHandle() const { return m_Handle; }

		// True while there are edits the file does not have. The window title
		// says so, and the editor asks before a project change throws them away.
		bool IsDirty() const { return m_Dirty; }

		void OnImGuiRender(bool* open);

		// Writes the graph back through the asset manager. Also what Ctrl+S in
		// the panel does.
		bool Save();

	private:
		// Screen position of a pin, and the graph-space <-> screen transforms.
		ImVec2 ToScreen(const Vec2& graph) const;
		Vec2 ToGraph(const ImVec2& screen) const;
		ImVec2 PinPos(const GraphNode& node, uint32_t pin, bool input) const;
		ImVec2 NodeSize(const GraphNode& node) const;

		void DrawGrid(ImDrawList* draw, const ImVec2& origin, const ImVec2& size) const;
		void DrawLinks(ImDrawList* draw);
		void DrawNodes(ImDrawList* draw);
		void DrawSidebar();
		void DrawProblems();

		// Put the whole graph in view. Called on open, and bound to F: a graph
		// authored around the origin has nodes at negative coordinates, and a
		// view that starts at 0,0 opens on empty canvas beside the work.
		void FrameAll();

		// Whether the pin under consideration would accept the wire currently
		// being dragged. Drives the dimming, so a type error is visible before
		// the mouse is released rather than explained after it.
		bool WouldAccept(const GraphNode& node, uint32_t pin, bool input) const;

		// The worst severity attached to a node this frame, or -1 for none.
		int IssueLevel(uint32_t node) const;
		void HandleInput(const ImVec2& origin, const ImVec2& size);
		void DrawAddMenu();

		bool IsSelected(uint32_t id) const;
		void Select(uint32_t id, bool add);

		// One snapshot per edit, and the whole graph each time. A graph is a
		// few hundred bytes and an undo that is obviously correct is worth
		// more here than one that is clever -- the terrain brush's "one stroke
		// = one undo" rule, applied to one gesture.
		void PushUndo();
		void Undo();
		void Redo();

		AssetHandle m_Handle;
		ScriptGraph m_Graph;
		bool m_Dirty = false;
		std::string m_Name;

		// The view. Pan is in graph space; zoom multiplies.
		Vec2 m_Pan;
		float m_Zoom = 1.0f;

		std::vector<uint32_t> m_Selection;

		// Dragging nodes.
		bool m_DraggingNodes = false;

		// Dragging a link out of a pin.
		bool m_DraggingLink = false;
		uint32_t m_LinkFromNode = 0;
		uint32_t m_LinkFromPin = 0;
		bool m_LinkFromInput = false;
		// Whether the cursor is currently over a pin that would refuse the
		// wire, so the wire itself can say so while the mouse is still down.
		bool m_DragOverBad = false;

		// Box select.
		bool m_BoxSelecting = false;
		ImVec2 m_BoxStart;

		// Why the last attempted link was refused, shown under the canvas so a
		// wire that will not connect says so rather than vanishing.
		std::string m_LinkRefusal;
		float m_LinkRefusalAge = 0.0f;

		// Where a right-click asked for the menu, in graph space, so the node
		// lands under the cursor rather than at the origin.
		Vec2 m_MenuPosition;

		// Recomputed every frame. A graph is a few dozen nodes and the walk is
		// linear, so caching it against an edit counter would be a staleness
		// bug traded for nothing measurable.
		std::vector<GraphIssue> m_Issues;

		std::vector<ScriptGraph> m_Undo;
		std::vector<ScriptGraph> m_Redo;
	};
}
