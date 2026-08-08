#include <rvpch.h>
#include "SceneSerializer.h"
#include <fstream>
#include <algorithm>
#include "Components.h"
#include "RageV/Renderer/Renderer.h"

namespace YAML
{
	template <>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& v)
		{
			Node node;

			node.push_back(v.x);
			node.push_back(v.y);
			node.push_back(v.z);

			return node;
		}

		static bool decode(const Node& node, glm::vec3& v)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;

			v.x = node[0].as<float>();
			v.y = node[1].as<float>();
			v.z = node[2].as<float>();
			return true;
		}
	};

	template <>
	struct convert<glm::vec4>
	{
		static Node encode(const glm::vec4& v)
		{
			Node node;

			node.push_back(v.x);
			node.push_back(v.y);
			node.push_back(v.z);
			node.push_back(v.w);

			return node;
		}

		static bool decode(const Node& node, glm::vec4& v)
		{
			if (!node.IsSequence() || node.size() != 4)
				return false;

			v.x = node[0].as<float>();
			v.y = node[1].as<float>();
			v.z = node[2].as<float>();
			v.w = node[3].as<float>();
			return true;
		}
	};
}

namespace RageV
{
	YAML::Emitter& operator << (YAML::Emitter& emitter, const glm::vec3& vec)
	{
		emitter << YAML::Flow;
		emitter << YAML::BeginSeq << vec.x << vec.y << vec.z << YAML::EndSeq;
		return emitter;
	}

	YAML::Emitter& operator << (YAML::Emitter& emitter, const glm::vec4& vec)
	{
		emitter << YAML::Flow;
		emitter << YAML::BeginSeq << vec.x << vec.y << vec.z << vec.w << YAML::EndSeq;
		return emitter;
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
		// A real identity now. This used to be the literal string
		// "12345678890" for every entity in every scene.
		emitter << YAML::Key << "EntityID" << YAML::Value << (uint64_t)entity.GetUUID();

		if (entity.HasComponent<TagComponent>())
		{
			auto& tag = entity.GetComponent<TagComponent>();
			emitter << YAML::Key << "TagComponent";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "Tag" << YAML::Value << tag.Name;
			emitter << YAML::EndMap;
		}

		// Only the parent link is written. Child lists are derived from it on
		// load, so the two can never disagree on disk.
		if (entity.HasComponent<RelationshipComponent>())
		{
			auto& relationship = entity.GetComponent<RelationshipComponent>();
			if (relationship.Parent.IsValid())
			{
				emitter << YAML::Key << "RelationshipComponent";
				emitter << YAML::BeginMap;
				emitter << YAML::Key << "Parent" << YAML::Value << (uint64_t)relationship.Parent;
				emitter << YAML::EndMap;
			}
		}

		if (entity.HasComponent<TransformComponent>())
		{
			auto& transform = entity.GetComponent<TransformComponent>();
			emitter << YAML::Key << "TransformComponent";
			emitter << YAML::BeginMap;
			// Local. World is derived every frame and is not persisted.
			emitter << YAML::Key << "Position" << YAML::Value << transform.Position;
			emitter << YAML::Key << "Rotation" << YAML::Value << transform.Rotation;
			emitter << YAML::Key << "Scale" << YAML::Value << transform.Scale;
			emitter << YAML::EndMap;
		}

		if (entity.HasComponent<CameraComponent>())
		{
			auto& camera = entity.GetComponent<CameraComponent>();
			emitter << YAML::Key << "CameraComponent";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "Camera";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.Camera.GetProjectionType();
			emitter << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.Camera.GetPerspectiveFOV();
			emitter << YAML::Key << "PerspectiveNearClip" << YAML::Value << camera.Camera.GetPerspectiveNearClip();
			emitter << YAML::Key << "PerspectiveFarClip" << YAML::Value << camera.Camera.GetPerspectiveFarClip();
			emitter << YAML::Key << "OrthographicScale" << YAML::Value << camera.Camera.GetOrthographicSize();
			emitter << YAML::Key << "OrthographicNearClip" << YAML::Value << camera.Camera.GetOrthoNearClip();
			emitter << YAML::Key << "OrthographicFarClip" << YAML::Value << camera.Camera.GetOrthoFarClip();
			emitter << YAML::EndMap;
			emitter << YAML::Key << "isPrimary" << YAML::Value << camera.isPrimary;
			emitter << YAML::Key << "FixedAspectRatio" << YAML::Value << camera.fixedAspectRatio;

			emitter << YAML::EndMap;
		}

		if (entity.HasComponent<ColorComponent>())
		{
			auto& color = entity.GetComponent<ColorComponent>();
			// Was keyed "Color" while every other component used its type name.
			// Version 1 files are still read under the old key below.
			emitter << YAML::Key << "ColorComponent";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "ColorValue" << YAML::Value << color.Color;
			emitter << YAML::EndMap;
		}

		if (entity.HasComponent<MeshComponent>())
		{
			auto& mesh = entity.GetComponent<MeshComponent>();
			emitter << YAML::Key << "MeshComponent";
			emitter << YAML::BeginMap;
			// By name, not by index: reordering the enum should not silently
			// turn every saved cube into a sphere.
			emitter << YAML::Key << "Primitive" << YAML::Value << PrimitiveTypeName(mesh.Primitive);

			if (mesh.Material)
			{
				const auto& params = mesh.Material->GetParams();
				emitter << YAML::Key << "Material";
				emitter << YAML::BeginMap;
				emitter << YAML::Key << "BaseColor" << YAML::Value << params.BaseColor;
				emitter << YAML::Key << "Emissive" << YAML::Value << params.EmissiveColor;
				emitter << YAML::Key << "Metallic" << YAML::Value << params.Metallic;
				emitter << YAML::Key << "Roughness" << YAML::Value << params.Roughness;
				emitter << YAML::Key << "Occlusion" << YAML::Value << params.Occlusion;
				emitter << YAML::EndMap;
			}

			emitter << YAML::EndMap;
		}

		// Lights were never serialized, so saving and reloading a scene silently
		// dropped every light in it.
		if (entity.HasComponent<LightComponent>())
		{
			auto& light = entity.GetComponent<LightComponent>();
			emitter << YAML::Key << "LightComponent";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "Type" << YAML::Value << (int)light.Light.GetLightType();
			emitter << YAML::Key << "Color" << YAML::Value << light.Light.GetLightColor();
			emitter << YAML::Key << "Intensity" << YAML::Value << light.Light.GetIntensity();
			emitter << YAML::Key << "Range" << YAML::Value << light.Light.GetRange();
			emitter << YAML::Key << "InnerCone" << YAML::Value << light.Light.GetInnerCone();
			emitter << YAML::Key << "OuterCone" << YAML::Value << light.Light.GetOuterCone();
			emitter << YAML::EndMap;
		}

		emitter << YAML::EndMap;
	}

	bool SceneSerializer::Deserialize(const std::string& filepath)
	{
		std::ifstream file(filepath);
		if (!file)
		{
			RV_CORE_ERROR("Could not open '{0}' for reading", filepath);
			return false;
		}

		std::stringstream ss;
		ss << file.rdbuf();
		return DeserializeFromString(ss.str());
	}

	bool SceneSerializer::DeserializeFromString(const std::string& yaml)
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
		{
			RV_CORE_WARN("Loading a version {0} scene; entity IDs and hierarchy will be regenerated", version);
		}

		// Loading replaces the scene. Without this, opening a file merged it
		// into whatever was already there.
		m_SceneRef->m_Registry.clear();
		m_SceneRef->m_EntityMap.clear();

		auto entities = node["Entities"];
		if (!entities)
			return true;

		// Parents are resolved after every entity exists: a child can appear
		// before its parent in the file.
		std::vector<std::pair<UUID, UUID>> pendingParents;

		for (auto entity : entities)
		{
			// Version 1 wrote the same hardcoded ID for every entity, so those
			// files get fresh ones -- honouring what is on disk would collapse
			// the whole scene onto a single identity.
			UUID id = (version >= 2 && entity["EntityID"])
					? UUID(entity["EntityID"].as<uint64_t>())
					: UUID();

			std::string name;
			if (auto tag = entity["TagComponent"])
				name = tag["Tag"].as<std::string>();

			Entity newEntity = m_SceneRef->CreateEntityWithUUID(id, name);

			if (auto relationship = entity["RelationshipComponent"])
			{
				if (auto parent = relationship["Parent"])
					pendingParents.emplace_back(id, UUID(parent.as<uint64_t>()));
			}

			if (auto transform = entity["TransformComponent"])
			{
				auto& tc = newEntity.GetComponent<TransformComponent>();
				tc.Position = transform["Position"].as<glm::vec3>();
				tc.Rotation = transform["Rotation"].as<glm::vec3>();
				tc.Scale = transform["Scale"].as<glm::vec3>();
			}

			if (auto cam = entity["CameraComponent"])
			{
				auto& cc = newEntity.AddComponent<CameraComponent>();
				cc.isPrimary = cam["isPrimary"].as<bool>();
				cc.fixedAspectRatio = cam["FixedAspectRatio"].as<bool>();

				auto camDetails = cam["Camera"];

				cc.Camera.SetProjectionType(SceneCamera::ProjectionType(camDetails["ProjectionType"].as<int>()));
				cc.Camera.SetOrthgraphicSize(camDetails["OrthographicScale"].as<float>());
				cc.Camera.SetOrthoNearClip(camDetails["OrthographicNearClip"].as<float>());
				cc.Camera.SetOrthoFarClip(camDetails["OrthographicFarClip"].as<float>());

				cc.Camera.SetPerspectiveFOV(camDetails["PerspectiveFOV"].as<float>());
				cc.Camera.SetPerspectiveNearClip(camDetails["PerspectiveNearClip"].as<float>());
				cc.Camera.SetPerspectiveFarClip(camDetails["PerspectiveFarClip"].as<float>());
			}

			// "Color" is the version 1 key.
			auto color = entity["ColorComponent"];
			if (!color)
				color = entity["Color"];
			if (color)
			{
				auto& cc = newEntity.AddComponent<ColorComponent>();
				cc.Color = color["ColorValue"].as<glm::vec4>();
			}

			if (auto mesh = entity["MeshComponent"])
			{
				auto& mc = newEntity.AddComponent<MeshComponent>();
				PrimitiveType primitive = PrimitiveType::Cube;
				if (PrimitiveTypeFromName(mesh["Primitive"].as<std::string>(), primitive))
					mc.Primitive = primitive;

				if (auto material = mesh["Material"]; material && Renderer::HasDevice())
				{
					mc.Material = std::make_shared<Material>(Renderer::GetDevice(), "Material");
					auto& params = mc.Material->GetParams();
					params.BaseColor = material["BaseColor"].as<glm::vec4>();
					params.EmissiveColor = material["Emissive"].as<glm::vec4>();
					params.Metallic = material["Metallic"].as<float>();
					params.Roughness = material["Roughness"].as<float>();
					params.Occlusion = material["Occlusion"].as<float>();
					mc.Material->Invalidate();
				}
			}

			if (auto light = entity["LightComponent"])
			{
				auto& lc = newEntity.AddComponent<LightComponent>();
				lc.Light.SetLightType((Light::LightType)light["Type"].as<int>());
				lc.Light.GetLightColor() = light["Color"].as<glm::vec3>();
				// Optional: scenes saved before these existed still load.
				if (light["Intensity"]) lc.Light.SetIntensity(light["Intensity"].as<float>());
				if (light["Range"])     lc.Light.SetRange(light["Range"].as<float>());
				if (light["InnerCone"]) lc.Light.SetInnerCone(light["InnerCone"].as<float>());
				if (light["OuterCone"]) lc.Light.SetOuterCone(light["OuterCone"].as<float>());
			}
		}

		// Linked directly rather than through Scene::SetParent. SetParent
		// preserves the world transform by recomputing the local one, which is
		// right for an editor drag and wrong here: what is on disk already is
		// the local transform.
		for (const auto& [childID, parentID] : pendingParents)
		{
			Entity child = m_SceneRef->GetEntityByUUID(childID);
			Entity parent = m_SceneRef->GetEntityByUUID(parentID);

			if (!child || !parent)
			{
				RV_CORE_WARN("Scene references a parent that is not in the file; entity left at the root");
				continue;
			}

			child.GetComponent<RelationshipComponent>().Parent = parentID;
			parent.GetComponent<RelationshipComponent>().Children.push_back(childID);
		}

		m_SceneRef->UpdateWorldTransforms();
		// Used to return false unconditionally, so every caller that checked
		// the result saw a successful load as a failure.
		return true;
	}
}
