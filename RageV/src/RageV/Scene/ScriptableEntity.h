#pragma once
#include "RageV/Core/Timestep.h"
#include "RageV/Core/UUID.h"
#include "RageV/Asset/Asset.h"
#include "Entity.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace RageV
{
	class Scene;

	// The surface a native script sees.
	//
	// Deliberately narrow and stated in engine terms rather than EnTT terms: a
	// script gets Entity values, never raw handles. Play mode restores the
	// scene by recreating entities, so a stored handle is dangling the moment
	// Stop is pressed -- the undo system learned that the expensive way.
	//
	// This is also the surface C# will mirror. Every change to it afterwards
	// costs a binding update, a marshalling update and a class-library update,
	// so it is worth building deliberately once.
	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity() = default;

		// Once, the first time the script is stepped after Play.
		virtual void OnCreate() {}
		// Every fixed simulation step. Not every frame -- a script that moves
		// something has to agree with the physics that will push it.
		virtual void OnUpdate(Timestep dt) {}
		// On destruction, and when play mode stops.
		virtual void OnDestroy() {}

	protected:
		// --- identity --------------------------------------------------------
		Entity GetEntity() const { return m_Entity; }
		Scene& GetScene() const;
		UUID GetUUID();
		const std::string& GetName();
		void SetName(const std::string& name);

		// --- components ------------------------------------------------------
		template<typename T>
		T& GetComponent() { return m_Entity.GetComponent<T>(); }

		template<typename T>
		bool HasComponent() { return m_Entity.HasComponent<T>(); }

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args) { return m_Entity.AddComponent<T>(std::forward<Args>(args)...); }

		template<typename T>
		void RemoveComponent() { m_Entity.RemoveComponent<T>(); }

		// --- transform -------------------------------------------------------
		// Local, relative to the parent. World is derived.
		glm::vec3& GetPosition();
		glm::vec3& GetRotation();   // radians
		glm::vec3& GetScale();

		void Translate(const glm::vec3& delta);
		void Rotate(const glm::vec3& eulerDelta);
		void LookAt(const glm::vec3& target, const glm::vec3& up = { 0.0f, 1.0f, 0.0f });

		glm::mat4 GetWorldTransform();
		glm::vec3 GetWorldPosition();

		// The entity's own axes, so "forward" means forward for this object
		// rather than for the world.
		glm::vec3 GetForward();
		glm::vec3 GetRight();
		glm::vec3 GetUp();

		// --- other entities --------------------------------------------------
		// Invalid when nothing matches; test with `if (entity)`.
		Entity FindEntityByName(const std::string& name);
		Entity FindEntityByUUID(UUID id);
		std::vector<Entity> FindEntitiesByName(const std::string& name);

		Entity Spawn(const std::string& name = "Entity");
		Entity SpawnPrefab(AssetHandle prefab);

		// Deferred to the end of the simulation step. Destroying an entity
		// while the script pass is walking them would invalidate the iteration,
		// and a script destroying itself mid-update would delete the object it
		// is executing in.
		void Destroy();
		void Destroy(Entity entity);

		// --- hierarchy -------------------------------------------------------
		Entity GetParent();
		void SetParent(Entity parent);
		std::vector<Entity> GetChildren();

		// --- input -----------------------------------------------------------
		// By action name, never by keycode. See RageV/Core/InputMap.h.
		static bool IsActionDown(const std::string& action);
		static bool WasActionPressed(const std::string& action);
		static bool WasActionReleased(const std::string& action);
		static float GetAxis(const std::string& axis);

		// --- time ------------------------------------------------------------
		static float GetFixedDeltaTime();
		// Seconds since the process started.
		static float GetTime();

	private:
		Entity m_Entity;
		friend class Scene;
	};
}
