#pragma once

#include "RageV/Math/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace RageV
{
	// A visual script (8.10, ENGINE-NOTES 7bh): nodes and the links between
	// them, authored on a canvas and **generated into ordinary C#**.
	//
	// This is an *authoring surface*, not a runtime. Nothing here executes and
	// nothing here is stepped: a `.rvgraph` emits a `.g.cs` that the existing
	// managed pipeline compiles, hot-reloads and attaches by name, which is why
	// there is no third scripting runtime in this engine and why 8.10 stopped
	// being XL. Read 7bh before adding anything that looks like evaluation.
	enum class GraphPinType : uint8_t
	{
		// Control flow, in the Blueprint sense: what gives the generated
		// statements an order. Without it a graph is an expression tree with
		// no way to say "and then".
		Exec = 0,
		Bool,
		Float,
		Vec3,
		String,
		Entity,
	};

	const char* GraphPinTypeName(GraphPinType type);

	// The v1 node set, and it is **closed** (7bh). This is where a visual
	// scripting system turns into a career, so the list is exactly what the
	// check's fixture needs and nothing else. A new node is a new roadmap row,
	// not a slot somebody adds while passing.
	enum class GraphNodeType : uint16_t
	{
		None = 0,

		// Events. Each becomes an override on the generated class, and each is
		// a root of one exec chain.
		OnCreate,
		OnUpdate,
		OnCollisionEnter,

		// Flow.
		Branch,
		Sequence,

		// Literals. `Value` carries the number(s), `Text` the string.
		LiteralBool,
		LiteralFloat,
		LiteralVec3,
		LiteralString,

		// The component surface, by registry name with text values -- the same
		// access C# already has, which is what keeps a graph from reaching
		// anything a script could not.
		GetField,
		SetField,

		// Maths.
		Add,
		Subtract,
		Multiply,
		Divide,
		Compare,

		// Output.
		Log,

		Count,
	};

	struct GraphPin
	{
		const char* Name = "";
		GraphPinType Type = GraphPinType::Exec;
	};

	// What a node *is*: its name, which pins it has and on which side. One
	// table, read by the canvas to draw a node and later by the generator to
	// emit one -- so a node cannot look like one thing and generate another.
	struct GraphNodeDesc
	{
		GraphNodeType Type = GraphNodeType::None;
		const char* Name = "";
		const char* Category = "";
		std::vector<GraphPin> Inputs;
		std::vector<GraphPin> Outputs;

		// An event is a root: it has no exec input and the generator starts a
		// method body at it.
		bool IsEvent = false;
	};

	// Every node type, in menu order. Indexed by GraphNodeType.
	const std::vector<GraphNodeDesc>& GraphNodeDescs();
	const GraphNodeDesc& GraphNodeDescOf(GraphNodeType type);
	const char* GraphNodeTypeName(GraphNodeType type);
	GraphNodeType GraphNodeTypeFromName(const std::string& name);

	struct GraphNode
	{
		// Stable, and **never reused** even after a delete. Two reasons, and
		// the second is the one that bites: links refer to nodes by id, and the
		// generated file is emitted in id order so that two saves of an
		// unchanged graph produce identical text (7bh, trap 1). A recycled id
		// silently re-parents a link.
		uint32_t Id = 0;
		GraphNodeType Type = GraphNodeType::None;

		// Canvas position, in graph space. Not screen space: the canvas pans
		// and zooms, and storing screen coordinates would make a saved graph
		// depend on where the view happened to be.
		Vec2 Position;

		// A literal's numbers, a `Compare`'s mode, or nothing.
		Vec4 Value;

		// A field name, a component name, a log message, or a literal string.
		std::string Text;
	};

	struct GraphLink
	{
		uint32_t Id = 0;
		uint32_t FromNode = 0;
		uint32_t FromPin = 0;
		uint32_t ToNode = 0;
		uint32_t ToPin = 0;
	};

	enum class GraphIssueSeverity : uint8_t
	{
		// The graph still generates; something in it will not do anything.
		Warning,
		// The graph does not generate at all. **No file is written** -- an
		// empty `class Foo : Script` compiles, attaches, runs and does nothing,
		// which is a green result computed from something other than the thing
		// under test (7bf, and 8.13's black frames one layer up).
		Error,
	};

	struct GraphIssue
	{
		GraphIssueSeverity Severity = GraphIssueSeverity::Warning;
		// 0 means the graph as a whole rather than any one node.
		uint32_t Node = 0;
		std::string Message;
	};

	class ScriptGraph
	{
	public:
		const std::vector<GraphNode>& GetNodes() const { return m_Nodes; }
		const std::vector<GraphLink>& GetLinks() const { return m_Links; }

		GraphNode* FindNode(uint32_t id);
		const GraphNode* FindNode(uint32_t id) const;

		uint32_t AddNode(GraphNodeType type, const Vec2& position);

		// Takes the node's links with it, which is the whole reason this is a
		// method rather than an erase at the call site.
		void RemoveNode(uint32_t id);

		// Refuses anything the graph could not generate from, and says which
		// rule stopped it. The canvas shows the reason rather than dropping a
		// dragged link on the floor, because a link that will not connect and
		// does not say why reads as a broken editor.
		bool CanLink(uint32_t fromNode, uint32_t fromPin,
					 uint32_t toNode, uint32_t toPin, std::string& reason) const;

		// Adds the link, replacing whatever occupied the destination. An input
		// takes one link and an exec output takes one: a second would be an
		// order nobody wrote down.
		uint32_t AddLink(uint32_t fromNode, uint32_t fromPin,
						 uint32_t toNode, uint32_t toPin);
		void RemoveLink(uint32_t id);

		// Whether an input pin already has something in it, which is what the
		// canvas draws differently and what the generator will require.
		bool IsInputLinked(uint32_t node, uint32_t pin) const;

		// The ids the next node and link will take. Serialised, so ids stay
		// unique across a save and load rather than restarting and colliding.
		uint32_t GetNextNodeId() const { return m_NextNodeId; }
		uint32_t GetNextLinkId() const { return m_NextLinkId; }
		void SetNextIds(uint32_t node, uint32_t link);

		// Everything wrong with this graph, worst first.
		//
		// Lives here rather than in the panel because the generator needs the
		// same answer: the canvas draws these as marks on nodes, and the
		// generator refuses to write a file while any Error is in the list.
		// Two implementations would drift, and the one that drifted would be
		// the one that decides whether a file is written.
		std::vector<GraphIssue> Validate() const;

		void Clear();

		// Loading writes the arrays wholesale; the serializer is the only
		// caller and it has already validated ids.
		void SetContents(std::vector<GraphNode> nodes, std::vector<GraphLink> links);

	private:
		std::vector<GraphNode> m_Nodes;
		std::vector<GraphLink> m_Links;
		uint32_t m_NextNodeId = 1;
		uint32_t m_NextLinkId = 1;
	};
}
