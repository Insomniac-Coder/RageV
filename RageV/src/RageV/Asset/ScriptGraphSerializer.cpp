#include <rvpch.h>
#include "ScriptGraphSerializer.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"

#include "yaml-cpp/yaml.h"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		// The file's version, so a graph written by a later node set can be
		// refused with a sentence rather than half-read. Bumped when the
		// *format* changes, not when a node is added -- an unknown node name
		// is refused below on its own terms, which is a different question
		// from the file's shape having changed.
		constexpr uint32_t kVersion = 1;
	}

	bool ScriptGraphSerializer::Save(const ScriptGraph& graph,
									 const std::filesystem::path& path)
	{
		// Copies, sorted by id. The in-memory order follows whatever somebody
		// happened to add and delete, and writing that order would mean two
		// saves of an unchanged graph produce different files (7bh, trap 1).
		std::vector<GraphNode> nodes = graph.GetNodes();
		std::vector<GraphLink> links = graph.GetLinks();
		std::sort(nodes.begin(), nodes.end(),
				  [](const GraphNode& a, const GraphNode& b) { return a.Id < b.Id; });
		std::sort(links.begin(), links.end(),
				  [](const GraphLink& a, const GraphLink& b) { return a.Id < b.Id; });

		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "ScriptGraph" << YAML::Value << kVersion;
		emitter << YAML::Key << "NextNodeId" << YAML::Value << graph.GetNextNodeId();
		emitter << YAML::Key << "NextLinkId" << YAML::Value << graph.GetNextLinkId();

		emitter << YAML::Key << "Nodes" << YAML::Value << YAML::BeginSeq;
		for (const GraphNode& node : nodes)
		{
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "Id" << YAML::Value << node.Id;
			emitter << YAML::Key << "Type" << YAML::Value << GraphNodeTypeName(node.Type);
			emitter << YAML::Key << "Pos" << YAML::Value << YAML::Flow << YAML::BeginSeq
					<< node.Position.x << node.Position.y << YAML::EndSeq;

			// Only when it carries something. A literal's number beside every
			// Branch would be four meaningless zeroes inviting an edit.
			if (node.Value.x != 0.0f || node.Value.y != 0.0f
				|| node.Value.z != 0.0f || node.Value.w != 0.0f)
			{
				emitter << YAML::Key << "Value" << YAML::Value << YAML::Flow << YAML::BeginSeq
						<< node.Value.x << node.Value.y << node.Value.z << node.Value.w
						<< YAML::EndSeq;
			}
			if (!node.Text.empty())
				emitter << YAML::Key << "Text" << YAML::Value << node.Text;

			emitter << YAML::EndMap;
		}
		emitter << YAML::EndSeq;

		// Only the ones that say something. A variable at the defaults is
		// exactly what the loader assumes for a name it has never been told
		// about, so writing it would add a line that changes nothing and churn
		// every graph written before declarations existed.
		bool anyVariable = false;
		for (const GraphVariable& variable : graph.GetVariables())
			anyVariable = anyVariable || variable.Public || !variable.ShowInEditor;

		if (anyVariable)
		{
			emitter << YAML::Key << "Variables" << YAML::Value << YAML::BeginSeq;
			for (const GraphVariable& variable : graph.GetVariables())
			{
				if (!variable.Public && variable.ShowInEditor)
					continue;

				emitter << YAML::BeginMap;
				emitter << YAML::Key << "Name" << YAML::Value << variable.Name;
				if (variable.Public)
					emitter << YAML::Key << "Public" << YAML::Value << true;
				if (!variable.ShowInEditor)
					emitter << YAML::Key << "ShowInEditor" << YAML::Value << false;
				emitter << YAML::EndMap;
			}
			emitter << YAML::EndSeq;
		}

		bool anyFunction = false;
		for (const GraphFunction& function : graph.GetFunctions())
			anyFunction = anyFunction || function.Public;

		if (anyFunction)
		{
			emitter << YAML::Key << "Functions" << YAML::Value << YAML::BeginSeq;
			for (const GraphFunction& function : graph.GetFunctions())
			{
				if (!function.Public)
					continue;

				emitter << YAML::BeginMap;
				emitter << YAML::Key << "Name" << YAML::Value << function.Name;
				emitter << YAML::Key << "Public" << YAML::Value << true;
				emitter << YAML::EndMap;
			}
			emitter << YAML::EndSeq;
		}

		emitter << YAML::Key << "Links" << YAML::Value << YAML::BeginSeq;
		for (const GraphLink& link : links)
		{
			emitter << YAML::BeginMap << YAML::Key << "Id" << YAML::Value << link.Id;
			emitter << YAML::Key << "From" << YAML::Value << YAML::Flow << YAML::BeginSeq
					<< link.FromNode << link.FromPin << YAML::EndSeq;
			emitter << YAML::Key << "To" << YAML::Value << YAML::Flow << YAML::BeginSeq
					<< link.ToNode << link.ToPin << YAML::EndSeq;
			emitter << YAML::EndMap;
		}
		emitter << YAML::EndSeq;

		emitter << YAML::EndMap;

		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream file(path);
		if (!file)
		{
			RV_CORE_ERROR("Could not write script graph {0}", path.string());
			return false;
		}
		file << emitter.c_str();
		return true;
	}

	namespace
	{
		// Say it once, to both places. The log is for whoever is running a
		// build; the string is for whoever is looking at the editor.
		bool Refuse(std::string* outError, const std::string& message)
		{
			RV_CORE_ERROR("{0}", message);
			if (outError)
				*outError = message;
			return false;
		}
	}

	bool ScriptGraphSerializer::Load(ScriptGraph& out, const std::filesystem::path& path,
									 std::string* outMessage, GraphLoadMode mode)
	{
		std::string* outError = outMessage;
		if (outMessage)
			outMessage->clear();

		// What DropUnknown left behind, so the caller can say how much.
		std::vector<std::string> droppedTypes;
		uint32_t droppedLinks = 0;

		std::string text;
		if (!IO::VFS::ReadText(path, text))
		{
			RV_CORE_ERROR("Could not read script graph {0}", path.string());
			return false;
		}

		YAML::Node root;
		try
		{
			root = YAML::Load(text);
		}
		catch (const std::exception& e)
		{
			RV_CORE_ERROR("Script graph {0} will not parse: {1}", path.string(), e.what());
			return false;
		}

		if (!root["ScriptGraph"])
		{
			RV_CORE_ERROR("{0} is not a script graph", path.string());
			return false;
		}

		const uint32_t version = root["ScriptGraph"].as<uint32_t>(0u);
		if (version > kVersion)
		{
			RV_CORE_ERROR("Script graph {0} is version {1} and this build reads {2}",
						  path.string(), version, kVersion);
			return false;
		}

		std::vector<GraphNode> nodes;
		uint32_t highestNode = 0;
		if (const YAML::Node list = root["Nodes"])
		{
			for (const YAML::Node& entry : list)
			{
				GraphNode node;
				node.Id = entry["Id"].as<uint32_t>(0u);
				const std::string typeName = entry["Type"].as<std::string>("None");
				node.Type = GraphNodeTypeFromName(typeName);

				// **Refused, not dropped** (10.10). This used to keep the rest
				// of the graph and carry on, which reads as tolerant and is
				// not: the graph then generated without that node, the
				// generated C# was overwritten, and a save wrote the shortened
				// graph back over the file. Nothing along that path is
				// reversible and nothing on it is loud.
				//
				// The version field above is what a *format* change uses. A
				// node type that no longer exists is not a format change; it
				// is this build being unable to represent what the file says,
				// and the only honest answer is to say so and touch nothing.
				if (node.Type == GraphNodeType::None)
				{
					if (mode == GraphLoadMode::DropUnknown)
					{
						droppedTypes.push_back(typeName);
						continue;
					}

					return Refuse(outError, fmt::format(
						"Script graph {0}: node {1} is of type '{2}', which this "
						"build does not know. The graph is left untouched -- "
						"opening or generating it would drop that node and save "
						"the loss.",
						path.string(), node.Id, typeName));
				}

				// Id 0 is not a node this build is too new for: `NextNodeId`
				// starts at 1 and a link names its endpoints by id, so a zero
				// is a file that has been edited by hand or truncated.
				if (node.Id == 0)
				{
					return Refuse(outError, fmt::format(
						"Script graph {0}: a node of type '{1}' has no id. Ids "
						"start at 1 and links name their endpoints by id, so "
						"this file cannot be read as written.",
						path.string(), typeName));
				}

				if (const YAML::Node position = entry["Pos"]; position && position.size() >= 2)
					node.Position = Vec2(position[0].as<float>(0.0f),
										 position[1].as<float>(0.0f));
				if (const YAML::Node value = entry["Value"]; value && value.size() >= 4)
					node.Value = Vec4(value[0].as<float>(0.0f), value[1].as<float>(0.0f),
									  value[2].as<float>(0.0f), value[3].as<float>(0.0f));
				node.Text = entry["Text"].as<std::string>("");

				highestNode = Math::Max(highestNode, node.Id);
				nodes.push_back(node);
			}
		}

		auto known = [&nodes](uint32_t id)
		{
			return std::any_of(nodes.begin(), nodes.end(),
							   [id](const GraphNode& node) { return node.Id == id; });
		};

		std::vector<GraphLink> links;
		uint32_t highestLink = 0;
		if (const YAML::Node list = root["Links"])
		{
			for (const YAML::Node& entry : list)
			{
				const YAML::Node from = entry["From"];
				const YAML::Node to = entry["To"];
				if (!from || from.size() < 2 || !to || to.size() < 2)
					continue;

				GraphLink link;
				link.Id = entry["Id"].as<uint32_t>(0u);
				link.FromNode = from[0].as<uint32_t>(0u);
				link.FromPin = from[1].as<uint32_t>(0u);
				link.ToNode = to[0].as<uint32_t>(0u);
				link.ToPin = to[1].as<uint32_t>(0u);

				// A link to a node that was dropped above, or that was never
				// there, is not a link. Keeping one would leave the canvas
				// drawing a wire into empty space. Counted, because under
				// DropUnknown this is half of what the user is agreeing to.
				if (!known(link.FromNode) || !known(link.ToNode))
				{
					droppedLinks++;
					continue;
				}

				highestLink = Math::Max(highestLink, link.Id);
				links.push_back(link);
			}
		}

		// The declarations (10.13). Absent is not an error and never will be:
		// every graph written before they existed has none, and a variable
		// nothing says anything about keeps the defaults it always had.
		std::vector<GraphVariable> variables;
		if (const YAML::Node list = root["Variables"])
		{
			for (const YAML::Node& entry : list)
			{
				const std::string name = entry["Name"].as<std::string>("");
				if (name.empty())
					continue;

				GraphVariable variable;
				variable.Name = name;
				variable.Public = entry["Public"].as<bool>(false);
				variable.ShowInEditor = entry["ShowInEditor"].as<bool>(true);
				variables.push_back(variable);
			}
		}

		std::vector<GraphFunction> functions;
		if (const YAML::Node list = root["Functions"])
		{
			for (const YAML::Node& entry : list)
			{
				const std::string name = entry["Name"].as<std::string>("");
				if (name.empty())
					continue;

				GraphFunction function;
				function.Name = name;
				function.Public = entry["Public"].as<bool>(false);
				functions.push_back(function);
			}
		}

		out.Clear();
		out.SetContents(std::move(nodes), std::move(links));
		out.SetVariables(std::move(variables));
		out.SetFunctions(std::move(functions));

		// Past the highest id actually present, not merely what the file said:
		// a hand-edited or truncated file must not hand out an id something
		// already has, because a repeated id re-parents links (7bh).
		out.SetNextIds(Math::Max(root["NextNodeId"].as<uint32_t>(1u), highestNode + 1),
					   Math::Max(root["NextLinkId"].as<uint32_t>(1u), highestLink + 1));

		// What DropUnknown cost. Named types rather than a count alone: "1
		// node" is not something anybody can go and look for, and
		// 'NumbersAppend' is.
		if (!droppedTypes.empty() && outMessage)
		{
			std::sort(droppedTypes.begin(), droppedTypes.end());
			std::string names;
			for (const std::string& type : droppedTypes)
			{
				if (!names.empty())
					names += ", ";
				names += "'" + type + "'";
			}

			*outMessage = fmt::format(
				"Opened without {0} node{1} this build cannot read ({2}), and "
				"{3} link{4} that touched {5}.",
				droppedTypes.size(), droppedTypes.size() == 1 ? "" : "s", names,
				droppedLinks, droppedLinks == 1 ? "" : "s",
				droppedTypes.size() == 1 ? "it" : "them");
		}

		return true;
	}
}
