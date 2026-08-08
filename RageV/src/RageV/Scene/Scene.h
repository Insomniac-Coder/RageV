#pragma once
#include "EnTT/entt.hpp"
#include "RageV/Core/Timestep.h"
#include <glm/glm.hpp>
#include <string>

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
		// By value: an Entity is a handle pair, and taking it by reference meant
		// callers could not pass a temporary.
		void DeleteEntity(Entity entity);

		// Simulation only. Split from rendering so the editor can draw a scene
		// from its own camera without stepping it, and so the play/edit split
		// has somewhere to land later.
		void OnUpdate(Timestep ts);

		// Draws through the primary camera entity. Draws nothing without one.
		void OnRenderRuntime();
		// Draws through the viewport's own camera, which needs no entity.
		void OnRenderEditor(const EditorCamera& camera);

		Entity GetPrimaryCameraEntity();

	private:
		void OnRender(const Camera& camera, const glm::mat4& cameraTransform);

		// Wired to EnTT's on_destroy signal rather than called from
		// DeleteEntity. Destroying an entity, removing the component and
		// clearing the registry all have to run it; a signal cannot be
		// forgotten at one of those call sites.
		void OnNativeScriptDestroyed(entt::registry& registry, entt::entity handle);

	private:
		entt::registry m_Registry;
		friend class Entity;
		friend class SceneHierarchyPanel;
		friend class SceneSerializer;
	};
}
