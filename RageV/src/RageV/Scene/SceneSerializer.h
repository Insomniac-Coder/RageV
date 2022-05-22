#pragma once
#include "Entity.h"
#include "Scene.h"
#include "yaml-cpp/yaml.h"

namespace RageV
{

	class SceneSerializer
	{
	public:
		SceneSerializer(const std::shared_ptr<Scene>& sceneRef);
		~SceneSerializer();
		void Serialize(const std::string& filepath);
		void SerializeEntity(YAML::Emitter& emitter, Entity entity);
		bool Deserialize(const std::string& filepath);

	private:
		std::shared_ptr<Scene> m_SceneRef;
	};

}