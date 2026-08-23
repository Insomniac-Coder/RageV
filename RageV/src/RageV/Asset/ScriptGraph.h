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

		// Containers (10.9). One pin type per element type rather than one
		// generic one: "a list" is not a type a wire can carry, because the
		// thing that makes iteration expressible is knowing what comes out.
		// Maps are keyed by string always -- a second key axis would double an
		// already-doubled set, and the engine's own named access is
		// string-keyed too.
		NumberList,
		EntityList,
		NumberMap,
		EntityMap,
	};

	const char* GraphPinTypeName(GraphPinType type);

	// How a node becomes C#. **On the descriptor, beside the pins**, because
	// 7bh's one-table rule is what stops a node looking like one thing on the
	// canvas and meaning another in the file -- and at ninety-nine nodes that
	// is no longer a style preference, it is the only way the two stay in step.
	//
	// `Code` is a format over the node's *inputs*: {0} is input 0, {1} input 1,
	// and so on, with exec pins resolving to nothing. So a node is usually one
	// line of this table and no code at all.
	enum class GraphEmit : uint8_t
	{
		// Nothing; the None entry.
		Special = 0,
		// `Code` is an expression over the inputs.
		Expression,
		// `Code` is a statement over them.
		Statement,
		// `Code` is the C# method signature this event overrides.
		Event,
		// `Code` is the C# type of the variable the node's Text names.
		GetVariable,
		SetVariable,
	};

	// The node set. **Grown to cover the scripting API rather than closed at
	// the eighteen v1 shipped with** (10.7): every entry point a C# script can
	// reach -- transform, hierarchy, physics, input, time, audio, raycasts,
	// components, UI and the maths library -- plus variables, which are the one
	// *language* construct a graph cannot work without.
	//
	// What is deliberately still absent, because a node is the wrong shape for
	// it: loops, user-defined functions and collections. Those want their own
	// design rather than a slot in this list.
	enum class GraphNodeType : uint16_t
	{
		None = 0,

		OnCreate,
		OnTick,
		OnFrame,
		OnDestroy,
		OnCollisionEnter,
		OnCollisionStay,
		OnCollisionExit,
		OnTriggerEnter,
		OnTriggerStay,
		OnTriggerExit,
		Branch,
		Sequence,
		ForLoop,
		WhileLoop,
		BreakLoop,
		FunctionEntry,
		CallFunction,
		LiteralBool,
		LiteralFloat,
		LiteralVec3,
		LiteralString,
		SelfEntity,
		GetNumber,
		SetNumber,
		GetVector,
		SetVector,
		GetFlag,
		SetFlag,
		GetText,
		SetText,
		GetEntityVar,
		SetEntityVar,
		GetNumbers,
		SetNumbers,
		GetEntities,
		SetEntities,
		GetNumberMap,
		SetNumberMap,
		GetEntityMap,
		SetEntityMap,
		NumbersAdd,
		NumbersAt,
		NumbersCount,
		NumbersHas,
		NumbersRemoveAt,
		NumbersClear,
		ForEachNumber,
		EntitiesAdd,
		EntitiesAt,
		EntitiesCount,
		EntitiesRemoveAt,
		EntitiesClear,
		ForEachEntity,
		NumberMapSet,
		NumberMapGet,
		NumberMapHas,
		NumberMapRemove,
		NumberMapCount,
		NumberMapClear,
		EntityMapSet,
		EntityMapGet,
		EntityMapHas,
		EntityMapRemove,
		EntityMapCount,
		EntityMapClear,
		Add,
		Subtract,
		Multiply,
		Divide,
		Compare,
		MinOf,
		MaxOf,
		AbsOf,
		ClampOf,
		LerpOf,
		SinOf,
		CosOf,
		AndOf,
		OrOf,
		NotOf,
		MakeVector,
		BreakVectorX,
		BreakVectorY,
		BreakVectorZ,
		VectorAdd,
		VectorSubtract,
		VectorScale,
		VectorLength,
		VectorNormalize,
		VectorDot,
		FindByName,
		SpawnEntity,
		SpawnPrefab,
		DestroyEntity,
		GetParent,
		EntityExists,
		GetEntityName,
		GetPosition,
		SetPosition,
		GetRotation,
		SetRotation,
		GetScale,
		SetScale,
		TranslateBy,
		RotateBy,
		LookAtPoint,
		GetWorldPosition,
		GetForward,
		GetRight,
		GetUp,
		AddForce,
		AddImpulse,
		GetVelocity,
		SetVelocity,
		RaycastHit,
		RaycastEntity,
		RaycastPoint,
		RaycastDistance,
		ActionDown,
		ActionPressed,
		ActionReleased,
		InputAxis,
		FixedDelta,
		ElapsedTime,
		PlaySound2D,
		PlaySoundAt,
		PlaySource,
		StopSource,
		HasComponent,
		AddComponent,
		RemoveComponent,
		GetField,
		SetField,
		SetUIText,
		ButtonClicked,
		NumberToText,
		TextToNumber,
		JoinText,
		TextEquals,
		SetFieldOn,
		RenderSettingText,
		GraphicsApi,
		FeatureActive,
		Log,
		LogWarning,
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

		// How this node becomes C#, and the format it becomes.
		GraphEmit Emit = GraphEmit::Special;
		const char* Code = "";
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

	// What a graph says *about* one of its variables (10.13, ENGINE-NOTES 7bm).
	//
	// Two flags rather than one, because they answer different questions and
	// the owner asked for both. `Public` is the generated field's access
	// modifier -- whether another script can reach it. `ShowInEditor` is
	// whether the Script component puts a row on screen for it. A variable can
	// be public and hidden (something another script drives), or private and
	// shown (a tuning number nobody else should touch), and neither of those
	// is a strange thing to want.
	//
	// **Neither affects per-entity independence.** Both forms are instance
	// fields on the generated class, so two entities carrying the same graph
	// hold their own values -- that is C#'s doing, not a rule this adds.
	struct GraphVariable
	{
		std::string Name;
		GraphPinType Type = GraphPinType::Float;
		bool Public = false;

		// The default is *shown*, matching what C# scripts already do:
		// `ScriptFields.Editable` reflects every instance field, private
		// included, on the argument that a field nobody can tune is the one
		// thing worth having. Unchecking this is opting out of that, and the
		// generator marks the field so the reflection skips it.
		bool ShowInEditor = true;
	};

	// The same question for a user-defined function: can anything outside the
	// class call it. Parameterless, as 7bh's design records -- pins come from a
	// node type, so a per-instance signature would need dynamic pins.
	struct GraphFunction
	{
		std::string Name;
		bool Public = false;
	};

	class ScriptGraph
	{
	public:
		// What a graph made by "New Graph..." starts as (10.11): On Create ->
		// Log, its text on a literal rather than typed into the Log node.
		//
		// Not an empty canvas, for the reason a new C# script is not an empty
		// file -- and the literal is deliberate rather than decoration. **The
		// one thing about a graph that is not guessable is that there are two
		// kinds of wire**, and a starter with only an execution wire in it
		// would teach half of that. Two nodes and a value, one white wire and
		// one coloured, is the smallest graph that shows both.
		//
		// Here rather than in the editor because it is *content*, and content
		// nothing headless can reach is content nothing can check: a starter
		// that does not validate or does not generate is a bad first five
		// minutes and would go unnoticed.
		static ScriptGraph Starter(const std::string& className);
		const std::vector<GraphNode>& GetNodes() const { return m_Nodes; }
		const std::vector<GraphLink>& GetLinks() const { return m_Links; }

		// --- declarations (10.13) ---------------------------------------------
		//
		// A graph is readable without these: a variable a node names and no
		// declaration covers keeps the defaults it had before they existed.
		// They are how somebody *says something* about one.
		const std::vector<GraphVariable>& GetVariables() const { return m_Variables; }
		const std::vector<GraphFunction>& GetFunctions() const { return m_Functions; }

		// By name, or null. The name is the key on both: it is what the nodes
		// carry and what the generated C# is called, so there is nothing else
		// it could be.
		GraphVariable* FindVariable(const std::string& name);
		const GraphVariable* FindVariable(const std::string& name) const;
		GraphFunction* FindFunction(const std::string& name);
		const GraphFunction* FindFunction(const std::string& name) const;

		// Declares one if it is not declared already, and returns it either
		// way -- so the editor can hand somebody a checkbox for a variable
		// that until now existed only because a node said its name.
		GraphVariable& DeclareVariable(const std::string& name, GraphPinType type);
		GraphFunction& DeclareFunction(const std::string& name);

		void SetVariables(std::vector<GraphVariable> variables)
		{
			m_Variables = std::move(variables);
		}
		void SetFunctions(std::vector<GraphFunction> functions)
		{
			m_Functions = std::move(functions);
		}

		// Every variable the graph *uses*, from the nodes, with the type each
		// was used at. The declarations do not have to cover all of them and a
		// declaration for one nothing uses is not an error -- somebody may be
		// part-way through wiring it up.
		std::vector<GraphVariable> UsedVariables() const;

		GraphNode* FindNode(uint32_t id);
		const GraphNode* FindNode(uint32_t id) const;

		uint32_t AddNode(GraphNodeType type, const Vec2& position);

		// Takes the node's links with it, which is the whole reason this is a
		// method rather than an erase at the call site.
		void RemoveNode(uint32_t id);

		// Whether a value of `from` can be used where `to` is wanted.
		//
		// Equal types, plus **widening to String**: the engine's named field
		// API is text (`SetComponentField` takes a string), so a Float, Bool or
		// Vec3 may feed a String input and the generator writes the invariant
		// text form. Without that rule nothing but a String literal could ever
		// reach `Set Field`, which would leave every maths node in the set with
		// no consumer.
		//
		// Widening only. A String into a Float is a parse that can fail, and a
		// graph that hides a failing parse behind a wire is worse than one that
		// refuses to draw it.
		static bool PinAccepts(GraphPinType from, GraphPinType to);

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
		std::vector<GraphVariable> m_Variables;
		std::vector<GraphFunction> m_Functions;
		uint32_t m_NextNodeId = 1;
		uint32_t m_NextLinkId = 1;
	};
}
