#pragma once
#include "Entity.h"
#include "Scene.h"
#include "yaml-cpp/yaml.h"

namespace RageV
{
	class SceneSerializer
	{
	public:
		// Bumped whenever the on-disk shape changes. Version 1 predates entity
		// UUIDs and the hierarchy; it wrote a hardcoded ID for every entity, so
		// files at that version cannot carry references and are loaded with
		// fresh IDs and no parents.
		static constexpr int kVersion = 2;

		SceneSerializer(const std::shared_ptr<Scene>& sceneRef);
		~SceneSerializer();

		bool Serialize(const std::string& filepath);
		bool Deserialize(const std::string& filepath);

		// Same output as Serialize, without touching the filesystem. The
		// round-trip test compares these directly.
		std::string SerializeToString();
		bool DeserializeFromString(const std::string& yaml);

	private:
		void SerializeEntity(YAML::Emitter& emitter, Entity entity);

		std::shared_ptr<Scene> m_SceneRef;
	};
}
