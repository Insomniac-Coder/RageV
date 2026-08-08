#pragma once
#include "RageV/Core/Timestep.h"
#include "RageV/Core/UUID.h"
#include "RageV/Asset/Asset.h"
#include "RageV/Physics/PhysicsWorld.h"
#include "Entity.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace RageV
{
	class Scene;

	// One collision or overlap, from the point of view of the script being
	// told about it.
	//
	// The same event reaches both entities involved, each with Other set to the
	// one it is not and Normal pointing back at itself -- so a script can be
	// written without knowing which side of the pair it happens to be.
	struct Collision
	{
		// Invalid if it has already been destroyed this step. Test with
		// `if (collision.Other)` before reaching into it.
		Entity Other;

		// Either collider is a trigger. Always true in OnTrigger*, always false
		// in OnCollision*; carried so one shared handler can tell.
		bool Trigger = false;

		// World space, and both zero on Exit -- there is no contact left to
		// describe by then.
		glm::vec3 Point{ 0.0f };
		// Points from Other towards this entity, so moving along it is moving
		// away from whatever was hit.
		glm::vec3 Normal{ 0.0f };

		// Closing speed along the normal when they met, in units per second,
		// never negative. Scale an impact sound or a damage value by it.
		float ImpactSpeed = 0.0f;
	};

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

		// --- physics ---------------------------------------------------------
		// Delivered after the simulation step that produced them, so a script
		// reacting to a hit is reacting to a resolved one.
		//
		// Solid colliders. Enter and Exit fire once each per pair; Stay fires
		// every step in between.
		//
		// A pair that has come to rest does NOT report Exit when the simulation
		// puts it to sleep -- the objects are still touching, and the engine
		// only reports what changed physically.
		virtual void OnCollisionEnter(const Collision& collision) {}
		virtual void OnCollisionStay(const Collision& collision) {}
		virtual void OnCollisionExit(const Collision& collision) {}

		// Colliders marked IsTrigger, which report overlaps without resisting
		// them. Note that a trigger only sees bodies that are awake: something
		// that falls asleep inside one stops being reported until it moves
		// again.
		virtual void OnTriggerEnter(const Collision& collision) {}
		virtual void OnTriggerStay(const Collision& collision) {}
		virtual void OnTriggerExit(const Collision& collision) {}

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

		// --- physics ---------------------------------------------------------
		// All no-ops outside play mode, and on an entity with no rigid body.
		// Velocities and impulses are world space.
		//
		// Forces accumulate over a step and are cleared by it; impulses change
		// velocity at once. A jump is an impulse, a thruster is a force.
		void AddForce(const glm::vec3& force);
		void AddImpulse(const glm::vec3& impulse);
		void SetLinearVelocity(const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity();

		// Nearest body along the ray, which may be this one. Direction need not
		// be normalised; the ray extends to its length. Test with `if (hit)`.
		RayHit Raycast(const glm::vec3& origin, const glm::vec3& direction);

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
