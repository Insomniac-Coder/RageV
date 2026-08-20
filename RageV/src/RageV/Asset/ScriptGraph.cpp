#include "rvpch.h"
#include "ScriptGraph.h"

#include <algorithm>
#include <functional>

namespace RageV
{
	namespace
	{
		using P = GraphPinType;

		// The one table. The canvas draws a node from this and the generator
		// will emit one from it, so a node cannot look like one thing on the
		// canvas and mean another in the file.
		std::vector<GraphNodeDesc> BuildDescs()
		{
			std::vector<GraphNodeDesc> descs((size_t)GraphNodeType::Count);

			auto set = [&](GraphNodeType type, const char* name, const char* category,
						   std::vector<GraphPin> inputs, std::vector<GraphPin> outputs,
						   bool isEvent = false)
			{
				GraphNodeDesc& desc = descs[(size_t)type];
				desc.Type = type;
				desc.Name = name;
				desc.Category = category;
				desc.Inputs = std::move(inputs);
				desc.Outputs = std::move(outputs);
				desc.IsEvent = isEvent;
			};

			set(GraphNodeType::None, "None", "", {}, {});

			// --- events: no exec input, because they are where a body starts.
			set(GraphNodeType::OnCreate, "On Create", "Events",
				{}, { { "", P::Exec } }, true);
			set(GraphNodeType::OnUpdate, "On Update", "Events",
				{}, { { "", P::Exec }, { "Delta", P::Float } }, true);
			set(GraphNodeType::OnCollisionEnter, "On Collision Enter", "Events",
				{}, { { "", P::Exec }, { "Other", P::Entity }, { "Speed", P::Float } }, true);

			// --- flow
			set(GraphNodeType::Branch, "Branch", "Flow",
				{ { "", P::Exec }, { "Condition", P::Bool } },
				{ { "True", P::Exec }, { "False", P::Exec } });
			set(GraphNodeType::Sequence, "Sequence", "Flow",
				{ { "", P::Exec } },
				{ { "Then 0", P::Exec }, { "Then 1", P::Exec } });

			// --- literals
			set(GraphNodeType::LiteralBool, "Bool", "Values", {}, { { "", P::Bool } });
			set(GraphNodeType::LiteralFloat, "Float", "Values", {}, { { "", P::Float } });
			set(GraphNodeType::LiteralVec3, "Vector 3", "Values", {}, { { "", P::Vec3 } });
			set(GraphNodeType::LiteralString, "String", "Values", {}, { { "", P::String } });

			// --- the component surface, by registry name. `Text` on the node
			// holds "Component.Field"; the value crosses as a string exactly
			// as it does for C#, which is what keeps the two surfaces equal.
			set(GraphNodeType::GetField, "Get Field", "Component",
				{}, { { "Value", P::String } });
			set(GraphNodeType::SetField, "Set Field", "Component",
				{ { "", P::Exec }, { "Value", P::String } }, { { "", P::Exec } });

			// --- maths
			set(GraphNodeType::Add, "Add", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } });
			set(GraphNodeType::Subtract, "Subtract", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } });
			set(GraphNodeType::Multiply, "Multiply", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } });
			set(GraphNodeType::Divide, "Divide", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Float } });
			set(GraphNodeType::Compare, "Compare", "Maths",
				{ { "A", P::Float }, { "B", P::Float } }, { { "", P::Bool } });

			// --- output
			set(GraphNodeType::Log, "Log", "Output",
				{ { "", P::Exec }, { "Message", P::String } }, { { "", P::Exec } });

			return descs;
		}

		struct NodeName { GraphNodeType Type; const char* Name; };

		// The name written to the file. Deliberately *not* the display name:
		// renaming a node in the menu must not orphan every saved graph.
		const NodeName kNodeNames[] = {
			{ GraphNodeType::None, "None" },
			{ GraphNodeType::OnCreate, "OnCreate" },
			{ GraphNodeType::OnUpdate, "OnUpdate" },
			{ GraphNodeType::OnCollisionEnter, "OnCollisionEnter" },
			{ GraphNodeType::Branch, "Branch" },
			{ GraphNodeType::Sequence, "Sequence" },
			{ GraphNodeType::LiteralBool, "LiteralBool" },
			{ GraphNodeType::LiteralFloat, "LiteralFloat" },
			{ GraphNodeType::LiteralVec3, "LiteralVec3" },
			{ GraphNodeType::LiteralString, "LiteralString" },
			{ GraphNodeType::GetField, "GetField" },
			{ GraphNodeType::SetField, "SetField" },
			{ GraphNodeType::Add, "Add" },
			{ GraphNodeType::Subtract, "Subtract" },
			{ GraphNodeType::Multiply, "Multiply" },
			{ GraphNodeType::Divide, "Divide" },
			{ GraphNodeType::Compare, "Compare" },
			{ GraphNodeType::Log, "Log" },
		};
	}

	const char* GraphPinTypeName(GraphPinType type)
	{
		switch (type)
		{
			case GraphPinType::Exec:   return "Exec";
			case GraphPinType::Bool:   return "Bool";
			case GraphPinType::Float:  return "Float";
			case GraphPinType::Vec3:   return "Vec3";
			case GraphPinType::String: return "String";
			case GraphPinType::Entity: return "Entity";
		}
		return "Exec";
	}

	const std::vector<GraphNodeDesc>& GraphNodeDescs()
	{
		static const std::vector<GraphNodeDesc> descs = BuildDescs();
		return descs;
	}

	const GraphNodeDesc& GraphNodeDescOf(GraphNodeType type)
	{
		const std::vector<GraphNodeDesc>& descs = GraphNodeDescs();
		const size_t index = (size_t)type;
		return index < descs.size() ? descs[index] : descs[0];
	}

	const char* GraphNodeTypeName(GraphNodeType type)
	{
		for (const NodeName& entry : kNodeNames)
		{
			if (entry.Type == type)
				return entry.Name;
		}
		return "None";
	}

	GraphNodeType GraphNodeTypeFromName(const std::string& name)
	{
		for (const NodeName& entry : kNodeNames)
		{
			if (name == entry.Name)
				return entry.Type;
		}
		return GraphNodeType::None;
	}

	GraphNode* ScriptGraph::FindNode(uint32_t id)
	{
		for (GraphNode& node : m_Nodes)
		{
			if (node.Id == id)
				return &node;
		}
		return nullptr;
	}

	const GraphNode* ScriptGraph::FindNode(uint32_t id) const
	{
		return const_cast<ScriptGraph*>(this)->FindNode(id);
	}

	uint32_t ScriptGraph::AddNode(GraphNodeType type, const Vec2& position)
	{
		GraphNode node;
		node.Id = m_NextNodeId++;
		node.Type = type;
		node.Position = position;
		m_Nodes.push_back(node);
		return node.Id;
	}

	void ScriptGraph::RemoveNode(uint32_t id)
	{
		m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
									 [id](const GraphLink& link)
									 {
										 return link.FromNode == id || link.ToNode == id;
									 }),
					  m_Links.end());

		m_Nodes.erase(std::remove_if(m_Nodes.begin(), m_Nodes.end(),
									 [id](const GraphNode& node) { return node.Id == id; }),
					  m_Nodes.end());
	}

	bool ScriptGraph::CanLink(uint32_t fromNode, uint32_t fromPin,
							  uint32_t toNode, uint32_t toPin, std::string& reason) const
	{
		if (fromNode == toNode)
		{
			reason = "a node cannot link to itself";
			return false;
		}

		const GraphNode* from = FindNode(fromNode);
		const GraphNode* to = FindNode(toNode);
		if (!from || !to)
		{
			reason = "one of those nodes is gone";
			return false;
		}

		const GraphNodeDesc& fromDesc = GraphNodeDescOf(from->Type);
		const GraphNodeDesc& toDesc = GraphNodeDescOf(to->Type);
		if (fromPin >= fromDesc.Outputs.size() || toPin >= toDesc.Inputs.size())
		{
			reason = "that pin does not exist";
			return false;
		}

		const GraphPinType fromType = fromDesc.Outputs[fromPin].Type;
		const GraphPinType toType = toDesc.Inputs[toPin].Type;
		if (fromType != toType)
		{
			reason = std::string("a ") + GraphPinTypeName(fromType) + " pin does not fit a "
				   + GraphPinTypeName(toType) + " one";
			return false;
		}

		return true;
	}

	uint32_t ScriptGraph::AddLink(uint32_t fromNode, uint32_t fromPin,
								  uint32_t toNode, uint32_t toPin)
	{
		const GraphNode* from = FindNode(fromNode);
		const bool exec = from
			&& fromPin < GraphNodeDescOf(from->Type).Outputs.size()
			&& GraphNodeDescOf(from->Type).Outputs[fromPin].Type == GraphPinType::Exec;

		// An input takes one link, and an *exec output* takes one too: two
		// would be an order of execution nobody wrote down. A data output may
		// feed as many inputs as it likes -- that is a value being read twice,
		// which is ordinary.
		m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
									 [&](const GraphLink& link)
									 {
										 if (link.ToNode == toNode && link.ToPin == toPin)
											 return true;
										 return exec && link.FromNode == fromNode
											 && link.FromPin == fromPin;
									 }),
					  m_Links.end());

		GraphLink link;
		link.Id = m_NextLinkId++;
		link.FromNode = fromNode;
		link.FromPin = fromPin;
		link.ToNode = toNode;
		link.ToPin = toPin;
		m_Links.push_back(link);
		return link.Id;
	}

	void ScriptGraph::RemoveLink(uint32_t id)
	{
		m_Links.erase(std::remove_if(m_Links.begin(), m_Links.end(),
									 [id](const GraphLink& link) { return link.Id == id; }),
					  m_Links.end());
	}

	bool ScriptGraph::IsInputLinked(uint32_t node, uint32_t pin) const
	{
		for (const GraphLink& link : m_Links)
		{
			if (link.ToNode == node && link.ToPin == pin)
				return true;
		}
		return false;
	}

	std::vector<GraphIssue> ScriptGraph::Validate() const
	{
		std::vector<GraphIssue> issues;

		auto error = [&issues](uint32_t node, std::string message)
		{
			issues.push_back({ GraphIssueSeverity::Error, node, std::move(message) });
		};
		auto warn = [&issues](uint32_t node, std::string message)
		{
			issues.push_back({ GraphIssueSeverity::Warning, node, std::move(message) });
		};

		// --- a link whose ends no longer agree about their type ---------------
		//
		// The canvas refuses to *create* one, so this catches the two ways one
		// arrives anyway: a hand-edited file, and a node whose pins changed
		// between builds. Left unchecked it is the worst kind of defect here --
		// the graph looks right and generates something that will not compile.
		for (const GraphLink& link : m_Links)
		{
			const GraphNode* from = FindNode(link.FromNode);
			const GraphNode* to = FindNode(link.ToNode);
			if (!from || !to)
				continue;

			const GraphNodeDesc& fromDesc = GraphNodeDescOf(from->Type);
			const GraphNodeDesc& toDesc = GraphNodeDescOf(to->Type);

			if (link.FromPin >= fromDesc.Outputs.size()
				|| link.ToPin >= toDesc.Inputs.size())
			{
				error(link.ToNode, std::string(toDesc.Name)
					  + " is linked through a pin that no longer exists");
				continue;
			}

			const GraphPinType fromType = fromDesc.Outputs[link.FromPin].Type;
			const GraphPinType toType = toDesc.Inputs[link.ToPin].Type;
			if (fromType != toType)
			{
				error(link.ToNode, std::string(toDesc.Name) + " takes a "
					  + GraphPinTypeName(toType) + " where "
					  + fromDesc.Name + " gives a " + GraphPinTypeName(fromType));
			}
		}

		// --- inputs nothing feeds ---------------------------------------------
		size_t events = 0;
		for (const GraphNode& node : m_Nodes)
		{
			const GraphNodeDesc& desc = GraphNodeDescOf(node.Type);
			if (desc.IsEvent)
				events++;

			for (uint32_t pin = 0; pin < desc.Inputs.size(); pin++)
			{
				if (IsInputLinked(node.Id, pin))
					continue;

				// An unconnected *exec* input is only a problem if nothing can
				// reach the node, which the reachability pass below answers
				// properly. An unconnected *data* input has no value at all --
				// unless the node carries its own literal for it, which Log
				// does. Getting this wrong is not cosmetic: a false error
				// blocks the file being written, so the check that guards
				// against nothing being generated would itself be the reason.
				if (desc.Inputs[pin].Type == GraphPinType::Exec)
					continue;
				if (node.Type == GraphNodeType::Log && pin == 1 && !node.Text.empty())
					continue;

				const char* name = desc.Inputs[pin].Name;
				error(node.Id, std::string(desc.Name) + "'s "
					  + (name && name[0] ? name : "input") + " has nothing feeding it");
			}

			// A field access with no field is a node that cannot be written.
			if (node.Type == GraphNodeType::GetField || node.Type == GraphNodeType::SetField)
			{
				const size_t dot = node.Text.find('.');
				if (node.Text.empty())
					error(node.Id, std::string(desc.Name) + " names no field");
				else if (dot == std::string::npos || dot == 0 || dot + 1 >= node.Text.size())
					error(node.Id, std::string(desc.Name) + " wants Component.Field, not '"
						  + node.Text + "'");
			}

		}

		if (m_Nodes.empty())
			return issues;

		if (events == 0)
			error(0, "no event node, so this graph generates nothing");

		// --- one event of each kind -------------------------------------------
		for (size_t i = 0; i < m_Nodes.size(); i++)
		{
			if (!GraphNodeDescOf(m_Nodes[i].Type).IsEvent)
				continue;
			for (size_t j = i + 1; j < m_Nodes.size(); j++)
			{
				if (m_Nodes[j].Type != m_Nodes[i].Type)
					continue;
				// Two would be one method written twice, and which one wins
				// would be an accident of ordering.
				error(m_Nodes[j].Id, std::string(GraphNodeDescOf(m_Nodes[j].Type).Name)
					  + " appears twice; there can be one of each event");
			}
		}

		// --- reachability and cycles, over the exec chain ----------------------
		//
		// Walked from the events, because that is exactly how the generator
		// will walk it: anything it does not reach is not emitted, and a loop
		// is a method that never returns.
		std::vector<uint32_t> reached;
		std::vector<uint32_t> stack;

		auto contains = [](const std::vector<uint32_t>& list, uint32_t id)
		{
			return std::find(list.begin(), list.end(), id) != list.end();
		};

		std::function<void(uint32_t)> walk = [&](uint32_t id)
		{
			if (contains(stack, id))
			{
				const GraphNode* node = FindNode(id);
				error(id, std::string(node ? GraphNodeDescOf(node->Type).Name : "a node")
					  + " is part of a loop, which would never finish");
				return;
			}
			if (contains(reached, id))
				return;

			reached.push_back(id);
			stack.push_back(id);

			const GraphNode* node = FindNode(id);
			if (node)
			{
				const GraphNodeDesc& desc = GraphNodeDescOf(node->Type);
				for (uint32_t pin = 0; pin < desc.Outputs.size(); pin++)
				{
					if (desc.Outputs[pin].Type != GraphPinType::Exec)
						continue;
					for (const GraphLink& link : m_Links)
					{
						if (link.FromNode == id && link.FromPin == pin)
							walk(link.ToNode);
					}
				}
			}

			stack.pop_back();
		};

		for (const GraphNode& node : m_Nodes)
		{
			if (GraphNodeDescOf(node.Type).IsEvent)
				walk(node.Id);
		}

		// A pure-data node is reached by being *read*, not by exec, so it
		// counts as reachable when something reachable links to it.
		bool grew = true;
		while (grew)
		{
			grew = false;
			for (const GraphLink& link : m_Links)
			{
				if (contains(reached, link.ToNode) && !contains(reached, link.FromNode))
				{
					reached.push_back(link.FromNode);
					grew = true;
				}
			}
		}

		for (const GraphNode& node : m_Nodes)
		{
			if (!contains(reached, node.Id))
				warn(node.Id, std::string(GraphNodeDescOf(node.Type).Name)
					 + " is not connected to any event, so it will not run");
		}

		// Errors first: a panel showing five warnings above the one error that
		// stops the file being written has buried the thing that matters.
		std::stable_sort(issues.begin(), issues.end(),
						 [](const GraphIssue& a, const GraphIssue& b)
						 {
							 return a.Severity > b.Severity;
						 });
		return issues;
	}

	void ScriptGraph::SetNextIds(uint32_t node, uint32_t link)
	{
		m_NextNodeId = Math::Max(node, 1u);
		m_NextLinkId = Math::Max(link, 1u);
	}

	void ScriptGraph::Clear()
	{
		m_Nodes.clear();
		m_Links.clear();
		m_NextNodeId = 1;
		m_NextLinkId = 1;
	}

	void ScriptGraph::SetContents(std::vector<GraphNode> nodes, std::vector<GraphLink> links)
	{
		m_Nodes = std::move(nodes);
		m_Links = std::move(links);
	}
}
