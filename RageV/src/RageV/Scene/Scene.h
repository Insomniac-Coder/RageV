#pragma once
#include "EnTT/entt.hpp"
#include "RageV/Core/Timestep.h"
#include "RageV/Core/UUID.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace RageV
{
	class Entity;
	class Camera;
	class EditorCamera;

	class Scene
	{
	public:
		Scene();
		~Scene();

		void OnViewportResize(float width, float height);

		Entity CreateEntity(const std::string& name = std::string());
		// Preserves an existing identity. The deserializer needs this: creating
		// entities with fresh IDs would break every reference in the file.
		Entity CreateEntityWithUUID(UUID id, const std::string& name = std::string());

		// By value: an Entity is a handle pair, and taking it by reference meant
		// callers could not pass a temporary. Children are destroyed with it.
		void DeleteEntity(Entity entity);

		Entity GetEntityByUUID(UUID id);
		bool HasEntity(UUID id) const { return m_EntityMap.find(id) != m_EntityMap.end(); }

		// --- hierarchy ------------------------------------------------------
		// Passing an invalid parent unparents. The child's world transform is
		// preserved across the move, which is what makes reparenting in the
		// hierarchy panel feel non-destructive.
		//
		// Returns false and changes nothing if the move would form a cycle.
		// This is the only place a cycle can be created, so rejecting it here
		// is what lets the transform pass recurse without a depth guard.
		bool SetParent(Entity child, Entity parent);
		bool IsDescendantOf(Entity entity, Entity possibleAncestor);

		Entity GetParent(Entity entity);
		const std::vector<UUID>& GetChildren(Entity entity);

		// Identity when the entity has no parent.
		glm::mat4 GetParentWorldTransform(Entity entity);
		glm::mat4 GetWorldTransform(Entity entity);

		// One top-down pass writing TransformComponent::World. Called by the
		// render entry points; call it directly after mutating transforms if a
		// world value is needed before the next frame.
		void UpdateWorldTransforms();

		// --- frame ----------------------------------------------------------
		// Simulation only. Split from rendering so the editor can draw a scene
		// from its own camera without stepping it, and so the play/edit split
		// has somewhere to land later.
		void OnUpdate(Timestep ts);

		// Draws through the primary camera entity. Draws nothing without one.
		void OnRenderRuntime();
		// Draws through the viewport's own camera, which needs no entity.
		void OnRenderEditor(const EditorCamera& camera);

		Entity GetPrimaryCameraEntity();

		// For tools and tests that need to iterate arbitrary component sets.
		// The editor panels reach it through friendship instead; this exists so
		// that code outside the engine does not have to.
		entt::registry& GetRegistry() { return m_Registry; }

	private:
		void OnRender(const Camera& camera, const glm::mat4& cameraTransform);
		void PropagateTransform(entt::entity handle, const glm::mat4& parentWorld);
		void UnlinkFromParent(Entity entity);

		// Wired to EnTT's on_destroy signals rather than called from
		// DeleteEntity. Destroying an entity, removing a component and clearing
		// the registry all have to run these; a signal cannot be forgotten at
		// one of those call sites.
		void OnNativeScriptDestroyed(entt::registry& registry, entt::entity handle);
		void OnIDDestroyed(entt::registry& registry, entt::entity handle);

	private:
		entt::registry m_Registry;
		std::unordered_map<UUID, entt::entity> m_EntityMap;

		friend class Entity;
		friend class SceneHierarchyPanel;
		friend class SceneSerializer;
	};
}
