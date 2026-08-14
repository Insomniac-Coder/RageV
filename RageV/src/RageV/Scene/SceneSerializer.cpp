#include <rvpch.h>
#include "SceneSerializer.h"
#include "RageV/IO/VFS.h"
#include <fstream>
#include <algorithm>
#include "Components.h"
#include "RageV/UI/Interaction.h"
#include "ComponentRegistry.h"
#include "RageV/Renderer/Renderer.h"

namespace RageV
{
	namespace
	{
		YAML::Emitter& EmitVec2(YAML::Emitter& emitter, const Vec2& v)
		{
			emitter << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
			return emitter;
		}

		YAML::Emitter& EmitVec3(YAML::Emitter& emitter, const Vec3& v)
		{
			emitter << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
			return emitter;
		}

		YAML::Emitter& EmitVec4(YAML::Emitter& emitter, const Vec4& v)
		{
			emitter << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
			return emitter;
		}

		Vec2 ReadVec2(const YAML::Node& node, const Vec2& fallback)
		{
			if (!node || !node.IsSequence() || node.size() != 2)
				return fallback;
			return { node[0].as<float>(), node[1].as<float>() };
		}

		Vec3 ReadVec3(const YAML::Node& node, const Vec3& fallback)
		{
			if (!node || !node.IsSequence() || node.size() != 3)
				return fallback;
			return { node[0].as<float>(), node[1].as<float>(), node[2].as<float>() };
		}

		Vec4 ReadVec4(const YAML::Node& node, const Vec4& fallback)
		{
			if (!node || !node.IsSequence() || node.size() != 4)
				return fallback;
			return { node[0].as<float>(), node[1].as<float>(),
					 node[2].as<float>(), node[3].as<float>() };
		}

		// Enums go to disk by name. Reordering an enum should not silently turn
		// every saved cube into a sphere -- that was already true of primitives
		// and is now true of light types and projections too.
		void WriteEnum(YAML::Emitter& emitter, const FieldDesc& field, int value)
		{
			if (field.Hint.EnumNames && value >= 0 && value < field.Hint.EnumCount)
				emitter << field.Hint.EnumNames[value];
			else
				emitter << value;
		}

		int ReadEnum(const YAML::Node& node, const FieldDesc& field, int fallback)
		{
			if (!node)
				return fallback;

			const std::string text = node.as<std::string>();

			if (field.Hint.EnumNames)
			{
				for (int i = 0; i < field.Hint.EnumCount; i++)
				{
					if (text == field.Hint.EnumNames[i])
						return i;
				}
			}

			// Version 1 and 2 wrote enums as indices, so those still load.
			try { return std::stoi(text); }
			catch (...) { return fallback; }
		}

		void WriteField(YAML::Emitter& emitter, const FieldDesc& field, void* component)
		{
			void* value = field.Access(component);
			emitter << YAML::Key << field.Name << YAML::Value;

			switch (field.Type)
			{
				case FieldType::Bool:   emitter << *(bool*)value; break;
				case FieldType::Int:    emitter << *(int*)value; break;
				case FieldType::Enum:   WriteEnum(emitter, field, *(int*)value); break;
				case FieldType::Float:  emitter << *(float*)value; break;
				case FieldType::Vec2:   EmitVec2(emitter, *(Vec2*)value); break;
				case FieldType::Vec3:   EmitVec3(emitter, *(Vec3*)value); break;
				case FieldType::Vec4:   EmitVec4(emitter, *(Vec4*)value); break;
				case FieldType::String: emitter << *(std::string*)value; break;
				case FieldType::Asset:  emitter << (uint64_t)*(AssetHandle*)value; break;
				// The UUID, exactly as an asset reference is written -- and for
				// the same reason: it is the only name for the target that
				// survives this file being closed.
				case FieldType::Entity: emitter << (uint64_t)*(EntityRef*)value; break;
			}
		}

		void ReadField(const YAML::Node& node, const FieldDesc& field, void* component)
		{
			if (!node)
				return;

			// A map where a handle should be is not malformed data -- it is a
			// version-5 scene, whose MeshComponent stored the material as a
			// nested block of scalars. The component's DeserializeExtra reads
			// that block into per-entity overrides; this field keeping its
			// default (no asset) is exactly the right outcome. Warning about it
			// made every legacy scene open under a wall of red that looked like
			// breakage and described none.
			if (field.Type == FieldType::Asset && node.IsMap())
				return;

			void* value = field.Access(component);

			// A field whose text does not convert leaves its default rather
			// than taking down the load. One malformed value in a hand-edited
			// scene should cost that value, not the file.
			try
			{
			switch (field.Type)
			{
				case FieldType::Bool:   *(bool*)value = node.as<bool>(); break;
				case FieldType::Int:    *(int*)value = node.as<int>(); break;
				case FieldType::Enum:   *(int*)value = ReadEnum(node, field, *(int*)value); break;
				case FieldType::Float:  *(float*)value = node.as<float>(); break;
				case FieldType::Vec2:   *(Vec2*)value = ReadVec2(node, *(Vec2*)value); break;
				case FieldType::Vec3:   *(Vec3*)value = ReadVec3(node, *(Vec3*)value); break;
				case FieldType::Vec4:   *(Vec4*)value = ReadVec4(node, *(Vec4*)value); break;
				case FieldType::String: *(std::string*)value = node.as<std::string>(); break;
				case FieldType::Asset:  *(AssetHandle*)value = AssetHandle(node.as<uint64_t>()); break;
				case FieldType::Entity: *(EntityRef*)value = EntityRef(UUID(node.as<uint64_t>())); break;
			}
			}
			catch (const YAML::Exception& e)
			{
				RV_CORE_WARN("Field '{0}' could not be read ({1}); left at its default",
							 field.Name, e.what());
			}
		}

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

		// Scene-wide, not owned by any entity.
		const SceneEnvironment& environment = m_SceneRef->GetEnvironment();
		emitter << YAML::Key << "Environment";
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "AmbientColor" << YAML::Value;
		EmitVec3(emitter, environment.AmbientColor);
		emitter << YAML::Key << "AmbientIntensity" << YAML::Value << environment.AmbientIntensity;
		emitter << YAML::Key << "Sky" << YAML::Value << (uint32_t)environment.Sky;
		emitter << YAML::Key << "SkyHorizon" << YAML::Value;
		EmitVec3(emitter, environment.SkyHorizon);
		emitter << YAML::Key << "SkyZenith" << YAML::Value;
		EmitVec3(emitter, environment.SkyZenith);
		emitter << YAML::Key << "SkyGround" << YAML::Value;
		EmitVec3(emitter, environment.SkyGround);
		emitter << YAML::Key << "SkyIntensity" << YAML::Value << environment.SkyIntensity;
		emitter << YAML::Key << "SkyRotation" << YAML::Value << environment.SkyRotation;
		emitter << YAML::Key << "SkyTexture" << YAML::Value << (uint64_t)environment.SkyTexture;
		emitter << YAML::Key << "Exposure" << YAML::Value << environment.Exposure;
		emitter << YAML::Key << "BloomEnabled" << YAML::Value << environment.BloomEnabled;
		emitter << YAML::Key << "BloomThreshold" << YAML::Value << environment.BloomThreshold;
		emitter << YAML::Key << "BloomKnee" << YAML::Value << environment.BloomKnee;
		emitter << YAML::Key << "BloomIntensity" << YAML::Value << environment.BloomIntensity;
		emitter << YAML::Key << "BloomClamp" << YAML::Value << environment.BloomClamp;
		emitter << YAML::Key << "ShadowsEnabled" << YAML::Value << environment.ShadowsEnabled;
		emitter << YAML::Key << "ShadowCascades" << YAML::Value << environment.ShadowCascades;
		emitter << YAML::Key << "ShadowResolution" << YAML::Value << environment.ShadowResolution;
		emitter << YAML::Key << "ShadowDistance" << YAML::Value << environment.ShadowDistance;
		emitter << YAML::Key << "ShadowSplitLambda" << YAML::Value << environment.ShadowSplitLambda;
		emitter << YAML::Key << "ShadowNormalOffset" << YAML::Value << environment.ShadowNormalOffset;
		emitter << YAML::Key << "AntiAliasing" << YAML::Value << (uint32_t)environment.AA;
		emitter << YAML::Key << "SupersampleFactor" << YAML::Value << environment.SupersampleFactor;
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
				SceneEnvironment& target = m_SceneRef->m_Environment;
				target.AmbientColor = ReadVec3(environment["AmbientColor"], target.AmbientColor);
				if (const YAML::Node intensity = environment["AmbientIntensity"])
					target.AmbientIntensity = intensity.as<float>();

				// Absent in a scene written before the sky existed. Left at the
				// struct's default, which is the gradient -- an older scene
				// gains a sky rather than a black void, and that is the better
				// of the two surprises.
				if (const YAML::Node value = environment["Sky"])
					target.Sky = (SkyType)value.as<uint32_t>();
				target.SkyHorizon = ReadVec3(environment["SkyHorizon"], target.SkyHorizon);
				target.SkyZenith = ReadVec3(environment["SkyZenith"], target.SkyZenith);
				target.SkyGround = ReadVec3(environment["SkyGround"], target.SkyGround);
				if (const YAML::Node value = environment["SkyIntensity"])
					target.SkyIntensity = value.as<float>();
				if (const YAML::Node value = environment["SkyRotation"])
					target.SkyRotation = value.as<float>();
				if (const YAML::Node value = environment["SkyTexture"])
					target.SkyTexture = UUID(value.as<uint64_t>());

				// Absent in a scene written before post processing existed, which is
				// why each is read only if present rather than defaulted to zero.
				if (const YAML::Node value = environment["Exposure"])
					target.Exposure = value.as<float>();
				if (const YAML::Node value = environment["BloomEnabled"])
					target.BloomEnabled = value.as<bool>();
				if (const YAML::Node value = environment["BloomThreshold"])
					target.BloomThreshold = value.as<float>();
				if (const YAML::Node value = environment["BloomKnee"])
					target.BloomKnee = value.as<float>();
				if (const YAML::Node value = environment["BloomIntensity"])
					target.BloomIntensity = value.as<float>();
				if (const YAML::Node value = environment["BloomClamp"])
					target.BloomClamp = value.as<float>();

				if (const YAML::Node value = environment["ShadowsEnabled"])
					target.ShadowsEnabled = value.as<bool>();
				if (const YAML::Node value = environment["ShadowCascades"])
					target.ShadowCascades = value.as<int>();
				if (const YAML::Node value = environment["ShadowResolution"])
					target.ShadowResolution = value.as<int>();
				if (const YAML::Node value = environment["ShadowDistance"])
					target.ShadowDistance = value.as<float>();
				if (const YAML::Node value = environment["ShadowSplitLambda"])
					target.ShadowSplitLambda = value.as<float>();
				if (const YAML::Node value = environment["ShadowNormalOffset"])
					target.ShadowNormalOffset = value.as<float>();
				if (const YAML::Node value = environment["AntiAliasing"])
					target.AA = (AntiAliasing)value.as<uint32_t>();
				if (const YAML::Node value = environment["SupersampleFactor"])
					target.SupersampleFactor = value.as<int>();
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
