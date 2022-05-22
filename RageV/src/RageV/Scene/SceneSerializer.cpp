#include <rvpch.h>
#include "SceneSerializer.h"
#include <fstream>
#include "Components.h"

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
	YAML::Emitter& operator << (YAML::Emitter& emitter, glm::vec3& vec)
	{
		emitter << YAML::Flow;
		emitter << YAML::BeginSeq << vec.x << vec.y << vec.z << YAML::EndSeq;
		return emitter;
	}

	YAML::Emitter& operator << (YAML::Emitter& emitter, glm::vec4 vec)
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

	void SceneSerializer::Serialize(const std::string& filepath)
	{
		YAML::Emitter emitter;
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "Scene" << YAML::Value << "Untitled";
		emitter << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		m_SceneRef->m_Registry.each([&](auto& entityID)
			{
				Entity entity = { entityID, m_SceneRef.get() };
				if (!entity)
					return;

				SerializeEntity(emitter, entity);
			}
		);

		emitter << YAML::EndSeq;
		emitter << YAML::EndMap;

		std::ofstream file(filepath.c_str());
		file << emitter.c_str();
		file.close();
	}

	void SceneSerializer::SerializeEntity(YAML::Emitter& emitter, Entity entity)
	{
		emitter << YAML::BeginMap;
		emitter << YAML::Key << "EntityID" << YAML::Value << "12345678890";

		if (entity.HasComponent<TagComponent>())
		{
			auto& tag = entity.GetComponent<TagComponent>();
			emitter << YAML::Key << "TagComponent";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "Tag" << YAML::Key << tag.Name;
			emitter << YAML::EndMap;
		}

		if (entity.HasComponent<TransformComponent>())
		{
			auto& transform = entity.GetComponent<TransformComponent>();
			emitter << YAML::Key << "TransformComponent";
			emitter << YAML::BeginMap;
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
			emitter << YAML::Key << "Color";
			emitter << YAML::BeginMap;
			emitter << YAML::Key << "ColorValue" << color.Color;
			emitter << YAML::EndMap;
		}
		emitter << YAML::EndMap;
	}

	bool SceneSerializer::Deserialize(const std::string& filepath)
	{
		std::ifstream file(filepath);
		std::stringstream ss;
		ss << file.rdbuf();

		YAML::Node node = YAML::Load(ss.str());

		if (!node["Scene"])
		{
			RV_CORE_ERROR("Invalid scene file!");
			return false;
		}

		std::string sceneName = node["Scene"].as<std::string>();

		auto entities = node["Entities"];

		if (entities)
		{
			for (auto entity : entities)
			{
				uint64_t entityID = entity["EntityID"].as<uint64_t>();
				std::string name;
				auto tag = entity["TagComponent"];
				if (tag)
					name = tag["Tag"].as<std::string>();

				Entity newEntity = m_SceneRef->CreateEntity(name);

				auto transform = entity["TransformComponent"];
				if (transform)
				{
					auto& tc = newEntity.GetComponent<TransformComponent>();
					tc.Position = transform["Position"].as<glm::vec3>();
					tc.Rotation = transform["Rotation"].as<glm::vec3>();
					tc.Scale = transform["Scale"].as<glm::vec3>();
				}

				auto cam = entity["CameraComponent"];
				if (cam)
				{
					auto& cc = newEntity.AddComponent<CameraComponent>();
					cc.isPrimary = cam["isPrimary"].as<bool>();
					cc.fixedAspectRatio = cam["FixedAspectRatio"].as<bool>();

					auto& camDetails = cam["Camera"];

					cc.Camera.SetProjectionType(SceneCamera::ProjectionType(camDetails["ProjectionType"].as<int>()));
					cc.Camera.SetOrthgraphicSize(camDetails["OrthographicScale"].as<float>());
					cc.Camera.SetOrthoNearClip(camDetails["OrthographicNearClip"].as<float>());
					cc.Camera.SetOrthoFarClip(camDetails["OrthographicFarClip"].as<float>());

					cc.Camera.SetPerspectiveFOV(camDetails["PerspectiveFOV"].as<float>());
					cc.Camera.SetPerspectiveNearClip(camDetails["PerspectiveNearClip"].as<float>());
					cc.Camera.SetPerspectiveFarClip(camDetails["PerspectiveFarClip"].as<float>());
				}

				auto color = entity["Color"];
				if (color)
				{
					auto& cc = newEntity.AddComponent<ColorComponent>();
					cc.Color = color["ColorValue"].as<glm::vec4>();
				}
			}
		}

		return false;
	}


}