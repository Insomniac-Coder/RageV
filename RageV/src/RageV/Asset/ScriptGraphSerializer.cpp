#include <rvpch.h>
#include "ScriptGraphSerializer.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"

#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <fstream>

namespace RageV::Assets
{
	namespace
	{
		// The file's version, so a graph written by a later node set can be
		// refused with a sentence rather than half-read. Bumped when the
		// *format* changes, not when a node is added: an unknown node name is
		// already handled below by dropping that node and saying so.
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

	bool ScriptGraphSerializer::Load(ScriptGraph& out, const std::filesystem::path& path)
	{
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

				// A node this build has never heard of is dropped and named.
				// Silently keeping it would mean saving a graph on an older
				// build quietly deletes work; dropping it *loudly* is the
				// honest half of that trade, and the version above is what a
				// real format change uses instead.
				if (node.Type == GraphNodeType::None || node.Id == 0)
				{
					RV_CORE_WARN("Script graph {0}: dropping node {1} of unknown type '{2}'",
								 path.string(), node.Id, typeName);
					continue;
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
				// drawing a wire into empty space.
				if (!known(link.FromNode) || !known(link.ToNode))
					continue;

				highestLink = Math::Max(highestLink, link.Id);
				links.push_back(link);
			}
		}

		out.Clear();
		out.SetContents(std::move(nodes), std::move(links));

		// Past the highest id actually present, not merely what the file said:
		// a hand-edited or truncated file must not hand out an id something
		// already has, because a repeated id re-parents links (7bh).
		out.SetNextIds(Math::Max(root["NextNodeId"].as<uint32_t>(1u), highestNode + 1),
					   Math::Max(root["NextLinkId"].as<uint32_t>(1u), highestLink + 1));
		return true;
	}
}
