#pragma once
#include "EnTT/entt.hpp"
#include "RageV/Core/Timestep.h"

namespace RageV
{
	class Entity;
	class Scene
	{
	public:
		Scene();
		~Scene();
		void OnViewportResize(float width, float height);
		Entity CreateEntity(const std::string& name = std::string());
		void OnUpdate(Timestep ts);
	private:
		entt::registry m_Registry;
		friend class Entity;
		friend class SceneHierarchyPanel;
	};
}
