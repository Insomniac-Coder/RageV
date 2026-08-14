#include <rvpch.h>
#include "SceneSerializer.h"
#include "RageV/IO/VFS.h"
#include <fstream>
#include <algorithm>
#include "Components.h"
#include "RageV/UI/Interaction.h"
#include "ComponentRegistry.h"
#include "FieldSerializer.h"
#include "RageV/Renderer/Renderer.h"
#include "RageV/Renderer/PostSettings.h"
#include "RageV/Renderer/RenderSettings.h"
#include <cstring>

namespace RageV
{
	namespace
	{
		// Which key an entity actually stores a component under. Version 1 keyed
		// the colour component "Color" while everything else used its type name.
		//
		// This returns a key rather than a node on purpose. Two yaml-cpp traps
		// live here:
		//
		//   * Node::operator= assigns *into the document* rather than rebinding
		//     the handle, so `node = fallback;` silently rewrites the file's
		//     contents in memory.
		//   * The non-const operator[] inserts the key when it is missing, so
		//     merely testing for a component would add it.
		//
		// Both are avoided by resolving the key first and indexing a const node
		// exactly once.
		const char* ComponentKey(const YAML::Node& entity, const ComponentDesc& desc)
		{
			if (entity[desc.Name])
				return desc.Name;
			return nullptr;
		}

		// Whether this file's value for a field is something other than the
		// field's default.
		//
		// By reading it into a default-constructed block and comparing the
		// bytes, rather than by parsing the scalar and comparing values per
		// type: the point is to ask a question the *registry* can answer for
		// any field, so a setting added later is covered without anybody
		// remembering this function exists.
		//
		// Trivially-copyable fields only, which every setting that moved is
		// (bool, int, float, enum). A String field would have to be compared
		// rather than memcmp'd, so it is excluded rather than mishandled.
		bool DiffersFromDefault(const YAML::Node& value, const FieldDesc& field, void* block)
		{
			if (field.Size == 0 || field.Type == FieldType::String)
				return false;

			std::vector<uint8_t> original(field.Size);
			std::memcpy(original.data(), field.Access(block), field.Size);

			ReadField(value, field, block);
			const bool differs =
				std::memcmp(original.data(), field.Access(block), field.Size) != 0;

			// Restored, so the caller's block stays pristine for the next
			// field -- reading one key must not change the answer for another.
			std::memcpy(field.Access(block), original.data(), field.Size);
			return differs;
		}

		std::string Join(const std::vector<std::string>& names)
		{
			std::string text;
			for (size_t i = 0; i < names.size(); i++)
			{
				if (i)
					text += (i + 1 == names.size()) ? " and " : ", ";
				text += names[i];
			}
			return text;
		}

		// A version-5 scene still carries the render and post settings that
		// version 6 moved. Say so, once, naming them and where each went.
		//
		// **Reported rather than migrated**, because migrating means a scene
		// load silently editing the project file or minting an asset, which is
		// hard to undo and unpleasant to discover. **Reported rather than
		// dropped**, because dropping is precisely what happened to
		// `TemporalFeedback` when it was missing from this serializer: three
		// scenes that set it rendered identically and the reason took a
		// diagnosis to find.
		//
		// Only keys whose value differs from the default are named. Every
		// generated test scene in this repository writes `AntiAliasing: 0`
		// because its generator always has, and a warning that fires on those
		// is a warning nobody reads.
		void ReportMovedSettings(const YAML::Node& environment, const std::string& sceneName)
		{
			RenderSettings render;
			PostSettings post;

			std::vector<std::string> toProject;
			std::vector<std::string> toProfile;

			for (const FieldDesc& field : RenderSettingsRegistry::Fields())
			{
				const YAML::Node& constEnv = environment;
				if (const YAML::Node value = constEnv[field.Name])
				{
					if (DiffersFromDefault(value, field, &render))
						toProject.push_back(field.Name);
				}
			}

			for (const FieldDesc& field : PostSettingsRegistry::Fields())
			{
				const YAML::Node& constEnv = environment;
				if (const YAML::Node value = constEnv[field.Name])
				{
					if (DiffersFromDefault(value, field, &post))
						toProfile.push_back(field.Name);
				}
			}

			if (toProject.empty() && toProfile.empty())
				return;

			std::string message = "Scene '" + sceneName + "' still sets ";
			if (!toProject.empty())
			{
				message += Join(toProject) + ", which moved to the .rvproject";
				if (!toProfile.empty())
					message += "; and ";
			}
			if (!toProfile.empty())
			{
				message += Join(toProfile) +
						   ", which moved to a .rvpostprofile attached to a camera";
			}
			message += ". Those values were not applied. ENGINE-NOTES 7s.";

			RV_CORE_WARN("{0}", message);
		}
	}

	SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& sceneRef)
		:m_SceneRef(sceneRef)
	{
	}

	SceneSerializer::~SceneSerializer()
	{
	}

	std::string SceneSerializer::SerializeToString()
	{
		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Scene" << YAML::Value << "Untitled";
		emitter << YAML::Key << "Version" << YAML::Value << kVersion;

		// Scene-wide, not owned by any entity -- and, since version 6, only
		// what a scene genuinely owns: ambient light and the sky. The cost
		// settings that used to be written here live on the project and the
		// look settings live on a `.rvpostprofile`. ENGINE-NOTES 7s.
		//
		// Registry-driven, like every component below it. The hand-written
		// list this replaced is where `TemporalFeedback` went missing.
		emitter << YAML::Key << "Environment";
		emitter << YAML::BeginMap;
		WriteFields(emitter, SceneEnvironmentRegistry::Fields(),
					&m_SceneRef->GetEnvironment());
		emitter << YAML::EndMap;

		emitter << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

		// EnTT iterates its entity storage in reverse creation order. Writing
		// that directly would flip the file's entity order on every save/load
		// cycle, so the round trip would never be stable. Reversing restores
		// creation order, which the deserializer then reproduces exactly.
		std::vector<entt::entity> handles;
		m_SceneRef->m_Registry.each([&](auto handle) { handles.push_back(handle); });
		std::reverse(handles.begin(), handles.end());

		for (entt::entity handle : handles)
		{
			Entity entity = { handle, m_SceneRef.get() };
			if (!entity)
				continue;

			SerializeEntity(emitter, entity);
		}

		emitter << YAML::EndSeq;
		emitter << YAML::EndMap;

		return std::string(emitter.c_str());
	}

	bool SceneSerializer::Serialize(const std::string& filepath)
	{
		std::ofstream file(filepath.c_str());
		if (!file)
		{
			RV_CORE_ERROR("Could not open '{0}' for writing", filepath);
			return false;
		}

		file << SerializeToString();
		return true;
	}

	void SceneSerializer::SerializeEntity(YAML::Emitter& emitter, Entity entity)
	{
		emitter << YAML::BeginMap;
		// A real identity. This used to be the literal string "12345678890"
		// for every entity in every scene.
		emitter << YAML::Key << "EntityID" << YAML::Value << (uint64_t)entity.GetUUID();

		// Identity and hierarchy are structural rather than editable data, so
		// they are written here rather than described in the registry -- a UUID
		// reference is not a field type the inspector can present.
		if (entity.HasComponent<RelationshipComponent>())
		{
			auto& relationship = entity.GetComponent<RelationshipComponent>();
			if (relationship.Parent.IsValid())
			{
				// Only the parent link. Child lists are derived from it on
				// load, so the two cannot disagree on disk.
				emitter << YAML::Key << "RelationshipComponent";
				emitter << YAML::BeginMap;
				emitter << YAML::Key << "Parent" << YAML::Value << (uint64_t)relationship.Parent;
				emitter << YAML::EndMap;
			}
		}

		// Everything else comes from the registry. Adding a field to a
		// component now writes it here without touching this function.
		for (const ComponentDesc& desc : ComponentRegistry::All())
		{
			void* component = desc.TryGet(entity);
			if (!component)
				continue;

			emitter << YAML::Key << desc.Name;
			emitter << YAML::BeginMap;

			// VisibleIf is ignored on purpose: hiding a field in the inspector
			// must not drop it from disk, or switching a light away from Spot
			// and back would lose its cone angles.
			for (const FieldDesc& field : desc.Fields)
				WriteField(emitter, field, component);

			if (desc.SerializeExtra)
				desc.SerializeExtra(emitter, component);

			emitter << YAML::EndMap;
		}

		emitter << YAML::EndMap;
	}

	bool SceneSerializer::Deserialize(const std::string& filepath)
	{
		// Through the VFS: a scene in a shipped pak and a scene on disk are
		// the same call. Writes stay ordinary files -- only the editor writes,
		// and the editor edits loose projects.
		std::string text;
		if (!VFS::ReadText(filepath, text))
		{
			RV_CORE_ERROR("Could not open '{0}' for reading", filepath);
			return false;
		}

		return DeserializeFromString(text);
	}

	// Snapshot of a subtree, in the same document shape a whole scene uses so
	// that one reader handles both.
	std::string SceneSerializer::SerializeSubtree(Entity root)
	{
		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Scene" << YAML::Value << "Subtree";
		emitter << YAML::Key << "Version" << YAML::Value << kVersion;
		emitter << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

		// Depth first from the root, so a parent is always written before its
		// children and creation order is reproduced on restore.
		std::vector<Entity> pending{ root };
		while (!pending.empty())
		{
			Entity entity = pending.back();
			pending.pop_back();
			if (!entity)
				continue;

			SerializeEntity(emitter, entity);

			const auto& children = m_SceneRef->GetChildren(entity);
			for (auto it = children.rbegin(); it != children.rend(); ++it)
				pending.push_back(m_SceneRef->GetEntityByUUID(*it));
		}

		emitter << YAML::EndSeq;
		emitter << YAML::EndMap;
		return std::string(emitter.c_str());
	}

	bool SceneSerializer::DeserializeAdditive(const std::string& yaml)
	{
		return Read(yaml, ReadMode::Additive);
	}

	bool SceneSerializer::DeserializeFromString(const std::string& yaml)
	{
		return Read(yaml, ReadMode::Replace);
	}

	Entity SceneSerializer::Instantiate(const std::string& yaml)
	{
		Entity root;
		if (!Read(yaml, ReadMode::Instantiate, &root))
			return {};
		return root;
	}

	bool SceneSerializer::Read(const std::string& yaml, ReadMode mode, Entity* firstRoot)
	{
		YAML::Node node;
		try
		{
			node = YAML::Load(yaml);
		}
		catch (const YAML::Exception& e)
		{
			RV_CORE_ERROR("Scene parse failed: {0}", e.what());
			return false;
		}

		if (!node["Scene"])
		{
			RV_CORE_ERROR("Invalid scene file: no Scene key");
			return false;
		}

		const int version = node["Version"] ? node["Version"].as<int>() : 1;
		if (version > kVersion)
		{
			RV_CORE_ERROR("Scene was written by a newer version ({0} > {1})", version, kVersion);
			return false;
		}
		if (version < kVersion)
			RV_CORE_WARN("Loading a version {0} scene and upgrading it to {1}", version, kVersion);

		// Loading a file replaces the scene. Without this, opening one merged
		// it into whatever was already there. Restoring a subtree for undo does
		// the opposite and adds to what is there.
		if (mode == ReadMode::Replace)
		{
			m_SceneRef->m_Registry.clear();
			m_SceneRef->m_EntityMap.clear();
			m_SceneRef->m_Environment = SceneEnvironment{};

			if (const YAML::Node environment = node["Environment"])
			{
				// Sparse and registry-driven: a key the file does not carry
				// keeps the struct's default, which is why a scene written
				// before the sky existed gains a gradient rather than a black
				// void.
				ReadFields(environment, SceneEnvironmentRegistry::Fields(),
						   &m_SceneRef->m_Environment);

				// And say so if this file still carries the settings that
				// moved, rather than dropping them without a word.
				if (version < 6)
					ReportMovedSettings(environment, node["Scene"].as<std::string>("Untitled"));
			}
		}

		const YAML::Node entities = node["Entities"];
		if (!entities)
			return true;

		// Parents are resolved after every entity exists: a child can appear
		// before its parent in the file.
		std::vector<std::pair<UUID, UUID>> pendingParents;

		// Old id -> new id, when instantiating. Parent links and entity
		// reference fields are both rewritten through it, so a copy points at
		// its own copies rather than at the original's entities.
		std::unordered_map<UUID, UUID> remapped;

		// Every EntityRef field read while instantiating, to be remapped once
		// the whole subtree is in and the map is complete. Held as
		// (entity, component, field) rather than as a pointer to the value:
		// adding a component to a later entity moves the pool the earlier one
		// lives in, and a pointer taken now would be pointing into the old
		// allocation by the time it was used.
		struct PendingReference
		{
			UUID Entity;
			const ComponentDesc* Component;
			const FieldDesc* Field;
		};
		std::vector<PendingReference> pendingReferences;

		for (const YAML::Node& entityNode : entities)
		{
			// Version 1 wrote the same hardcoded ID for every entity, so those
			// files get fresh ones -- honouring what is on disk would collapse
			// the whole scene onto a single identity.
			UUID id = (version >= 2 && entityNode["EntityID"])
					? UUID(entityNode["EntityID"].as<uint64_t>())
					: UUID();

			if (mode == ReadMode::Instantiate)
			{
				const UUID fresh;
				remapped[id] = fresh;
				id = fresh;
			}

			Entity entity = m_SceneRef->CreateEntityWithUUID(id);

			// The first entity in the file is the subtree's root, because
			// SerializeSubtree writes depth first from it.
			if (firstRoot && !*firstRoot)
				*firstRoot = entity;

			if (const YAML::Node relationship = entityNode["RelationshipComponent"])
			{
				if (const YAML::Node parent = relationship["Parent"])
					pendingParents.emplace_back(id, UUID(parent.as<uint64_t>()));
			}

			for (const ComponentDesc& desc : ComponentRegistry::All())
			{
				const char* key = ComponentKey(entityNode, desc);
				if (!key)
					continue;

				const YAML::Node componentNode = entityNode[key];
				if (!componentNode.IsMap())
					continue;

				void* component = desc.Add(entity);

				// Versions 1 and 2 nested the camera's projection settings in a
				// "Camera" sub-map; the fields are flat now, so both places are
				// checked per field.
				const YAML::Node nested = componentNode["Camera"];

				for (const FieldDesc& field : desc.Fields)
				{
					const YAML::Node direct = componentNode[field.Name];
					ReadField(direct ? direct : (nested ? nested[field.Name] : direct),
							  field, component);

					// An entity reference inside an instantiated subtree has to
					// follow the copy, exactly as a parent link does -- a
					// button in a prefab must call the script on *its* manager,
					// not on the one belonging to whoever placed the prefab
					// first.
					//
					// Deferred rather than remapped here, because a reference
					// may point at an entity later in the file, and because
					// adding components moves the storage this pointer is in.
					if (mode == ReadMode::Instantiate && field.Type == FieldType::Entity)
						pendingReferences.push_back({ id, &desc, &field });
				}

				if (desc.DeserializeExtra)
					desc.DeserializeExtra(componentNode, component);
				if (desc.OnChanged)
					desc.OnChanged(component);
			}
		}

		// Linked directly rather than through Scene::SetParent. SetParent
		// preserves the world transform by recomputing the local one, which is
		// right for an editor drag and wrong here: what is on disk already is
		// the local transform.
		for (const auto& [childID, parentID] : pendingParents)
		{
			// A parent outside the subtree stays outside: an instantiated
			// prefab's root has no parent in its own file, and the remap only
			// covers what the file contained.
			UUID resolvedParent = parentID;
			if (mode == ReadMode::Instantiate)
			{
				const auto it = remapped.find(parentID);
				if (it == remapped.end())
					continue;
				resolvedParent = it->second;
			}

			Entity child = m_SceneRef->GetEntityByUUID(childID);
			Entity parent = m_SceneRef->GetEntityByUUID(resolvedParent);

			if (!child || !parent)
			{
				RV_CORE_WARN("Scene references a parent that is not in the file; entity left at the root");
				continue;
			}

			child.GetComponent<RelationshipComponent>().Parent = resolvedParent;
			parent.GetComponent<RelationshipComponent>().Children.push_back(childID);
		}

		// After every entity exists, so a reference pointing forward in the file
		// resolves as readily as one pointing back.
		for (const PendingReference& pending : pendingReferences)
		{
			Entity owner = m_SceneRef->GetEntityByUUID(pending.Entity);
			if (!owner)
				continue;

			void* component = pending.Component->TryGet(owner);
			if (!component)
				continue;

			EntityRef& reference = *(EntityRef*)pending.Field->Access(component);

			// A reference *out* of the subtree is left exactly as it is. That
			// is the deliberate half: a prefab whose button drives a scene-wide
			// GameManager should keep driving that one, and only references to
			// entities the file itself contained are the prefab's own.
			const auto it = remapped.find(reference.Value);
			if (it != remapped.end())
				reference = EntityRef(it->second);
		}

		m_SceneRef->UpdateWorldTransforms();

		// A button whose OnClick names something unreachable, reported when the
		// scene opens rather than when somebody eventually clicks it.
		//
		// **Replace only.** Instantiating a prefab or restoring an undone
		// delete brings in a *fragment*, and a binding pointing at the rest of
		// the scene would read as broken while the fragment is the only thing
		// that has been read so far.
		//
		// A warning, not a failure: an author mid-edit has half-wired buttons
		// all the time, and a load that refused them would be unusable. The
		// packager is where this becomes an error.
		if (mode == ReadMode::Replace)
		{
			for (const UI::BindingProblem& problem : UI::ValidateBindings(*m_SceneRef))
				RV_CORE_WARN("{0}", problem.Describe());
		}

		// Used to return false unconditionally, so every caller that checked
		// the result saw a successful load as a failure.
		return true;
	}
}
