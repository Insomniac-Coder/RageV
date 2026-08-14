#pragma once
#include "Entity.h"
#include "Scene.h"
#include "yaml-cpp/yaml.h"

namespace RageV
{
	class SceneSerializer
	{
	public:
		// Bumped whenever the on-disk shape changes.
		//
		//   1  Hardcoded entity IDs, no hierarchy, lights never written, the
		//      colour component keyed "Color", camera settings nested.
		//   2  Real UUIDs and parent links.
		//   3  Registry-driven: camera settings flattened, enums by name.
		//   4  Meshes referenced by asset handle instead of a primitive name.
		//   5  Cameras ranked by ViewRank instead of an isPrimary flag.
		//   6  Render and post settings left the scene. Anti-aliasing and
		//      shadows belong to the project; exposure and bloom belong to a
		//      `.rvpostprofile` a camera points at. What the Environment block
		//      still holds is ambient light and the sky. ENGINE-NOTES 7s.
		//
		// Older files are read and upgraded on load. Version 1 entities get
		// fresh IDs, since honouring what they contain would collapse every
		// entity in the scene onto a single identity. A version-5 file whose
		// render or post keys were set to something other than the default is
		// *reported* rather than migrated or silently dropped -- see
		// ReportMovedSettings in the implementation.
		static constexpr int kVersion = 6;

		SceneSerializer(const std::shared_ptr<Scene>& sceneRef);
		~SceneSerializer();

		bool Serialize(const std::string& filepath);
		bool Deserialize(const std::string& filepath);

		// Same output as Serialize, without touching the filesystem. The
		// round-trip test compares these directly.
		std::string SerializeToString();
		bool DeserializeFromString(const std::string& yaml);

		// One entity and its descendants, in the same document shape as a whole
		// scene so the same reader restores it. Undo uses this to bring a
		// deleted subtree back with its original ids -- which is what keeps
		// anything referring to it resolving. Prefabs will want it too.
		std::string SerializeSubtree(Entity root);

		// Adds to the scene instead of replacing it. Ids in the snapshot are
		// preserved, so restoring twice would collide -- callers are expected
		// to be undoing a delete, not duplicating.
		bool DeserializeAdditive(const std::string& yaml);

		// Adds a copy with fresh ids, remapping internal parent links so the
		// hierarchy survives. This is what separates stamping out a prefab from
		// restoring a deleted one: two instances of the same prefab must not
		// share identities, or every reference to one would resolve to both.
		//
		// Returns the root of what was added, or an invalid entity.
		Entity Instantiate(const std::string& yaml);

	private:
		enum class ReadMode
		{
			Replace,      // opening a scene file
			Additive,     // undoing a delete: ids preserved
			Instantiate,  // stamping a prefab: ids remapped
		};

	public:

	private:
		void SerializeEntity(YAML::Emitter& emitter, Entity entity);
		bool Read(const std::string& yaml, ReadMode mode, Entity* firstRoot = nullptr);

		std::shared_ptr<Scene> m_SceneRef;
	};
}
